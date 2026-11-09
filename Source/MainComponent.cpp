#include "MainComponent.h"
#include "AppSettings.h"
#include "CoreTruth.h"
#include "DeckLinks.h"
#include "AudioCpu.h"
#include "StemJob.h"
#include "GridEdit.h"
#include "OscTruthFile.h"
#include "SyncLabels.h"
#include "Tips.h"
#include "SurfaceJuce.h"

#include <cstdlib>
#include <iostream>

//==============================================================================
MainComponent::MainComponent()
{
	setLookAndFeel (&lookAndFeel);

	// The one truth, chosen once and hashed at once (spec truth-from-core):
	// the keeper's own fingerprint must be of this truth, not of a cache
	// Motion may write a moment later.
	{
		const auto truth = coretruth::choose();
		truthPath = truth.path;
		truthHash = truth.hash;
	}

	// Where Pro DJ Link listens: the one truth. Without it the PIO clock
	// does not start and its status line says why.
	{
		std::string truthError;
		proLinkPorts = osctruth::proLinkPortsFrom (osctruth::readListeners (truthPath, truthError));
		if (! truthError.empty())
			std::cerr << "StemDeck: " << truthError << std::endl;
	}

	// How every meter on screen moves: Core's numbers, one set for the
	// system (decided 2026-10-07). A new truth restarts StemDeck, so once is enough.
	{
		std::string truthError;
		const auto meterParameters = osctruth::readMeterParameters (truthPath, truthError);
		mixer.setMeterParameters (meterParameters);
		rec.setMeterParameters (meterParameters);
	}

	appProperties.setStorageParameters (appsettings::options());

	formatManager.registerBasicFormats();
	for (int d = 0; d < numDecks; ++d)
	{
		auto& host = hosts[(size_t) d];
		host.onAnalysing = [this, d] (bool analysing) { decks[(size_t) d]->setAnalysing (analysing); };
		host.onAnalysisStored = [this] { library.analysisChanged(); };
	}
	showTips (tips::shown (settings().getValue (tips::settingKey).toStdString()));

	for (int d = 0; d < numDecks; ++d)
	{
		addAndMakeVisible (waves[(size_t) d]);
		addAndMakeVisible (decks[(size_t) d]);
		decks[(size_t) d]->addBandPartsTo (*this);
		waves[(size_t) d]->onSetDropped = [this, d] (const juce::String& id) { loadDroppedSet (id, d); };
		decks[(size_t) d]->onSetDropped = [this, d] (const juce::String& id) { loadDroppedSet (id, d); };
		decks[(size_t) d]->onSyncToggled = [this, d] (bool enabled) { setSync (d, enabled); };
		decks[(size_t) d]->onGridEdit = [this, d] (DeckPanel::GridAction action, double seconds) { editGrid (d, action, seconds); };
		decks[(size_t) d]->onMasterPressed = [this, d] { pressMaster (d); };
		decks[(size_t) d]->onStep = [this, d] (int direction) { stepDeck (d, direction); };

		auto& strip = eventStrips[(size_t) d];
		strip = std::make_unique<EventStrip> (hosts[(size_t) d], d, 2);
		strip->showOn (*waves[(size_t) d], decks[(size_t) d]->overviewWaveform());
		addAndMakeVisible (*strip);
		mixer.strip (d).onOneShotPressed = [this, d] (int stem, motionlite::Shape shape, bool shift, std::uint64_t seed)
		{
			eventStrips[(size_t) d]->pressRecorded (stem, shape, shift, seed);
		};
	}

	addAndMakeVisible (mixer);
	mixer.addBandPartsTo (*this);
	addAndMakeVisible (library);

	library.onLoadSet = [this] (const StemSet& set, int deckIndex) { loadSet (set, deckIndex); };
	library.lookUpBeatGrid = [this] (const StemSet& set) { return analysisCache.find (set); };
	library.onOrderChanged = [this] { findSteps(); };
	creatorLink = std::make_unique<StemCreatorLink> (library, stemCreator, settings(), *this);

	settingsButton.onClick = [this] { showSettings(); };
	settingsButton.setMouseClickGrabsKeyboardFocus (false);
	addAndMakeVisible (settingsButton);
	// The on-screen keyboard for a rig without a real one. The button must not
	// take the focus, or pressing it would leave the text field it is for.
	keysButton.setTooltip (keyboard.isAvailable() ? "On-screen keyboard (onboard): show or hide"
												  : "On-screen keyboard: onboard is not installed");
	keysButton.onClick = [this] { keyboard.keysPressed(); };
	keysButton.setMouseClickGrabsKeyboardFocus (false);
	addAndMakeVisible (keysButton);
	keyboard.onHint = [this] (const juce::String& hint)
	{
		keysButton.setTooltip (hint);
		deviceStatus.setText (hint, juce::dontSendNotification);
		deviceStatus.setColour (juce::Label::textColourId, Theme::cue);
		statusCountdown = 240;   // four seconds at 60 Hz, then the device again
	};
	workspaceSwitch.attachTo (*this);
	deviceStatus.setColour (juce::Label::textColourId, Theme::textDim);
	addAndMakeVisible (deviceStatus);

	liveBadge.setJustificationType (juce::Justification::centred);
	liveBadge.setColour (juce::Label::textColourId, Theme::cue);
	liveBadge.setColour (juce::Label::outlineColourId, Theme::cue);
	liveBadge.setInterceptsMouseClicks (false, false);
	addChildComponent (liveBadge);
	remote.onLiveChanged = [this] (bool live)
	{
		liveBadge.setVisible (live);
		resized();
	};

	rec.button.onClick = [this] { rec.toggle (audio->jackClient().getSampleRate()); };
	rec.addTo (*this);

	autoDjButton.setClickingTogglesState (true);
	autoDjButton.setMouseClickGrabsKeyboardFocus (false);
	autoDjButton.setColour (juce::TextButton::buttonOnColourId, Theme::play);
	for (auto* player : players)
		player->setKeyLockEngine (appsettings::keyLockEngine (settings()));
	setAutoDjFade ({ settings().getIntValue ("autoDjOverlapBars", AutoDj::defaultMixBars),
					 settings().getDoubleValue ("autoDjMixSeconds", AutoDj::defaultNoGridMixSeconds) });
	autoDjButton.onClick = [this] { setAutoDj (autoDjButton.getToggleState()); };
	addAndMakeVisible (autoDjButton);

	// SYNC source: the other deck, or the Pioneer tempo master (PIO).
	syncSourceButton.setClickingTogglesState (true);
	syncSourceButton.setMouseClickGrabsKeyboardFocus (false);
	syncSourceButton.onClick = [this] { setSyncSource (syncSourceButton.getToggleState()); };
	addAndMakeVisible (syncSourceButton);
	pioStatus.setColour (juce::Label::textColourId, Theme::textDim);
	addAndMakeVisible (pioStatus);
	for (int player = 1; player <= 4; ++player)
		pioPlayer.addItem (syncLabels::player (player), player);
	pioPlayer.setTooltip ("No master info: the player to follow");
	pioPlayer.onChange = [this]
	{
		pioClock.choosePlayer (pioPlayer.getSelectedId());
		appProperties.getUserSettings()->setValue ("pioPlayer", pioPlayer.getSelectedId());
	};
	addChildComponent (pioPlayer);
	if (const auto player = appProperties.getUserSettings()->getIntValue ("pioPlayer", 0); player > 0)
		pioClock.choosePlayer (player);

	// Mixxx-like keys: D/L play, S/K cue (hold to preview), 1-4 / 7-0 stem mutes.
	const auto bind = [this] (int key, std::function<void (bool)> action) { keyBindings.push_back ({ key, std::move (action) }); };
	bind ('d', [this] (bool down) { if (down) deckA.playPressed(); });
	bind ('l', [this] (bool down) { if (down) deckB.playPressed(); });
	bind ('s', [this] (bool down) { down ? deckA.cuePressed() : deckA.cueReleased(); });
	bind ('k', [this] (bool down) { down ? deckB.cuePressed() : deckB.cueReleased(); });

	for (int s = 0; s < StemSet::numStems; ++s)
	{
		bind ('1' + s, [this, s] (bool down) { if (down) mixer.strip (0).toggleMute (s); });
		bind ("7890"[s], [this, s] (bool down) { if (down) mixer.strip (1).toggleMute (s); });
	}

	createSpatialPorts();
	for (int d = 0; d < numDecks; ++d)
	{
		auto& ports = hosts[(size_t) d].spatialPorts();
		mixer.setSpatial (d, ports.deckSpatial() != nullptr ? &ports : nullptr);
	}
	initialiseAudio();

	if (appProperties.getUserSettings()->getValue ("syncSource") == "pio")
	{
		syncSourceButton.setToggleState (true, juce::dontSendNotification);
		setSyncSource (true);
	}
	updatePioneerStatus();

	setWantsKeyboardFocus (true);
	setSize (1500, 960);
	restoreSession();
	startTimerHz (60);

	// Every change of the switches goes to Core, from any path; whatever the
	// restore (and the Auto-DJ taking over at start) changed before the link
	// runs goes when it starts -- Core asks on our hello only when the hello
	// is news to it, and a quick restart is not (2026-10-14).
	mixer.onBusesChanged = [this] (const buses::Masks& now, int answered) { remote.busesChanged (now, answered); };
	static_assert (AutoDj::auxBus == buses::aux && AutoDj::numStems == buses::stemsPerDeck);
	mixer.onDjSwitch = [this] (int deck, int stem, int bus, bool on) { autoDj.djSwitched (deck, stem, bus, on); };
	remote.masterDeck = [this] { return masterDeck; };
	remote.start (truthPath, truthHash);

	motionPanel = std::make_unique<PanelDevice>();
	motionPanel->onEvent = [this] (const panel::Event& e) { handlePanel (e); };

	truthKeeper = coretruth::follow ({ truthPath, truthHash });
}

