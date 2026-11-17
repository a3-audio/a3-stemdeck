// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Which analysis steps a set still needs, as the deck decides them when a set
// is loaded (DeckHost::takeOn) and as stemdeck-analyse decides them for a
// whole library. Pure: no JUCE, testable.
namespace AnalysisSteps
{
	struct State
	{
		bool hasGrid = false;
		bool hasFeatures = false;
		bool gridNeedsNewDownbeat = false;   // CachedGrid::needsNewDownbeat
	};

	struct Todo
	{
		bool grid = false;
		bool sections = false;
		bool downbeat = false;

		bool any() const { return grid || sections || downbeat; }
	};

	inline Todo todoFor (const State& state)
	{
		Todo todo;
		todo.grid = ! state.hasGrid;
		todo.sections = ! state.hasFeatures;
		// A fresh grid already carries the one found from the stems.
		todo.downbeat = state.hasGrid && state.gridNeedsNewDownbeat;
		return todo;
	}
}
