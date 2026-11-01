#include "SectionAnalysis.h"
#include "StemReader.h"

namespace
{
	// As TempoAnalysis: plenty for levels and 10 ms attack slices, cheap to read.
	constexpr double analysisRate = 11025.0;
}

std::optional<sections::Features> SectionAnalysis::analyse (const StemSet& set, juce::AudioFormatManager& formatManager,
															 const BeatGrid& grid, const std::function<bool()>& shouldAbort)
{
	if (! grid.isValid())
		return std::nullopt;

	std::array<std::unique_ptr<juce::AudioFormatReader>, StemSet::numStems> readers;
	juce::int64 length = 0;
	double sampleRate = 0.0;

	for (size_t i = 0; i < readers.size(); ++i)
	{
		if (! set.hasStem ((int) i))
			continue;

		readers[i].reset (createStemReader (set, (int) i, formatManager));

		if (readers[i] == nullptr)
			return std::nullopt;

		// The stems are read in step at one rate; at another, a stem's beats
		// would land in the wrong bars. No features rather than wrong ones.
		if (sampleRate > 0.0 && ! juce::exactlyEqual (readers[i]->sampleRate, sampleRate))
			return std::nullopt;

		sampleRate = readers[i]->sampleRate;
		length = juce::jmax (length, readers[i]->lengthInSamples);
	}

	// Each stem to mono and decimated by averaging (a crude low-pass), as
	// TempoAnalysis::analyse does for their sum.
	if (! (sampleRate > 0.0))
		return std::nullopt;   // not one file

	const auto decimation = juce::jmax (1, juce::roundToInt (sampleRate / analysisRate));
	sections::FeatureBuilder builder (sampleRate / decimation, grid.firstBeat, grid.beatLength());

	const int chunk = 65536 - (65536 % decimation);
	juce::AudioBuffer<float> buffer (2, chunk);
	std::array<std::vector<float>, StemSet::numStems> mono;
	for (auto& stem : mono)
		stem.resize ((size_t) (chunk / decimation));

	for (juce::int64 start = 0; start < length; start += chunk)
	{
		if (shouldAbort())
			return std::nullopt;

		const auto numSamples = (int) juce::jmin ((juce::int64) chunk, length - start);
		const auto numMono = numSamples / decimation;

		for (size_t s = 0; s < readers.size(); ++s)
		{
			if (readers[s] == nullptr)
			{
				std::fill (mono[s].begin(), mono[s].end(), 0.0f);   // an empty slot
				continue;
			}
			auto& reader = *readers[s];
			reader.read (&buffer, 0, numSamples, start, true, true);
			const auto* left = buffer.getReadPointer (0);
			const auto* right = buffer.getReadPointer (reader.numChannels > 1 ? 1 : 0);

			for (int i = 0; i < numMono; ++i)
			{
				float sum = 0.0f;
				for (int j = 0; j < decimation; ++j)
					sum += left[i * decimation + j] + right[i * decimation + j];
				mono[s][(size_t) i] = sum / (float) (2 * decimation);
			}
		}

		if (! set.isComplete())
			sections::foldIntoOneMix (mono);

		builder.add ({ mono[0].data(), mono[1].data(), mono[2].data(), mono[3].data() }, numMono);
	}

	return builder.finish();
}