MainComponent::~MainComponent()
{
	stopTimer();
	motionPanel.reset();   // dark, and no more presses
	saveSession();
	audio->closeJack();  // no more input blocks ...
	recorder.stop();     // ... then the file is closed complete
	pioSender.setMaster (nullptr);
	pioSender.stop();   // before the players it reads go away
	proLink.stop();
	analysisPool.removeAllJobs (true, 5000);
	audio->close();
	setLookAndFeel (nullptr);
}

//==============================================================================
void MainComponent::loadSet (const StemSet& set, int deckIndex)
{
	// No deck given (double-click, Enter): first deck that isn't playing.
	if (deckIndex < 0)
	{
		for (int d = 0; d < numDecks && deckIndex < 0; ++d)
			if (! players[(size_t) d]->isPlaying())
				deckIndex = d;

		if (deckIndex < 0)
			return;
	}

	const auto d = (size_t) deckIndex;
	if (const auto error = hosts[d].load (set); error.isNotEmpty())
	{
		juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Cannot load the set", error);
		return;
	}

	thumbs[d]->setSet (set);
	decks[d]->setSet (set);
	waves[d]->setTitle (set.name);
	findSteps();
}

void MainComponent::sendPreview()
{
	std::array<preview::DeckState, numDecks> states;

	for (int d = 0; d < numDecks; ++d)
	{
		const auto& player = *players[(size_t) d];
		const auto grid = player.getBeatGrid();
		auto& state = states[(size_t) d];
		state.view = { player.isPlaying(), player.getDeckGain() };
		state.generation = hosts[(size_t) d].loadGeneration();
		state.speed = player.getSpeed();
		state.bpm = grid.bpm;
		state.firstBeat = grid.firstBeat;
		state.position = player.getPosition();
		if (player.hasLoop())
			state.loopSeconds = std::make_pair (player.getLoop().getStart(), player.getLoop().getEnd());
		state.bars = hosts[(size_t) d].sectionBars();
	}

	const auto moment = preview::momentOf (states, masterDeck);
	if (previewGate.shouldSend (moment, juce::Time::getMillisecondCounterHiRes() / 1000.0))
		remote.sendAhead (moment.ahead);
}

//==============================================================================
void MainComponent::setSyncBent (int deckIndex, bool bent)
{
	deckfollow::setBent (*decks[(size_t) deckIndex], *players[(size_t) deckIndex], syncBent[(size_t) deckIndex], bent);
}

void MainComponent::setSync (int deckIndex, bool enabled)
{
	setSyncBent (deckIndex, false);

	if (pioSource)
	{
		// Every deck follows the master on its own; both may be on at once.
		pioSynced[(size_t) deckIndex] = enabled;
		pioMultiple[(size_t) deckIndex] = 0.0; // chosen on the first update
		if (! enabled)
			players[(size_t) deckIndex]->setSyncNudge (1.0);
		return;
	}

	if (enabled)
	{
		// Only one follower: the other deck becomes the leader.
		const auto other = 1 - deckIndex;
		decks[(size_t) other]->setSyncEnabled (false);
		players[(size_t) other]->setSyncNudge (1.0);
		syncFollower = deckIndex;
		syncMultiple = 0.0; // chosen on the first update with both grids known
		updateSync();
	}
	else if (syncFollower == deckIndex)
	{
		players[(size_t) deckIndex]->setSyncNudge (1.0);
		syncFollower = -1;
	}
}

