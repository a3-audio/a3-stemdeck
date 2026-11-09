#pragma once

#include <JuceHeader.h>
#include "CuePlay.h"
#include "DeckLoop.h"
#include "GridAction.h"
#include "HoldKey.h"
#include "PlayerTransport.h"
#include "StemDeckPlayer.h"
#include "StemThumbnails.h"
#include "Waveforms.h"
#include "JogWheel.h"
#include "PlayMode.h"
#include "Session.h"
#include "SurfaceLayout.h"

// One deck, as a column: the times, the Grid Adjust rows, track search |
// CUE | PLAY | track search, and the keys below (2026-10-08). Its overview
// (stem names on its lanes), title, BPM and pitch fader (with value, range
// key and KEY, the key lock) stand in the band under the decks and the mixer: the deck owns and
// drives them, the window holds and places them (addBandPartsTo, setBandBounds).
//
// CUE and PLAY work like a CDJ-3000's (CuePlay.h): while playing CUE jumps
// back to the cue point and stops; while stopped it sets the cue point, or,
// if already on it, plays for as long as it is held -- and PLAY meanwhile
// plays on. The loop keys are a CDJ's too (DeckLoop.h).
class DeckPanel : public juce::Component,
				  public StemSetDropTarget
{
public:
	DeckPanel (StemDeckPlayer& player, StemThumbnails& thumbnails, int deckIndex);

	void setSet (const StemSet& set);
	void setAnalysing (bool isAnalysing) { analysing = isAnalysing; }
	void refresh(); // called by the main timer

	// The keys, from the screen, the panels and the keyboard alike.
	void playPressed();
	void cuePressed();
	void cueReleased();
	void loopIn();
	void loopOut();
	void toggleLoop();     // RELOOP / EXIT
	void loopInOrOut();    // the SCS.3d's one key for both
	bool isLoopInMarked() const { return deckLoop.isInMarked(); }
	bool canReloop() const { return deckLoop.canReloop (transport); }

	// Tempo set by sync: widens the tempo range if needed and doesn't count
	// as the user moving the fader.
	void setTempoFromSync (double rate);
	void setSyncEnabled (bool enabled) { syncButton.setToggleState (enabled, juce::dontSendNotification); }

	// A controller's relative pitch slider: `steps` of 1/128 of the range's
	// full travel, moved as by hand (so it takes the deck out of sync).
	void moveTempo (double steps) { pitchFader.setValue (pitchFader.getValue() + steps * 2.0 * tempoRange / 128.0, juce::sendNotificationSync); }
	// -1 .. 1: where the tempo is in its range.
	double getTempoPosition() const { return (pitchFader.getValue() - 1.0) / tempoRange; }
	bool isSyncEnabled() const { return syncButton.getToggleState(); }
	// SYNC on but only the tempo followed: bent by hand (CDJ-3000), until SHIFT.
	void setSyncBpmOnly (bool bpmOnly) { syncButton.setButtonText (bpmOnly ? "SYNC BPM" : "SYNC"); }

	std::function<void (bool enabled)> onSyncToggled;

	// The play mode (PlayMode.h): REPEAT and, on the one-deck screen only,
	// CONTINUE -- one of them lit at most, their state their light.
	playmode::Mode playMode() const;
	void setPlayMode (playmode::Mode);
	void toggleRepeat() { setPlayMode (playmode::toggledRepeat (playMode())); }
	void toggleContinue();   // nothing where there is no CONTINUE

	// Previous / next track in the loaded track's folder (MainComponent says
	// which way there is one, and what a press does under Auto DJ).
	std::function<void (int direction)> onStep;
	void setStepsAvailable (bool previous, bool next, const juce::String& previousTip, const juce::String& nextTip);

	// MASTER, like a CDJ's: this deck's beat goes out to the Pioneer network.
	std::function<void()> onMasterPressed;
	void setMaster (bool isMaster) { masterButton.setToggleState (isMaster, juce::dontSendNotification); }
	bool isMasterLit() const { return masterButton.getToggleState(); }
	// SYNC and MASTER need another deck or the link. A -bformat instance
	// meets the others on the link (S4.7); without its ports in the truth the
	// keys are shown, not pressable, and a restored SYNC stays dark.
	void setSyncAvailable (bool available);
	void setSyncTooltip (const juce::String&);

	// The deck's own controls for the session (tempo, range, key lock, vinyl,
	// repeat, SYNC); restoring SYNC is the caller's, through onSyncToggled.
	using GridAction = ::GridAction;   // GridAction.h

	// The parts in the band: made children of `parent`, placed by it.
	void addBandPartsTo (juce::Component& parent);
	void setBandBounds (const surface::DeckTile& parts);   // window coordinates

	// The one-deck screen's: |< >| CUE PLAY and the loop block (LOOP, IN,
	// OUT) made children of `parent`, placed by it in its foot row
	// (surface::oneDeckFoot); the column loses LOOP OFF, and its other keys
	// take the room.
	void addTransportTo (juce::Component& parent);
	void setTransportBounds (const surface::TransportKeys& keys);   // `parent`'s coordinates

	// The overview in the band.
	OverviewWaveform& overviewWaveform() { return overview; }

	juce::TextButton& cueKey() { return cueButton; }
	juce::TextButton& playKey() { return playButton; }
	juce::TextButton& previousKey() { return previousButton; }
	juce::TextButton& nextKey() { return nextButton; }
	juce::TextButton& loopKey() { return loopButton; }
	juce::TextButton& loopInKey() { return loopInButton; }
	juce::TextButton& loopOutKey() { return loopOutButton; }
	juce::TextButton& loopOffKey() { return loopOffButton; }
	juce::TextButton& repeatKey() { return repeatButton; }
	juce::TextButton& continueKey() { return continueButton; }
	std::function<void (GridAction, double seconds)> onGridEdit;

	void saveState (DeckSession& state) const;
	void restoreState (const DeckSession& state);

	void paint (juce::Graphics& g) override;
	void resized() override;

private:
	void setTempoRange (double range);

	StemDeckPlayer& player;
	PlayerTransport transport { player };
	CuePlay cuePlay;
	DeckLoop deckLoop;
	const int deckIndex;
	void updateCueLight();
	void updateLoopLights();
	bool analysing = false;
	bool settingTempoFromSync = false;
	double tempoRange = 0.08;
	juce::String syncTooltip { "Lock tempo and beats to the other deck" };

	juce::Label titleLabel, stemsLabel, elapsedLabel, remainingLabel;
	juce::Label bpmLabel, bpmInfoLabel;
	OverviewWaveform overview;
	JogWheel jog;

	juce::TextButton previousButton { juce::String::fromUTF8 ("|\xe2\x97\x80") }, nextButton { juce::String::fromUTF8 ("\xe2\x96\xb6|") };
	HoldKey cueButton { "CUE" };
	juce::TextButton playButton { "PLAY" }, loopOffButton { "LOOP OFF" }, repeatButton { "REPEAT" };
	juce::TextButton continueButton { "CONTINUE" };   // the one-deck column's only
	bool hasContinue() const { return transportPlace == surface::Transport::elsewhere; }
	// The one-deck foot's loop block; their state is their light, never their text.
	juce::TextButton loopButton { "LOOP" }, loopInButton { "IN" }, loopOutButton { "OUT" };
	juce::TextButton syncButton { "SYNC" }, masterButton { "MASTER" }, vinylButton { "VINYL" };
	juce::TextButton gridButton { "GRID" };
	juce::TextButton halfBackButton { juce::String::fromUTF8 ("\xe2\x80\xb9" "\xc2\xbd") }, halfForwardButton { juce::String::fromUTF8 ("\xc2\xbd" "\xe2\x80\xba") };
	juce::TextButton snapButton { "SNAP" }, shiftButton { "SHIFT" }, resetGridButton { "RESET" };
	juce::TextButton downbeatButton { "SET 1" };
	juce::TextButton doubleTempoButton { juce::String::fromUTF8 ("\xc3\x97" "2") }, halveTempoButton { juce::String::fromUTF8 ("\xc2\xbd") };
	juce::TextButton oneBackButton { juce::String::fromUTF8 ("\xe2\x97\x80" " 1") }, oneForwardButton { juce::String::fromUTF8 ("1 " "\xe2\x96\xb6") };
	void setGridMode (bool on);
	void showPitchValue();
	surface::Metrics metrics() const;   // the window's gap and line width
	juce::Rectangle<int> transportTile;   // empty while the transport stands elsewhere
	surface::Transport transportPlace = surface::Transport::inColumn;
	juce::Slider pitchFader { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
	juce::Label pitchValue;
	juce::TextButton rangeKey;
	juce::TextButton keyLockButton { "KEY" };

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeckPanel)
};
