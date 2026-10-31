#include "NiStemCache.h"
#include "NiStems.h"

namespace
{
	juce::uint32 argbOf (const juce::String& hex)
	{
		const auto digits = hex.trimCharactersAtStart ("#");
		return digits.length() == 6 ? 0xff000000u | (juce::uint32) digits.getHexValue32() : 0u;
	}

	std::array<juce::File, 4> filesIn (const juce::File& folder, const juce::String& suffix)
	{
		std::array<juce::File, 4> files;
		for (int slot = 0; slot < 4; ++slot)
			files[(size_t) slot] = folder.getChildFile (juce::String (slot + 1) + suffix);
		return files;
	}

	bool runToEnd (juce::ChildProcess& process, const std::function<bool()>& shouldAbort)
	{
		while (! process.waitForProcessToFinish (200))
			if (shouldAbort())
			{
				process.kill();
				return false;
			}
		return process.getExitCode() == 0;
	}
}

std::optional<NiStemInfo> parseNiStemJson (const juce::String& json)
{
	const auto parsed = juce::JSON::parse (json);
	const auto* stems = parsed.getProperty ("stems", {}).getArray();
	if (stems == nullptr || stems->size() < 4)
		return std::nullopt;

	NiStemInfo info;
	for (int slot = 0; slot < 4; ++slot)
	{
		const auto& stem = stems->getReference (slot);
		info.names[(size_t) slot] = stem.getProperty ("name", {}).toString();
		info.colours[(size_t) slot] = argbOf (stem.getProperty ("color", {}).toString());
	}
	return info;
}

NiStemFileInfo readNiStemFileInfo (const juce::File& source)
{
	NiStemFileInfo info;
	info.setName = juce::String (niStemTitle (source.getFileName().toStdString()));

	juce::FileInputStream in (source);
	if (! in.openedOk())
		return info;
	const auto read = [&in] (uint64_t offset, size_t length)
	{
		std::string bytes (length, '\0');
		if (! in.setPosition ((juce::int64) offset))
			return std::string();
		bytes.resize ((size_t) juce::jmax (0, in.read (bytes.data(), (int) juce::jmin (length, (size_t) 1 << 26))));
		return bytes;
	};
	const auto metadata = readNiStemMetadata (read, (uint64_t) source.getSize());

	const auto title = juce::String::fromUTF8 (metadata.title.c_str()).trim();
	const auto artist = juce::String::fromUTF8 (metadata.artist.c_str()).trim();
	if (title.isNotEmpty())
		info.setName = artist.isNotEmpty() ? artist + " - " + title : title;
	if (metadata.stemJson)
		info.stems = parseNiStemJson (juce::String::fromUTF8 (metadata.stemJson->c_str()));
	return info;
}

NiStemCache::NiStemCache (juce::File r) : root (std::move (r)) {}

std::array<juce::File, 4> NiStemCache::stemFilesFor (const juce::File& source) const
{
	const auto folder = root.getChildFile (niStemCacheFolderName (source.getFullPathName().toStdString(), source.getSize(),
																  source.getLastModificationTime().toMilliseconds()));
	return filesIn (folder, ".flac");
}

bool NiStemCache::isReady (const juce::File& source) const
{
	for (const auto& stem : stemFilesFor (source))
		if (! stem.existsAsFile())
			return false;
	return true;
}

bool NiStemCache::prepare (const juce::File& source, const std::function<bool()>& shouldAbort) const
{
	if (isReady (source))
		return true;

	const auto stems = stemFilesFor (source);
	const auto folder = stems[0].getParentDirectory();
	const auto parts = filesIn (folder, ".part.flac");
	if (! folder.createDirectory())
		return false;

	std::array<std::string, 4> partPaths;
	for (size_t slot = 0; slot < parts.size(); ++slot)
		partPaths[slot] = parts[slot].getFullPathName().toStdString();

	// Niced: a deck may be playing while the library is prepared.
	juce::StringArray args { "nice", "-n", "19" };
	for (const auto& arg : niStemExtractArgs (source.getFullPathName().toStdString(), partPaths))
		args.add (juce::String (arg));

	juce::ChildProcess ffmpeg;
	bool done = ffmpeg.start (args, 0) && runToEnd (ffmpeg, shouldAbort);
	for (size_t slot = 0; done && slot < parts.size(); ++slot)
		done = parts[slot].moveFileTo (stems[slot]);

	if (! done)
		folder.deleteRecursively();
	return done;
}