// Keeps the follower at the leader's tempo and nudges its beats into phase.
void MainComponent::updateSync()
{
	if (pioSource)
	{
		followPioneer();
		return;
	}

	if (syncFollower < 0)
		return;

	auto& follower = *players[(size_t) syncFollower];
	auto& leader = *players[(size_t) (1 - syncFollower)];
	const auto followerGrid = follower.getBeatGrid();
	const auto leaderGrid = leader.getBeatGrid();

	if (! followerGrid.isValid() || ! leaderGrid.isValid())
		return;

	FollowInput in;
	in.leaderBpm = leaderGrid.bpm * leader.getSpeed();
	in.leaderBeatPhase = leaderGrid.beatsAt (leader.getPosition());
	in.leaderPlaying = leader.isPlaying();
	in.anyScratching = follower.isScratching() || leader.isScratching();
	in.multiple = syncMultiple;
	in.alignBars = true;   // deck to deck: the downbeats line up too

	// Both positions move with the same audio block, so they compare as read.
	syncMultiple = applyFollow (syncFollower, in, follower.getPosition());
}

void MainComponent::setSyncSource (bool pio)
{
	// Whatever was synced stops: a deck that followed the other deck must not
	// suddenly follow a CDJ, or the other way round, without being asked.
	for (int d = 0; d < numDecks; ++d)
	{
		decks[(size_t) d]->setSyncEnabled (false);
		players[(size_t) d]->setSyncNudge (1.0);
	}
	syncFollower = -1;
	pioSynced = {};

	pioSource = pio;
	syncSourceButton.setButtonText (syncLabels::sourceButton (pio));
	appProperties.getUserSettings()->setValue ("syncSource", pio ? "pio" : "deck");

	updateNetwork();
	updatePioneerStatus();
}

// Every deck with SYNC on against the Pioneer clock.
void MainComponent::followPioneer()
{
	for (auto& event : proLink.drain())
	{
		if (const auto* beat = std::get_if<prolink::BeatPacket> (&event.packet))
			pioClock.onBeat (*beat, event.seconds);
		else if (const auto* status = std::get_if<prolink::StatusPacket> (&event.packet))
			pioClock.onStatus (*status, event.seconds);
	}

	const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
	if (pioClock.bpm() <= 0.0)
		return; // nothing heard yet: nothing to follow

	for (int d = 0; d < numDecks; ++d)
	{
		if (! pioSynced[(size_t) d] || ! players[(size_t) d]->getBeatGrid().isValid())
			continue;

		auto in = leaderInputAt (pioClock, now); // silent master: tempo held, phase left alone
		in.anyScratching = players[(size_t) d]->isScratching();
		in.multiple = pioMultiple[(size_t) d];

		// The Pioneer phase is for now; the deck's position is from its last
		// audio block. Carried forward, or the gap jitters by up to a block
		// and the nudge wobbles the pitch.
		auto& player = *players[(size_t) d];
		const auto position = positionAt (player.getPosition(), player.getPositionStamp(), now,
										  player.getEffectiveRate(), player.isPlaying() && ! player.isScratching());
		pioMultiple[(size_t) d] = applyFollow (d, in, position);
	}
}

void MainComponent::updatePioneerStatus()
{
	// Not on the link -- the network was not up yet at start-up, or the ports
	// were busy: try again every two seconds, and say why meanwhile.
	if (proLinkPorts.complete() && ! proLink.isRunning() && --pioRetryCountdown <= 0)
	{
		pioRetryCountdown = 120;
		updateNetwork();
	}

	const auto sending = masterDeck >= 0 && pioSender.isRunning();
	const auto masterText = sending ? juce::String (syncLabels::sendingMaster (masterDeck)) : juce::String();

	pioStatus.setVisible (pioSource || masterDeck >= 0);
	if (! pioSource)
	{
		pioPlayer.setVisible (false);
		const auto error = proLink.error();
		pioStatus.setText (! error.empty() ? juce::String (syncLabels::error (error)) : masterText, juce::dontSendNotification);
		return;
	}

	const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
	const auto error = proLink.error();
	const auto noMasterInfo = proLink.isRunning() && pioClock.bpm() > 0.0 && ! pioClock.hasMaster (now);
	pioPlayer.setVisible (noMasterInfo);
	if (noMasterInfo && pioPlayer.getSelectedId() != pioClock.chosenPlayer())
		pioPlayer.setSelectedId (pioClock.chosenPlayer(), juce::dontSendNotification);

	juce::String text;
	std::string status;
	if (! error.empty())
		status = syncLabels::error (error);
	else if (pioClock.bpm() <= 0.0)
		status = syncLabels::nothingHeard();
	else if (noMasterInfo)
		status = syncLabels::noMaster();
	else
		status = syncLabels::following (pioClock.bpm(), pioClock.isLive (now) ? pioClock.leader (now) : 0);
	text = juce::String::fromUTF8 (status.c_str());

	if (sending)
		text = masterText + juce::String::fromUTF8 (" \xc2\xb7 ") + text;
	pioStatus.setText (text, juce::dontSendNotification);
}

//==============================================================================
Session MainComponent::gatherSession() const
{
	Session session;
	for (int d = 0; d < numDecks; ++d)
	{
		auto& deck = session.decks[(size_t) d];
		const auto& host = hosts[(size_t) d];
		deck.setId = library.storedId (host.loadedSetId());
		host.gather (deck);
		decks[(size_t) d]->saveState (deck);
		mixer.strip (d).saveState (deck);
	}
	session.masterDeck = masterDeck;
	session.masterTurnedOff = masterTurnedOff;
	session.autoDj = autoDj.isEnabled();
	library.saveState (session.library);
	creatorLink->gather (session.stemJobs);
	return session;
}

void MainComponent::editGrid (int deckIndex, DeckPanel::GridAction action, double seconds)
{
	std::optional<double> leaderBeatPhase;
	if (action == DeckPanel::GridAction::shiftToLeader)
	{
		// The leader: the Pioneer master under SYNC: PIO, else the other deck.
		const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
		if (pioSource)
		{
			if (pioClock.bpm() > 0.0)
				leaderBeatPhase = pioClock.beatPhaseAt (now);
		}
		else
		{
			const auto& leader = *players[(size_t) (1 - deckIndex)];
			const auto leaderGrid = leader.getBeatGrid();
			if (leaderGrid.isValid() && leader.isPlaying())
			{
				const auto leaderPosition = positionAt (leader.getPosition(), leader.getPositionStamp(), now,
														leader.getEffectiveRate(), ! leader.isScratching());
				const auto multiple = syncMultiple > 0.0 ? syncMultiple : 1.0;
				leaderBeatPhase = leaderGrid.beatsAt (leaderPosition) * multiple;
			}
		}
	}

	// SHIFT GRID ends a bend: the grid now says what the ear did, the beat follows again.
	if (hosts[(size_t) deckIndex].editGrid (action, seconds, leaderBeatPhase) && action == DeckPanel::GridAction::shiftToLeader)
		setSyncBent (deckIndex, false);
}

