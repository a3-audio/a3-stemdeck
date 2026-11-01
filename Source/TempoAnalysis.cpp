#include "TempoAnalysis.h"
#include "StemReader.h"
#include "Sections.h"
#include "FileLock.h"

namespace
{
	constexpr double analysisRate = 11025.0;  // plenty for onsets, cheap to process
	constexpr int fftOrder = 9;               // 512-point frames
	constexpr int fftSize = 1 << fftOrder;
	constexpr int hopSize = 128;              // ~11.6 ms at 11025 Hz
	constexpr double minBpm = 70.0, maxBpm = 180.0;
	constexpr double halfTempoRatio = 0.6;

	// Spectral flux: how much the log spectrum grows from frame to frame.
	// Peaks where notes and drums start.
	std::vector<float> onsetEnvelope (const std::vector<float>& samples, const std::function<bool()>& shouldAbort)
	{
		if ((int) samples.size() < fftSize * 4)
			return {};

		juce::dsp::FFT fft (fftOrder);
		juce::dsp::WindowingFunction<float> window ((size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false);

		const auto numFrames = ((int) samples.size() - fftSize) / hopSize;
		std::vector<float> envelope ((size_t) numFrames, 0.0f);
		std::vector<float> frame ((size_t) fftSize * 2), previous ((size_t) fftSize / 2 + 1, 0.0f);

		for (int t = 0; t < numFrames; ++t)
		{
			if ((t & 1023) == 0 && shouldAbort())
				return {};

			std::fill (frame.begin(), frame.end(), 0.0f);
			std::copy_n (samples.begin() + t * hopSize, fftSize, frame.begin());
			window.multiplyWithWindowingTable (frame.data(), (size_t) fftSize);
			fft.performFrequencyOnlyForwardTransform (frame.data(), true);

			float flux = 0.0f;

			for (size_t k = 1; k < previous.size(); ++k)
			{
				const auto magnitude = std::log1p (100.0f * frame[k]);
				flux += juce::jmax (0.0f, magnitude - previous[k]);
				previous[k] = magnitude;
			}

			envelope[(size_t) t] = t == 0 ? 0.0f : flux;
		}

		// Keep only what stands out from the local average (~0.4 s).
		const int radius = 16;
		std::vector<float> result (envelope.size(), 0.0f);
		double runningSum = 0.0;
		int count = 0;

		for (int t = -radius; t < numFrames; ++t)
		{
			if (t + radius < numFrames) { runningSum += envelope[(size_t) (t + radius)]; ++count; }
			if (t - radius - 1 >= 0)    { runningSum -= envelope[(size_t) (t - radius - 1)]; --count; }

			if (t >= 0)
				result[(size_t) t] = juce::jmax (0.0f, envelope[(size_t) t] - (float) (runningSum / juce::jmax (1, count)));
		}

		return result;
	}

	// How sharply the onsets pile up when the track is folded at this period.
	// The right period stacks every beat into the same phase bin.
	struct FoldResult { double score = 0.0; double phaseFrames = 0.0; };

	FoldResult foldAtPeriod (const std::vector<float>& envelope, double periodFrames)
	{
		constexpr int numBins = 96;
		std::array<double, numBins> bins {};

		for (size_t t = 0; t < envelope.size(); ++t)
		{
			const auto phase = std::fmod ((double) t, periodFrames) / periodFrames;
			bins[(size_t) juce::jmin (numBins - 1, (int) (phase * numBins))] += envelope[t];
		}

		// Light circular smoothing so one lucky bin doesn't win.
		std::array<double, numBins> smoothed {};
		double total = 0.0;

		for (int b = 0; b < numBins; ++b)
		{
			smoothed[(size_t) b] = 0.25 * bins[(size_t) ((b + numBins - 1) % numBins)] + 0.5 * bins[(size_t) b]
								 + 0.25 * bins[(size_t) ((b + 1) % numBins)];
			total += bins[(size_t) b];
		}

		const auto best = (int) std::distance (smoothed.begin(), std::max_element (smoothed.begin(), smoothed.end()));

		// Parabolic interpolation for a sub-bin phase.
		const auto left = smoothed[(size_t) ((best + numBins - 1) % numBins)];
		const auto centre = smoothed[(size_t) best];
		const auto right = smoothed[(size_t) ((best + 1) % numBins)];
		const auto denominator = left - 2.0 * centre + right;
		const auto offset = std::abs (denominator) > 1e-12 ? 0.5 * (left - right) / denominator : 0.0;

		FoldResult result;
		result.score = total > 0.0 ? centre / (total / numBins) : 0.0;
		result.phaseFrames = ((best + 0.5 + offset) / numBins) * periodFrames;
		return result;
	}
}

BeatGrid TempoAnalysis::analyseMono (const std::vector<float>& samples, double sampleRate, const std::function<bool()>& shouldAbort)
{
	const auto envelope = onsetEnvelope (samples, shouldAbort);

	if (envelope.empty())
		return {};

	const auto frameRate = sampleRate / hopSize;
	const auto numFrames = (int) envelope.size();

	// 1. Candidate tempos from the autocorrelation of the onset envelope, with
	//    its double-period echo added in.
	const auto minLag = (int) std::floor (frameRate * 60.0 / 200.0);
	const auto maxLag = (int) std::ceil (frameRate * 60.0 / 60.0);
	std::vector<double> acf ((size_t) (maxLag * 2 + 2), 0.0);

	for (int lag = minLag; lag < (int) acf.size() && lag < numFrames; ++lag)
	{
		double sum = 0.0;

		for (int t = 0; t + lag < numFrames; ++t)
			sum += (double) envelope[(size_t) t] * envelope[(size_t) (t + lag)];

		acf[(size_t) lag] = sum / (numFrames - lag);
	}

	if (shouldAbort())
		return {};

	// Weighted towards typical dance tempos, but only mildly: the fold below
	// decides between related tempos (e.g. 90 vs. 120 with dotted delays).
	const auto prior = [] (double bpm) { return std::exp (-0.5 * std::pow (std::log2 (bpm / 120.0) / 0.9, 2.0)); };

	// Candidates: the strongest local maxima of the autocorrelation in range.
	std::vector<std::pair<double, double>> peaks; // score, bpm

	for (int lag = minLag + 1; lag < maxLag; ++lag)
	{
		const auto score = [&] (int l) { return prior (60.0 * frameRate / l) * (acf[(size_t) l] + 0.5 * acf[(size_t) (l * 2)]); };
		const auto bpm = 60.0 * frameRate / lag;

		if (bpm >= minBpm && bpm < maxBpm && score (lag) > score (lag - 1) && score (lag) >= score (lag + 1) && score (lag) > 0.0)
			peaks.emplace_back (score (lag), bpm);
	}

	if (peaks.empty())
		return {};

	std::sort (peaks.begin(), peaks.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
	peaks.resize (juce::jmin ((size_t) 5, peaks.size()));

	if (shouldAbort())
		return {};

	// 2. Fine tempo and phase: fold the whole track at candidate periods and
	//    keep the one where the onsets line up best. Over several minutes this
	//    resolves the tempo to a few hundredths of a BPM.
	const auto periodFor = [frameRate] (double bpm) { return 60.0 * frameRate / bpm; };
	auto bestBpm = 0.0;
	auto bestWeighted = 0.0;
	FoldResult best;

	for (const auto& peak : peaks)
	{
		const auto coarseBpm = peak.second;

		for (auto bpm = coarseBpm * 0.98; bpm <= coarseBpm * 1.02; bpm += 0.01)
		{
			const auto fold = foldAtPeriod (envelope, periodFor (bpm));
			const auto weighted = fold.score * std::sqrt (prior (bpm));

			if (weighted > bestWeighted)
			{
				bestWeighted = weighted;
				best = fold;
				bestBpm = bpm;
			}
		}

		if (shouldAbort())
			return {};
	}

	if (bestBpm <= 0.0)
		return {};

	// Produced music is usually at a whole BPM; prefer it when nearly as good.
	const auto rounded = std::round (bestBpm);

	if (std::abs (rounded - bestBpm) < 0.1)
	{
		const auto fold = foldAtPeriod (envelope, periodFor (rounded));

		if (fold.score >= best.score * 0.97)
		{
			best = fold;
			bestBpm = rounded;
		}
	}

	// Octave check: folding at half the tempo keeps only every other beat. If
	// that is nearly as sharp, the in-between beats were weak off-beats and the
	// slower tempo is the real one. An odd whole BPM isn't halved: 147 is far
	// more likely than 73.5.
	const bool oddWholeBpm = juce::approximatelyEqual (bestBpm, std::round (bestBpm)) && ((int) std::round (bestBpm)) % 2 == 1;

	if (bestBpm / 2.0 >= minBpm && ! oddWholeBpm)
	{
		const auto half = foldAtPeriod (envelope, periodFor (bestBpm / 2.0));

		if (half.score >= best.score * halfTempoRatio)
		{
			best = half;
			bestBpm /= 2.0;
		}
	}

	// Frame t covers samples [t * hop, t * hop + fftSize); its onset sits near the centre.
	BeatGrid grid;
	grid.bpm = bestBpm;
	const auto firstBeatSeconds = (best.phaseFrames * hopSize + fftSize / 2.0) / sampleRate;
	grid.firstBeat = std::fmod (firstBeatSeconds, grid.beatLength());
	return grid;
}

std::optional<BarPhase::Stems> TempoAnalysis::readStems (const StemSet& set, juce::AudioFormatManager& formatManager,
														const std::function<bool()>& shouldAbort)
{
	std::array<std::unique_ptr<juce::AudioFormatReader>, StemSet::numStems> readers;
	juce::int64 length = 0;
	double sampleRate = 0.0;

	for (int i = 0; i < StemSet::numStems; ++i)
	{
		if (! set.hasStem (i))
			continue;

		readers[(size_t) i].reset (createStemReader (set, i, formatManager));

		if (readers[(size_t) i] == nullptr)
			return std::nullopt;

		sampleRate = readers[(size_t) i]->sampleRate;
		length = juce::jmax (length, readers[(size_t) i]->lengthInSamples);
	}

	if (length == 0)
		return std::nullopt;

	// Each stem to mono, decimated to ~11 kHz (averaging as a crude low-pass).
	const auto decimation = juce::jmax (1, juce::roundToInt (sampleRate / analysisRate));
	BarPhase::Stems stems;
	stems.sampleRate = sampleRate / decimation;
	for (auto& mono : stems.mono)
		mono.assign ((size_t) (length / decimation), 0.0f);

	const int chunk = 65536 - (65536 % decimation);
	juce::AudioBuffer<float> buffer (2, chunk);

	for (juce::int64 start = 0; start < length; start += chunk)
	{
		if (shouldAbort())
			return std::nullopt;

		const auto numSamples = (int) juce::jmin ((juce::int64) chunk, length - start);

		for (size_t s = 0; s < readers.size(); ++s)
		{
			auto& reader = readers[s];
			if (reader == nullptr)
				continue;   // an empty slot stays zeros
			auto& mono = stems.mono[s];
			reader->read (&buffer, 0, numSamples, start, true, true);
			const auto* left = buffer.getReadPointer (0);
			const auto* right = buffer.getReadPointer (reader->numChannels > 1 ? 1 : 0);

			for (int i = 0; i + decimation <= numSamples; i += decimation)
			{
				const auto index = (size_t) ((start + i) / decimation);

				if (index >= mono.size())
					break;

				float sum = 0.0f;
				for (int j = 0; j < decimation; ++j)
					sum += left[i + j] + right[i + j];

				mono[index] = sum / (float) (2 * decimation);
			}
		}
	}

	if (! set.isComplete())
		sections::foldIntoOneMix (stems.mono);

	return stems;
}

BeatGrid TempoAnalysis::analyseBeats (const BarPhase::Stems& stems, const std::function<bool()>& shouldAbort)
{
	// All four stems summed, so drums on any stem count.
	std::vector<float> mix;
	for (const auto& mono : stems.mono)
	{
		mix.resize (juce::jmax (mix.size(), mono.size()), 0.0f);
		for (size_t i = 0; i < mono.size(); ++i)
			mix[i] += mono[i];
	}

	return analyseMono (mix, stems.sampleRate, shouldAbort);
}

BeatGrid TempoAnalysis::findDownbeat (const BarPhase::Stems& stems, BeatGrid grid, BarPhase::Beats beats,
									  const std::function<bool()>& shouldAbort)
{
	if (! grid.isValid())
		return grid;

	grid.firstBeat = BarPhase::find (stems, grid.bpm, grid.firstBeat, beats, shouldAbort).firstDownbeat;
	return grid;
}

BeatGrid TempoAnalysis::analyse (const StemSet& set, juce::AudioFormatManager& formatManager, const std::function<bool()>& shouldAbort)
{
	const auto stems = readStems (set, formatManager, shouldAbort);
	if (! stems)
		return {};

	const auto beats = analyseBeats (*stems, shouldAbort);
	if (shouldAbort())
		return {};

	return findDownbeat (*stems, beats, BarPhase::Beats::ontoTheKick, shouldAbort);
}

BeatGrid TempoAnalysis::redetectDownbeat (const StemSet& set, juce::AudioFormatManager& formatManager, const BeatGrid& grid,
										  const std::function<bool()>& shouldAbort)
{
	const auto stems = readStems (set, formatManager, shouldAbort);
	if (! stems || shouldAbort())
		return {};

	if (! grid.isValid())
		return grid;

	auto found = grid;
	found.firstBeat = BarPhase::foundAgain (*stems, grid.bpm, grid.firstBeat, shouldAbort);
	return found;
}

//==============================================================================
AnalysisCache::AnalysisCache (const juce::File& f) : file (f)
{
	xml = juce::XmlDocument::parse (file);

	if (xml == nullptr || ! xml->hasTagName ("ANALYSIS"))
		xml = std::make_unique<juce::XmlElement> ("ANALYSIS");
}

juce::String AnalysisCache::keyFor (const StemSet& set)
{
	juce::String key;

	for (const auto& f : set.files)
		key << f.getFileName() << "|" << f.getSize() << "|" << f.getLastModificationTime().toMilliseconds() << ";";

	return juce::String::toHexString (key.hashCode64());
}

std::optional<BeatGrid> AnalysisCache::find (const StemSet& set) const
{
	if (auto* entry = xml->getChildByAttribute ("key", keyFor (set)); entry != nullptr && entry->hasAttribute ("correctedBpm"))
	{
		BeatGrid grid;
		grid.bpm = entry->getDoubleAttribute ("correctedBpm");
		grid.firstBeat = entry->getDoubleAttribute ("correctedFirstBeat");
		if (grid.isValid())
			return grid;
	}
	return findAnalysed (set);
}

std::optional<BeatGrid> AnalysisCache::findAnalysed (const StemSet& set) const
{
	if (auto* entry = xml->getChildByAttribute ("key", keyFor (set)))
	{
		BeatGrid grid;
		grid.bpm = entry->getDoubleAttribute ("bpm");
		grid.firstBeat = entry->getDoubleAttribute ("firstBeat");

		if (grid.isValid())
			return grid;
	}

	return std::nullopt;
}

CachedGrid::Entry AnalysisCache::entryOf (const juce::XmlElement& element)
{
	CachedGrid::Entry entry;
	entry.bpm = element.getDoubleAttribute ("bpm");
	entry.firstBeat = element.getDoubleAttribute ("firstBeat");
	entry.version = element.getIntAttribute ("version", 1);
	entry.corrected = element.hasAttribute ("correctedBpm");
	return entry;
}

bool AnalysisCache::needsNewDownbeat (const StemSet& set) const
{
	const auto* element = xml->getChildByAttribute ("key", keyFor (set));
	return element != nullptr && CachedGrid::needsNewDownbeat (entryOf (*element));
}

void AnalysisCache::storeNewDownbeat (const StemSet& set, double firstBeat)
{
	auto* element = xml->getChildByAttribute ("key", keyFor (set));
	if (element == nullptr)
		return;

	const auto before = entryOf (*element);
	if (! CachedGrid::needsNewDownbeat (before))
		return;   // corrected by hand or analysed anew meanwhile

	const auto after = CachedGrid::withNewDownbeat (before, firstBeat);

	element->setAttribute ("firstBeat", after.firstBeat);
	element->setAttribute ("version", after.version);
	touch (keyFor (set));
	write();
}

void AnalysisCache::storeCorrected (const StemSet& set, const BeatGrid& grid)
{
	if (auto* entry = xml->getChildByAttribute ("key", keyFor (set)))
	{
		entry->setAttribute ("correctedBpm", grid.bpm);
		entry->setAttribute ("correctedFirstBeat", grid.firstBeat);
		touch (keyFor (set));
	}
}

void AnalysisCache::clearCorrected (const StemSet& set)
{
	if (auto* entry = xml->getChildByAttribute ("key", keyFor (set)))
	{
		entry->removeAttribute ("correctedBpm");
		entry->removeAttribute ("correctedFirstBeat");
		touch (keyFor (set));
	}
}

void AnalysisCache::flush()
{
	if (unwritten)
		write();
}

void AnalysisCache::touch (const juce::String& key)
{
	touched.insert (key);
	unwritten = true;
}

void AnalysisCache::store (const StemSet& set, const BeatGrid& grid)
{
	const auto key = keyFor (set);
	auto* entry = xml->getChildByAttribute ("key", key);

	if (entry == nullptr)
	{
		entry = xml->createNewChildElement ("SET");
		entry->setAttribute ("key", key);
	}

	entry->setAttribute ("name", set.name);
	entry->setAttribute ("bpm", grid.bpm);
	entry->setAttribute ("firstBeat", grid.firstBeat);
	entry->setAttribute ("version", CachedGrid::currentVersion);
	touch (key);
	write();
}

// Under analysis.xml.lock: the file as another instance left it, with this
// one's touched sets put in. Without the lock (a folder that refuses the lock
// file) it is written whole, as before two instances shared it.
void AnalysisCache::write()
{
	file.getParentDirectory().createDirectory();
	FileLock lock (file.getSiblingFile (file.getFileName() + ".lock"));
	if (lock.acquire() == FileLock::Result::acquired)
		mergeFromDisk();
	if (xml->writeTo (file))   // through a temporary file, renamed over it
	{
		unwritten = false;
		touched.clear();
	}
}

// Every set this process did not touch is taken from the file; a touched
// set is ours, whatever the file says (the last write wins).
void AnalysisCache::mergeFromDisk()
{
	auto onDisk = juce::XmlDocument::parse (file);
	if (onDisk == nullptr || ! onDisk->hasTagName ("ANALYSIS"))
		return;

	for (const auto& key : touched)
	{
		if (auto* theirs = onDisk->getChildByAttribute ("key", key))
			onDisk->removeChildElement (theirs, true);
		if (const auto* ours = xml->getChildByAttribute ("key", key))
			onDisk->addChildElement (new juce::XmlElement (*ours));
	}
	xml = std::move (onDisk);
}

juce::File AnalysisCache::featuresFileFor (const StemSet& set) const
{
	return file.getSiblingFile ("sections").getChildFile (keyFor (set) + ".txt");
}

std::optional<sections::Features> AnalysisCache::findFeatures (const StemSet& set) const
{
	const auto featuresFile = featuresFileFor (set);

	if (! featuresFile.existsAsFile())
		return std::nullopt;

	return sections::decode (featuresFile.loadFileAsString().toStdString());
}

void AnalysisCache::storeFeatures (const StemSet& set, const sections::Features& features)
{
	const auto featuresFile = featuresFileFor (set);
	featuresFile.getParentDirectory().createDirectory();
	featuresFile.replaceWithText (juce::String (sections::encode (features)));
}
