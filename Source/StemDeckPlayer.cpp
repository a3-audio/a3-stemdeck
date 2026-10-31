#include "StemDeckPlayer.h"
#include "Remote.h"
#include "GridEdit.h"
#include "Buses.h"

#include <algorithm>
#include <limits>
#include <utility>


// The session keeps one mix state per stem (Session.h counts them as the buses do).
static_assert (StemDeckPlayer::numStems == buses::stemsPerDeck);

StemDeckPlayer::StemDeckPlayer (juce::AudioFormatManager& fm) : formatManager (fm)
{
	for (int i = 0; i < numStems; ++i)
	{
		stemGain[(size_t) i] = 1.0f;
		stemMuted[(size_t) i] = false;
		stemCue[(size_t) i] = false;
		stemBuses[(size_t) i] = buses::defaultMask (i);
		stemPeak[(size_t) i] = 0.0f;
	}

	diskThread.addTimeSliceClient (this);
	diskThread.startThread (juce::Thread::Priority::normal);
}

StemDeckPlayer::~StemDeckPlayer()
{
	standbyBuilder.removeAllJobs (true, 10000);
	decoders.removeAllJobs (true, 10000);
	clearStaged();
	stagedDecoders.removeAllJobs (true, 10000);
	diskThread.removeTimeSliceClient (this);
	diskThread.stopThread (2000);

	const juce::ScopedLock sl (setLock);
	loaded.reset();
}

StemDeckPlayer::LoadedSet::~LoadedSet()
{
	for (const auto& frames : decoded)
		bytes -= (juce::int64) frames.getNumChannels() * frames.getNumSamples() * (juce::int64) sizeof (float);
}

bool StemDeckPlayer::isReady() const
{
	const juce::ScopedLock sl (setLock);
	return loaded != nullptr && loaded->ready.load();
}

// Opens every file of `set` into `into`: mapped where the format allows,
// else a reader handed back in `toDecode` for the decoding jobs. An empty
// slot stays neither mapped nor decoded: readStemRun reads it as zeros.
juce::String StemDeckPlayer::open (const StemSet& set, LoadedSet& into, Readers& toDecode)
{
	if (set.numLoaded() == 0)
		return "The set has no file: " + set.name;
	if (set.isPreparing())
		return "Still decoding this NI Stems file, ready in a few seconds: " + set.name;

	bool rateTaken = false;
	for (int i = 0; i < numStems; ++i)
	{
		if (! set.hasStem (i))
			continue;
		const auto& file = set.files[(size_t) i];
		auto* format = formatManager.findFormatForFileExtension (file.getFileExtension());

		if (format == nullptr)
			return "Unbekanntes Format: " + file.getFileName();

		juce::AudioFormatReader* reader = nullptr;

		if (std::unique_ptr<juce::MemoryMappedAudioFormatReader> mapped { format->createMemoryMappedReader (file) };
			mapped != nullptr && mapped->mapEntireFile())
		{
			reader = mapped.get();
			into.mapped[(size_t) i] = std::move (mapped);
		}
		else if (auto* plain = formatManager.createReaderFor (file); plain != nullptr && plain->lengthInSamples > 0)
		{
			reader = plain;
			toDecode[(size_t) i].reset (plain);
		}
		else
		{
			delete plain;
		}

		if (reader == nullptr)
			return "Cannot read the file: " + file.getFileName();

		if (! std::exchange (rateTaken, true))
			into.sampleRate = reader->sampleRate;
		else if (! juce::approximatelyEqual (reader->sampleRate, into.sampleRate))
			return "Stems haben unterschiedliche Samplerates: " + set.name;

		into.length = juce::jmax (into.length, reader->lengthInSamples);
	}

	const auto numToDecode = (int) std::count_if (toDecode.begin(), toDecode.end(), [] (const auto& r) { return r != nullptr; });
	into.pending = numToDecode;
	into.ready = numToDecode == 0;
	return {};
}

void StemDeckPlayer::startDecoding (juce::ThreadPool& pool, const std::shared_ptr<LoadedSet>& set, const Readers& toDecode)
{
	for (int s = 0; s < numStems; ++s)
		if (auto reader = toDecode[(size_t) s])
			pool.addJob ([set, s, reader] { decodeStem (*set, s, *reader); });
}

// Opens the stems and swaps the set in at once; the stems not mapped are
// decoded afterwards by `decoders`, and the set is ready when they are. The
// previous set is stopped and freed before the new one's memory is taken,
// so a reload holds no more than the larger of the two. A staged set goes too.
juce::String StemDeckPlayer::load (const StemSet& set)
{
	auto newSet = std::make_shared<LoadedSet> (decodedByteCount);
	Readers toDecode;
	if (auto error = open (set, *newSet, toDecode); error.isNotEmpty())
		return error;

	// The previous set's decoding stops before its memory goes.
	decoders.removeAllJobs (true, 10000);
	clearStaged();
	releaseRetired();

	playing = false;
	scratching = false;
	ranOut = false;
	loopEnd = 0;
	gridBpm = 0.0;
	loopStart = 0;
	cuePoint = 0.0;
	fileSampleRate = newSet->sampleRate;
	lengthInSamples = newSet->length;
	seekTo (0);

	{
		const juce::ScopedLock sl (setLock);
		std::swap (loaded, newSet);
	}

	// The previous set (now in newSet) is released here, outside the lock,
	// before the new one's stems take their memory.
	newSet.reset();
	resampler.flushBuffers();
	updateResamplingRatio();
	startDecoding (decoders, loaded, toDecode);

	if (keyLock.load())
		buildStretchers();   // for this file's sample rate
	return {};
}

