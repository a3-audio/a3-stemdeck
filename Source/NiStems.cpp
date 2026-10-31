#include "NiStems.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace
{
	const std::string niSuffix = ".stem.mp4";

	std::string lowerCase (std::string text)
	{
		std::transform (text.begin(), text.end(), text.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
		return text;
	}

	std::string withoutTrailingSlash (std::string path)
	{
		while (path.size() > 1 && path.back() == '/')
			path.pop_back();
		return path;
	}

	uint64_t bigEndian (const std::string& bytes, size_t at, size_t count)
	{
		uint64_t value = 0;
		for (size_t i = 0; i < count; ++i)
			value = (value << 8) | (unsigned char) bytes[at + i];
		return value;
	}

	struct Box
	{
		std::string type;
		uint64_t payload = 0;   // where its content starts
		uint64_t end = 0;
	};

	// The boxes directly inside [begin, end), read header by header: a box that
	// claims more than its parent holds ends the walk (a cut or lying file).
	std::vector<Box> childBoxes (const ReadAt& read, uint64_t begin, uint64_t end)
	{
		std::vector<Box> boxes;
		for (auto at = begin; at + 8 <= end;)
		{
			const auto header = read (at, 16);
			if (header.size() < 8)
				break;
			uint64_t size = bigEndian (header, 0, 4);
			uint64_t headerSize = 8;
			if (size == 1)
			{
				if (header.size() < 16)
					break;
				size = bigEndian (header, 8, 8);
				headerSize = 16;
			}
			else if (size == 0)
			{
				size = end - at;   // to the end of its parent
			}
			if (size < headerSize || size > end - at)
				break;
			boxes.push_back ({ header.substr (4, 4), at + headerSize, at + size });
			at += size;
		}
		return boxes;
	}

	std::optional<Box> child (const ReadAt& read, const Box& parent, const std::string& type, uint64_t skip = 0)
	{
		for (const auto& box : childBoxes (read, parent.payload + skip, parent.end))
			if (box.type == type)
				return box;
		return std::nullopt;
	}

	std::string payloadOf (const ReadAt& read, const Box& box)
	{
		return read (box.payload, (size_t) (box.end - box.payload));
	}

	// An iTunes tag item holds a 'data' box: 4 bytes type, 4 bytes locale, then the text.
	std::string tagText (const ReadAt& read, const Box& ilst, const std::string& type)
	{
		const auto item = child (read, ilst, type);
		if (! item)
			return {};
		const auto data = child (read, *item, "data");
		if (! data)
			return {};
		const auto payload = payloadOf (read, *data);
		return payload.size() > 8 ? payload.substr (8) : std::string();
	}
}

bool isNiStemFileName (const std::string& fileName)
{
	return fileName.size() > niSuffix.size()
		&& lowerCase (fileName.substr (fileName.size() - niSuffix.size())) == niSuffix;
}

std::string niStemTitle (const std::string& fileName)
{
	return isNiStemFileName (fileName) ? fileName.substr (0, fileName.size() - niSuffix.size()) : fileName;
}

std::string niStemCacheFolderName (const std::string& path, long long size, long long modifiedMs)
{
	// FNV-1a: stable across runs and builds, unlike std::hash.
	uint64_t hash = 14695981039346656037ull;
	for (const char c : path + "|" + std::to_string (size) + "|" + std::to_string (modifiedMs))
		hash = (hash ^ (uint64_t) (unsigned char) c) * 1099511628211ull;
	char hex[17];
	std::snprintf (hex, sizeof hex, "%016llx", (unsigned long long) hash);
	return hex;
}

std::string niStemCacheRoot (const std::string& home, const char* xdgCacheHome)
{
	const bool usable = xdgCacheHome != nullptr && xdgCacheHome[0] == '/';
	const auto base = usable ? withoutTrailingSlash (xdgCacheHome) : withoutTrailingSlash (home) + "/.cache";
	return base + "/stemdeck/ni-stems";
}

NiStemMetadata readNiStemMetadata (const ReadAt& read, uint64_t fileSize)
{
	NiStemMetadata metadata;
	const Box file { "", 0, fileSize };
	const auto moov = child (read, file, "moov");
	if (! moov)
		return metadata;
	const auto udta = child (read, *moov, "udta");
	if (! udta)
		return metadata;

	if (const auto stem = child (read, *udta, "stem"))
		metadata.stemJson = payloadOf (read, *stem);

	// 'meta' is a full box: version and flags come before its children.
	if (const auto meta = child (read, *udta, "meta"))
		if (const auto ilst = child (read, *meta, "ilst", 4))
		{
			metadata.title = tagText (read, *ilst, "\xa9nam");
			metadata.artist = tagText (read, *ilst, "\xa9" "ART");
		}
	return metadata;
}

std::vector<std::string> niStemExtractArgs (const std::string& source, const std::array<std::string, 4>& stemFiles)
{
	std::vector<std::string> args { "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-i", source };
	for (size_t slot = 0; slot < stemFiles.size(); ++slot)
		args.insert (args.end(), { "-map", "0:a:" + std::to_string (slot + 1), "-vn", "-ar", "44100", "-sample_fmt", "s32",
								   "-bits_per_raw_sample", "24", "-c:a", "flac", stemFiles[slot] });
	return args;
}
