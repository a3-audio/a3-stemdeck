#include "GridEdit.h"

#include <cmath>

namespace GridEdit
{
	Grid normalised (Grid grid)
	{
		if (grid.bpm <= 0.0)
			return grid;
		const auto bar = beatsPerBar * 60.0 / grid.bpm;
		grid.firstBeat -= std::floor (grid.firstBeat / bar) * bar;
		return grid;
	}

	Grid shift (Grid grid, double seconds)
	{
		grid.firstBeat += seconds;
		return normalised (grid);
	}

	Grid shiftHalfBeat (Grid grid, bool forwards)
	{
		if (grid.bpm <= 0.0)
			return grid;
		return shift (grid, (forwards ? 0.5 : -0.5) * 60.0 / grid.bpm);
	}

	Grid moveOne (Grid grid, bool later)
	{
		if (grid.bpm <= 0.0)
			return grid;
		return shift (grid, (later ? 1.0 : -1.0) * 60.0 / grid.bpm);
	}

	Grid doubleTempo (Grid grid)
	{
		if (grid.bpm <= 0.0)
			return grid;
		grid.bpm *= 2.0;
		return normalised (grid);   // the bar got shorter: firstBeat may now lie past it
	}

	Grid halveTempo (Grid grid)
	{
		if (grid.bpm <= 0.0)
			return grid;
		grid.bpm /= 2.0;
		return normalised (grid);
	}

	Grid snapToCue (Grid grid, double cueSeconds)
	{
		grid.firstBeat = cueSeconds;
		return normalised (grid);
	}

	Grid shiftToPhase (Grid grid, double position, double leaderBeatPhase)
	{
		if (grid.bpm <= 0.0)
			return grid;
		const auto beatLength = 60.0 / grid.bpm;
		const auto ownPhase = (position - grid.firstBeat) / beatLength;
		auto error = (ownPhase - std::floor (ownPhase)) - (leaderBeatPhase - std::floor (leaderBeatPhase));
		error -= std::round (error);   // -0.5 .. 0.5: positive, this deck is ahead of its grid's beat
		return shift (grid, error * beatLength);
	}

	double snappedPoint (Grid grid, double seconds)
	{
		if (grid.bpm <= 0.0)
			return seconds;
		const auto beatLength = 60.0 / grid.bpm;
		return grid.firstBeat + std::round ((seconds - grid.firstBeat) / beatLength) * beatLength;
	}

	Loop snappedLoop (Grid grid, double startSeconds, double endSeconds)
	{
		if (grid.bpm <= 0.0)
			return { startSeconds, endSeconds };

		const auto beatLength = 60.0 / grid.bpm;
		const auto start = snappedPoint (grid, startSeconds);
		auto end = snappedPoint (grid, endSeconds);
		if (end <= start)
			end = start + beatLength;
		return { start, end };
	}
}