juce::String StemDeckPlayer::stageNext (const StemSet& set, BeatGrid grid)
{
	if (! isReady())
		return "The loaded set is still decoding";

	clearStaged();
	releaseRetired();

	auto next = std::make_shared<LoadedSet> (decodedByteCount);
	next->grid = grid;
	Readers toDecode;
	if (auto error = open (set, *next, toDecode); error.isNotEmpty())
		return error;

	{
		const juce::ScopedLock sl (setLock);
		staged = next;
	}
	startDecoding (stagedDecoders, next, toDecode);
	return {};
}

// The staged set's decoding stops, and its memory goes outside the lock --
// or with its last decoding job.
void StemDeckPlayer::clearStaged()
{
	std::shared_ptr<LoadedSet> dropped;   // freed after the lock is let go: declared before it
	const juce::ScopedLock sl (setLock);
	std::swap (dropped, staged);
	if (dropped != nullptr)
		dropped->cancelled = true;
}

bool StemDeckPlayer::hasStaged() const
{
	const juce::ScopedLock sl (setLock);
	return staged != nullptr;
}

bool StemDeckPlayer::isStagedReady() const
{
	const juce::ScopedLock sl (setLock);
	return staged != nullptr && staged->ready.load();
}

void StemDeckPlayer::releaseRetired()
{
	std::shared_ptr<LoadedSet> old;
	std::unique_ptr<Stretchers> oldStretchers;
	{
		const juce::ScopedLock sl (setLock);
		std::swap (old, retired);
		std::swap (oldStretchers, retiredStretchers);
	}
}

// Nothing is freed here: the played-out set waits in `retired` for the
// message thread (releaseRetired), so a change waits for one release.
// Under key lock the stretchers go on from the new set's start
// (restartPending); the plain path's resampler simply reads on.
bool StemDeckPlayer::handOver()
{
	if (staged == nullptr || retired != nullptr || ! continuing.load())
		return false;

	retired = std::move (loaded);
	loaded = std::move (staged);
	fileSampleRate = loaded->sampleRate;
	lengthInSamples = loaded->length;
	gridBpm = loaded->grid.bpm;
	gridFirstBeat = loaded->grid.firstBeat;
	cuePoint = 0.0;
	loopEnd = 0;
	loopStart = 0;
	plainSeam = {};
	readPosition = 0;
	takeStandby();
	++handoverCount;
	return true;
}

// At CONTINUE's change, with setLock held on the audio thread: a standby
// for the new set's rate takes over -- a pointer swap; the running
// stretchers wait in retiredStretchers for releaseRetired().
bool StemDeckPlayer::takeStandby()
{
	if (standby == nullptr || retiredStretchers != nullptr || ! standby->are (fileSampleRate.load(), engine.load()))
		return false;

	retiredStretchers = std::move (stretchers);
	stretchers = std::move (standby);
	return true;
}

void StemDeckPlayer::play()
{
	if (! isLoaded())
		return;

	if (readPosition.load() >= lengthInSamples.load())
		seekTo (hasLoop() ? loopStart.load() : 0);

	playing = true;
}

void StemDeckPlayer::pause()
{
	playing = false;
}

void StemDeckPlayer::stop()
{
	playing = false;
	seekTo (hasLoop() ? loopStart.load() : 0);
	resampler.flushBuffers();
}

void StemDeckPlayer::setPosition (double seconds)
{
	seekTo (juce::jlimit ((juce::int64) 0, lengthInSamples.load(), (juce::int64) (seconds * fileSampleRate.load())));
	resampler.flushBuffers();
}

// Written here for the views, and handed to the audio thread so the key
// lock path starts again from there; the plain path reads readPosition.
// While looping, a jump behind the loop's end -- the sync's phase jump, a
// jog search -- lands in the loop: the loop the screen shows is the one
// that plays.
void StemDeckPlayer::seekTo (juce::int64 position)
{
	position = inLoop (position);
	readPosition = position;
	pendingSeek = position;
}

// `position` as the loop plays it: at or behind its end, folded back by
// whole loops; anywhere else, as it is (before the loop the deck plays into it).
juce::int64 StemDeckPlayer::inLoop (juce::int64 position) const
{
	const auto start = loopStart.load(), end = loopEnd.load();
	if (end <= start || position < end)
		return position;
	return start + (position - start) % (end - start);
}

double StemDeckPlayer::getPosition() const
{
	return (double) readPosition.load() / fileSampleRate.load();
}

