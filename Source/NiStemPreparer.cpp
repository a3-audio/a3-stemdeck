#include "NiStemPreparer.h"

NiStemPreparer::NiStemPreparer (const NiStemCache& c) : juce::Thread ("NI Stems"), cache (c)
{
	startThread (juce::Thread::Priority::background);
}

NiStemPreparer::~NiStemPreparer()
{
	signalThreadShouldExit();
	wake.signal();
	stopThread (5000);   // prepare() kills a running ffmpeg when asked to exit
	cancelPendingUpdate();
}

void NiStemPreparer::prepare (const juce::Array<juce::File>& sources)
{
	{
		const juce::ScopedLock sl (lock);
		waiting.assign (sources.begin(), sources.end());
	}
	wake.signal();
}

std::optional<juce::File> NiStemPreparer::nextSource()
{
	const juce::ScopedLock sl (lock);
	if (waiting.empty())
		return std::nullopt;
	auto source = waiting.front();
	waiting.pop_front();
	return source;
}

void NiStemPreparer::run()
{
	while (! threadShouldExit())
	{
		const auto source = nextSource();
		if (! source)
		{
			wake.wait (-1);
			continue;
		}
		const bool ok = cache.prepare (*source, [this] { return threadShouldExit(); });
		if (threadShouldExit())
			return;
		{
			const juce::ScopedLock sl (lock);
			finished.emplace_back (*source, ok);
		}
		triggerAsyncUpdate();
	}
}

void NiStemPreparer::handleAsyncUpdate()
{
	std::vector<std::pair<juce::File, bool>> done;
	{
		const juce::ScopedLock sl (lock);
		done.swap (finished);
	}
	for (const auto& [source, ok] : done)
		if (const auto& report = ok ? onReady : onFailed)
			report (source);
}
