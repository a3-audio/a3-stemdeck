#include "NiStemInfo.h"
#include "NiStems.h"

namespace
{
	juce::uint32 argbOf (const juce::String& hex)
	{
		const auto digits = hex.trimCharactersAtStart ("#");
		return digits.length() == 6 ? 0xff000000u | (juce::uint32) digits.getHexValue32() : 0u;
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
