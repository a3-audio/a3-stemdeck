// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DeckHost.h"

#include "FollowLeader.h"
#include "GridEdit.h"
#include "SectionAnalysis.h"

#include <utility>

namespace
{
double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

std::function<bool()> abortWithJob()
{
	auto* job = juce::ThreadPoolJob::getCurrentThreadPoolJob();
	return [job] { return job != nullptr && job->shouldExit(); };
}
}

DeckHost::DeckHost (juce::AudioFormatManager& formatManager, juce::ThreadPool& analysisPool, AnalysisCache& cache,
                    outputs::SpatialLayout layout)
	: formats (formatManager), pool (analysisPool), analysisCache (cache), ports (layout)
{
}

DeckHost::~DeckHost() = default;

//==============================================================================
bool DeckHost::prepare (double sampleRate, int samplesPerBlockExpected)
{
	if (samplesPerBlockExpected <= 0)
		return false;
	stemPairs.setSize (StemDeckPlayer::numOutputChannels, juce::jmax (samplesPerBlockExpected, 4096));
	deckPlayer.prepareToPlay (samplesPerBlockExpected, sampleRate);
	return ports.prepare (sampleRate, samplesPerBlockExpected);
}

const juce::AudioBuffer<float>& DeckHost::render (juce::AudioBuffer<float>& spatialOutputs, int firstChannel, int numSamples)
{
	if (numSamples > stemPairs.getNumSamples())
		stemPairs.setSize (StemDeckPlayer::numOutputChannels, numSamples, false, false, true);

	deckPlayer.getNextAudioBlock (juce::AudioSourceChannelInfo (&stemPairs, 0, numSamples));
	ports.render (deckPlayer, spatialOutputs, firstChannel, numSamples);
	return stemPairs;
}

//==============================================================================
juce::String DeckHost::load (const StemSet& newSet)
{
	if (const auto error = deckPlayer.load (newSet); error.isNotEmpty())
		return error;

	stagedSet.reset();
	seenHandovers = deckPlayer.handovers();
	takeOn (newSet);
	return {};
}

juce::String DeckHost::stageNext (const StemSet& next)
{
	const auto grid = analysisCache.find (next);
	if (auto error = deckPlayer.stageNext (next, grid ? *grid : BeatGrid {}); error.isNotEmpty())
		return error;
	stagedSet = next;
	return {};
}

void DeckHost::clearStaged()
{
	deckPlayer.clearStaged();
	stagedSet.reset();
}

juce::String DeckHost::stagedSetId() const
{
	return stagedSet ? stagedSet->firstFile().getFullPathName() : juce::String();
}

// The player swapped the staged set in on the audio thread: it is the
// loaded one now, and the played-out one is freed here.
bool DeckHost::followHandover()
{
	const auto handovers = deckPlayer.handovers();
	if (handovers == seenHandovers)
		return false;
	seenHandovers = handovers;
	deckPlayer.releaseRetired();
	if (stagedSet)
		takeOn (*std::exchange (stagedSet, std::nullopt));
	return true;
}

// The set the player holds now, and its grid: from the cache, else analysed.
void DeckHost::takeOn (const StemSet& newSet)
{
	setId = newSet.firstFile().getFullPathName();
	set = newSet;
	++generation;
	features.reset();
	bars.clear();

	if (const auto cached = analysisCache.find (newSet))
	{
		deckPlayer.setBeatGrid (*cached);
		setAnalysing (false);

		// A set analysed before the sections existed gets them now.
		features = analysisCache.findFeatures (newSet);
		if (features)
			rebuildSections();
		else
			startSectionAnalysis (*cached);
		if (analysisCache.needsNewDownbeat (newSet))
			startDownbeatAnalysis();
	}
	else
	{
		startAnalysis();
	}
}

