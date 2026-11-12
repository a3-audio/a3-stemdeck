#include <gtest/gtest.h>

#include "Outputs.h"

#include <set>
#include <string>
#include <vector>

// StemDeck's one output mode since spec stemdeck-remote (2026-10-01): the five
// stereo buses, switched by remote control from the desk. The 8x stereo
// mode (every stem on its own pair) is gone, and since 2026-10-07 so are the
// PHONES outputs: the cue is the desk channel, in REAPER.

TEST (Outputs, FiveStereoBusesThenTheCuePair)
{
	EXPECT_EQ (outputs::busChannelCount(), 10);
	EXPECT_EQ (outputs::stereo::channelCount(), 12);
	EXPECT_EQ (outputs::portName (0), "deck1_L");
	EXPECT_EQ (outputs::portName (9), "aux_R");
}

// The names as registered on the rig and carried by the ten channels to the Core
// (start.sh: deck1_L ... aux_R).
TEST (Outputs, TheBusPortsKeepTodaysNames)
{
	const char* expected[] = { "deck1_L", "deck1_R", "deck2_L", "deck2_R", "deck3_L", "deck3_R",
							   "deck4_L", "deck4_R", "aux_L", "aux_R" };
	ASSERT_EQ (outputs::busChannelCount(), (int) std::size (expected));
	for (int ch = 0; ch < outputs::busChannelCount(); ++ch)
		EXPECT_EQ (outputs::portName (ch), expected[ch]);
}

// Appended after the buses, so no existing port moves.
TEST (Outputs, TheCuePairComesLast)
{
	EXPECT_EQ (outputs::portName (10), "cue_L");
	EXPECT_EQ (outputs::portName (11), "cue_R");
	EXPECT_EQ (outputs::cueChannel(), 10);
}

// Today's twelve carry no phones port; the spatial ports (R7) come after them.
TEST (Outputs, NoPhonesPorts)
{
	for (int ch = 0; ch < outputs::stereo::channelCount(); ++ch)
		EXPECT_EQ (outputs::portName (ch).find ("phones"), std::string::npos) << outputs::portName (ch);
}

// Stage S2 (ruling R7): after today's twelve, each deck adds its B-format, a
// binaural cue and phones -- 20 channels, deck A first.
TEST (Outputs, TodaysTwelvePortsKeepTheirNamesAndOrder)
{
	const char* expected[] = { "deck1_L", "deck1_R", "deck2_L", "deck2_R", "deck3_L", "deck3_R",
							   "deck4_L", "deck4_R", "aux_L", "aux_R", "cue_L", "cue_R" };
	ASSERT_EQ (outputs::stereo::channelCount(), (int) std::size (expected));
	for (int ch = 0; ch < outputs::stereo::channelCount(); ++ch)
		EXPECT_EQ (outputs::portName (ch), expected[ch]);
}

TEST (Outputs, EachDeckAddsTwentySpatialPorts)
{
	EXPECT_EQ (outputs::spatialFirstChannel (0), 12);
	EXPECT_EQ (outputs::spatialFirstChannel (1), 32);
	EXPECT_EQ (outputs::portName (12), "A_bformat_1");
	EXPECT_EQ (outputs::portName (27), "A_bformat_16");
	EXPECT_EQ (outputs::portName (28), "A_bcue_L");
	EXPECT_EQ (outputs::portName (29), "A_bcue_R");
	EXPECT_EQ (outputs::portName (30), "A_phones_L");
	EXPECT_EQ (outputs::portName (31), "A_phones_R");
	EXPECT_EQ (outputs::portName (32), "B_bformat_1");
	EXPECT_EQ (outputs::portName (47), "B_bformat_16");
	EXPECT_EQ (outputs::portName (48), "B_bcue_L");
	EXPECT_EQ (outputs::portName (51), "B_phones_R");
	EXPECT_EQ (outputs::channelCount(), 52);
}

TEST (Outputs, EveryPortNameIsUnique)
{
	std::set<std::string> names;
	for (int ch = 0; ch < outputs::channelCount(); ++ch)
		EXPECT_TRUE (names.insert (outputs::portName (ch)).second) << outputs::portName (ch);
}

// Without JACK: the buses are summed down onto fewer device outputs, as
// today; the cue and every spatial channel go only to an output of their own.
TEST (Outputs, SpatialChannelsAreNeverSummedDown)
{
	EXPECT_EQ (outputs::foldsInto (0, 2), 0);
	EXPECT_EQ (outputs::foldsInto (3, 2), 1);
	EXPECT_EQ (outputs::foldsInto (9, 2), 1);
	EXPECT_EQ (outputs::foldsInto (9, 12), 9);
	for (int ch = outputs::cueChannel(); ch < outputs::channelCount(); ++ch)
	{
		EXPECT_EQ (outputs::foldsInto (ch, 2), -1) << ch;
		EXPECT_EQ (outputs::foldsInto (ch, ch), -1) << ch;
		EXPECT_EQ (outputs::foldsInto (ch, ch + 1), ch) << ch;
		EXPECT_EQ (outputs::foldsInto (ch, 64), ch) << ch;
	}
	EXPECT_EQ (outputs::foldsInto (0, 0), -1);
}

