#include "StemLevels.h"
#include "StemReader.h"

#include <optional>

namespace
{
	constexpr int chunk = 65536;

	// One slot's envelope; null if it cannot be read or the read is aborted.
	std::optional<StemHandover::Envelope> envelopeOf (const StemSet& set, int slot, juce::AudioFormatManager& formatManager,
													  const std::function<bool()>& shouldAbort)
	{
		std::unique_ptr<juce::AudioFormatReader> reader (createStemReader (set, slot, formatManager));
		if (reader == nullptr)
			return std::nullopt;

		juce::AudioBuffer<float> buffer (2, chunk);
		const auto channels = juce::jlimit (1, 2, (int) reader->numChannels);
		StemHandover::EnvelopeBuilder builder (reader->sampleRate);
		for (juce::int64 start = 0; start < reader->lengthInSamples; start += chunk)
		{
			if (shouldAbort())
				return std::nullopt;
			const auto numSamples = (int) juce::jmin ((juce::int64) chunk, reader->lengthInSamples - start);
			reader->read (&buffer, 0, numSamples, start, true, channels > 1);
			builder.add (buffer.getArrayOfReadPointers(), channels, numSamples);
		}
		return builder.finish();
	}
}

std::shared_ptr<const StemHandover::Envelopes> StemLevels::read (const StemSet& set, juce::AudioFormatManager& formatManager,
																 const std::function<bool()>& shouldAbort)
{
	static_assert (StemSet::numStems == StemHandover::numStems);
	auto levels = std::make_shared<StemHandover::Envelopes>();
	size_t longest = 0;

	for (int s = 0; s < StemSet::numStems; ++s)
	{
		if (! set.hasStem (s))
			continue;
		auto envelope = envelopeOf (set, s, formatManager, shouldAbort);
		if (! envelope)
			return nullptr;
		longest = std::max (longest, envelope->rms.size());
		(*levels)[(size_t) s] = std::move (*envelope);
	}

	if (longest == 0)
		return nullptr;

	// An empty slot is silent for the whole set.
	for (int s = 0; s < StemSet::numStems; ++s)
		if (! set.hasStem (s))
			(*levels)[(size_t) s].rms.assign (longest, 0.0f);
	return levels;
}