double StemDeckPlayer::getLength() const
{
	return (double) lengthInSamples.load() / fileSampleRate.load();
}

void StemDeckPlayer::setLoop (double requestedStart, double requestedEnd)
{
	// On the beat grid, from wherever it comes: the overview drag, the
	// controller's loop in/out, a restored session (2026-09-30).
	const auto snapped = GridEdit::snappedLoop ({ gridBpm.load(), gridFirstBeat.load() }, requestedStart, requestedEnd);
	const auto startSeconds = snapped.start, endSeconds = snapped.end;
	const auto rate = fileSampleRate.load();
	const auto length = lengthInSamples.load();
	const auto start = juce::jlimit ((juce::int64) 0, length, (juce::int64) (startSeconds * rate));
	const auto end = juce::jlimit ((juce::int64) 0, length, (juce::int64) (endSeconds * rate));

	// Reject empty or reversed regions rather than handing them to the audio thread.
	if (end - start < (juce::int64) (rate * 0.01))
		return;

	loopEnd = 0; // disable while the bounds change
	loopStart = start;
	loopEnd = end;
	setPosition (startSeconds);
}

// One stem, whole, into its set's memory, in pieces so a newer load can
// stop it. The last stem done makes the set ready. A stem that cannot be
// held or read stays silent, as an unreadable stretch of a file would.
void StemDeckPlayer::decodeStem (LoadedSet& set, int stem, juce::AudioFormatReader& reader)
{
	auto* job = juce::ThreadPoolJob::getCurrentThreadPoolJob();
	const auto shouldStop = [job, &set] { return set.cancelled.load() || (job != nullptr && job->shouldExit()); };
	auto& frames = set.decoded[(size_t) stem];
	const auto channels = reader.numChannels > 1 ? 2 : 1;
	const auto length = (int) juce::jmin (reader.lengthInSamples, (juce::int64) std::numeric_limits<int>::max());

	try
	{
		frames.setSize (channels, length);
		set.bytes += (juce::int64) channels * length * (juce::int64) sizeof (float);
	}
	catch (const std::bad_alloc&)
	{
		frames.setSize (0, 0);
	}

	constexpr int piece = 1 << 16;
	for (int at = 0; at < frames.getNumSamples(); at += piece)
	{
		if (shouldStop())
			return;
		reader.read (&frames, at, juce::jmin (piece, length - at), at, true, channels > 1);
	}

	if (set.pending.fetch_sub (1) == 1)
		set.ready.store (true, std::memory_order_release);
}

void StemDeckPlayer::clearLoop()
{
	loopEnd = 0;
}

juce::Range<double> StemDeckPlayer::getLoop() const
{
	const auto rate = fileSampleRate.load();
	return hasLoop() ? juce::Range<double> ((double) loopStart.load() / rate, (double) loopEnd.load() / rate)
					 : juce::Range<double>();
}

void StemDeckPlayer::setSpeed (double ratio)
{
	speed = ratio;
	updateResamplingRatio();
}

void StemDeckPlayer::setStemGain (int stem, float gain)
{
	stemGain[(size_t) stem] = gain;
}

void StemDeckPlayer::setStemMuted (int stem, bool muted)
{
	stemMuted[(size_t) stem] = muted;
}

float StemDeckPlayer::popStemPeak (int stem)
{
	return stemPeak[(size_t) stem].exchange (0.0f);
}

remote::Level StemDeckPlayer::popDeskLevel (int stem)
{
	return deskLevels[(size_t) stem].pop();
}

// Switched on, it starts anew (crossfaded from the plain path): the
// stretchers are built for the engine chosen. Already on, nothing changes
// under the ear.
void StemDeckPlayer::setKeyLock (bool on)
{
	if (on && ! keyLock.load())
		buildStretchers();

	keyLock = on;
}

void StemDeckPlayer::setKeyLockEngine (keylock::Engine chosen)
{
	if (engine.exchange (chosen) == chosen)
		return;

	dropStandby();   // one for the engine chosen before is no use now

	{
		const juce::ScopedLock sl (setLock);
		if (stretchers == nullptr || stretchers->engine == chosen)
			return;   // built when KEY is first switched on; or already this one
	}

	if (keylock::engineSwap (keyLock.load(), playing.load()) == keylock::EngineSwap::now)
		buildStretchers();
	else
		prepareStandby();
}

std::optional<keylock::Engine> StemDeckPlayer::keyLockEngineInUse() const
{
	const juce::ScopedLock sl (setLock);
	if (stretchers == nullptr)
		return std::nullopt;
	return stretchers->engine;
}

bool StemDeckPlayer::hasStandbyStretchers() const
{
	const juce::ScopedLock sl (setLock);
	return standby != nullptr;
}

StemDeckPlayer::Stretchers::Stretchers (double fileRate, keylock::Engine chosen)
	: sampleRate (fileRate), engine (chosen)
{
	const auto alignment = KeyLockStretcher::alignmentFor (fileRate, chosen);
	for (auto& stem : stems)
		stem = std::make_unique<KeyLockStretcher> (2, fileRate, chosen, alignment);
}

