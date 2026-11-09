#pragma once

#include <JuceHeader.h>
#include "StemSet.h"
#include "StemReader.h"
#include "ThumbnailLoad.h"

// The four waveform thumbnails of a deck, shared by its overview and its
// scrolling waveform. Broadcasts a change when a new set is given, and while
// it loads only as often as thumbnailload::Tracker says (each broadcast
// re-renders the whole overview).
class StemThumbnails : public juce::ChangeBroadcaster,
					   private juce::ChangeListener
{
public:
	StemThumbnails (juce::AudioFormatManager& formats, juce::AudioThumbnailCache& cache) : formatManager (formats)
	{
		// Fine resolution (128 samples per point) so the zoomed view stays sharp.
		for (int i = 0; i < StemSet::numStems; ++i)
			thumbnails.add (new juce::AudioThumbnail (128, formatManager, cache))->addChangeListener (this);
	}

	~StemThumbnails() override
	{
		for (auto* t : thumbnails)
			t->removeChangeListener (this);
	}

	void setSet (const StemSet& set)
	{
		load.reset();
		for (int i = 0; i < StemSet::numStems; ++i)
		{
			// An NI Stems track has no file of its own to hand over: its reader instead.
			if (set.niStems && set.hasStem (i))
				thumbnails[i]->setReader (createStemReader (set, i, formatManager),
										  (set.files[(size_t) i].getFullPathName() + "#" + juce::String (i + 1)).hashCode64());
			else if (set.hasStem (i))
				thumbnails[i]->setSource (new juce::FileInputSource (set.files[(size_t) i]));
			else
				thumbnails[i]->clear();   // an empty slot draws an empty lane
		}

		sendChangeMessage();
	}

	juce::AudioThumbnail& operator[] (int stem) { return *thumbnails[stem]; }

	// Vertical zoom that makes the stem's loudest peak fill its lane, so quiet
	// stems stay readable; 1 until the stem's thumbnail is complete.
	float getDisplayGain (int stem) const { return load.gain (stem); }

private:
	static_assert (StemSet::numStems == thumbnailload::stems);

	void changeListenerCallback (juce::ChangeBroadcaster*) override
	{
		std::array<thumbnailload::StemState, thumbnailload::stems> states;
		for (int i = 0; i < StemSet::numStems; ++i)
		{
			float low = 0.0f, high = 0.0f;
			auto* t = thumbnails[i];

			if (t->getTotalLength() > 0.0)
				t->getApproximateMinMax (0.0, t->getTotalLength(), 0, low, high);

			states[(size_t) i] = { t->isFullyLoaded(), t->getProportionComplete(), juce::jmax (std::abs (low), std::abs (high)) };
		}

		if (load.update (states))
			sendChangeMessage();
	}

	juce::AudioFormatManager& formatManager;
	juce::OwnedArray<juce::AudioThumbnail> thumbnails;
	thumbnailload::Tracker load;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemThumbnails)
};
