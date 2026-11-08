#pragma once

#include <JuceHeader.h>
#include "GridEdit.h"
#include "KeyLockStretcher.h"
#include "Remote.h"
#include "StemSet.h"
#include "TempoAnalysis.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <optional>
#include <utility>

// Plays the four stems of a StemSet from one shared playhead, so they stay
// sample-locked. Output is 8 channels: stem N -> channels 2N, 2N+1.
//
// WAV/AIFF stems are memory-mapped and read directly in the audio callback,
// which makes seeking and looping instant; a background thread touches the
// pages ahead of the playhead so the callback does not wait on the disk.
// Other formats (FLAC, the library's; Ogg) are decoded whole into memory,
// as float, by background jobs when the set loads -- every position is then
// as instantly there as in a mapped file. Until they are done the deck is
// not ready: it stands, silent, and PLAY pressed meanwhile starts it the
// moment they are, from where it stands. A buffered reader in the meantime
// would read zeros after any jump, and zeros were the fault this replaced.
// Float, as the callback reads it, so 24-bit stems stay exact: a 7-minute
// stereo stem at 44.1 kHz holds 148 MB, a set of four 593 MB, decoded in
// ~0.3 s on a3coreV01. A reload frees the previous set before the next one
// takes its memory, so a deck never holds more than the larger of the two.
//
// With key lock on, each stem goes through its Rubber Band stretcher
// (KeyLockStretcher) before the resampler: the stretchers play the tempo,
// the resampler only converts the file's rate to the device's. The reported
// position is then what is heard, the stretchers' latency taken off.
class StemDeckPlayer : public juce::AudioSource,
					   private juce::TimeSliceClient
{
public:
	static constexpr int numStems = StemSet::numStems;
	static constexpr int numOutputChannels = numStems * 2;

	// The loop's seam, in what is read -- before the stretchers, so they
	// read one stream and never start again. From the seam on, the loop's
	// start plays exactly as it is, at full level from its first frame, as
	// on a CDJ; the track as it went on behind the end is added under it,
	// fading out over seamFade frames (a raised cosine) -- a micro-fade of
	// the outgoing side only. Nothing from before the loop's start is ever
	// heard. 0, a bare cut: on music snapped to the grid it clicks no more
	// than the music does (tests/spatial/PlayerLoop.cpp), and 16 or 32 only
	// cost level under key lock -- measured 2026-10-29.
	static constexpr int seamFade = 0;

	explicit StemDeckPlayer (juce::AudioFormatManager& formatManager);
	~StemDeckPlayer() override;

	// Returns an error message, or an empty string on success.
	juce::String load (const StemSet& set);
	bool isLoaded() const { return lengthInSamples.load() > 0; }
	// Loaded and every stem playable: mapped, or decoded into memory.
	bool isReady() const;
	// What the decoded stems hold, a set on its way out included.
	juce::int64 decodedBytes() const { return decodedByteCount.load(); }

	void play();
	void pause();
	void stop(); // pause and return to the start (or the loop start)
	bool isPlaying() const { return playing.load(); }

	// At the end of the track, start again from the beginning instead of stopping.
	void setRepeat (bool shouldRepeat) { repeat = shouldRepeat; }
	bool isRepeating() const { return repeat.load(); }

	// CONTINUE (PlayMode.h): at the end of the track, the staged set takes
	// over on the audio thread at the end frame and plays from its start,
	// its grid with it. REPEAT wins; a loop never reaches the end. The staged
	// set decodes beside the playing one (both are held until the change);
	// the one played out is freed by releaseRetired(), never by the audio
	// thread. A load drops what is staged.
	void setContinue (bool on) { continuing = on; }
	bool isContinuing() const { return continuing.load(); }
	// An error, or empty. Refused while the loaded set still decodes: its
	// jobs would be stopped, and a short set is staged on the next try.
	juce::String stageNext (const StemSet& set, BeatGrid grid = {});
	void clearStaged();
	bool hasStaged() const;
	bool isStagedReady() const;
	int handovers() const { return handoverCount.load(); }   // changes counted
	void releaseRetired();
	// Stopped at its end in CONTINUE with nothing staged: true once.
	bool takeRanOut() { return ranOut.exchange (false); }

	void setPosition (double seconds);
	// A jump the user makes (the overview click): onto the nearest beat when
	// quantize is on. setPosition stays raw -- the player moves the playhead
	// itself too (a loop's start, the restore).
	void jumpTo (double seconds);
	// QUANTIZE: jumps, the cue point and loop edges land on the grid's beats.
	// The switch is the user's wish and is kept (the session saves it); it
	// reads false while the deck has no grid, and then nothing snaps.
	void setQuantize (bool on) { quantizeWanted = on; }
	bool quantize() const { return quantizeWanted.load() && gridBpm.load() > 0.0; }
	bool quantizeRequested() const { return quantizeWanted.load(); }
	double getPosition() const;
	// When the audio thread last moved the position, in
	// juce::Time::getMillisecondCounterHiRes() seconds -- see positionAt().
	double getPositionStamp() const { return positionStamp.load(); }
	double getLength() const;

	// Loop between two positions in seconds; the playhead jumps to the start.
	void setLoop (double startSeconds, double endSeconds);
	// A saved loop, as it was saved: never snapped, so a restored loop never moves.
	void restoreLoop (double startSeconds, double endSeconds);
	void clearLoop();
	bool hasLoop() const { return loopEnd.load() > 0; }
	juce::Range<double> getLoop() const;
	// The loop's in and out in beats of the grid; empty without a loop or a grid.
	std::optional<std::pair<double, double>> loopBeats() const;

	// The playhead's discontinuities, for the event scheduler (EventScheduler.h).
	// Both only ever count up, across loads too. seeks: every jump -- a user's
	// or a loop set's, a scratching block, a change of the path that reports
	// the position. loopWraps: every pass from the loop's end to its start, in
	// the block whose position is past the wrap. Read the position first, then
	// these: a seek counts before it moves the position.
	std::uint32_t seeks() const { return seekCount.load(); }
	std::uint32_t loopWraps() const { return wrapCount.load(); }

	void setSpeed (double ratio);            // tempo fader
	double getSpeed() const { return speed.load(); }
	void setPitchBend (double factor) { pitchBend = factor; } // jog ring, temporary
	double getPitchBend() const { return pitchBend.load(); }
	void setSyncNudge (double factor) { syncNudge = factor; } // phase correction while synced
	double getEffectiveRate() const { return speed.load() * pitchBend.load() * syncNudge.load(); }

	// Key lock (a CDJ's MASTER TEMPO): tempo changes keep the pitch. The
	// stretchers are built on the first switch on, and anew on loading a file
	// of another sample rate -- here, never on the audio thread. Off, the
	// deck plays exactly as without it. Switching crossfades the two paths.
	void setKeyLock (bool on);
	bool isKeyLocked() const { return keyLock.load(); }

	// KEY quality: the stretchers' engine. A new engine takes over where it
	// cannot be heard starting (keylock::engineSwap): at once with KEY off
	// or the deck stopped; under a playing KEY it is built beside the
	// running stretchers, off the audio thread (the standby), and taken at
	// the next track -- a load, or CONTINUE's change on the audio thread.
	void setKeyLockEngine (keylock::Engine engine);
	keylock::Engine keyLockEngine() const { return engine.load(); }
	// The engine of the stretchers built, if any; and whether a standby waits.
	std::optional<keylock::Engine> keyLockEngineInUse() const;
	bool hasStandbyStretchers() const;

	// Scratching (jog wheel top in vinyl mode): while active the playhead
	// follows a target position that scratchBy() moves, forwards or backwards,
	// regardless of play/pause. Afterwards the deck carries on as it was.
	void beginScratch();
	void scratchBy (double seconds);
	void endScratch() { scratching = false; }
	bool isScratching() const { return scratching.load(); }

	// A new grid moves the beat under the playhead: it counts as a jump, before
	// either half is stored, so a new or half-written grid comes with its seek.
	void setBeatGrid (const BeatGrid& grid)
	{
		seekCount.fetch_add (1);
		gridBpm = grid.bpm;
		gridFirstBeat = grid.firstBeat;
	}
	BeatGrid getBeatGrid() const { return { gridBpm.load(), gridFirstBeat.load() }; }
	void setStemGain (int stem, float gain);
	float getStemGain (int stem) const { return stemGain[(size_t) stem].load(); }
	void setStemMuted (int stem, bool muted);
	bool isStemMuted (int stem) const { return stemMuted[(size_t) stem].load(); }

	// Routing only, applied by the mixer (Buses.h): which buses a stem is on,
	// as a mask. The mixer keeps it in the one-stem-per-bus rule.
	void setStemBuses (int stem, unsigned mask) { stemBuses[(size_t) stem] = mask; }
	unsigned getStemBuses (int stem) const { return stemBuses[(size_t) stem].load(); }
	bool isStemOnBus (int stem, int bus) const { return (stemBuses[(size_t) stem].load() >> bus) & 1u; }

	// A stem's cue send (Buses.h), apart from the buses: the stem straight
	// after its knob, before the mute and the deck fader. Rendered here, since
	// the mute is applied here; the mixer takes the block from getCueBlock().
	// Ramped over 10 ms on any change.
	void setStemCue (int stem, bool on) { stemCue[(size_t) stem] = on; }
	bool isStemCued (int stem) const { return stemCue[(size_t) stem].load(); }

	// The cue send of the block getNextAudioBlock just rendered: stem N on
	// channels 2N, 2N+1, numSamples long, valid on the audio thread only. 0
	// samples: nothing to take (the deck stood, or the block outgrew the room).
	const juce::AudioBuffer<float>& getCueBlock() const { return cueBuffer; }
	int getCueBlockSamples() const { return cueSamples; }

	// The stems of the block getNextAudioBlock just rendered after their
	// knob, before their mute and the fader -- what the spatial engine is fed
	// (ruling R5: it mutes after the encoder). Same layout, validity and
	// 0-samples rule as the cue block; the cue send is this block times the
	// cue switch.
	const juce::AudioBuffer<float>& getPreMuteBlock() const { return preMuteBuffer; }
	int getPreMuteBlockSamples() const { return cueSamples; }

	// Channel fader. Not applied here: the mixer puts it on every bus. The
	// stem output is after knob and mute.
	void setDeckGain (float gain) { deckGain = gain; }
	float getDeckGain() const { return deckGain.load(); }

	// Only stored here so every view can draw it; reset to 0 on load.
	void setCuePoint (double seconds) { restoreCuePoint (snappedIfQuantized (seconds)); }
	// A saved cue, as it was saved: never snapped, so a restored cue never moves.
	// Snapping can round to a beat before the track (a first beat more than
	// half a beat in); the cue then stays at the start.
	void restoreCuePoint (double seconds) { cuePoint = std::max (0.0, seconds); }
	double getCuePoint() const { return cuePoint.load(); }

	// The screen's stem meter since the last call: the stem's peak after knob
	// and mute, before the deck fader, with the bus trim (remote::beforeFader)
	// -- at the fader's top the same level as popDeskLevel.
	float popStemPeak (int stem);

	// The desk's meter for a stem since the last call (spec stemdeck-remote):
	// what the stem leaves StemDeck with on a bus -- after knob, mute, fader
	// and the bus trim (remote::sentToBus), so it reads as the desk channel's
	// input does. The same on every bus; a stem on none shows what it would
	// send. Peak of the louder side, rms over both.
	remote::Level popDeskLevel (int stem);

	// AudioSource: the buffer must have at least numOutputChannels channels.
	void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;

	// Frames the key lock stretchers have processed so far: their work, for
	// the tests. Audio thread, or a test's own.
	std::int64_t stretcherWork() const;
	void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
	void releaseResources() override;

private:
	// A stem is either mapped or decoded. The decoding jobs write `decoded`
	// before `ready`; the audio thread reads it only after.
	struct LoadedSet
	{
		explicit LoadedSet (std::atomic<juce::int64>& byteCount) : bytes (byteCount) {}
		~LoadedSet();
		std::array<std::unique_ptr<juce::MemoryMappedAudioFormatReader>, numStems> mapped; // null: decoded
		std::array<juce::AudioBuffer<float>, numStems> decoded;   // one channel for a mono stem
		juce::int64 length = 0;
		double sampleRate = 44100.0;
		std::atomic<int> pending { 0 };   // stems still decoding
		std::atomic<bool> ready { false };
		std::atomic<juce::int64>& bytes;
		BeatGrid grid;   // a staged set's, taken on at the change
		// Its decoding stops: a staged set dropped. Not by stopping the pool's
		// jobs, which may be decoding the set just handed over to as well.
		std::atomic<bool> cancelled { false };
	};

	// Opens a set's stems: mapped, or readers handed back to be decoded. An error, or empty.
	using Readers = std::array<std::shared_ptr<juce::AudioFormatReader>, StemSet::numStems>;
	juce::String open (const StemSet& set, LoadedSet& into, Readers& toDecode);
	static void startDecoding (juce::ThreadPool& pool, const std::shared_ptr<LoadedSet>& set, const Readers& toDecode);
	// At the end of the track with a set staged: it becomes the loaded one.
	// Audio thread, setLock held; false (nothing changed) if it cannot.
	bool handOver();

	// The outgoing side of a seam: the track as it went on behind the loop's
	// end, `done` frames of the fade-out gone. Audio thread only.
	struct SeamTail
	{
		juce::int64 from = 0;
		int done = 1 << 30;   // past any fade: none
	};

	// Reads the stems at the playhead; this is what the resampler pulls from.
	struct StemReader : public juce::AudioSource
	{
		explicit StemReader (StemDeckPlayer& o) : owner (o) {}
		void prepareToPlay (int, double) override {}
		void releaseResources() override {}
		void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override { owner.readStems (info); }
		StemDeckPlayer& owner;
	};

	// The key lock path: a stem's stretcher pulls the stem through this.
	struct KeyLockInput : public KeyLockStretcher::Input
	{
		KeyLockInput (StemDeckPlayer& o, int s) : owner (o), stem (s) {}
		int read (float* const* dest, int count, std::int64_t position) override;
		std::int64_t jumpFrom (std::int64_t position) override;
		StemDeckPlayer& owner;
		const int stem;
		SeamTail seam;
	};

	// A deck's stretchers, one per stem, built and swapped as one.
	struct Stretchers
	{
		Stretchers (double fileRate, keylock::Engine engine);
		bool are (double fileRate, keylock::Engine wanted) const;
		double sampleRate;
		keylock::Engine engine;
		std::array<std::unique_ptr<KeyLockStretcher>, numStems> stems;
	};

	// What the key lock path's resampler pulls from: the stretchers' output.
	struct KeyLockSource : public juce::AudioSource
	{
		explicit KeyLockSource (StemDeckPlayer& o) : owner (o) {}
		void prepareToPlay (int, double) override {}
		void releaseResources() override {}
		void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override { owner.renderStretched (info); }
		StemDeckPlayer& owner;
	};

	void readStems (const juce::AudioSourceChannelInfo& info);
	juce::int64 inLoop (juce::int64 position) const;
	void addSeamTail (int stem, float* const* dest, int count, const SeamTail& tail);
	void readRun (float* const* dest, int count, juce::int64 position);
	void readStemRun (int stem, float* const* dest, int count, juce::int64 position);
	static void decodeStem (LoadedSet&, int stem, juce::AudioFormatReader&);
	void renderPreMute (int stem, const juce::AudioSourceChannelInfo& info);
	void renderCue (int stem, int numSamples);
	void renderScratch (const juce::AudioSourceChannelInfo& info);
	void renderPlayback (const juce::AudioSourceChannelInfo& info);
	void renderPath (bool stretched, const juce::AudioSourceChannelInfo& info);
	void renderStretched (const juce::AudioSourceChannelInfo& info);
	void restartStretchers (juce::int64 position);
	double stretchedPosition() const;
	void buildStretchers();
	void prepareStandby();
	void dropStandby();
	bool takeStandby();
	void seekTo (juce::int64 position);
	void takeHeardPosition (double heard);
	void updateResamplingRatio();
	int useTimeSlice() override;

	juce::AudioFormatManager& formatManager;
	juce::TimeSliceThread diskThread { "Stem prefetch" };
	// One job per compressed stem, so a set decodes on as many cores.
	juce::ThreadPool decoders { juce::ThreadPoolOptions{}.withThreadName ("Stem decoding").withNumberOfThreads (numStems) };
	std::atomic<juce::int64> decodedByteCount { 0 };

	// Held by the audio callback for its whole duration and by load() only for
	// the pointer swap. The prefetch thread takes a shared copy, so disk I/O
	// never happens while the lock is held.
	mutable juce::CriticalSection setLock;
	std::shared_ptr<LoadedSet> loaded;
	std::atomic<juce::int64> lengthInSamples { 0 };
	// CONTINUE's, under setLock: the set to go on with, and the one played
	// out, kept for the message thread to free. Staged sets decode in their
	// own pool, so staging never stops the loaded set's decoding.
	std::shared_ptr<LoadedSet> staged, retired;
	juce::ThreadPool stagedDecoders { juce::ThreadPoolOptions{}.withThreadName ("Stem staging").withNumberOfThreads (numStems) };
	std::atomic<int> handoverCount { 0 };
	std::atomic<bool> continuing { false }, ranOut { false };

	std::atomic<bool> playing { false }, repeat { false };
	std::atomic<juce::int64> readPosition { 0 };
	std::atomic<double> positionStamp { 0.0 };
	// loopEnd == 0: no loop. While there is one the playhead never passes its
	// end: the audio thread wraps at it, and a jump behind it lands in it.
	std::atomic<juce::int64> loopStart { 0 }, loopEnd { 0 };
	std::atomic<std::uint32_t> seekCount { 0 }, wrapCount { 0 };
	std::atomic<double> fileSampleRate { 44100.0 }, deviceSampleRate { 44100.0 }, speed { 1.0 };
	std::atomic<double> pitchBend { 1.0 }, syncNudge { 1.0 };
	std::atomic<double> gridBpm { 0.0 }, gridFirstBeat { 0.0 };
	std::atomic<bool> quantizeWanted { true };
	double snappedIfQuantized (double seconds) const
	{
		return quantize() ? GridEdit::snappedPoint ({ gridBpm.load(), gridFirstBeat.load() }, seconds) : seconds;
	}

	std::atomic<bool> scratching { false };
	std::atomic<double> scratchTarget { 0.0 };        // file samples; written by the UI only
	double scratchPosition = 0.0, scratchVelocity = 0.0; // audio thread only
	float scratchGain = 0.0f;
	bool wasScratching = false;
	bool wasRendering = false;   // audio thread only
	juce::AudioBuffer<float> scratchBuffer;
	static constexpr double maxScratchSpeed = 16.0;

	std::atomic<float> deckGain { 1.0f };
	std::atomic<double> cuePoint { 0.0 };
	std::array<std::atomic<float>, numStems> stemGain;
	std::array<std::atomic<bool>, numStems> stemMuted;
	std::array<std::atomic<bool>, numStems> stemCue;
	std::array<std::atomic<unsigned>, numStems> stemBuses;
	std::array<std::atomic<float>, numStems> stemPeak;
	// The desk's meters: written by the audio thread, emptied by popDeskLevel.
	std::array<remote::LevelTap, numStems> deskLevels;
	std::array<juce::SmoothedValue<float>, numStems> gainSmoothers;
	// Audio thread only: the cue send's knob (without the mute) and switch.
	std::array<juce::SmoothedValue<float>, numStems> cueKnobSmoothers, cueSwitchSmoothers;
	juce::AudioBuffer<float> cueBuffer, preMuteBuffer;
	int cueSamples = 0;   // of both blocks

	StemReader stemReader { *this };
	juce::ResamplingAudioSource resampler { &stemReader, false, numOutputChannels };

	// Key lock. The stretchers are swapped under setLock, like the set; so
	// are the standby (a new engine waiting for the next track) and the
	// stretchers CONTINUE's change put aside, freed with the played-out set.
	std::unique_ptr<Stretchers> stretchers, standby, retiredStretchers;
	std::atomic<bool> keyLock { false };
	std::atomic<keylock::Engine> engine { keylock::Engine::standard };
	juce::ThreadPool standbyBuilder { juce::ThreadPoolOptions{}.withThreadName ("Key lock standby").withNumberOfThreads (1) };
	// A seek from outside the audio thread, for the stretchers to start
	// again there; -1: none.
	std::atomic<juce::int64> pendingSeek { -1 };
	// Audio thread only.
	bool stretching = false, restartPending = false;
	bool heardOwnsPosition = false;   // the stretchers report the position
	const Stretchers* stretchersInUse = nullptr;
	int fadeRemaining = 0, fadeLength = 1;
	juce::AudioBuffer<float> fadeBuffer;

	juce::AudioBuffer<float> seamScratch { 2, juce::jmax (1, seamFade) };
	SeamTail plainSeam;   // the plain path's
	std::array<KeyLockInput, numStems> keyLockInputs { { { *this, 0 }, { *this, 1 }, { *this, 2 }, { *this, 3 } } };
	KeyLockSource keyLockSource { *this };
	juce::ResamplingAudioSource keyLockResampler { &keyLockSource, false, numOutputChannels };

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemDeckPlayer)
};