void MainComponent::pressMaster (int deckIndex)
{
	const auto chosen = chooseMaster (deckIndex, decksPlaying(), masterDeck, true);
	masterTurnedOff = chosen < 0;
	setMasterDeck (chosen);
}

//==============================================================================
void MainComponent::scanControllers()
{
	const auto found = Scs3dDevice::find();

	bool same = found.size() == controllers.size();
	for (size_t i = 0; same && i < found.size(); ++i)
		same = controllers[i]->getIdentifier() == found[i].first.identifier;
	if (same)
		return;

	controllers.clear();
	const auto swap = settings().getBoolValue ("scs3dSwap");
	for (size_t i = 0; i < std::min<size_t> (found.size(), numDecks); ++i)
	{
		auto device = std::make_unique<Scs3dDevice> (found[i].first, found[i].second);
		if (! device->isOpen())
			continue;
		const auto deck = swap ? numDecks - 1 - (int) i : (int) i;
		device->onEvent = [this, deck] (const scs3d::Event& e) { handleController (deck, e); };
		controllers.push_back (std::move (device));
	}
}

void MainComponent::handleController (int d, const scs3d::Event& e)
{
	auto& player = *players[(size_t) d];
	auto& deck = *decks[(size_t) d];
	using Type = scs3d::Event::Type;

	switch (e.type)
	{
		case Type::mute:    mixer.strip (d).toggleMute (e.stem); break;
		case Type::play:    deck.playPressed(); break;
		case Type::cueDown: deck.cuePressed(); break;
		case Type::cueUp:   deck.cueReleased(); break;
		case Type::master:  pressMaster (d); break;
		case Type::sync:
		{
			const auto on = ! deck.isSyncEnabled();
			deck.setSyncEnabled (on);
			setSync (d, on);
			break;
		}
		case Type::gain:  mixer.strip (d).setFaderTravel (e.value); break;
		case Type::pitch: deck.moveTempo (e.value); break;

		// Loop on button 3: the first press marks in, the second loops from
		// there to here. Button 4: loop off, and on again (back to its start).
		// The deck's own loop keys (DeckLoop.h), as on the screen.
		case Type::loopInOut:  deck.loopInOrOut(); break;
		case Type::loopToggle: deck.toggleLoop(); break;

		// Around the circle: the library. A tap in the centre loads -- but not
		// over a deck that is playing: a stray touch must not stop the music.
		case Type::previous: library.selectRelative (-1); break;
		case Type::next:     library.selectRelative (1); break;
		case Type::load:
			if (const auto* set = library.selectedSet(); set != nullptr && ! player.isPlaying())
				loadSet (*set, d);   // the deck forgets its loop marks with the old track
			break;

		// The circle as the platter: touch holds, turning scratches, release lets go.
		case Type::scratchTouch:   if (player.isLoaded()) player.beginScratch(); break;
		case Type::scratchMove:    if (player.isScratching()) player.scratchBy (e.value * scs3d::secondsPerScratchStep); break;
		case Type::scratchRelease: player.endScratch(); break;
		case Type::none: break;
	}
}

void MainComponent::showControllers()
{
	const auto swap = settings().getBoolValue ("scs3dSwap");
	for (size_t i = 0; i < controllers.size(); ++i)
	{
		const auto d = swap ? numDecks - 1 - (int) i : (int) i;
		const auto& player = *players[(size_t) d];
		const auto& deck = *decks[(size_t) d];

		scs3d::Leds leds;
		leds.deck = d;
		for (int s = 0; s < StemSet::numStems; ++s)
			leds.muted[(size_t) s] = mixer.strip (d).isMuted (s);
		leds.looping = player.hasLoop();
		leds.loopInSet = deck.isLoopInMarked();
		leds.loopStored = deck.canReloop();
		leds.playing = player.isPlaying();
		leds.atCue = player.isLoaded() && std::abs (player.getPosition() - player.getCuePoint()) < 0.01;
		leds.synced = deck.isSyncEnabled();
		leds.syncBent = syncBent[(size_t) d];
		leds.master = masterDeck == d;
		leds.gain = mixer.strip (d).getFaderTravel();
		leds.pitch = deck.getTempoPosition();
		leds.platter = player.getPosition() / JogWheel::secondsPerRevolution;
		controllers[i]->show (leds);
	}
}

void MainComponent::handlePanel (const panel::Event& e)
{
	if (e.deck < 0 || e.deck >= numDecks)
		return;
	auto& deck = *decks[(size_t) e.deck];
	auto& strip = mixer.strip (e.deck);
	using Type = panel::Event::Type;

	switch (e.type)
	{
		case Type::route:   mixer.pressBus (e.deck, e.stem, e.bus); break;
		case Type::cueDown: deck.cuePressed(); break;
		case Type::cueUp:   deck.cueReleased(); break;
		case Type::play:    deck.playPressed(); break;
		case Type::stemCue: strip.toggleCue (e.stem); break;
		case Type::gain:    strip.nudgeStemGain (e.stem, e.value); break;
		case Type::mute:    strip.toggleMute (e.stem); break;
		case Type::fader:   strip.setFaderTravel (e.value); break;
	}
}

void MainComponent::showPanel()
{
	if (motionPanel == nullptr)
		return;

	panel::LedState state;
	for (int d = 0; d < numDecks; ++d)
	{
		const auto& player = *players[(size_t) d];
		for (int s = 0; s < StemSet::numStems; ++s)
		{
			const auto index = (size_t) buses::stemIndex (d, s);
			state.masks[index] = player.getStemBuses (s);
			state.muted[index] = mixer.strip (d).isMuted (s);
			state.cued[index] = player.isStemCued (s);
		}
		state.playing[(size_t) d] = player.isPlaying();
		state.atCue[(size_t) d] = player.isLoaded() && ! player.isPlaying()
								  && std::abs (player.getPosition() - player.getCuePoint()) < 0.01;
	}
	for (int s = 0; s < StemSet::numStems; ++s)
		state.stemColours[(size_t) s] = panel::rgbOf (Theme::stem (s));
	state.playColour = panel::rgbOf (Theme::play);
	state.cueColour = panel::rgbOf (Theme::cue);
	state.muteColour = panel::rgbOf (Theme::mute);
	state.auxColour = panel::rgbOf (Theme::aux);
	state.offColour = panel::rgbOf (Theme::panelRaised);
	for (int d = 0; d < numDecks; ++d)
		state.deckColours[(size_t) d] = panel::rgbOf (Theme::deck (d));
	motionPanel->show (state);
}

