// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "DeckLinks.h"

#include <a3-links/LinkPlan.h>

namespace
{
	using Peers = std::vector<std::string>;

	// A home of its own per test: the user file lives under it.
	struct Home
	{
		explicit Home (const char* name)
			: dir (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sd-decklinks-" + juce::String (name)))
		{
			dir.deleteRecursively();
			dir.createDirectory();
		}
		~Home() { dir.deleteRecursively(); }

		std::string path() const { return dir.getFullPathName().toStdString(); }
		juce::File userFile (char letter) const
		{
			return dir.getChildFile (".config/StemDeck/links-" + juce::String::charToString (letter) + ".json");
		}
		juce::File write (const juce::String& relative, const juce::String& text) const
		{
			const auto file = dir.getChildFile (relative);
			file.getParentDirectory().createDirectory();
			file.replaceWithText (text);
			return file;
		}

		juce::File dir;
	};

	const juce::File shippedCoreMap { STEMDECK_DATA_DIR "/links-core.json" };
	const std::vector<std::string> enginePorts { "a3-engine:rec_L", "a3-engine:rec_R" };

	// The links a client named `client` would make now, in a graph of its own rec ports and `peers`.
	a3::links::ConnectionPlan planFor (const a3::links::LinkMap& map, const std::string& client, std::vector<std::string> peers)
	{
		peers.push_back (client + ":rec_L");
		peers.push_back (client + ":rec_R");
		return a3::links::planConnections (map, {}, {}, peers, decklinks::ownInputs(), client);
	}

	std::vector<a3::links::Link> recFrom (const std::string& client, const std::string& left, const std::string& right)
	{
		return { { left, client + ":rec_L" }, { right, client + ":rec_R" } };
	}

