// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TitleLayout.h"

#include <cstddef>

namespace titlelayout
{
	namespace
	{
		const std::string ellipsis = "\xE2\x80\xA6";
		constexpr float scaleStep = 0.05f;

		bool isContinuation (char c) { return (static_cast<unsigned char> (c) & 0xC0) == 0x80; }

		// Byte length of the code point starting at `at`.
		size_t glyphLength (const std::string& text, size_t at)
		{
			size_t end = at + 1;
			while (end < text.size() && isContinuation (text[end]))
				++end;
			return end - at;
		}

		std::vector<std::string> splitWords (const std::string& text)
		{
			std::vector<std::string> words;
			size_t start = 0;
			while (start < text.size())
			{
				const auto space = text.find (' ', start);
				const auto end = space == std::string::npos ? text.size() : space;
				if (end > start)
					words.push_back (text.substr (start, end - start));
				start = end + 1;
			}
			return words;
		}

		// The longest head of `word` that fits; at least one glyph, so the loop advances.
		size_t fittingHead (const std::string& word, float room, const Measure& measure)
		{
			size_t head = glyphLength (word, 0);
			while (head < word.size())
			{
				const auto next = head + glyphLength (word, head);
				if (measure (word.substr (0, next)) > room)
					break;
				head = next;
			}
			return head;
		}

		// Greedy wrap into lines no wider than `room`; words wider than a line are split.
		std::vector<std::string> wrap (const std::string& text, float room, const Measure& measure)
		{
			std::vector<std::string> lines;
			std::string line;
			for (auto word : splitWords (text))
			{
				while (true)
				{
					const auto joined = line.empty() ? word : line + " " + word;
					if (measure (joined) <= room)
					{
						line = joined;
						break;
					}
					if (! line.empty())
					{
						lines.push_back (line);
						line.clear();
						continue;
					}
					const auto head = fittingHead (word, room, measure);
					lines.push_back (word.substr (0, head));
					word = word.substr (head);
					if (word.empty())
						break;
				}
			}
			if (! line.empty())
				lines.push_back (line);
			return lines;
		}

		// `text` cut at a glyph, plus the ellipsis, so that it fits `room`.
		std::string withEllipsis (std::string text, float room, const Measure& measure)
		{
			while (! text.empty() && measure (text + ellipsis) > room)
			{
				auto last = text.size() - 1;
				while (last > 0 && isContinuation (text[last]))
					--last;
				text.erase (last);
			}
			return text + ellipsis;
		}

		std::string tail (const std::vector<std::string>& lines, size_t from)
		{
			std::string joined;
			for (auto i = from; i < lines.size(); ++i)
				joined += (joined.empty() ? "" : " ") + lines[i];
			return joined;
		}
	}

	Result layout (const std::string& title, float width, const Measure& measure)
	{
		Result result;
		if (title.empty() || width <= 0.0f || measure (title.substr (0, glyphLength (title, 0))) * smallestScale > width)
			return result;

		for (float scale = 1.0f; scale > smallestScale - scaleStep / 2.0f; scale -= scaleStep)
		{
			const auto lines = wrap (title, width / scale, measure);
			if (lines.size() <= static_cast<size_t> (maxLines))
			{
				result.lines = lines;
				result.scale = scale;
				return result;
			}
		}

		const auto room = width / smallestScale;
		const auto lines = wrap (title, room, measure);
		result.lines.assign (lines.begin(), lines.begin() + (maxLines - 1));
		result.lines.push_back (withEllipsis (tail (lines, maxLines - 1), room, measure));
		result.scale = smallestScale;
		result.ellipsized = true;
		return result;
	}
}