void MainComponent::setAutoDj (bool on)
{
	autoDjButton.setToggleState (on, juce::dontSendNotification);
	autoDjButton.setButtonText ("AUTO DJ");
	autoDj.setEnabled (on);
	// While it plays nothing reaches AUX; off, the routing stays as it is.
	mixer.setSpare (on ? buses::Spare::toOff : buses::Spare::toAux);
	if (on)
		runAutoDj();
}

// Next / Prev asked for the track beside the playing one; when there is none
// after all (the library changed since), Auto DJ's own pick rather than none.
const StemSet* MainComponent::autoDjPick (AutoDj::Pick pick, int loadDeck)
{
	if (pick != AutoDj::Pick::random)
		if (const auto* set = library.neighbourInFolder (hosts[(size_t) (1 - loadDeck)].loadedSetId(), pick == AutoDj::Pick::next ? 1 : -1))
			return set;
	return library.randomVisibleSet (autoDjPlayed);
}

void MainComponent::setAutoDjFade (AutoDj::MixLength length)
{
	autoDj.setMixLength (length);
	const auto applied = autoDj.mixLength();
	// The fade's bar count was written on every start whether chosen or not,
	// so it says nothing about a choice: the overlap starts from its default.
	settings().removeValue ("autoDjMixBars");
	settings().setValue ("autoDjOverlapBars", applied.bars);
	settings().setValue ("autoDjMixSeconds", applied.noGridSeconds);
	const auto overlap = applied.bars == AutoDj::aboutTwentySeconds ? juce::String ("about 20 s")
																	: juce::String (applied.bars) + " bars";
	autoDjButton.setTooltip ("Auto DJ: plays at random from the library as searched/filtered and hands each track to the "
							 "next stem by stem on the beat, over " + overlap + " (" + juce::String (applied.noGridSeconds, 0)
							 + " s without a beat grid)");
}

void MainComponent::readLevels (int deckIndex)
{
	const auto d = (size_t) deckIndex;
	const auto generation = hosts[d].loadGeneration();
	if (levelsGeneration[d] == generation || ! hosts[d].loadedSet())
		return;
	levelsGeneration[d] = generation;
	deckLevels[d].reset();
	deckSpans[d].reset();

	juce::Component::SafePointer<MainComponent> safeThis (this);
	analysisPool.addJob ([this, safeThis, set = *hosts[d].loadedSet(), deckIndex, generation]
	{
		auto* job = juce::ThreadPoolJob::getCurrentThreadPoolJob();
		auto levels = StemLevels::read (set, formatManager, [job] { return job != nullptr && job->shouldExit(); });
		const auto span = levels != nullptr ? StemHandover::audibleSpan (*levels) : std::nullopt;
		juce::MessageManager::callAsync ([safeThis, deckIndex, generation, levels, span]
		{
			if (safeThis == nullptr || safeThis->hosts[(size_t) deckIndex].loadGeneration() != generation)
				return;
			safeThis->deckLevels[(size_t) deckIndex] = levels;
			safeThis->deckSpans[(size_t) deckIndex] = span;
			safeThis->levelsRead[(size_t) deckIndex] = generation;
		});
	});
}

void MainComponent::stepDeck (int deckIndex, int direction)
{
	if (autoDj.isEnabled())
	{
		// Only the deck Auto DJ plays: the other one is where its mix goes.
		if (deckIndex == autoDj.playingDeck())
			autoDj.requestMixNow (direction > 0 ? AutoDj::Pick::next : AutoDj::Pick::previous);
		return;
	}

	if (const auto* set = library.neighbourInFolder (hosts[(size_t) deckIndex].loadedSetId(), direction))
		loadSet (*set, deckIndex);
}

void MainComponent::findSteps()
{
	for (int d = 0; d < numDecks; ++d)
	{
		const auto& id = hosts[(size_t) d].loadedSetId();
		steps[(size_t) d] = { library.neighbourInFolder (id, -1) != nullptr, library.neighbourInFolder (id, 1) != nullptr };
	}
	showSteps();
}

void MainComponent::showSteps()
{
	for (int d = 0; d < numDecks; ++d)
	{
		auto previous = steps[(size_t) d].previous;
		auto next = steps[(size_t) d].next;
		juce::String previousTip ("Previous track in this folder"), nextTip ("Next track in this folder");

		if (autoDj.isEnabled())
		{
			const auto takes = d == autoDj.playingDeck() && autoDj.canMixNow();
			previous = previous && takes;
			next = next && takes;
			previousTip = "Auto DJ: mix to the previous track in this folder now";
			nextTip = "Auto DJ: mix to the next track in this folder now";
		}
		decks[(size_t) d]->setStepsAvailable (previous, next, previousTip, nextTip);
	}
}

void MainComponent::runAutoDj()
{
	if (! autoDj.isEnabled())
		return;

	std::array<AutoDj::DeckView, numDecks> views;
	for (int d = 0; d < numDecks; ++d)
	{
		readLevels (d);
		const auto& player = *players[(size_t) d];
		auto& view = views[(size_t) d];
		const auto grid = player.getBeatGrid();
		view.loaded = player.isLoaded();
		view.playing = player.isPlaying();
		view.position = player.getPosition();
		view.length = player.getLength();
		view.gridBpm = grid.isValid() ? grid.bpm : 0.0;
		view.firstBeat = grid.firstBeat;
		view.rate = player.getEffectiveRate();
		view.looping = player.hasLoop();
		view.levels = deckLevels[(size_t) d].get();
		if (const auto& span = deckSpans[(size_t) d])
		{
			view.audibleStart = span->start;
			view.audibleEnd = span->end;
		}
		for (int s = 0; s < AutoDj::numStems; ++s)
		{
			const auto mask = player.getStemBuses (s);
			view.bus[(size_t) s] = mask == buses::offMask ? AutoDj::offBus
								 : (mask & (1u << buses::aux)) != 0 ? AutoDj::auxBus
								 : (int) juce::findHighestSetBit (mask);
		}
		view.levelsPending = levelsGeneration[(size_t) d] == hosts[(size_t) d].loadGeneration()
						  && levelsRead[(size_t) d] != hosts[(size_t) d].loadGeneration();
	}

	const auto c = autoDj.update (views);
	autoDjButton.setButtonText (autoDj.phase() == AutoDj::Phase::mixing
									? "MIX " + juce::String (juce::roundToInt (autoDj.mixProgress() * 100.0)) + " %"
									: juce::String ("AUTO DJ"));

	if (c.load >= 0)
	{
		if (const auto* set = autoDjPick (c.loadPick, c.load))
		{
			if (autoDjPlayed.count (set->files[0].getFullPathName()) > 0)
				autoDjPlayed.clear();   // every one was played: round again
			autoDjPlayed.insert (set->files[0].getFullPathName());
			loadSet (*set, c.load);
		}
		else
		{
			setAutoDj (false);   // nothing to play: the search shows no sets
			return;
		}
	}

	std::vector<buses::Route> routes;
	for (int d = 0; d < numDecks; ++d)
	{
		if (const auto db = c.faderDb[(size_t) d])
			mixer.strip (d).setFaderDb (*db);
		for (int s = 0; s < AutoDj::numStems; ++s)
			if (const auto bus = c.bus[(size_t) d][(size_t) s])
				routes.push_back ({ buses::stemIndex (d, s), *bus == AutoDj::offBus ? buses::off : *bus });
	}
	// All of a tick's switches at once, before the start, and to Core as a
	// click's are: the desk sees every stem it moved.
	if (! routes.empty())
		mixer.route (routes);

	if (c.syncOn >= 0)
	{
		decks[(size_t) c.syncOn]->setSyncEnabled (true);
		setSync (c.syncOn, true);
	}

	if (c.start >= 0)
	{
		auto& player = *players[(size_t) c.start];
		player.setPosition (c.startAt);
		player.play();
	}

	if (c.stop >= 0)
		players[(size_t) c.stop]->pause();

	if (c.syncOff >= 0)
	{
		decks[(size_t) c.syncOff]->setSyncEnabled (false);
		setSync (c.syncOff, false);
	}
}

