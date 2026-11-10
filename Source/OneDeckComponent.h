// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include "AppSettings.h"
#include "AudioOutput.h"
#include "CoreTruth.h"
#include "DeckInstance.h"
#include "DeckLink.h"
#include "OneDeckAudio.h"
#include "OneDeckSync.h"
#include "OneDeckView.h"
#include "OnScreenKeys.h"
#include "PanelDevice.h"
#include "PanelMapOneDeck.h"
#include "PanelFollowsWorkspace.h"
#include "PanelOwnership.h"
#include "PanelWorkspacePoller.h"
#include "AuxPots.h"
#include "DoublePress.h"
#include "ProLinkReceiver.h"
#include "ProLinkPresence.h"
#include "ProLinkSender.h"
#include "RecorderControls.h"
#include "Session.h"
#include "StemCreatorLink.h"
#include "StemLibrary.h"
#include "Theme.h"
#include "WorkspaceSwitch.h"

// The content of a -bformat instance (ruling R10): one deck, its JACK client
// StemDeck-<X> with the 43 one-deck ports and rec_L/R, its own session
// (~/.config/StemDeck/<X>/session.xml, imported once from the two-deck one),
// the library, the stem creator and the recorder as the two-deck screen has
// them. No buses, no Auto DJ (B15). SYNC and MASTER meet the other
// instances' decks on the link (OneDeckSync, B17), as player
// DeckInstance::proLinkDevice(). The A³ Motion panel
// (PanelMapOneDeck, B16) belongs to one instance: the one named in
// ~/.config/StemDeck/panel.lock, deck A when there is none (PanelOwnership).
//
// The screen is v4 (spec section 9): the top bar (deck letter, REC, AUTO DJ
// shown off, SYNC's source, the device, Settings), the deck (OneDeckView:
// waveform, deck panel, four stem columns, outputs) and the library with one
// Load.
class OneDeckComponent : public juce::Component,
						 private juce::Timer
{
public:
	// `pathLatencySeconds`: audio in flight to the ears (STEMDECK_PATH_LATENCY_MS).
	// `panelWorkspace` (STEMDECK_PANEL_WORKSPACE): the panel is held while that i3 workspace is in front (R9).
	explicit OneDeckComponent (DeckInstance, double pathLatencySeconds = 0.0, std::optional<std::string> panelWorkspace = std::nullopt);
	~OneDeckComponent() override;

	juce::PropertiesFile& settings() { return *appProperties.getUserSettings(); }
	// This deck's own: panel choice, phones, window place (~/.config/StemDeck/<X>/StemDeck.settings).
	juce::PropertiesFile& instanceSettings() { return *instanceProperties.getUserSettings(); }

	void paint (juce::Graphics&) override;
	void resized() override;
	// Space is PLAY (OneDeckView::spaceKey), edge-detected, never while a text field has the focus.
	bool keyPressed (const juce::KeyPress&) override;
	bool keyStateChanged (bool isKeyDown) override;

private:
	void loadSet (const StemSet&);
	void showLoaded (const StemSet&);
	// CONTINUE (PlayMode.h): stages the next set of the library's list near
	// the end, and takes on the one the player handed over to. Every tick.
	void followContinue();
	void loadById (const juce::String& setId);
	void step (int direction);
	void findSteps();
	void updatePhonesDecoder();
	void restoreSession();
	void saveSession();
	OneDeckSession gatherSession() const;
	void updateDeviceStatus();
	void keepLinks();   // the JACK links of this deck's map (DeckLinks)
	// The A³ Motion panel: held by one instance (panel.lock). Looked at every half second.
	juce::String panelPreferenceKey() const;
	void startPanel();
	void updatePanelOwner();
	void followWorkspace();   // R9: one step of the rule, then the lock follows
	void handlePanel (const panel1::Event&);
	void showPanel();
	void showSettings();
	// The link (S4.7): joined while SYNC or MASTER is on; the MASTER deck publishes.
	void startSync();
	void masterChanged();
	void updateLink();
	void showSyncStatus();
	void timerCallback() override;

	const DeckInstance instance;
	DJLookAndFeel lookAndFeel;
	juce::ApplicationProperties appProperties;
	juce::ApplicationProperties instanceProperties;
	coretruth::Chosen truth;
	std::unique_ptr<DeckLink> deckLink;             // hello and the desk's 3D for this deck
	std::unique_ptr<TruthKeeperLink> truthKeeper;   // also says whether Core is heard (R12)

	juce::AudioFormatManager formatManager;
	juce::ThreadPool analysisPool { juce::ThreadPoolOptions{}.withThreadName ("Tempo analysis").withNumberOfThreads (2) };
	AnalysisCache analysisCache { appsettings::analysisCacheFile() };
	OneDeckAudio deckAudio { formatManager, analysisPool, analysisCache };
	juce::String spatialError;
	spatial::PhonesMode phonesMode = spatial::PhonesMode::automatic;

	StemLibrary library { formatManager };
	StemCreator stemCreator;
	std::unique_ptr<StemCreatorLink> creatorLink;
	Recorder recorder;
	RecorderControls rec { recorder };
	std::unique_ptr<AudioOutput> audio;

	// The restored session: what no part of the screen keeps (the deck's
	// fader and buses, two-deck only) passes through to the next save.
	OneDeckSession restored;
	juce::String lastSessionText;
	int sessionCountdown = 0, statusCountdown = 0;

	panel::Ownership panelOwnership;
	std::unique_ptr<PanelDevice> panelDevice;
	int panelCountdown = 0;
	std::optional<PanelFollowsWorkspace> panelFollow;   // set: the panel follows the i3 workspace
	std::unique_ptr<PanelWorkspacePoller> workspacePoller;
	unsigned seenWorkspaceChanges = 0;
	AuxPots auxPots { [this] (int stem) -> StemColumn& { return view.column (stem); } };   // absolute: the deck wins
	std::array<DoublePress, StemDeckPlayer::numStems> gainPushes;   // the GAIN encoder's push: twice = reset

	OneDeckView view { deckAudio, formatManager, instance.letter - 'A' };
	juce::Label deckLabel, deviceStatus;
	juce::TextButton autoDjButton { "AUTO DJ" }, syncSourceButton { "SYNC: DECK" }, settingsButton { "Settings" };
	OnScreenKeys keys;   // KEYS and onboard: the rig has no real keyboard
	// The way out of this workspace by touch: the i3 bar is hidden on the rig.
	WorkspaceSwitch workspaceSwitch;

	osctruth::ProLinkPorts linkPorts;   // from the truth; incomplete: SYNC and MASTER greyed
	OneDeckSync sync { deckAudio.host().player(), view.deckPanel() };
	ProLinkReceiver link;
	ProLinkSender linkSender;   // reads the player: stopped before it goes
	int linkRetryCountdown = 0;
	juce::String refusedStage;   // a set the player could not open: not tried again every tick

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OneDeckComponent)
};
