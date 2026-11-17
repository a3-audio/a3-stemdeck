#include <gtest/gtest.h>

#include "AnalysisSteps.h"

using AnalysisSteps::State;

TEST (AnalysisSteps, ASetNeverSeenNeedsEveryStepButTheOneFoundAgain)
{
	const auto todo = AnalysisSteps::todoFor (State { false, false, false });
	EXPECT_TRUE (todo.grid);
	EXPECT_TRUE (todo.sections);
	EXPECT_FALSE (todo.downbeat);   // the new analysis finds the one itself
}

TEST (AnalysisSteps, ASetWithAGridButNoSectionsGetsTheSections)
{
	const auto todo = AnalysisSteps::todoFor (State { true, false, false });
	EXPECT_FALSE (todo.grid);
	EXPECT_TRUE (todo.sections);
}

TEST (AnalysisSteps, AnOldGridGetsItsOneFoundAgain)
{
	const auto todo = AnalysisSteps::todoFor (State { true, true, true });
	EXPECT_FALSE (todo.grid);
	EXPECT_FALSE (todo.sections);
	EXPECT_TRUE (todo.downbeat);
}

TEST (AnalysisSteps, ACompleteSetHasNothingToDo)
{
	EXPECT_FALSE (AnalysisSteps::todoFor (State { true, true, false }).any());
}
