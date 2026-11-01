#pragma once

#include <JuceHeader.h>
#include "StemSet.h"
#include "StemReader.h"

// The four waveform thumbnails of a deck, shared by its overview and its
// scrolling waveform. Broadcasts a change while they load.
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
	// stems stay readable. Capped so near-silent stems don't show noise.
	float getDisplayGain (int stem) const { return displayGains[(size_t) stem]; }

private:
	void changeListenerCallback (juce::ChangeBroadcaster*) override
	{
		for (int i = 0; i < StemSet::numStems; ++i)
		{
			float low = 0.0f, high = 0.0f;
			auto* t = thumbnails[i];

			if (t->getTotalLength() > 0.0)
				t->getApproximateMinMax (0.0, t->getTotalLength(), 0, low, high);

			const auto peak = juce::jmax (std::abs (low), std::abs (high));
			displayGains[(size_t) i] = peak > 0.0f ? juce::jlimit (1.0f, 4.0f, 0.95f / peak) : 1.0f;
		}

		sendChangeMessage();
	}

	juce::AudioFormatManager& formatManager;
	juce::OwnedArray<juce::AudioThumbnail> thumbnails;
	std::array<float, StemSet::numStems> displayGains { 1.0f, 1.0f, 1.0f, 1.0f };

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemThumbnails)
};