// A -bformat instance (spec M1/P10): its own JACK client with 43 outputs --
// the deck's B-format, its 16-channel AUX bus, its 2nd-order cue and its
// phones. None of today's bus ports, and no cue_L/R or aux_L/R pairs.
TEST (Outputs, OneDeckHasFortyThreePorts)
{
	std::vector<std::string> expected;
	for (int c = 1; c <= 16; ++c)
		expected.push_back ("bformat_" + std::to_string (c));
	for (int c = 1; c <= 16; ++c)
		expected.push_back ("aux_" + std::to_string (c));
	for (int c = 1; c <= 9; ++c)
		expected.push_back ("cue_" + std::to_string (c));
	expected.push_back ("phones_L");
	expected.push_back ("phones_R");

	ASSERT_EQ (outputs::oneDeck::channelCount(), 43);
	for (int ch = 0; ch < outputs::oneDeck::channelCount(); ++ch)
		EXPECT_EQ (outputs::oneDeck::portName (ch), expected[(size_t) ch]) << ch;

	const auto layout = outputs::oneDeck::layout();
	EXPECT_EQ (layout.channels, 43);
	EXPECT_EQ (layout.bformat, 0);
	EXPECT_EQ (layout.aux, 16);
	EXPECT_EQ (layout.cue, 32);
	EXPECT_EQ (layout.phones, 41);
	EXPECT_FALSE (layout.binauralCue);
	for (int ch = 0; ch < 43; ++ch)
	{
		EXPECT_NE (outputs::oneDeck::portName (ch), "cue_L");
		EXPECT_NE (outputs::oneDeck::portName (ch), "aux_L");
	}
}

// The two-deck names are the rig's: byte for byte what main lists.
TEST (Outputs, TwoDeckPortsUnchanged)
{
	std::vector<std::string> expected = { "deck1_L", "deck1_R", "deck2_L", "deck2_R", "deck3_L", "deck3_R",
										  "deck4_L", "deck4_R", "aux_L", "aux_R", "cue_L", "cue_R" };
	for (const auto* deck : { "A_", "B_" })
	{
		for (int c = 1; c <= 16; ++c)
			expected.push_back (std::string (deck) + "bformat_" + std::to_string (c));
		for (const auto* name : { "bcue_L", "bcue_R", "phones_L", "phones_R" })
			expected.push_back (std::string (deck) + name);
	}
	ASSERT_EQ (outputs::channelCount(), 52);
	ASSERT_EQ (expected.size(), 52u);
	for (int ch = 0; ch < 52; ++ch)
		EXPECT_EQ (outputs::portName (ch), expected[(size_t) ch]) << ch;
}

// Two-deck mode keeps its 20 spatial channels per deck and renders no AUX
// there: the AUX bus stays the AUX.
TEST (Outputs, TwoDeckSpatialLayoutIsUnchanged)
{
	const auto layout = outputs::twoDeckSpatialLayout();
	EXPECT_EQ (layout.channels, outputs::spatialChannelsPerDeck());
	EXPECT_EQ (layout.bformat, outputs::bformatOffset);
	EXPECT_EQ (layout.cue, outputs::binauralCueOffset);
	EXPECT_EQ (layout.aux, -1);
	EXPECT_EQ (layout.phones, outputs::phonesOffset);
}

// Without JACK: on a device with all 43 outputs each channel has its own; on
// a smaller one (a laptop's stereo) only the phones play, on its first pair --
// a B-format summed into stereo is no mix anyone could listen to.
TEST (Outputs, OneDeckFoldsOnlyPhonesOntoAStereoDevice)
{
	using outputs::oneDeck::foldsInto;
	const auto phones = outputs::oneDeck::layout().phones;
	for (int ch = 0; ch < 43; ++ch)
		EXPECT_EQ (foldsInto (ch, 43), ch) << ch;
	EXPECT_EQ (foldsInto (phones, 2), 0);
	EXPECT_EQ (foldsInto (phones + 1, 2), 1);
	for (int ch = 0; ch < phones; ++ch)
		EXPECT_EQ (foldsInto (ch, 2), -1) << ch;
	EXPECT_EQ (foldsInto (phones, 1), -1);
	EXPECT_EQ (foldsInto (phones, 0), -1);
}

