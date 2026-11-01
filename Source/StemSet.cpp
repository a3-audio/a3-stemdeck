#include "StemSet.h"
#include "LibraryPath.h"
#include "StemNames.h"
#include "NiStemInfo.h"
#include "StemReader.h"
#include "NiStems.h"
#include <map>

namespace
{
	// Its folder below the library folder, and artist and album from it (LibraryPath.h).
	void placeInLibrary (StemSet& set, const juce::File& setFolder, const juce::File& library)
	{
		// Straight in the library folder JUCE's relative path is ".", not "".
		const auto relative = setFolder == library ? juce::String() : setFolder.getRelativePathFrom (library);
		set.folder = relative.replaceCharacter ('\\', '/');
		const auto place = libraryPlaceOf (set.folder.toStdString());
		set.artist = juce::String (place.artist);
		set.album = juce::String (place.album);
	}

	bool inIgnoredFolder (const juce::File& file, const juce::File& library)
	{
		const auto fileFolder = file.getParentDirectory();
		return fileFolder != library
			&& isIgnoredLibraryFolder (fileFolder.getRelativePathFrom (library).replaceCharacter ('\\', '/').toStdString());
	}

	// From the first slot's header; an NI set's from its first stem's track.
	void readLength (StemSet& set, juce::AudioFormatManager& formats)
	{
		for (int slot = 0; slot < StemSet::numStems; ++slot)
			if (set.hasStem (slot))
			{
				if (std::unique_ptr<juce::AudioFormatReader> reader { createStemReader (set, slot, formats) })
					set.lengthSeconds = (double) reader->lengthInSamples / reader->sampleRate;
				return;
			}
	}

	// One set per NI Stems file; names from NI's metadata, else StemDeck's own.
	void addNiStemSets (std::vector<StemSet>& sets, const juce::File& library, juce::AudioFormatManager& formats)
	{
		const std::array<juce::String, StemSet::numStems> defaultNames { "drums", "bass", "other", "vocals" };
		for (const auto& entry : juce::RangedDirectoryIterator (library, true, "*.mp4;*.MP4;*.Mp4;*.mP4", juce::File::findFiles))
		{
			const auto file = entry.getFile();
			if (! isNiStemFileName (file.getFileName().toStdString()) || inIgnoredFolder (file, library))
				continue;

			const auto info = readNiStemFileInfo (file);
			StemSet set;
			set.niStems = true;
			set.name = info.setName;
			set.files.fill (file);
			set.stemNames = info.stems ? info.stems->names : defaultNames;
			placeInLibrary (set, file.getParentDirectory(), library);
			readLength (set, formats);
			sets.push_back (std::move (set));
		}
	}
}

std::vector<StemSet> StemSet::scanFolder (const juce::File& folder, juce::AudioFormatManager& formats)
{
	// Grouped per directory, so equal names in different folders stay apart.
	std::map<juce::String, std::vector<juce::File>> byFolder;

	for (const auto& entry : juce::RangedDirectoryIterator (folder, true, formats.getWildcardForAllFormats(), juce::File::findFiles))
	{
		const auto file = entry.getFile();
		// The stem creator's originals are not sets (LibraryPath.h).
		if (inIgnoredFolder (file, folder))
			continue;
		byFolder[file.getParentDirectory().getFullPathName()].push_back (file);
	}

	const auto naturalLess = [] (const std::string& a, const std::string& b)
	{
		return juce::String (a).compareNatural (juce::String (b)) < 0;
	};

	std::vector<StemSet> sets;

	for (const auto& [path, files] : byFolder)
	{
		std::vector<std::string> baseNames;
		for (const auto& file : files)
			baseNames.push_back (file.getFileNameWithoutExtension().toStdString());

		for (const auto& grouping : groupStemFiles (baseNames, naturalLess))
		{
			StemSet set;
			set.name = juce::String (grouping.name);
			for (size_t s = 0; s < grouping.slots.size(); ++s)
			{
				if (grouping.slots[s])
					set.files[s] = files[*grouping.slots[s]];
				set.stemNames[s] = juce::String (grouping.stemNames[s]);
			}

			placeInLibrary (set, juce::File (path), folder);
			readLength (set, formats);
			sets.push_back (std::move (set));
		}
	}

	addNiStemSets (sets, folder, formats);

	std::sort (sets.begin(), sets.end(), [] (const StemSet& a, const StemSet& b)
	{
		if (const auto order = a.artist.compareNatural (b.artist); order != 0) return order < 0;
		if (const auto order = a.album.compareNatural (b.album); order != 0)   return order < 0;
		return a.name.compareNatural (b.name) < 0;
	});

	return sets;
}
