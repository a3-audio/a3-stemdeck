// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Analyses every set of a stems folder the way StemDeck does when a set is
// loaded (DeckHost::takeOn): the tempo grid with the bar's one, the section
// features, and the bar's one found again for a grid from before it was found
// from the stems. What a set already has in the cache is left alone; a grid
// corrected by hand is never touched. Same code as the app, so the results
// are the app's.
//
//   stemdeck-analyse [--cache FILE] [--dry-run] [--only NAME] <stems folder>
//
// The cache is ~/.config/StemDeck/analysis.xml unless --cache says otherwise.
// Exit 0 when every set was done, 1 when one could not be analysed.

#include <JuceHeader.h>
#include "AnalysisSteps.h"
#include "SectionAnalysis.h"
#include "TempoAnalysis.h"

#include <cstdio>

namespace
{
	struct Options
	{
		juce::File cache = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".config/StemDeck/analysis.xml");
		juce::File folder;
		juce::String only;
		bool dryRun = false;
	};

	std::optional<Options> parse (const juce::StringArray& args)
	{
		Options options;
		for (int i = 0; i < args.size(); ++i)
		{
			const auto& word = args[i];
			if (word == "--dry-run")
				options.dryRun = true;
			else if ((word == "--cache" || word == "--only") && i + 1 < args.size())
			{
				if (word == "--cache")
					options.cache = juce::File::getCurrentWorkingDirectory().getChildFile (args[++i]);
				else
					options.only = args[++i];
			}
			else if (! word.startsWith ("--") && options.folder == juce::File())
				options.folder = juce::File::getCurrentWorkingDirectory().getChildFile (word);
			else
				return std::nullopt;
		}
		if (! options.folder.isDirectory())
			return std::nullopt;
		return options;
	}

	AnalysisSteps::Todo todoOf (const AnalysisCache& cache, const StemSet& set)
	{
		AnalysisSteps::State state;
		state.hasGrid = cache.findAnalysed (set).has_value();
		state.hasFeatures = cache.findFeatures (set).has_value();
		state.gridNeedsNewDownbeat = cache.needsNewDownbeat (set);
		return AnalysisSteps::todoFor (state);
	}

	juce::String stepsText (const AnalysisSteps::Todo& todo)
	{
		juce::StringArray steps;
		if (todo.grid)     steps.add ("grid");
		if (todo.sections) steps.add ("sections");
		if (todo.downbeat) steps.add ("downbeat");
		return steps.joinIntoString (" + ");
	}

	// The three steps in the order the deck runs them. False if one failed.
	bool analyseSet (AnalysisCache& cache, const StemSet& set, const AnalysisSteps::Todo& todo, juce::AudioFormatManager& formats)
	{
		if (todo.grid)
		{
			const auto grid = TempoAnalysis::analyse (set, formats);
			if (! grid.isValid())
				return false;
			cache.store (set, grid);
		}

		const auto grid = cache.findAnalysed (set);
		if (! grid)
			return false;

		if (todo.sections)
		{
			const auto features = SectionAnalysis::analyse (set, formats, *grid);
			if (! features)
				return false;
			cache.storeFeatures (set, *features);
		}

		if (todo.downbeat)
		{
			const auto found = TempoAnalysis::redetectDownbeat (set, formats, *grid);
			if (! found.isValid())
				return false;
			cache.storeNewDownbeat (set, found.firstBeat);
		}
		return true;
	}
}

int main (int argc, char* argv[])
{
	juce::StringArray args;
	for (int i = 1; i < argc; ++i)
		args.add (argv[i]);

	const auto options = parse (args);
	if (! options)
	{
		std::fprintf (stderr, "usage: stemdeck-analyse [--cache FILE] [--dry-run] [--only NAME] <stems folder>\n");
		return 2;
	}

	juce::AudioFormatManager formats;
	formats.registerBasicFormats();
	AnalysisCache cache (options->cache);

	int done = 0, failed = 0, skipped = 0;
	for (const auto& set : StemSet::scanFolder (options->folder, formats))
	{
		if (options->only.isNotEmpty() && set.name != options->only)
			continue;

		const auto todo = todoOf (cache, set);
		if (! todo.any())
		{
			++skipped;
			continue;
		}

		std::printf ("%s: %s\n", set.name.toRawUTF8(), stepsText (todo).toRawUTF8());
		std::fflush (stdout);
		if (options->dryRun)
			continue;

		if (analyseSet (cache, set, todo, formats))
			++done;
		else
		{
			++failed;
			std::fprintf (stderr, "could not analyse: %s\n", set.name.toRawUTF8());
		}
	}

	cache.flush();
	std::printf ("%d analysed, %d failed, %d already complete%s\n", done, failed, skipped, options->dryRun ? " (dry run)" : "");
	return failed == 0 ? 0 : 1;
}
