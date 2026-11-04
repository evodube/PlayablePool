#pragma once

// The computer opponent's shot picking. Plain C++ like the rest of sim/, tested in tests/ai_test.cpp.
//
// For every legal ball and every pocket it works out the ghost-ball aim (where the cue ball's centre has to be at
// contact to send the object ball at the pocket's mouth), drops shots that are blocked or cut too thin, ranks the
// rest, and plays the best few out on a copy of the table to keep only the ones that really pot a ball without a
// foul. Then it spoils the chosen shot a little according to skill.

#include <cstdint>
#include <random>

#include "sim/Rules.h"
#include "sim/Sim.h"

namespace pool
{
	struct AiParams
	{
		double skill{ 0.6 };      // 0 = hopeless, 1 = never misses its aim
		double minSpeed{ 28.0 };  // the shot speed range, as the player's ([Shot] fMinSpeed / fMaxSpeed)
		double maxSpeed{ 365.0 };
		int    validate{ 8 };     // how many of the best shots are played out on a copy of the table
	};

	struct AiShot
	{
		Vec2   dir{ 1.0, 0.0 };  // which way to hit the cue ball (unit)
		double speed{ 0.0 };
		int    target{ -1 };     // the ball it means to pot (-1: a safety, just a legal hit)
		int    pocket{ -1 };     // and where
		bool   placeCue{ false };  // ball in hand: put the cue ball at cuePos first
		Vec2   cuePos;
		bool   checked{ false };   // playing it out on a copy potted the target without a foul
		double score{ 0.0 };
	};

	// The shot for a_player. With ball in hand it also says where to put the cue ball. The aim and speed errors
	// for a_params.skill are already in dir and speed.
	AiShot ChooseShot(const Sim& a_sim, const Rules& a_rules, int a_player, const AiParams& a_params, std::mt19937& a_rng);

	// What ChooseShot would play with perfect aim (no error added), from the cue ball where it is now.
	AiShot BestShot(const Sim& a_sim, const Rules& a_rules, int a_player, const AiParams& a_params);
}