bool StemDeckPlayer::Stretchers::are (double fileRate, keylock::Engine wanted) const
{
	return engine == wanted && juce::approximatelyEqual (sampleRate, fileRate);
}

// Builds the stretchers for the loaded file's sample rate and the engine
// chosen, unless they are there -- or waiting as the standby; allocates,
// so never on the audio thread.
void StemDeckPlayer::buildStretchers()
{
	const auto rate = fileSampleRate.load();
	const auto chosen = engine.load();
	std::unique_ptr<Stretchers> fresh;

	{
		const juce::ScopedLock sl (setLock);
		if (stretchers != nullptr && stretchers->are (rate, chosen))
			return;
		if (standby != nullptr && standby->are (rate, chosen))
			fresh = std::move (standby);
	}

	if (fresh == nullptr)
		fresh = std::make_unique<Stretchers> (rate, chosen);

	{
		const juce::ScopedLock sl (setLock);
		std::swap (stretchers, fresh);
	}
	// The old ones (now in fresh) go here, outside the lock.
}

// The standby, built on its own thread: for the rate playing and the engine
// chosen, kept only if both still hold when it is done.
void StemDeckPlayer::prepareStandby()
{
	const auto rate = fileSampleRate.load();
	const auto chosen = engine.load();

	standbyBuilder.addJob ([this, rate, chosen]
	{
		auto built = std::make_unique<Stretchers> (rate, chosen);
		const juce::ScopedLock sl (setLock);
		const bool wanted = engine.load() == chosen && juce::approximatelyEqual (fileSampleRate.load(), rate)
						 && (stretchers == nullptr || ! stretchers->are (rate, chosen));
		if (wanted)
			std::swap (standby, built);
		// What is in `built` now -- the unwanted one, or a standby it
		// replaced -- is freed after the lock (declared before it).
	});
}

void StemDeckPlayer::dropStandby()
{
	std::unique_ptr<Stretchers> dropped;
	const juce::ScopedLock sl (setLock);
	std::swap (dropped, standby);
}

void StemDeckPlayer::updateResamplingRatio()
{
	resampler.setResamplingRatio (getEffectiveRate() * fileSampleRate.load() / deviceSampleRate.load());
}

void StemDeckPlayer::beginScratch()
{
	scratchTarget = (double) readPosition.load();
	scratching = true;
}

void StemDeckPlayer::scratchBy (double seconds)
{
	const auto target = scratchTarget.load() + seconds * fileSampleRate.load();
	scratchTarget = juce::jlimit (0.0, (double) lengthInSamples.load(), target);
}

//==============================================================================
void StemDeckPlayer::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
	deviceSampleRate = sampleRate;
	updateResamplingRatio();
	resampler.prepareToPlay (samplesPerBlockExpected, sampleRate);
	keyLockResampler.prepareToPlay (samplesPerBlockExpected, sampleRate);

	// Room for one block at the highest scratch speed (plus interpolation margin).
	const auto maxBlock = juce::jmax (samplesPerBlockExpected, 4096);
	scratchBuffer.setSize (numOutputChannels, (int) (maxBlock * maxScratchSpeed * 4) + 8);

	// Key lock on or off while playing: the two paths crossfade, short
	// enough to sound like a switch, long enough not to click.
	fadeBuffer.setSize (numOutputChannels, maxBlock);
	fadeLength = juce::jmax (1, juce::roundToInt (sampleRate * 0.02));

	cueBuffer.setSize (numOutputChannels, maxBlock);
	cueBuffer.clear();
	preMuteBuffer.setSize (numOutputChannels, maxBlock);
	preMuteBuffer.clear();
	cueSamples = 0;

	for (int i = 0; i < numStems; ++i)
	{
		gainSmoothers[(size_t) i].reset (sampleRate, 0.02);
		gainSmoothers[(size_t) i].setCurrentAndTargetValue (stemMuted[(size_t) i] ? 0.0f : stemGain[(size_t) i].load());
		cueKnobSmoothers[(size_t) i].reset (sampleRate, 0.02);
		cueKnobSmoothers[(size_t) i].setCurrentAndTargetValue (stemGain[(size_t) i].load());
		cueSwitchSmoothers[(size_t) i].reset (sampleRate, 0.01);
		cueSwitchSmoothers[(size_t) i].setCurrentAndTargetValue (stemCue[(size_t) i].load() ? 1.0f : 0.0f);
	}
}

void StemDeckPlayer::releaseResources()
{
	resampler.releaseResources();
	keyLockResampler.releaseResources();
}