	juce::String userText (const juce::String& peer) { return R"({"jack": {"bformat_1": [")" + peer + R"("]}})"; }
}

TEST (DeckLinks, UserFileWins)
{
	const Home home ("user");
	home.write (".config/StemDeck/links-B.json", userText ("user:in_1"));
	const auto envFile = home.write ("env.json", userText ("env:in_1"));
	juce::String error;

	const auto map = deckLinkMap ('B', home.path(), envFile.getFullPathName().toRawUTF8(), shippedCoreMap, error);

	ASSERT_TRUE (map.has_value()) << error;
	EXPECT_EQ (map->jack.at ("bformat_1"), Peers { "user:in_1" });
}

TEST (DeckLinks, EnvFileWithLetterSubstituted)
{
	const Home home ("env");
	const auto envFile = home.write ("env.json",
		R"({"jack": {"bformat_1": ["AES67 deck {X}:send_MAIN1"], "rec_L": ["deck {X} from {X}:out"]}})");
	juce::String error;

	const auto map = deckLinkMap ('B', home.path(), envFile.getFullPathName().toRawUTF8(), shippedCoreMap, error);

	ASSERT_TRUE (map.has_value()) << error;
	EXPECT_EQ (map->jack.at ("bformat_1"), Peers { "AES67 deck B:send_MAIN1" });
	EXPECT_EQ (map->jack.at ("rec_L"), Peers { "deck B from B:out" });
}

TEST (DeckLinks, NoFileAndNoDefaultIsEmpty)
{
	const Home home ("none");
	juce::String error;

	const auto map = deckLinkMap ('A', home.path(), nullptr, home.dir.getChildFile ("not-installed.json"), error);

	ASSERT_TRUE (map.has_value()) << error;
	EXPECT_TRUE (map->jack.empty());
	EXPECT_TRUE (map->directLinks.empty());
	EXPECT_TRUE (error.isEmpty());
}

TEST (DeckLinks, NamedEnvFileThatIsMissingIsRefused)
{
	const Home home ("missing");
	juce::String error;

	const auto map = deckLinkMap ('B', home.path(), (home.path() + "/nowhere.json").c_str(), shippedCoreMap, error);

	EXPECT_FALSE (map.has_value());
	EXPECT_TRUE (error.contains ("nowhere.json")) << error;
}

TEST (DeckLinks, UnknownOwnPortIsRefused)
{
	const Home home ("unknown");
	home.write (".config/StemDeck/links-B.json", R"({"jack": {"cue_10": ["x:y"]}})");
	juce::String error;

	const auto map = deckLinkMap ('B', home.path(), nullptr, shippedCoreMap, error);

	EXPECT_FALSE (map.has_value());
	EXPECT_TRUE (error.contains ("cue_10")) << error;
}

TEST (DeckLinks, ShippedAes67MapHas43Links)
{
	const Home home ("shipped");
	juce::String error;

	const auto map = deckLinkMap ('B', home.path(), STEMDECK_DATA_DIR "/links-aes67.json", shippedCoreMap, error);

	ASSERT_TRUE (map.has_value()) << error;
	const auto inputs = decklinks::ownInputs();
	EXPECT_EQ (inputs, Peers ({ "rec_L", "rec_R" }));
	EXPECT_EQ (decklinks::ownOutputs().size(), 43u);

	std::size_t outgoing = 0, incoming = 0;
	for (const auto& [port, peers] : map->jack)
	{
		EXPECT_EQ (peers.size(), 1u) << port;
		(a3::links::isInputPort (port, inputs) ? incoming : outgoing) += peers.size();
	}
	EXPECT_EQ (outgoing, 41u);
	EXPECT_EQ (incoming, 2u);
	EXPECT_TRUE (map->directLinks.empty());

	EXPECT_EQ (map->jack.at ("bformat_1"), Peers { "AES67 deck B:send_MAIN1" });
	EXPECT_EQ (map->jack.at ("bformat_16"), Peers { "AES67 deck B:send_MAIN16" });
	EXPECT_EQ (map->jack.at ("aux_1"), Peers { "AES67 deck B:send_AUX1" });
	EXPECT_EQ (map->jack.at ("aux_16"), Peers { "AES67 deck B:send_AUX16" });
	EXPECT_EQ (map->jack.at ("cue_1"), Peers { "AES67 deck B:send_CUE1" });
	EXPECT_EQ (map->jack.at ("cue_9"), Peers { "AES67 deck B:send_CUE9" });
	EXPECT_EQ (map->jack.at ("rec_L"), Peers { "AES67 a3nuc1 out:receive_CH1" });
	EXPECT_EQ (map->jack.at ("rec_R"), Peers { "AES67 a3nuc1 out:receive_CH2" });
	EXPECT_EQ (map->jack.count ("phones_L"), 0u);
	EXPECT_EQ (map->jack.count ("phones_R"), 0u);
}

TEST (DeckLinks, ForeignLinkIsNeverRemoved)
{
	const Home home ("foreign");
	juce::String error;
	const auto map = deckLinkMap ('B', home.path(), STEMDECK_DATA_DIR "/links-aes67.json", shippedCoreMap, error);
	ASSERT_TRUE (map.has_value()) << error;

	const a3::links::Link foreign { "StemDeck-B:bformat_1", "somebody:in_1" };
	const auto plan = a3::links::planConnections (*map, { foreign }, {},
		{ "StemDeck-B:bformat_1", "somebody:in_1", "AES67 deck B:send_MAIN1" }, decklinks::ownInputs(), "StemDeck-B");

	EXPECT_TRUE (plan.remove.empty());
	ASSERT_EQ (plan.add.size(), 1u);
	EXPECT_EQ (plan.add.front(), (a3::links::Link { "StemDeck-B:bformat_1", "AES67 deck B:send_MAIN1" }));
}

// Configuration 1: the two-deck player (client "StemDeck") on the Core records the engine's REC bus.
TEST (RecorderLinks, Configuration1TwoDeckPlayerOnTheCoreRecordsTheRecBus)
{
	juce::String error;
	const auto map = recorderLinkMap (shippedCoreMap, error);

	ASSERT_TRUE (map.has_value()) << error;
	const auto plan = planFor (*map, "StemDeck", enginePorts);
	EXPECT_EQ (plan.add, recFrom ("StemDeck", "a3-engine:rec_L", "a3-engine:rec_R"));
	EXPECT_TRUE (plan.missingPeers.empty());
}

// Configuration 2: deck A on the Core (no STEMDECK_LINK_MAP) takes the shipped default.
TEST (RecorderLinks, Configuration2DeckAOnTheCoreRecordsTheRecBus)
{
	const Home home ("config2");
	juce::String error;
	const auto map = deckLinkMap ('A', home.path(), nullptr, shippedCoreMap, error);

	ASSERT_TRUE (map.has_value()) << error;
	EXPECT_EQ (planFor (*map, "StemDeck-A", enginePorts).add, recFrom ("StemDeck-A", "a3-engine:rec_L", "a3-engine:rec_R"));
	EXPECT_EQ (map->jack.size(), 2u);   // only the recorder: deck A's sends are the engine's links
}

// Configuration 3: deck B on a deck host; the role's STEMDECK_LINK_MAP wins over the default.
TEST (RecorderLinks, Configuration3DeckHostRecordsTheRecBusFromAes67)
{
	const Home home ("config3");
	juce::String error;
	const auto map = deckLinkMap ('B', home.path(), STEMDECK_DATA_DIR "/links-aes67.json", shippedCoreMap, error);

	ASSERT_TRUE (map.has_value()) << error;
	const auto plan = planFor (*map, "StemDeck-B", { "AES67 a3nuc1 out:receive_CH1", "AES67 a3nuc1 out:receive_CH2" });
	EXPECT_EQ (plan.add, recFrom ("StemDeck-B", "AES67 a3nuc1 out:receive_CH1", "AES67 a3nuc1 out:receive_CH2"));
}

// Without the engine (a laptop) the default map is no error: nothing to link, the peers are missing.
TEST (RecorderLinks, WithoutTheEngineTheDefaultLinksNothingAndIsNoError)
{
	const Home home ("laptop");
	juce::String error;
	const auto map = deckLinkMap ('A', home.path(), "", shippedCoreMap, error);

	ASSERT_TRUE (map.has_value()) << error;
	EXPECT_TRUE (error.isEmpty()) << error;
	const auto plan = planFor (*map, "StemDeck-A", {});
	EXPECT_TRUE (plan.add.empty());
	EXPECT_EQ (plan.missingPeers, enginePorts);
}

TEST (RecorderLinks, TheTwoDeckPlayerWithoutTheShippedMapLinksNothing)
{
	const Home home ("two-deck-dev");
	juce::String error;
	const auto map = recorderLinkMap (home.dir.getChildFile ("not-installed.json"), error);

	ASSERT_TRUE (map.has_value()) << error;
	EXPECT_TRUE (map->jack.empty());
}

TEST (RecorderLinks, TheDefaultIsTheInstalledFile)
{
	EXPECT_STREQ (decklinks::installedCoreMap, "/usr/share/stemdeck/links-core.json");
}

//==============================================================================
// Output: Stereo -- a deck host's master pair goes to the sender's first two
// ports; the AES67 map for B-format names ports a stereo instance does not have.
TEST (DeckLinksStereo, OwnOutputsAreTheFourStereoPorts)
{
	EXPECT_EQ (decklinks::ownOutputs (outputs::OutputMode::stereo), Peers ({ "master_L", "master_R", "phones_L", "phones_R" }));
	EXPECT_EQ (decklinks::ownOutputs().size(), 43u) << "B-format stays the default";
}

TEST (DeckLinksStereo, ShippedStereoMapSendsTheMasterAndRecordsFromTheCore)
{
	const Home home ("stereo-shipped");
	juce::String error;

	// The role names links-aes67.json; the stereo instance reads the one beside it.
	const auto map = deckLinkMap ('B', home.path(), STEMDECK_DATA_DIR "/links-aes67.json", shippedCoreMap, error,
								  outputs::OutputMode::stereo);

	ASSERT_TRUE (map.has_value()) << error;
	EXPECT_EQ (map->jack.size(), 4u);
	EXPECT_EQ (map->jack.at ("master_L"), Peers { "AES67 deck B:send_MASTER_L" });
	EXPECT_EQ (map->jack.at ("master_R"), Peers { "AES67 deck B:send_MASTER_R" });
	EXPECT_EQ (map->jack.at ("rec_L"), Peers { "AES67 a3nuc1 out:receive_CH1" });
	EXPECT_EQ (map->jack.at ("rec_R"), Peers { "AES67 a3nuc1 out:receive_CH2" });
}

// The role sets STEMDECK_LINK_MAP to links-aes67.json; a stereo instance reads
// the file beside it that ends in -stereo.json.
TEST (DeckLinksStereo, TheEnvMapIsTheStereoSiblingInStereoMode)
{
	const Home home ("stereo-sibling");
	const auto bformat = home.write ("links-x.json", userText ("bformat:in"));
	home.write ("links-x-stereo.json", R"({"jack": {"master_L": ["stereo:in_{X}"]}})");
	juce::String error;

	const auto stereo = deckLinkMap ('B', home.path(), bformat.getFullPathName().toRawUTF8(), shippedCoreMap, error,
									 outputs::OutputMode::stereo);
	ASSERT_TRUE (stereo.has_value()) << error;
	EXPECT_EQ (stereo->jack.at ("master_L"), Peers { "stereo:in_B" });

	const auto plain = deckLinkMap ('B', home.path(), bformat.getFullPathName().toRawUTF8(), shippedCoreMap, error);
	ASSERT_TRUE (plain.has_value()) << error;
	EXPECT_EQ (plain->jack.at ("bformat_1"), Peers { "bformat:in" });
}

TEST (DeckLinksStereo, WithoutASiblingTheStereoInstanceIsRefusedNotMislinked)
{
	const Home home ("stereo-nosibling");
	const auto bformat = home.write ("links-x.json", userText ("bformat:in"));
	juce::String error;

	const auto map = deckLinkMap ('B', home.path(), bformat.getFullPathName().toRawUTF8(), shippedCoreMap, error,
								  outputs::OutputMode::stereo);

	EXPECT_FALSE (map.has_value());
	EXPECT_TRUE (error.contains ("links-x-stereo.json")) << error;
}

TEST (DeckLinksStereo, TheUserFileOfAStereoInstanceEndsInStereo)
{
	const Home home ("stereo-user");
	home.write (".config/StemDeck/links-B.json", userText ("bformat:in"));
	home.write (".config/StemDeck/links-B-stereo.json", R"({"jack": {"master_R": ["mine:in"]}})");
	juce::String error;

	const auto map = deckLinkMap ('B', home.path(), nullptr, shippedCoreMap, error, outputs::OutputMode::stereo);

	ASSERT_TRUE (map.has_value()) << error;
	EXPECT_EQ (map->jack.at ("master_R"), Peers { "mine:in" });
}

TEST (DeckLinksStereo, TheDefaultCoreMapStillRecords)
{
	const Home home ("stereo-core");
	juce::String error;
	const auto map = deckLinkMap ('A', home.path(), nullptr, shippedCoreMap, error, outputs::OutputMode::stereo);
	ASSERT_TRUE (map.has_value()) << error;
	EXPECT_EQ (map->jack.size(), 2u);
}
