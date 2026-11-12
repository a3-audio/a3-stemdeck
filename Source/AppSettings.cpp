// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "AppSettings.h"

#include "PanelOwnership.h"

#include <cstdlib>
#include <unistd.h>

namespace appsettings
{
	namespace
	{
		constexpr const char* keyLockEngineKey = "keyLockEngine";
		constexpr const char* outputModeKey = "outputMode";   // also read by tools/stemdeck-aes67-deck
	}

	juce::PropertiesFile::Options options()
	{
		juce::PropertiesFile::Options options;
		options.applicationName = "StemDeck";
	   #if JUCE_LINUX
		options.folderName = "~/.config/StemDeck";
	   #else
		options.folderName = "StemDeck";
	   #endif
		options.filenameSuffix = ".settings";
		options.osxLibrarySubFolder = "Application Support";
		return options;
	}

	juce::File analysisCacheFile()
	{
		return options().getDefaultFile().getSiblingFile ("analysis.xml");
	}

	juce::File panelLockFile()
	{
		return juce::File (juce::String (panel::lockFilePath (std::getenv ("XDG_RUNTIME_DIR"), (unsigned) ::getuid()).string()));
	}

	juce::File instanceSettingsFile (const juce::File& configDir, char letter)
	{
		return configDir.getChildFile (juce::String::charToString ((juce::juce_wchar) letter)).getChildFile ("StemDeck.settings");
	}

	juce::PropertiesFile::Options instanceOptions (const juce::File& configDir, char letter)
	{
		const auto file = instanceSettingsFile (configDir, letter);
		juce::PropertiesFile::Options options;
		options.applicationName = file.getFileNameWithoutExtension();
		options.filenameSuffix = file.getFileExtension();   // JUCE always adds one
		options.folderName = file.getParentDirectory().getFullPathName();
		options.osxLibrarySubFolder = "Application Support";
		return options;
	}

	juce::PropertiesFile::Options instanceOptions (char letter)
	{
		return instanceOptions (options().getDefaultFile().getParentDirectory(), letter);
	}

	keylock::Engine keyLockEngine (const juce::PropertiesFile& settings)
	{
		return keylock::engineFrom (settings.getValue (keyLockEngineKey).toStdString());
	}

	void storeKeyLockEngine (juce::PropertiesFile& settings, keylock::Engine engine)
	{
		settings.setValue (keyLockEngineKey, juce::String (keylock::engineName (engine)));
	}

	outputs::OutputMode outputMode (const juce::PropertiesFile& settings)
	{
		return outputs::outputModeFrom (settings.getValue (outputModeKey).toStdString());
	}

	void storeOutputMode (juce::PropertiesFile& settings, outputs::OutputMode mode)
	{
		settings.setValue (outputModeKey, juce::String (outputs::outputModeName (mode)));
	}
}