// The one-deck screen's three meters (2026-10-28): MAIN, AUX and CUE each show
// their bus's W, ACN 0 -- the first port of each, never a higher order.
TEST (Outputs, OneDeckMetersReadEachBussW)
{
	using outputs::oneDeck::MeteredBus;
	const auto layout = outputs::oneDeck::layout();
	EXPECT_EQ (outputs::oneDeck::wChannel (MeteredBus::main), layout.bformat);
	EXPECT_EQ (outputs::oneDeck::wChannel (MeteredBus::aux), layout.aux);
	EXPECT_EQ (outputs::oneDeck::wChannel (MeteredBus::cue), layout.cue);
	EXPECT_EQ (outputs::oneDeck::portName (outputs::oneDeck::wChannel (MeteredBus::main)), "bformat_1");
	EXPECT_EQ (outputs::oneDeck::portName (outputs::oneDeck::wChannel (MeteredBus::aux)), "aux_1");
	EXPECT_EQ (outputs::oneDeck::portName (outputs::oneDeck::wChannel (MeteredBus::cue)), "cue_1");
	ASSERT_EQ (outputs::oneDeck::meteredBuses.size(), 3u);
	EXPECT_EQ (outputs::oneDeck::meteredBuses[0], MeteredBus::main) << "MAIN, AUX, CUE, left to right";
	EXPECT_EQ (outputs::oneDeck::meteredBuses[1], MeteredBus::aux);
	EXPECT_EQ (outputs::oneDeck::meteredBuses[2], MeteredBus::cue);
}

//==============================================================================
// Output: Stereo (setting outputMode): the deck's stems as ONE master pair for
// Core's analog channel, plus the local phones pair. Nothing else.
TEST (OutputMode, ItsNameRoundTripsAndAnythingElseIsBFormat)
{
	using outputs::OutputMode;
	EXPECT_STREQ (outputs::outputModeName (OutputMode::bformat), "bformat");
	EXPECT_STREQ (outputs::outputModeName (OutputMode::stereo), "stereo");
	EXPECT_EQ (outputs::outputModeFrom ("stereo"), OutputMode::stereo);
	EXPECT_EQ (outputs::outputModeFrom ("bformat"), OutputMode::bformat);
	EXPECT_EQ (outputs::outputModeFrom (""), OutputMode::bformat) << "unset: today's behaviour";
	EXPECT_EQ (outputs::outputModeFrom ("Stereo?"), OutputMode::bformat) << "unknown: today's behaviour";
}

TEST (OutputMode, StereoHasMasterAndPhonesPairsOnly)
{
	using outputs::OutputMode;
	ASSERT_EQ (outputs::oneDeckChannelCount (OutputMode::stereo), 4);
	const std::vector<std::string> expected { "master_L", "master_R", "phones_L", "phones_R" };
	for (int ch = 0; ch < 4; ++ch)
		EXPECT_EQ (outputs::oneDeckPortName (OutputMode::stereo, ch), expected[(size_t) ch]) << ch;
}

TEST (OutputMode, BFormatIsToday)
{
	using outputs::OutputMode;
	ASSERT_EQ (outputs::oneDeckChannelCount (OutputMode::bformat), 43);
	for (int ch = 0; ch < 43; ++ch)
	{
		EXPECT_EQ (outputs::oneDeckPortName (OutputMode::bformat, ch), outputs::oneDeck::portName (ch)) << ch;
		EXPECT_EQ (outputs::oneDeckFoldsInto (OutputMode::bformat, ch, 2), outputs::oneDeck::foldsInto (ch, 2)) << ch;
	}
}

// Without JACK: a four-output device takes all four; a stereo device plays
// the master (in B-format mode it could only play the phones).
TEST (OutputMode, StereoWithoutJackPlaysTheMasterOnAStereoDevice)
{
	using outputs::OutputMode;
	for (int ch = 0; ch < 4; ++ch)
		EXPECT_EQ (outputs::oneDeckFoldsInto (OutputMode::stereo, ch, 4), ch) << ch;
	EXPECT_EQ (outputs::oneDeckFoldsInto (OutputMode::stereo, 0, 2), 0);
	EXPECT_EQ (outputs::oneDeckFoldsInto (OutputMode::stereo, 1, 2), 1);
	EXPECT_EQ (outputs::oneDeckFoldsInto (OutputMode::stereo, 2, 2), -1) << "phones need their own outputs";
	EXPECT_EQ (outputs::oneDeckFoldsInto (OutputMode::stereo, 3, 2), -1);
	EXPECT_EQ (outputs::oneDeckFoldsInto (OutputMode::stereo, 0, 1), -1);
	EXPECT_EQ (outputs::oneDeckFoldsInto (OutputMode::stereo, 0, 0), -1);
}

// The screen's MAIN bar reads the master pair (the louder side); AUX and CUE
// do not exist in stereo and stay dark.
TEST (OutputMode, TheMetersReadTheMasterInStereo)
{
	using outputs::OutputMode;
	using outputs::oneDeck::MeteredBus;
	const auto main = outputs::oneDeckMeterChannels (OutputMode::stereo, MeteredBus::main);
	EXPECT_EQ (main[0], 0);
	EXPECT_EQ (main[1], 1);
	for (const auto bus : { MeteredBus::aux, MeteredBus::cue })
	{
		const auto none = outputs::oneDeckMeterChannels (OutputMode::stereo, bus);
		EXPECT_EQ (none[0], -1);
		EXPECT_EQ (none[1], -1);
	}
	for (const auto bus : outputs::oneDeck::meteredBuses)
	{
		const auto bformat = outputs::oneDeckMeterChannels (OutputMode::bformat, bus);
		EXPECT_EQ (bformat[0], outputs::oneDeck::wChannel (bus));
		EXPECT_EQ (bformat[1], -1);
	}
}
