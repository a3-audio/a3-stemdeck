#include "OnScreenKeyboard.h"

#include <deque>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
	// Long enough that a hop from one text field to the next, or a tap on a
	// button between two of them, does not flash the keyboard away.
	constexpr int hideDebounceMs = 300;

	// dbus-send's own limit, and how long the worker waits for it.
	constexpr int replyTimeoutMs = 1000;
	constexpr int processTimeoutMs = 1500;

	// A freshly started onboard needs a moment to claim its D-Bus name.
	constexpr int startupPollMs = 150;
	constexpr int startupPolls = 20;

	bool isOnPath (const juce::String& program)
	{
		const auto path = juce::SystemStats::getEnvironmentVariable ("PATH", "/usr/local/bin:/usr/bin:/bin");

		for (const auto& dir : juce::StringArray::fromTokens (path, ":", {}))
			if (dir.isNotEmpty() && juce::File (dir).getChildFile (program).existsAsFile())
				return true;

		return false;
	}

	bool isTextField (juce::Component* component)
	{
		const auto* editor = dynamic_cast<juce::TextEditor*> (component);
		return editor != nullptr && ! editor->isReadOnly();
	}

	// Started in its own session and orphaned at once (double fork): onboard is
	// the desktop's keyboard, not StemDeck's child. It keeps no descriptor of
	// ours either -- a crashed StemDeck must find its Pro DJ Link sockets free.
	bool launchDetached (const char* program)
	{
		char* const argv[] = { const_cast<char*> (program), nullptr };

		const auto child = fork();

		if (child < 0)
			return false;

		if (child == 0)
		{
			setsid();

			if (fork() == 0)
			{
				const auto devNull = open ("/dev/null", O_RDWR);
				dup2 (devNull, STDIN_FILENO);
				dup2 (devNull, STDOUT_FILENO);
				dup2 (devNull, STDERR_FILENO);
				close_range (3, ~0U, 0);
				execvp (program, argv);
				_exit (127);
			}

			_exit (0);
		}

		int status = 0;
		waitpid (child, &status, 0);
		return WIFEXITED (status) && WEXITSTATUS (status) == 0;
	}
}

//==============================================================================
// One queue, one thread: the calls keep their order (a show and the hide
// right after it must not overtake each other) and none waits on the UI.
class OnScreenKeyboard::Worker  : private juce::Thread
{
public:
	enum class Request { show, hide, toggle };
	enum class Failure { couldNotStart, noAnswer };

	explicit Worker (std::function<void (Failure)> reportFailure)
		: juce::Thread ("On-screen keyboard"), report (std::move (reportFailure))
	{
		startThread();
	}

	~Worker() override
	{
		signalThreadShouldExit();
		wake.signal();
		stopThread (processTimeoutMs + 1000);
	}

	void post (Request request)
	{
		{
			const juce::ScopedLock lock (queueLock);
			queue.push_back (request);
		}
		wake.signal();
	}

private:
	void run() override
	{
		while (! threadShouldExit())
		{
			wake.wait (-1);

			while (! threadShouldExit())
			{
				const auto next = takeNext();

				if (! next.has_value())
					break;

				perform (*next);
			}
		}
	}

	std::optional<Request> takeNext()
	{
		const juce::ScopedLock lock (queueLock);

		if (queue.empty())
			return std::nullopt;

		const auto request = queue.front();
		queue.pop_front();
		return request;
	}

	void perform (Request request)
	{
		if (request == Request::hide)
		{
			// Not running is as good as hidden.
			callOnboard ("Hide");
			return;
		}

		if (callOnboard (request == Request::toggle ? "ToggleVisible" : "Show"))
			return;

		// Not running: both a show and a toggle mean "show".
		startAndShow();
	}