void StemDeckPlayer::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
	jassert (info.buffer->getNumChannels() >= numOutputChannels);

	// Only ever contended for a pointer copy/swap, so waiting beats a dropout.
	const juce::ScopedLock sl (setLock);
	cueSamples = 0;

	const bool scratchNow = scratching.load() && loaded != nullptr;

	if (scratchNow != wasScratching)
	{
		if (scratchNow)
		{
			// Hand on the record: start from the current motion, then follow the hand.
			scratchPosition = (double) readPosition.load();
			scratchVelocity = playing.load() ? getEffectiveRate() * fileSampleRate.load() / deviceSampleRate.load() : 0.0;
			scratchGain = playing.load() ? 1.0f : 0.0f;
		}
		else
		{
			resampler.flushBuffers();
			restartPending = true;   // the key lock path goes on from where the hand left the record
		}

		wasScratching = scratchNow;
	}

	// Before its stems are decoded the deck stands: PLAY waits for them.
	if (loaded == nullptr || ! loaded->ready.load (std::memory_order_acquire) || (! playing.load() && ! scratchNow))
	{
		wasRendering = false;
		info.clearActiveBufferRegion();
		return;
	}

	// From a stop, the stems start at their set level: a mute set while the
	// deck stood holds from the first sample instead of letting 20 ms of the
	// stem through.
	if (! wasRendering)
		for (int s = 0; s < numStems; ++s)
		{
			gainSmoothers[(size_t) s].setCurrentAndTargetValue (stemMuted[(size_t) s] ? 0.0f : stemGain[(size_t) s].load());
			cueKnobSmoothers[(size_t) s].setCurrentAndTargetValue (stemGain[(size_t) s].load());
			cueSwitchSmoothers[(size_t) s].setCurrentAndTargetValue (stemCue[(size_t) s].load() ? 1.0f : 0.0f);
		}
	wasRendering = true;

	if (scratchNow)
		renderScratch (info);
	else
		renderPlayback (info);

	// The position is final for this block now: stamped here, the UI carries
	// it forward from this moment (positionAt, PIO sync).
	positionStamp = juce::Time::getMillisecondCounterHiRes() / 1000.0;

	const auto cueFits = info.numSamples <= cueBuffer.getNumSamples();
	if (cueFits)
		cueSamples = info.numSamples;

	for (int s = 0; s < numStems; ++s)
	{
		if (cueFits)
		{
			renderPreMute (s, info);
			renderCue (s, info.numSamples);
		}

		auto& smoother = gainSmoothers[(size_t) s];
		smoother.setTargetValue (stemMuted[(size_t) s] ? 0.0f : stemGain[(size_t) s].load());

		const auto from = smoother.getCurrentValue();
		smoother.skip (info.numSamples);
		const auto to = smoother.getCurrentValue();

		float peak = 0.0f;
		double squares = 0.0;

		for (int ch = s * 2; ch < s * 2 + 2; ++ch)
		{
			info.buffer->applyGainRamp (ch, info.startSample, info.numSamples, from, to);
			peak = juce::jmax (peak, info.buffer->getMagnitude (ch, info.startSample, info.numSamples));
			const double rms = info.buffer->getRMSLevel (ch, info.startSample, info.numSamples);
			squares += rms * rms * info.numSamples;
		}

		// One measurement, two meters. The desk's: as the stem leaves on its
		// bus, scaled rather than measured on the bus, which sums every stem
		// switched to it. The screen's, behind the deck's volume fader: before
		// that fader, as a DJ mixer's channel meter.
		const remote::Block afterKnob { peak, squares, 2 * info.numSamples };
		deskLevels[(size_t) s].add (remote::sentToBus (afterKnob, deckGain.load()));
		const auto screenPeak = remote::beforeFader (afterKnob).peak;
		if (screenPeak > stemPeak[(size_t) s].load())
			stemPeak[(size_t) s] = screenPeak;
	}
}

// The stem as it is before its mute: the knob alone. Called from
// getNextAudioBlock with setLock held, before the stem's own gain is applied
// to `info`; nothing is allocated here.
void StemDeckPlayer::renderPreMute (int stem, const juce::AudioSourceChannelInfo& info)
{
	auto& knob = cueKnobSmoothers[(size_t) stem];
	knob.setTargetValue (stemGain[(size_t) stem].load());

	const auto knobFrom = knob.getCurrentValue();
	knob.skip (info.numSamples);
	const auto knobTo = knob.getCurrentValue();

	for (int ch = stem * 2; ch < stem * 2 + 2; ++ch)
	{
		preMuteBuffer.copyFrom (ch, 0, *info.buffer, ch, info.startSample, info.numSamples);
		preMuteBuffer.applyGainRamp (ch, 0, info.numSamples, knobFrom, knobTo);
	}
}

// The cue send: the pre-mute stem through the cue switch. Called right after
// renderPreMute for the same stem.
void StemDeckPlayer::renderCue (int stem, int numSamples)
{
	auto& send = cueSwitchSmoothers[(size_t) stem];
	send.setTargetValue (stemCue[(size_t) stem].load() ? 1.0f : 0.0f);

	const auto sendFrom = send.getCurrentValue();
	send.skip (numSamples);
	const auto sendTo = send.getCurrentValue();

	for (int ch = stem * 2; ch < stem * 2 + 2; ++ch)
	{
		if (sendFrom == 0.0f && sendTo == 0.0f)
		{
			cueBuffer.clear (ch, 0, numSamples);
			continue;
		}
		cueBuffer.copyFrom (ch, 0, preMuteBuffer, ch, 0, numSamples);
		cueBuffer.applyGainRamp (ch, 0, numSamples, sendFrom, sendTo);
	}
}

