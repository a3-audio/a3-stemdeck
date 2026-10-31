#pragma once

#include <JuceHeader.h>
#include "NiStemCache.h"

#include <deque>

// Decodes NI Stems files into the cache one after another on a thread of its
// own, so neither a scan nor a load waits for ffmpeg; each finished file is
// reported on the message thread.
class NiStemPreparer : private juce::Thread, private juce::AsyncUpdater
{
public:
	explicit NiStemPreparer (const NiStemCache& cache);
	~NiStemPreparer() override;

	// What to decode, in this order; replaces what was still waiting.
	void prepare (const juce::Array<juce::File>& sources);

	std::function<void (const juce::File& source)> onReady;   // message thread
	std::function<void (const juce::File& source)> onFailed;  // message thread

private:
	void run() override;
	void handleAsyncUpdate() override;
	std::optional<juce::File> nextSource();

	const NiStemCache& cache;
	juce::CriticalSection lock;
	std::deque<juce::File> waiting;
	std::vector<std::pair<juce::File, bool>> finished;   // the file, and whether it worked
	juce::WaitableEvent wake;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NiStemPreparer)
};