void MainComponent::saveSession()
{
	analysisCache.flush();   // grid corrections, written with the session
	// Nothing playing and nothing touched: no write.
	const auto text = gatherSession().toXml()->toString();
	if (text == lastSessionText)
		return;
	if (Session::file (settings().getFile()).replaceWithText (text))
		lastSessionText = text;
}

void MainComponent::restoreSession()
{
	const auto session = Session::load (Session::file (settings().getFile()));
	if (! session)
		return;

	library.restoreState (session->library);

	for (int d = 0; d < numDecks; ++d)
	{
		const auto& deck = session->decks[(size_t) d];
		auto& player = *players[(size_t) d];

		mixer.strip (d).restoreState (deck);
		hosts[(size_t) d].restoreSpatial (deck);

		if (deck.setId.isEmpty())
			continue;
		if (const auto* set = library.findSet (library.idFromStored (deck.setId)))
			loadSet (*set, d);
		if (! player.isLoaded())
			continue;

		decks[(size_t) d]->restoreState (deck);
		hosts[(size_t) d].restoreTransport (deck);
	}
	// Both decks' switches at once: an older session may break the
	// one-stem-per-bus rule, which spans the two decks.
	buses::Masks stored {};
	for (int d = 0; d < numDecks; ++d)
		for (int s = 0; s < buses::stemsPerDeck; ++s)
			stored[(size_t) buses::stemIndex (d, s)] = session->decks[(size_t) d].stems[(size_t) s].buses;
	mixer.restoreBuses (stored);

	for (int d = 0; d < numDecks; ++d)
		if (session->decks[(size_t) d].sync && players[(size_t) d]->isLoaded())
			setSync (d, true);

	masterTurnedOff = session->masterTurnedOff;
	if (session->masterDeck >= 0 && players[(size_t) session->masterDeck]->isLoaded())
		setMasterDeck (session->masterDeck);

	// Last: what was playing plays on.
	for (int d = 0; d < numDecks; ++d)
		if (session->decks[(size_t) d].playing && players[(size_t) d]->isLoaded())
			players[(size_t) d]->play();

	if (session->autoDj)
		setAutoDj (true);   // takes the playing deck as it is

	creatorLink->restore (session->stemJobs);

	lastSessionText = session->toXml()->toString();
}

std::array<bool, 2> MainComponent::decksPlaying() const
{
	return { players[0]->isPlaying(), players[1]->isPlaying() };
}

void MainComponent::setMasterDeck (int deckIndex)
{
	masterDeck = deckIndex;
	for (int d = 0; d < numDecks; ++d)
		decks[(size_t) d]->setMaster (d == deckIndex);
	pioSender.setMaster (deckIndex >= 0 ? players[(size_t) deckIndex] : nullptr);
	proLink.wantMaster (deckIndex >= 0);
	updateNetwork();
}

// The deck this player's status and position report: the master deck, else a
// synced playing deck, else a playing one; -1 for none (no track).
int MainComponent::reportedDeck() const
{
	if (masterDeck >= 0)
		return masterDeck;
	for (int d = 0; d < numDecks; ++d)
		if (pioSynced[(size_t) d] && players[(size_t) d]->isPlaying())
			return d;
	for (int d = 0; d < numDecks; ++d)
		if (players[(size_t) d]->isPlaying())
			return d;
	return -1;
}

// The link hands MASTER here (another master paused, a DJM's command), or
// another player takes it (its MASTER pressed): the deck's MASTER follows, as
// a CDJ's does.
void MainComponent::followLinkMaster()
{
	using Change = prolink::TempoMaster::Change;
	const auto change = proLink.takeMasterChange();
	if (change == Change::yielded && masterDeck >= 0)
	{
		masterTurnedOff = true;   // not straight back by the lone-deck rule
		setMasterDeck (-1);
	}
	else if (change == Change::granted && masterDeck < 0)
	{
		masterTurnedOff = false;
		setMasterDeck (std::max (0, reportedDeck()));
	}

	const auto reported = reportedDeck();
	pioSender.setReported (reported >= 0 ? players[(size_t) reported] : nullptr);
	pioSender.setSynced (reported >= 0 && pioSynced[(size_t) reported]);
}

// On the Pioneer network from the start, as a CDJ: the receiver announces our
// player on the link and listens, the sender sends its status whether master or
// not. PIO and MASTER change what is sent -- MASTER adds the master deck's
// beats -- not whether the player is there.
void MainComponent::updateNetwork()
{
	const auto device = appProperties.getUserSettings()->getIntValue ("pioDevice", 6);
	const auto presence = prolink::presence (proLinkPorts.complete());

	if (! presence.receiver)
	{
		pioSender.stop();
		proLink.stop();
		return;
	}

	if (! proLink.isRunning())
		proLink.start (device, proLinkPorts);

	if (presence.sender && proLink.isRunning() && ! pioSender.isRunning())
		pioSender.start (device, proLink.broadcastAddress(), proLinkPorts, "StemDeck", &proLink);
}

// One synced deck against its leader: the rules in followLeader(), the
// result put on the deck.
double MainComponent::applyFollow (int deckIndex, FollowInput in, double followerPosition)
{
	return deckfollow::apply (*decks[(size_t) deckIndex], *players[(size_t) deckIndex], syncBent[(size_t) deckIndex], in, followerPosition);
}

void MainComponent::loadDroppedSet (const juce::String& setId, int deckIndex)
{
	if (const auto* set = library.findSet (setId))
		loadSet (*set, deckIndex);
}

//==============================================================================
void MainComponent::createSpatialPorts()
{
	for (auto& host : hosts)
		if (const auto error = host.createSpatial(); error.isNotEmpty() && spatialError.isEmpty())
			spatialError = error;
}

