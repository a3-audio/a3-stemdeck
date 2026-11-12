// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "AppSettings.h"

namespace
{
	juce::File freshConfigDir (const char* name)
	{
		auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sd-appsettings-" + juce::String (name));
		dir.deleteRecursively();
		return dir;
	}
}

TEST (AppSettings, AnInstanceSettingsFileFollowsItsLetterBesideItsSession)
{
	const auto config = freshConfigDir ("path");
	EXPECT_EQ (appsettings::instanceSettingsFile (config, 'A'), config.getChildFile ("A").getChildFile ("StemDeck.settings"));
	EXPECT_EQ (appsettings::instanceSettingsFile (config, 'B'), config.getChildFile ("B").getChildFile ("StemDeck.settings"));

	juce::PropertiesFile file (appsettings::instanceOptions (config, 'B'));
	EXPECT_EQ (file.getFile(), appsettings::instanceSettingsFile (config, 'B'));
}

TEST (AppSettings, TwoInstancesWritingInterleavedKeepTheirOwnKeys)
{
	const auto config = freshConfigDir ("interleaved");
	{
		juce::PropertiesFile a (appsettings::instanceOptions (config, 'A'));
		juce::PropertiesFile b (appsettings::instanceOptions (config, 'B'));   // both open before either saves
		a.setValue ("panelHere", true);
		b.setValue ("panelHere", false);
		b.setValue ("phonesMode", "on");
		a.setValue ("windowState", "wsA");
		ASSERT_TRUE (b.saveIfNeeded());
		ASSERT_TRUE (a.saveIfNeeded());
	}
	juce::PropertiesFile a (appsettings::instanceOptions (config, 'A'));
	juce::PropertiesFile b (appsettings::instanceOptions (config, 'B'));
	EXPECT_TRUE (a.getBoolValue ("panelHere"));
	EXPECT_EQ (a.getValue ("windowState"), "wsA");
	EXPECT_FALSE (b.getBoolValue ("panelHere"));
	EXPECT_EQ (b.getValue ("phonesMode"), "on");
	config.deleteRecursively();
}

// KEY quality is a device setting: written, saved, read back by the next
// start; a file without it gives the light engine.
TEST (AppSettings, TheKeyQualityPersistsAndReloads)
{
	const auto config = freshConfigDir ("keyquality");
	{
		juce::PropertiesFile a (appsettings::instanceOptions (config, 'A'));
		EXPECT_EQ (appsettings::keyLockEngine (a), keylock::Engine::standard) << "the default";
		appsettings::storeKeyLockEngine (a, keylock::Engine::high);
		ASSERT_TRUE (a.saveIfNeeded());
	}
	juce::PropertiesFile a (appsettings::instanceOptions (config, 'A'));
	EXPECT_EQ (appsettings::keyLockEngine (a), keylock::Engine::high);
	juce::PropertiesFile b (appsettings::instanceOptions (config, 'B'));
	EXPECT_EQ (appsettings::keyLockEngine (b), keylock::Engine::standard) << "per instance";
	config.deleteRecursively();
}

TEST (AppSettings, TheOutputModePersistsAndDefaultsToBFormat)
{
	const auto config = freshConfigDir ("outputmode");
	{
		juce::PropertiesFile a (appsettings::instanceOptions (config, 'A'));
		EXPECT_EQ (appsettings::outputMode (a), outputs::OutputMode::bformat) << "the default";
		appsettings::storeOutputMode (a, outputs::OutputMode::stereo);
		ASSERT_TRUE (a.saveIfNeeded());
	}
	juce::PropertiesFile a (appsettings::instanceOptions (config, 'A'));
	EXPECT_EQ (appsettings::outputMode (a), outputs::OutputMode::stereo);
	juce::PropertiesFile b (appsettings::instanceOptions (config, 'B'));
	EXPECT_EQ (appsettings::outputMode (b), outputs::OutputMode::bformat) << "per instance";
	config.deleteRecursively();
}