// Variable-speed playback in either direction, following scratchTarget.
// Called from getNextAudioBlock with setLock held.
void StemDeckPlayer::renderScratch (const juce::AudioSourceChannelInfo& info)
{
	const auto numSamples = info.numSamples;
	const auto normalSpeed = fileSampleRate.load() / deviceSampleRate.load();
	const auto maxSpeed = maxScratchSpeed * normalSpeed;

	// Velocity eases towards what reaches the target within this block, so
	// hand movements arriving at UI rate become smooth motion.
	const auto wanted = (scratchTarget.load() - scratchPosition) / numSamples;
	scratchVelocity += (wanted - scratchVelocity) * 0.5;
	scratchVelocity = juce::jlimit (-maxSpeed, maxSpeed, scratchVelocity);

	const auto start = scratchPosition;
	const auto end = scratchPosition + scratchVelocity * numSamples;
	const auto first = (juce::int64) std::floor (juce::jmin (start, end)) - 1;
	const auto count = juce::jmin (scratchBuffer.getNumSamples(), (int) (std::ceil (juce::jmax (start, end)) - (double) first) + 3);

	for (int s = 0; s < numStems; ++s)
	{
		float* dest[2] = { scratchBuffer.getWritePointer (s * 2), scratchBuffer.getWritePointer (s * 2 + 1) };
		readStemRun (s, dest, count, first);
	}

	// A record held still is silent, not a frozen sample: fade with speed.
	const auto targetGain = (float) juce::jlimit (0.0, 1.0, std::abs (scratchVelocity) / (0.05 * normalSpeed));
	const auto gainStep = (targetGain - scratchGain) / (float) numSamples;

	for (int ch = 0; ch < numOutputChannels; ++ch)
	{
		const auto* source = scratchBuffer.getReadPointer (ch);
		auto* out = info.buffer->getWritePointer (ch, info.startSample);
		auto gain = scratchGain;

		for (int i = 0; i < numSamples; ++i)
		{
			const auto p = start + scratchVelocity * i - (double) first;
			const auto index = juce::jlimit (0, count - 2, (int) p);
			const auto frac = (float) (p - index);
			out[i] = (source[index] + frac * (source[index + 1] - source[index])) * gain;
			gain += gainStep;
		}
	}

	scratchGain = targetGain;
	scratchPosition = juce::jlimit (0.0, (double) loaded->length, end);
	readPosition = (juce::int64) scratchPosition;
}

// Called by the resampler from within getNextAudioBlock, with setLock held.
// The loop's end is checked at every run, not only where a run ends: a
// playhead that came to stand at or behind it by any way is folded back.
void StemDeckPlayer::readStems (const juce::AudioSourceChannelInfo& info)
{
	auto* buffer = info.buffer;
	const auto startPos = readPosition.load();
	auto pos = inLoop (startPos);
	int done = 0;

	while (done < info.numSamples)
	{
		const auto lEnd = loopEnd.load();
		const auto stopAt = lEnd > 0 && pos < lEnd ? lEnd : loaded->length;
		const auto numToRead = (int) juce::jmin ((juce::int64) (info.numSamples - done), stopAt - pos);

		if (numToRead <= 0)
		{
			// End of track: start over, go on with the staged set, or stop.
			if (repeat.load() && loaded->length > 0)
			{
				pos = 0;
				continue;
			}

			const auto handedOver = handOver();
			if (handedOver && loaded->ready.load (std::memory_order_acquire))
			{
				pos = 0;
				continue;
			}

			for (int ch = 0; ch < numOutputChannels; ++ch)
				buffer->clear (ch, info.startSample + done, info.numSamples - done);

			// Handed over to a set still decoding: it stands, playing, until it is ready.
			if (handedOver)
			{
				pos = 0;
				break;
			}
			ranOut = continuing.load();
			playing = false;
			break;
		}

		std::array<float*, numOutputChannels> dest;
		for (int ch = 0; ch < numOutputChannels; ++ch)
			dest[(size_t) ch] = buffer->getWritePointer (ch, info.startSample + done);
		readRun (dest.data(), numToRead, pos);
		for (int s = 0; s < numStems; ++s)
			addSeamTail (s, dest.data() + s * 2, numToRead, plainSeam);
		plainSeam.done += numToRead;

		pos += numToRead;
		done += numToRead;

		if (lEnd > 0 && pos == lEnd)
			plainSeam = { lEnd, 0 };
		pos = inLoop (pos);
	}

	// If the UI seeked meanwhile, its position wins.
	auto expected = startPos;
	readPosition.compare_exchange_strong (expected, pos);
}

