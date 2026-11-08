// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "DeleteArm.h"

// DELETE takes two deliberate presses: the first only arms.
TEST (DeleteArm, FirstPressArmsOnly)
{
	DeleteArm arm;
	EXPECT_FALSE (arm.armed (10.0));
	arm.press (10.0);
	EXPECT_TRUE (arm.armed (10.0));
	EXPECT_TRUE (arm.armed (11.9));
	EXPECT_FALSE (arm.confirmed()) << "one press deletes nothing";
	EXPECT_FALSE (arm.armed (12.1)) << "the arm lets go after two seconds";
}

TEST (DeleteArm, SecondPressWithinTwoSecondsConfirms)
{
	DeleteArm arm;
	arm.press (10.0);
	arm.press (11.5);
	EXPECT_TRUE (arm.confirmed());
	EXPECT_FALSE (arm.armed (11.5)) << "confirmed, not armed for a third";

	arm.press (11.6);
	EXPECT_FALSE (arm.confirmed()) << "true once: the next press starts over";
	EXPECT_TRUE (arm.armed (11.6));
}

TEST (DeleteArm, ASlowSecondPressArmsAgain)
{
	DeleteArm arm;
	arm.press (10.0);
	arm.press (12.5);
	EXPECT_FALSE (arm.confirmed());
	EXPECT_TRUE (arm.armed (12.5)) << "armed afresh from the slow press";
	arm.press (13.0);
	EXPECT_TRUE (arm.confirmed());
}

TEST (DeleteArm, ArmDropsWhenTheSelectionChanges)
{
	DeleteArm arm;
	arm.press (10.0);
	ASSERT_TRUE (arm.armed (10.0));
	arm.drop();
	EXPECT_FALSE (arm.armed (10.5));
	arm.press (10.5);
	EXPECT_FALSE (arm.confirmed()) << "the press after a new selection only arms";
	EXPECT_TRUE (arm.armed (10.5));
}