void DeckHost::startAnalysis()
{
	setAnalysing (true);
	juce::WeakReference<DeckHost> safeThis (this);

	pool.addJob ([safeThis, &formats = formats, analysed = *set, loaded = generation]
	{
		const auto grid = TempoAnalysis::analyse (analysed, formats, abortWithJob());

		juce::MessageManager::callAsync ([safeThis, analysed, loaded, grid]
		{
			if (safeThis == nullptr)
				return;

			if (grid.isValid())
			{
				safeThis->analysisCache.store (analysed, grid);
				safeThis->analysisStored();
			}

			// Only apply it if the deck still has that set.
			if (safeThis->generation != loaded)
				return;
			safeThis->deckPlayer.setBeatGrid (grid);
			safeThis->setAnalysing (false);

			if (grid.isValid())
				safeThis->startSectionAnalysis (grid);
		});
	});
}

void DeckHost::startSectionAnalysis (const BeatGrid& grid)
{
	juce::WeakReference<DeckHost> safeThis (this);

	pool.addJob ([safeThis, &formats = formats, analysed = *set, loaded = generation, grid]
	{
		const auto found = SectionAnalysis::analyse (analysed, formats, grid, abortWithJob());

		juce::MessageManager::callAsync ([safeThis, analysed, loaded, found]
		{
			if (safeThis == nullptr || ! found)
				return;

			safeThis->analysisCache.storeFeatures (analysed, *found);

			// Only apply them if the deck still has that set.
			if (safeThis->generation != loaded)
				return;

			safeThis->features = found;
			safeThis->rebuildSections();
		});
	});
}

// A grid from before the one was found from the stems is played with at
// once; its first beat, onto the kick and the one, follows a few seconds later.
void DeckHost::startDownbeatAnalysis()
{
	const auto analysedGrid = analysisCache.findAnalysed (*set);
	if (! analysedGrid)
		return;

	juce::WeakReference<DeckHost> safeThis (this);

	pool.addJob ([safeThis, &formats = formats, analysed = *set, loaded = generation, grid = *analysedGrid]
	{
		const auto found = TempoAnalysis::redetectDownbeat (analysed, formats, grid, abortWithJob());

		juce::MessageManager::callAsync ([safeThis, analysed, loaded, found]
		{
			if (safeThis == nullptr || ! found.isValid())
				return;

			safeThis->analysisCache.storeNewDownbeat (analysed, found.firstBeat);
			safeThis->analysisStored();

			// A correction made meanwhile is what find() returns, and stays.
			if (safeThis->generation == loaded)
				if (const auto current = safeThis->analysisCache.find (analysed))
				{
					safeThis->deckPlayer.setBeatGrid (*current);
					// The sections are counted in bars from the one.
					safeThis->rebuildSections();
				}
		});
	});
}

void DeckHost::rebuildSections()
{
	const auto grid = deckPlayer.getBeatGrid();

	if (! features || ! set || ! grid.isValid())
	{
		bars.clear();
		return;
	}

	const auto& names = set->stemNames;
	const auto roles = set->isComplete() ? sections::rolesFor ({ names[0].toStdString(), names[1].toStdString(),
																   names[2].toStdString(), names[3].toStdString() })
										 : sections::oneMixRoles;   // what SectionAnalysis measured
	bars = sections::classify (sections::barLevels (*features, grid.bpm, grid.firstBeat, roles));
}

void DeckHost::setAnalysing (bool analysing)
{
	if (onAnalysing)
		onAnalysing (analysing);
}

void DeckHost::analysisStored()
{
	if (onAnalysisStored)
		onAnalysisStored();
}