void MainComponent::initialiseAudio()
{
	juce::StringArray portNames;

	for (int ch = 0; ch < outputs::channelCount(); ++ch)
		portNames.add (juce::String (outputs::portName (ch)));

	audio = std::make_unique<AudioOutput> (settings());
	audio->onDeviceChanged = [this] { updateDeviceStatus(); };
	audio->open ("StemDeck", portNames, *this, { "rec_L", "rec_R" }, &recorder);
	rec.button.setEnabled (audio->usingJack());
	keepRecorderLinks();

	updateDeviceStatus();
}

// P39: rec records the REC bus without a hand-made link.
void MainComponent::keepRecorderLinks()
{
	juce::String error;
	if (auto map = recorderLinkMap (juce::File (decklinks::installedCoreMap), error))
		audio->keepLinks (std::move (*map));
	else
		std::cerr << "StemDeck: no links: " << error << std::endl;
}

void MainComponent::updateDeviceStatus()
{
	// Summed down means the buses; the cue and spatial ports are only ever left out.
	const auto status = audio->status (outputs::stereo::channelCount());
	deviceStatus.setText (AudioOutput::withSpatialError (status.text, spatialError), juce::dontSendNotification);
	deviceStatus.setColour (juce::Label::textColourId, AudioOutput::colourOf (status.health));
}

void MainComponent::showTips (bool shown)
{
	if (shown && tooltips == nullptr)
		tooltips = std::make_unique<juce::TooltipWindow> (this, 600);
	else if (! shown)
		tooltips.reset();
}

void MainComponent::showSettings()
{
	juce::DialogWindow::LaunchOptions dialog;
	SettingsPanel::Values values;
	values.libraryFolder = library.getFolder();
	values.autoDjFade = autoDj.mixLength();
	values.audioDeviceChoosable = ! audio->usingJack();
	values.audioDeviceNote = audio->usingJack() ? "Under JACK: route with qjackctl or a patchbay" : juce::String();
	values.tipsShown = tooltips != nullptr;
	values.keyLockEngine = playerA.keyLockEngine();

	SettingsPanel::Actions actions;
	actions.onLibraryFolder = [this] (const juce::File& folder) { library.setFolder (folder); };
	actions.onAutoDjFade = [this] (AutoDj::MixLength length) { setAutoDjFade (length); };
	actions.onAudioDevice = [this] { audio->showDeviceSettings(); };
	actions.onKeyLockEngine = [this] (keylock::Engine engine)
	{
		appsettings::storeKeyLockEngine (settings(), engine);
		for (auto* player : players)
			player->setKeyLockEngine (engine);
	};
	actions.onTips = [this] (bool shown)
	{
		settings().setValue (tips::settingKey, juce::String (tips::stored (shown)));
		showTips (shown);
	};

	dialog.content.setOwned (new SettingsPanel (values, std::move (actions)));
	dialog.dialogTitle = "Settings";
	dialog.dialogBackgroundColour = Theme::panel;
	dialog.useNativeTitleBar = true;
	dialog.resizable = false;
	dialog.launchAsync();
}

void MainComponent::timerCallback()
{
	for (int d = 0; d < numDecks; ++d)
	{
		waves[(size_t) d]->repaint();
		decks[(size_t) d]->refresh();
		eventStrips[(size_t) d]->refresh();
	}

	mixer.refresh();
	updateSync();
	sendPreview();

	followLinkMaster();

	// With no master yet, the only playing deck becomes it (like a lone CDJ).
	if (const auto chosen = chooseMaster (-1, decksPlaying(), masterDeck, ! pioSource && ! masterTurnedOff); chosen != masterDeck)
		setMasterDeck (chosen);

	updatePioneerStatus();
	creatorLink->showStatus();

	runAutoDj();
	showSteps();
	rec.update (audio->usingJack() ? audio->jackClient().getClientName() : juce::String());

	if (--controllerScanCountdown <= 0)
	{
		controllerScanCountdown = 180;   // three seconds
		scanControllers();
	}
	showControllers();
	showPanel();

	if (--sessionCountdown <= 0)
	{
		sessionCountdown = 120;   // two seconds at 60 Hz
		saveSession();
	}

	for (int ch = 0; ch < numOutputChannels; ++ch)
		mixer.setOutputLevel (ch, outputPeaks[(size_t) ch].exchange (0.0f));

	if (--statusCountdown <= 0)
	{
		statusCountdown = 60;
		updateDeviceStatus();
	}
}

//==============================================================================
void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
	const auto maxBlock = juce::jmax (samplesPerBlockExpected, 4096);
	busBuffer.setSize (outputs::channelCount(), maxBlock);

	for (int d = 0; d < numDecks; ++d)
	{
		hosts[(size_t) d].prepare (sampleRate, samplesPerBlockExpected);

		const auto& player = *players[(size_t) d];
		for (int s = 0; s < StemSet::numStems; ++s)
			for (int bus = 0; bus < buses::count; ++bus)
			{
				auto& route = routeGains[(size_t) d][(size_t) s][(size_t) bus];
				route.reset (sampleRate, 0.01);
				route.setCurrentAndTargetValue (buses::gain (player.isStemOnBus (s, bus), player.getDeckGain()));
			}
	}
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
	pinAudioThreadToItsCpu();

	auto& out = *bufferToFill.buffer;
	const auto numSamples = bufferToFill.numSamples;
	const auto numOut = out.getNumChannels();

	const auto numBusChannels = outputs::channelCount();

	if (numSamples > busBuffer.getNumSamples())
		busBuffer.setSize (numBusChannels, numSamples, false, false, true);

	busBuffer.clear (0, numSamples);

	for (int d = 0; d < numDecks; ++d)
	{
		auto& player = *players[(size_t) d];
		const auto& deckBuffer = hosts[(size_t) d].render (busBuffer, outputs::spatialFirstChannel (d), numSamples);

		// Each stem onto every bus it is switched to, post fader; ramped over
		// 10 ms on any change.
		const auto fader = player.getDeckGain();
		for (int s = 0; s < StemSet::numStems; ++s)
			for (int bus = 0; bus < buses::count; ++bus)
			{
				auto& route = routeGains[(size_t) d][(size_t) s][(size_t) bus];
				route.setTargetValue (buses::gain (player.isStemOnBus (s, bus), fader));
				const auto from = route.getCurrentValue();
				route.skip (numSamples);
				const auto to = route.getCurrentValue();
				if (from == 0.0f && to == 0.0f)
					continue;

				for (int c = 0; c < 2; ++c)
					busBuffer.addFromWithRamp (bus * 2 + c, 0, deckBuffer.getReadPointer (s * 2 + c), numSamples, from, to);
			}

		// The cue pair: every stem on cue, as the player rendered it (after
		// its knob, before its mute and the fader), ramped there already.
		if (player.getCueBlockSamples() == numSamples)
			for (int s = 0; s < StemSet::numStems; ++s)
				for (int c = 0; c < 2; ++c)
					busBuffer.addFrom (outputs::cueChannel() + c, 0, player.getCueBlock(), s * 2 + c, 0, numSamples);
	}

	// The fixed 6 dB trim on every bus and on the cue pair, before the
	// meters, so they show what leaves StemDeck. The spatial ports carry it
	// already (DeckSpatial).
	for (int ch = 0; ch < outputs::stereo::channelCount(); ++ch)
		busBuffer.applyGain (ch, 0, numSamples, buses::trim);

	// The desk's SA meter: the AUX bus as it goes to the return, side by side.
	for (int side = 0; side < 2; ++side)
		auxLevels[(size_t) side].add (remote::measure (busBuffer.getReadPointer (buses::aux * 2 + side), numSamples));

	// The meters show the five buses.
	for (int ch = 0; ch < numOutputChannels; ++ch)
	{
		const auto peak = busBuffer.getMagnitude (ch, 0, numSamples);

		if (peak > outputPeaks[(size_t) ch].load())
			outputPeaks[(size_t) ch] = peak;
	}

	// One output per bus channel; with fewer outputs (e.g. plain stereo for
	// monitoring) the buses are summed down. The cue pair and the spatial
	// ports only go to outputs of their own: summed down they would land on
	// a bus (outputs::foldsInto).
	bufferToFill.clearActiveBufferRegion();

	for (int ch = 0; ch < numBusChannels; ++ch)
		if (const auto target = outputs::foldsInto (ch, numOut); target >= 0)
			out.addFrom (target, bufferToFill.startSample, busBuffer, ch, 0, numSamples);
}