// Adds a seam's outgoing side under `count` frames of one stem: what
// followed the loop's end, fading out on a raised cosine. The loop's own
// frames in `dest` stay as they are.
void StemDeckPlayer::addSeamTail (int stem, float* const* dest, int count, const SeamTail& tail)
{
	const auto n = juce::jmin (count, seamFade - tail.done);
	if (n <= 0)
		return;

	float* behind[2] = { seamScratch.getWritePointer (0), seamScratch.getWritePointer (1) };
	readStemRun (stem, behind, n, tail.from + tail.done);

	for (int ch = 0; ch < 2; ++ch)
		for (int i = 0; i < n; ++i)
		{
			const auto t = juce::MathConstants<float>::pi * ((float) (tail.done + i) + 0.5f) / (float) seamFade;
			dest[ch][i] += 0.5f * (1.0f + std::cos (t)) * behind[ch][i];
		}
}

// Every stem's `count` frames from `position` on, stem N into dest[2N], dest[2N+1].
// Audio thread, with setLock held.
void StemDeckPlayer::readRun (float* const* dest, int count, juce::int64 position)
{
	for (int s = 0; s < numStems; ++s)
		readStemRun (s, dest + s * 2, count, position);
}

namespace
{
// `count` frames of a decoded stem from `position` into dest[0], dest[1]:
// zeros where it has none, a mono stem on both.
void readDecoded (const juce::AudioBuffer<float>& frames, float* const* dest, int count, juce::int64 position)
{
	const auto length = (juce::int64) frames.getNumSamples();
	const auto from = (int) juce::jlimit ((juce::int64) 0, (juce::int64) count, -position);
	const auto to = (int) juce::jlimit ((juce::int64) from, (juce::int64) count, length - position);

	for (int ch = 0; ch < 2; ++ch)
	{
		juce::FloatVectorOperations::clear (dest[ch], from);
		if (to > from)
			juce::FloatVectorOperations::copy (dest[ch] + from, frames.getReadPointer (juce::jmin (ch, frames.getNumChannels() - 1), (int) (position + from)), to - from);
		juce::FloatVectorOperations::clear (dest[ch] + to, count - to);
	}
}
}

// One stem's `count` frames into dest[0], dest[1]; a mono stem on both.
// Audio thread, with setLock held and the set ready.
void StemDeckPlayer::readStemRun (int stem, float* const* dest, int count, juce::int64 position)
{
	auto* reader = loaded->mapped[(size_t) stem].get();
	if (reader == nullptr)
	{
		readDecoded (loaded->decoded[(size_t) stem], dest, count, position);
		return;
	}

	float* pair[2] = { dest[0], dest[1] };
	if (reader->numChannels == 1)
	{
		reader->read (pair, 1, position, count);
		juce::FloatVectorOperations::copy (pair[1], pair[0], count);
	}
	else
	{
		reader->read (pair, 2, position, count);
	}
}

//==============================================================================
// Playing, not scratching: the plain path or the key lock path, and for
// fadeLength after a switch both, crossfaded. Audio thread, setLock held.
void StemDeckPlayer::renderPlayback (const juce::AudioSourceChannelInfo& info)
{
	const auto seek = pendingSeek.exchange (-1);
	const bool ready = stretchers != nullptr && juce::approximatelyEqual (stretchers->sampleRate, fileSampleRate.load());
	const bool wanted = keylock::usesStretcher (keyLock.load(), ready, false);

	if (stretchers.get() != stretchersInUse)
	{
		stretchersInUse = stretchers.get();
		restartPending = true;
	}

	if (stretching && ! ready)
	{
		stretching = false;   // its stretchers are gone: the plain path, as it is
		fadeRemaining = 0;
	}

	if (seek >= 0)
		fadeRemaining = 0;   // a jump is a cut anyway

	if (stretching && (seek >= 0 || restartPending))
		restartStretchers (seek >= 0 ? seek : readPosition.load());

	restartPending = false;

	if (wanted != stretching)
	{
		if (wanted)
		{
			restartStretchers (readPosition.load());   // takes over where the plain path is
		}
		else
		{
			// The plain path goes on from what is heard.
			readPosition = juce::jlimit ((juce::int64) 0, loaded->length, (juce::int64) stretchedPosition());
			resampler.flushBuffers();
		}

		stretching = wanted;
		fadeRemaining = fadeLength;
	}

	if (fadeRemaining > 0 && info.numSamples <= fadeBuffer.getNumSamples())
	{
		const juce::AudioSourceChannelInfo outgoing { &fadeBuffer, 0, info.numSamples };
		renderPath (! stretching, outgoing);
		renderPath (stretching, info);

		for (int ch = 0; ch < numOutputChannels; ++ch)
		{
			const auto* from = fadeBuffer.getReadPointer (ch);
			auto* to = info.buffer->getWritePointer (ch, info.startSample);

			for (int i = 0; i < info.numSamples; ++i)
			{
				const auto in = juce::jlimit (0.0f, 1.0f, 1.0f - (float) (fadeRemaining - i) / (float) fadeLength);
				to[i] = to[i] * in + from[i] * (1.0f - in);
			}
		}

		fadeRemaining = juce::jmax (0, fadeRemaining - info.numSamples);
	}
	else
	{
		fadeRemaining = 0;
		renderPath (stretching, info);
	}

	// While the plain path still fades out, it owns the position; then the
	// stretchers report what is heard. A seek meanwhile wins.
	if (stretching && fadeRemaining == 0)
	{
		const auto heard = stretchedPosition();

		if (stretchers->stems[0]->hasEnded() && heard >= (double) loaded->length)
		{
			// The stretchers go on from the new set's start in the next block.
			if (handOver())
			{
				restartPending = true;
				return;
			}
			ranOut = continuing.load();
			playing = false;
		}

		if (pendingSeek.load() < 0)
			readPosition = juce::jlimit ((juce::int64) 0, loaded->length, (juce::int64) heard);
	}
}