	void startAndShow()
	{
		if (! launchDetached ("onboard"))
		{
			report (Failure::couldNotStart);
			return;
		}

		// Onboard shows itself on start, unless it is set to start minimised;
		// asking until it answers covers both.
		for (int poll = 0; poll < startupPolls; ++poll)
		{
			wait (startupPollMs);

			if (threadShouldExit())
				return;

			if (callOnboard ("Show"))
				return;
		}

		report (Failure::noAnswer);
	}

	// --print-reply makes dbus-send wait for onboard's answer, so its exit
	// code says whether anyone took the call.
	static bool callOnboard (const juce::String& method)
	{
		juce::ChildProcess dbusSend;
		const juce::StringArray command { "dbus-send", "--session", "--print-reply",
										  "--reply-timeout=" + juce::String (replyTimeoutMs),
										  "--dest=org.onboard.Onboard", "/org/onboard/Onboard/Keyboard",
										  "org.onboard.Onboard.Keyboard." + method };

		if (! dbusSend.start (command, 0))
			return false;

		if (! dbusSend.waitForProcessToFinish (processTimeoutMs))
		{
			dbusSend.kill();
			return false;
		}

		return dbusSend.getExitCode() == 0;
	}

	std::function<void (Failure)> report;
	juce::CriticalSection queueLock;
	std::deque<Request> queue;
	juce::WaitableEvent wake;
};

//==============================================================================
OnScreenKeyboard::OnScreenKeyboard()
	: policy (isOnPath ("onboard") && isOnPath ("dbus-send"))
{
	// Created here, on the message thread; the worker only copies it.
	juce::WeakReference<OnScreenKeyboard> self (this);

	worker = std::make_unique<Worker> ([self] (Worker::Failure failure)
	{
		juce::MessageManager::callAsync ([self, failure]
		{
			if (auto* keyboard = self.get())
			{
				if (failure == Worker::Failure::couldNotStart)
					keyboard->onboardFailed ("On-screen keyboard: onboard could not be started", false);
				else
					keyboard->onboardFailed ("On-screen keyboard: onboard did not answer", true);
			}
		});
	});

	onboard = [this] (KeyboardPolicy::Command command) { post (command); };
	juce::Desktop::getInstance().addFocusChangeListener (this);
}

OnScreenKeyboard::OnScreenKeyboard (bool onboardAvailable, Onboard sink)
	: policy (onboardAvailable), onboard (std::move (sink))
{
	juce::Desktop::getInstance().addFocusChangeListener (this);
}

OnScreenKeyboard::~OnScreenKeyboard()
{
	juce::Desktop::getInstance().removeFocusChangeListener (this);
	stopTimer();
	worker.reset();
}

void OnScreenKeyboard::post (KeyboardPolicy::Command command)
{
	using Command = KeyboardPolicy::Command;
	const auto request = command == Command::show ? Worker::Request::show
					   : command == Command::hide ? Worker::Request::hide
												  : Worker::Request::toggle;
	worker->post (request);
}

void OnScreenKeyboard::keysPressed()
{
	run (policy.keysPressed());
}

void OnScreenKeyboard::globalFocusChanged (juce::Component* focusedComponent)
{
	run (policy.focusChanged (isTextField (focusedComponent) ? focusedComponent : nullptr));
}

void OnScreenKeyboard::timerCallback()
{
	stopTimer();
	run (policy.hideDue());
}

void OnScreenKeyboard::run (KeyboardPolicy::Command command)
{
	using Command = KeyboardPolicy::Command;

	switch (command)
	{
		case Command::none:     return;
		case Command::show:
		case Command::hide:
		case Command::toggle:   onboard (command); return;
		case Command::hideSoon: startTimer (hideDebounceMs); return;
		case Command::hint:
			if (onHint != nullptr)
				onHint ("On-screen keyboard: onboard is not installed");
			return;
	}
}

void OnScreenKeyboard::onboardFailed (const juce::String& why, bool stillAvailable)
{
	policy.setAvailable (stillAvailable);

	if (onHint != nullptr)
		onHint (why);
}