void MainComponent::releaseResources()
{
	for (auto& host : hosts)
		host.releaseResources();
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
	g.fillAll (Theme::background);
	// The band: one tile per deck (the meters' tile paints itself).
	for (const auto& tile : deckTiles)
	{
		g.setColour (Theme::panel);
		g.fillRoundedRectangle (tile.toFloat(), Theme::corner (tile.toFloat()));
	}

	// Each deck's line, from its column's stripe (the column paints that)
	// down, along the gap above the band and down between the deck's tile
	// and the meters' tile; every tile one gap from it.
	for (size_t d = 0; d < (size_t) numDecks; ++d)
	{
		g.setColour (Theme::deck ((int) d));
		for (const auto& piece : { deckLines[d].drop, deckLines[d].top, deckLines[d].side })
			g.fillRect (surface::toJuce (piece));
	}
}

void MainComponent::resized()
{
	// The sections (SurfaceLayout.h): the top bar; the waveforms with a deck's
	// pitch column at each outer edge; deck A | mixer | deck B; the library.
	const auto layout = surface::sections (getWidth(), getHeight());

	auto topBar = surface::toJuce (layout.topBar);
	// The rest as shares of the window's width (sized for 768 px, the rig).
	const auto share = [this] (int perMille) { return getWidth() * perMille / 1000; };
	const auto gap = layout.metrics.gap;
	topBar = workspaceSwitch.place (topBar, gap);
	settingsButton.setBounds (topBar.removeFromRight (share (117)));
	topBar.removeFromRight (gap);
	keysButton.setBounds (topBar.removeFromRight (share (78)));
	topBar.removeFromRight (gap);
	syncSourceButton.setBounds (topBar.removeFromRight (share (143)));
	topBar.removeFromRight (gap);
	autoDjButton.setBounds (topBar.removeFromRight (share (130)));
	topBar.removeFromRight (gap);
	// The input meters left of REC, where the eye comes from.
	rec.button.setBounds (topBar.removeFromRight (share (130)));
	topBar.removeFromRight (gap * 2 / 3);
	const auto recMeterInset = topBar.getHeight() / 10;
	rec.right.setBounds (topBar.removeFromRight (share (8)).reduced (0, recMeterInset));
	topBar.removeFromRight (gap / 3);
	rec.left.setBounds (topBar.removeFromRight (share (8)).reduced (0, recMeterInset));
	topBar.removeFromRight (gap);
	pioPlayer.setBounds (topBar.removeFromRight (share (117)));
	pioStatus.setBounds (topBar.removeFromRight (share (286)));
	if (liveBadge.isVisible())
	{
		// Next to the device line, a share of the bar like its neighbours.
		topBar.removeFromRight (gap);
		const auto badge = topBar.removeFromRight (share (170));
		liveBadge.setBounds (badge.reduced (0, recMeterInset));
		liveBadge.setFont (juce::FontOptions (liveBadge.getHeight() * 0.5f).withStyle ("Bold"));
	}
	deviceStatus.setBounds (topBar);

	waveA.setBounds (surface::toJuce (layout.waveA));
	waveB.setBounds (surface::toJuce (layout.waveB));
	for (int d = 0; d < numDecks; ++d)
	{
		decks[(size_t) d]->setBounds (surface::toJuce (layout.deck[(size_t) d]));
		eventStrips[(size_t) d]->setBounds (surface::toJuce (layout.events[(size_t) d]));
	}
	mixer.setBounds (surface::toJuce (layout.mixer));
	library.setBounds (surface::toJuce (layout.library));

	// The band under the decks and the mixer: their parts, placed here.
	const auto band = surface::band (layout.band, layout.metrics);
	for (size_t d = 0; d < (size_t) numDecks; ++d)
	{
		decks[d]->setBandBounds (band.deck[d]);
		deckTiles[d] = surface::toJuce (band.deck[d].tile);
		deckLines[d] = surface::deckLine (layout, band, (int) d);
	}
	mixer.setBandBounds (surface::toJuce (band.deck[0].fader), surface::toJuce (band.deck[1].fader),
						 surface::toJuce (band.metersTile), layout.metrics.gap);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
	// Handled edge-triggered in keyStateChanged; swallow the repeats here.
	for (const auto& binding : keyBindings)
		if (key.getTextCharacter() == binding.key)
			return true;

	return false;
}

bool MainComponent::keyStateChanged (bool)
{
	// Typing in the search box must not trigger the decks.
	if (dynamic_cast<juce::TextEditor*> (getCurrentlyFocusedComponent()) != nullptr)
		return false;

	bool handled = false;

	for (auto& binding : keyBindings)
	{
		const bool down = juce::KeyPress::isKeyCurrentlyDown (binding.key)
					   || juce::KeyPress::isKeyCurrentlyDown (juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) binding.key));

		if (down != binding.wasDown)
		{
			binding.wasDown = down;
			binding.action (down);
			handled = true;
		}
	}

	return handled;
}