void StemDeckPlayer::renderPath (bool stretched, const juce::AudioSourceChannelInfo& info)
{
	if (stretched)
	{
		keyLockResampler.setResamplingRatio (keylock::resamplingRatio (getEffectiveRate(), fileSampleRate.load(), deviceSampleRate.load(), true));
		keyLockResampler.getNextAudioBlock (info);
	}
	else
	{
		updateResamplingRatio(); // follows pitch bend and sync nudge block by block
		resampler.getNextAudioBlock (info);
	}
}

// Called by keyLockResampler from within renderPath, with setLock held.
void StemDeckPlayer::renderStretched (const juce::AudioSourceChannelInfo& info)
{
	const auto rate = getEffectiveRate();

	for (int s = 0; s < numStems; ++s)
	{
		float* pair[2] = { info.buffer->getWritePointer (s * 2, info.startSample),
						   info.buffer->getWritePointer (s * 2 + 1, info.startSample) };
		stretchers->stems[(size_t) s]->render (pair, info.numSamples, rate, keyLockInputs[(size_t) s]);
	}
}

std::int64_t StemDeckPlayer::stretcherWork() const
{
	std::int64_t work = 0;
	if (stretchers != nullptr)
		for (const auto& stem : stretchers->stems)
			work += stem->framesProcessed();
	return work;
}

void StemDeckPlayer::restartStretchers (juce::int64 position)
{
	const auto rate = getEffectiveRate();

	for (auto& input : keyLockInputs)
		input.seam = {};   // a jump is a cut: no seam's tail under it

	for (int s = 0; s < numStems; ++s)
		stretchers->stems[(size_t) s]->restart (position, rate, keyLockInputs[(size_t) s]);

	keyLockResampler.flushBuffers();
}

// The first stem's playhead stands for the deck's: the stems' agree to a
// fraction of a millisecond, and around a loop's end an average of them
// would be neither end.
double StemDeckPlayer::stretchedPosition() const
{
	return stretchers->stems[0]->position();
}

// As readStems() reads: up to the loop's end, then jumpFrom() goes on at
// its start, the seam's outgoing side under it; without a loop, up to the
// track's end.
// At or behind the end it reads nothing, so the jump comes -- a read that
// had ended exactly on the end once read on from there as if there were no
// loop, and after a few passes the deck left its loop.
int StemDeckPlayer::KeyLockInput::read (float* const* dest, int count, std::int64_t position)
{
	const auto start = owner.loopStart.load(), end = owner.loopEnd.load();
	const bool looping = end > start;
	const auto stopAt = looping ? end : owner.loaded->length;
	const auto n = (int) juce::jlimit ((juce::int64) 0, (juce::int64) count, stopAt - position);

	if (n > 0)
	{
		owner.readStemRun (stem, dest, n, position);
		owner.addSeamTail (stem, dest, n, seam);
		seam.done += n;
	}
	return n;
}

std::int64_t StemDeckPlayer::KeyLockInput::jumpFrom (std::int64_t position)
{
	const auto start = owner.loopStart.load(), end = owner.loopEnd.load();

	if (end > start && position >= end)
	{
		seam = { end, 0 };
		return start;
	}
	if (position >= owner.loaded->length && owner.repeat.load() && owner.loaded->length > 0)
		return 0;
	return -1;
}

// Background thread: touch the mapped pages ahead of the playhead and around
// the loop start so the audio callback reads from memory, not from disk.
int StemDeckPlayer::useTimeSlice()
{
	std::shared_ptr<LoadedSet> set;

	{
		const juce::ScopedLock sl (setLock);
		set = loaded;
	}

	if (set == nullptr)
		return 100;

	const auto rate = (juce::int64) set->sampleRate;
	const auto touchRange = [&set] (juce::int64 start, juce::int64 numSamples)
	{
		const auto end = juce::jmin (set->length, start + numSamples);

		for (const auto& reader : set->mapped)
			if (reader != nullptr)
				for (auto s = juce::jmax ((juce::int64) 0, start); s < end; s += 256)
					reader->touchSample (s);
	};

	touchRange (readPosition.load() - rate * 2, rate * 10); // a little behind too, for scratching

	if (hasLoop())
		touchRange (loopStart.load(), rate * 2);

	return 50;
}
