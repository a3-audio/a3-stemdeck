// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "StemEventsFile.h"

#include "NiStems.h"

#include <cmath>
#include <optional>

namespace stemevents
{
	namespace
	{
		// "ping" is the pad CROSS; the file keeps the word (a3-doc, tools/prepare_events.py).
		const char* const shapeNames[] = { "orbit", "ping", "rise", "scat" };
		constexpr int shapeCount = 4;

		juce::String shapeName (motionlite::Shape shape)
		{
			return shapeNames[(int) shape];
		}

		std::optional<motionlite::Shape> shapeFromName (const juce::String& name)
		{
			for (int i = 0; i < shapeCount; ++i)
				if (name == shapeNames[i])
					return (motionlite::Shape) i;
			return std::nullopt;
		}

		std::optional<Origin> originFromName (const juce::String& name)
		{
			if (name == "hand")     return Origin::hand;
			if (name == "prepared") return Origin::prepared;
			return std::nullopt;
		}

		std::optional<std::uint64_t> seedFromText (const juce::String& text)
		{
			if (text.isEmpty() || ! text.containsOnly ("0123456789") || text.length() > 20)
				return std::nullopt;
			try
			{
				return (std::uint64_t) std::stoull (text.toStdString());
			}
			catch (const std::exception&)
			{
				return std::nullopt;   // above 2^64 - 1
			}
		}

		constexpr int stemCount = 4;
		// The app plays any length it does not know as 4 bars (motionlite::validLength).
		constexpr double unknownBars = 4.0;

		std::optional<double> finiteNumber (const juce::var& v)
		{
			if (! v.isDouble() && ! v.isInt() && ! v.isInt64())
				return std::nullopt;
			const double value = v;
			if (! std::isfinite (value))
				return std::nullopt;
			return value;
		}

		std::optional<int> stemFrom (const juce::var& v)
		{
			const auto value = finiteNumber (v);
			if (! value || *value != std::floor (*value) || *value < 0.0 || *value >= (double) stemCount)
				return std::nullopt;
			return (int) *value;
		}

		double barsFrom (const juce::var& v)
		{
			const auto value = finiteNumber (v);
			return value && *value > 0.0 ? *value : unknownBars;
		}

		bool hasAll (const juce::var& v, std::initializer_list<const char*> names)
		{
			for (const auto* name : names)
				if (! v.hasProperty (name))
					return false;
			return true;
		}

		std::optional<Event> eventFrom (const juce::var& v)
		{
			if (! v.isObject() || ! hasAll (v, { "stem", "beat", "shape", "bars", "reversed", "seed", "active", "origin" }))
				return std::nullopt;
			const auto stem = stemFrom (v["stem"]);
			const auto beat = finiteNumber (v["beat"]);
			const auto shape = shapeFromName (v["shape"].toString());
			const auto origin = originFromName (v["origin"].toString());
			const auto seed = seedFromText (v["seed"].toString());
			if (! stem || ! beat || ! shape || ! origin || ! seed)
				return std::nullopt;

			Event e;
			e.stem = *stem;
			e.beat = *beat;
			e.shape = *shape;
			e.lengthBars = barsFrom (v["bars"]);
			e.reversed = (bool) v["reversed"];
			e.seed = *seed;
			e.active = (bool) v["active"];
			e.origin = *origin;
			return e;
		}

		juce::var toVar (const Event& e)
		{
			auto* object = new juce::DynamicObject();
			object->setProperty ("stem", e.stem);
			object->setProperty ("beat", e.beat);
			object->setProperty ("shape", shapeName (e.shape));
			object->setProperty ("bars", e.lengthBars);
			object->setProperty ("reversed", e.reversed);
			object->setProperty ("seed", juce::String (std::to_string (e.seed)));
			object->setProperty ("active", e.active);
			object->setProperty ("origin", e.origin == Origin::prepared ? "prepared" : "hand");
			return juce::var (object);
		}

		juce::String documentFor (const EventList& list)
		{
			juce::Array<juce::var> events;
			for (const auto& e : list.events())
				events.add (toVar (e));

			auto* root = new juce::DynamicObject();
			root->setProperty ("version", fileVersion);
			root->setProperty ("events", events);
			return juce::JSON::toString (juce::var (root));
		}
	}

	juce::File fileFor (const juce::File& firstStemFile)
	{
		const auto name = firstStemFile.getFileName();
		const auto base = isNiStemFileName (name.toStdString())
			? juce::String (niStemTitle (name.toStdString()))
			: firstStemFile.getFileNameWithoutExtension();
		return firstStemFile.getSiblingFile (base + ".a3events.json");
	}

	Loaded load (const juce::File& file)
	{
		Loaded loaded;
		if (! file.existsAsFile())
			return loaded;

		loaded.status = Status::damaged;
		const auto root = juce::JSON::parse (file.loadFileAsString());
		if (! root.isObject() || ! root.hasProperty ("version") || ! root.hasProperty ("events"))
			return loaded;

		if (! root["version"].isInt() && ! root["version"].isInt64())
			return loaded;
		// Whole: cut to an int, 2^32 + 1 would read as version 1.
		const auto version = (juce::int64) root["version"];
		if (version > fileVersion)
		{
			loaded.status = Status::newer;
			return loaded;
		}
		if (version < 1 || ! root["events"].isArray())
			return loaded;

		for (const auto& item : *root["events"].getArray())
			if (const auto e = eventFrom (item))
				loaded.list.add (*e);   // a full list or a doubled slot drops that event
		loaded.status = Status::ok;
		return loaded;
	}

	bool save (const juce::File& file, const EventList& list)
	{
		if (load (file).status == Status::newer)
			return false;

		if (list.events().empty())
			return ! file.existsAsFile() || file.deleteFile();

		juce::TemporaryFile temporary (file);
		return temporary.getFile().replaceWithText (documentFor (list))
			&& temporary.overwriteTargetFileWithTemporary();
	}
}