//==============================================================================
bool DeckHost::editGrid (GridAction action, double seconds, std::optional<double> leaderBeatPhase)
{
	const auto current = deckPlayer.getBeatGrid();
	if (! set || ! current.isValid())
		return false;

	using Action = GridAction;
	if (action == Action::reset)
	{
		analysisCache.clearCorrected (*set);
		const auto analysed = analysisCache.findAnalysed (*set);
		if (analysed)
		{
			deckPlayer.setBeatGrid (*analysed);
			rebuildSections();
		}
		return analysed.has_value();
	}

	// Where the playhead is now, carried from the last audio block.
	const auto playhead = [this]
	{
		return positionAt (deckPlayer.getPosition(), deckPlayer.getPositionStamp(), nowSeconds(),
						   deckPlayer.getEffectiveRate(), deckPlayer.isPlaying() && ! deckPlayer.isScratching());
	};

	GridEdit::Grid grid { current.bpm, current.firstBeat };
	switch (action)
	{
		case Action::shift:              grid = GridEdit::shift (grid, seconds); break;
		case Action::halfBack:           grid = GridEdit::shiftHalfBeat (grid, false); break;
		case Action::halfForward:        grid = GridEdit::shiftHalfBeat (grid, true); break;
		case Action::oneBack:            grid = GridEdit::moveOne (grid, false); break;
		case Action::oneForward:         grid = GridEdit::moveOne (grid, true); break;
		case Action::doubleTempo:        grid = GridEdit::doubleTempo (grid); break;
		case Action::halveTempo:         grid = GridEdit::halveTempo (grid); break;
		case Action::snapToCue:          grid = GridEdit::snapToCue (grid, deckPlayer.getCuePoint()); break;
		case Action::downbeatAtPlayhead: grid = GridEdit::snapToCue (grid, playhead()); break;
		case Action::shiftToLeader:
			if (! leaderBeatPhase)
				return false;
			grid = GridEdit::shiftToPhase (grid, playhead(), *leaderBeatPhase);
			break;
		case Action::reset: break;
	}

	const BeatGrid edited { grid.bpm, grid.firstBeat };
	deckPlayer.setBeatGrid (edited);
	analysisCache.storeCorrected (*set, edited);

	// The bars follow the downbeat at once: the next preview says so (Preview.h).
	rebuildSections();
	return true;
}

//==============================================================================
// The spatial state is the deck's DeckSpatial::Controls. With no spatial engine
// (IEM refused) nothing changes it, so the values read at restore are written
// back as they were: a session saved meanwhile must not reset them.
void DeckHost::gather (DeckSession& deck) const
{
	deck.position = deckPlayer.getPosition();
	deck.playing = deckPlayer.isPlaying();
	deck.cuePoint = deckPlayer.getCuePoint();
	deck.loop = deckPlayer.hasLoop() ? deckPlayer.getLoop() : juce::Range<double>();

	const auto* spatial = ports.deckSpatial();
	for (size_t s = 0; s < deck.stems.size(); ++s)
	{
		// The motion object exists with or without an engine and remembers the
		// length it was given, so this needs no carried copy.
		deck.stems[s].lengthBars = ports.motion().lengthBars ((int) s);
		if (spatial == nullptr)
		{
			deck.stems[s].azimuth = restoredSpatial[s].azimuth;
			deck.stems[s].elevation = restoredSpatial[s].elevation;
			deck.stems[s].width = restoredSpatial[s].width;
			deck.stems[s].aux = restoredSpatial[s].aux;
			continue;
		}
		const auto& controls = ports.settings();
		deck.stems[s].azimuth = controls.azimuth[s].load();
		deck.stems[s].elevation = controls.elevation[s].load();
		deck.stems[s].width = controls.width[s].load();
		deck.stems[s].aux = controls.aux[s].load();
	}
}

void DeckHost::restoreSpatial (const DeckSession& deck)
{
	restoredSpatial = deck.stems;
	for (size_t s = 0; s < deck.stems.size(); ++s)
		ports.motion().setLength ((int) s, deck.stems[s].lengthBars);

	if (ports.deckSpatial() == nullptr)
		return;

	auto& controls = ports.settings();
	for (size_t s = 0; s < deck.stems.size(); ++s)
	{
		controls.azimuth[s].store ((float) deck.stems[s].azimuth);
		controls.elevation[s].store ((float) deck.stems[s].elevation);
		controls.width[s].store ((float) deck.stems[s].width);
		controls.aux[s].store ((float) deck.stems[s].aux);
	}
}

void DeckHost::restoreTransport (const DeckSession& deck)
{
	deckPlayer.setCuePoint (deck.cuePoint);
	if (! deck.loop.isEmpty())
		deckPlayer.setLoop (deck.loop.getStart(), deck.loop.getEnd());
	deckPlayer.setPosition (deck.position);
}
