#pragma once

// 8-ball rules (close to the WPA's, simplified where a video game wants it simpler). Plain C++ like the physics;
// tested in tests/rules_test.cpp.
//
//  - The break: anything potted counts and the breaker carries on, but the table stays open (no groups yet).
//    The 8 potted on the break is spotted again, not a loss. A scratch on the break is a foul.
//  - Open table: any ball but the 8 may be hit first. The first ball legally potted decides the shooter's group
//    (solids 1-7 or stripes 9-15).
//  - A legal shot: the cue ball first hits one of the shooter's own balls (the 8 once they're cleared), and after
//    that something is potted or some ball touches a cushion.
//  - Fouls (scratch, hitting nothing, hitting the wrong ball first, no cushion after contact) end the turn and the
//    opponent gets ball in hand: the cue ball anywhere on the table. Balls potted on a foul stay down.
//  - The shooter carries on after a legal shot that pots one of their own balls.
//  - Potting the 8 wins only once the shooter's group was already cleared before the shot, on a legal shot, and (if
//    calling is on) into the called pocket. Otherwise potting the 8 loses.
//
// 9-ball (two players): balls 1-9 only. Every shot must hit the lowest-numbered ball on the table first; then
// something goes in or a ball touches a cushion (not asked of the break). Any ball potted on a legal shot keeps the
// shooter at the table; the 9 potted on a legal shot (combinations and the break included) wins. Fouls give ball in
// hand; the 9 potted on a foul is spotted again. No calling, no push-out, no three-foul rule.
//
// Solo 9-ball: the same, alone: always the lowest ball first; a foul gives ball in hand (the 9 on a foul is
// spotted); the 9 on a legal shot wins.
//
// Solo Play: one player, no turns and no scoring. Pot all 14 (any order, misses don't matter), then the 8 into the
// called pocket to win. Lost: the 8 on the break, the 8 before the 14, the 8 in another pocket, or a scratch while
// shooting the 8. Any other scratch just gives ball in hand.

#include "sim/Sim.h"

#include <cstdint>
#include <vector>

namespace pool
{
	enum class Group : std::uint8_t
	{
		kNone,
		kSolids,   // 1-7
		kStripes,  // 9-15
	};

	enum class Foul : std::uint8_t
	{
		kNone,
		kScratch,         // the cue ball went in
		kNoContact,       // the cue ball hit nothing
		kWrongBallFirst,  // hit the opponent's ball (or the 8 too early) first
		kNoRail,          // nothing potted and nothing touched a cushion after the contact
	};

	enum class Mode : std::uint8_t
	{
		kEightBall,  // two players, the rules above
		kSolo,       // Solo Play: one player pots all 14, then the 8 into the called pocket (see Rules::EndShot)
		kNineBall,   // two players, 9-ball
		kSoloNine,   // one player, 9-ball
	};

	inline bool IsNineBall(Mode a_mode) { return a_mode == Mode::kNineBall || a_mode == Mode::kSoloNine; }
	inline bool IsSolo(Mode a_mode) { return a_mode == Mode::kSolo || a_mode == Mode::kSoloNine; }

	enum class GameEnd : std::uint8_t
	{
		kNone,
		kWon,            // potted the 8 legally
		kEightEarly,     // lost: potted the 8 before clearing their group
		kEightOnFoul,    // lost: potted the 8 on a foul
		kEightWrongPocket, // lost: the 8 went in a pocket other than the called one
		kEightOnBreak,     // lost (Solo Play): the 8 went in on the break
		kScratchOnEight,   // lost (Solo Play): scratched while shooting the 8
	};

	struct ShotResult
	{
		int              shooter{ 0 };
		Foul             foul{ Foul::kNone };
		bool             breakShot{ false };
		bool             turnContinues{ false };
		bool             groupsSet{ false };     // groups were decided on this shot
		bool             respotEight{ false };   // the 8 went in on the break: put it back on the foot spot
		bool             respotNine{ false };    // 9-ball: the 9 went in on a foul: put it back on the foot spot
		bool             ballInHand{ false };    // the next shooter places the cue ball
		GameEnd          end{ GameEnd::kNone };
		int              winner{ -1 };
		std::vector<int> pocketed;               // object balls potted on this shot, in order
		int              ownPotted{ 0 };         // how many of them were the shooter's
	};

	inline Group GroupOfBall(int a_number)
	{
		return a_number >= 1 && a_number <= 7 ? Group::kSolids : a_number >= 9 && a_number <= 15 ? Group::kStripes : Group::kNone;
	}

	class Rules
	{
	public:
		static constexpr int kPlayers = 2;

		// A fresh game: a_breaker breaks with the cue ball behind the head string (the game places it).
		void NewGame(int a_breaker, Mode a_mode = Mode::kEightBall);
		Mode GameMode() const { return _mode; }

		int   Shooter() const { return _shooter; }
		Group GroupOf(int a_player) const { return _groups[a_player]; }
		bool  OpenTable() const { return _groups[0] == Group::kNone; }
		bool  IsBreak() const { return _break; }
		bool  BallInHand() const { return _ballInHand; }
		bool  Over() const { return _over; }
		int   Winner() const { return _winner; }

		// Balls of a_player's group still on the table (7 while the table is open).
		int  Remaining(int a_player, const Sim& a_sim) const;
		// a_player's group is cleared, so they're shooting at the 8. (Never in 9-ball.)
		bool OnEight(int a_player, const Sim& a_sim) const;
		// 9-ball: the ball to hit first (the lowest on the table, 1-9); 0 if none
		int  LowestBall(const Sim& a_sim) const;

		// a_player gives up: the game's over and the other one wins (Solo: just over).
		void Forfeit(int a_player)
		{
			_over = true;
			_winner = IsSolo(_mode) ? -1 : 1 - a_player;
			_inShot = false;
		}

		// The next shooter has placed the cue ball.
		void PlacedCueBall() { _ballInHand = false; }

		// Call before Sim::Strike. a_calledPocket is where the shooter says the 8 will go (-1 = not calling).
		void BeginShot(const Sim& a_sim, int a_calledPocket);
		// Feed every event the physics reports until the balls stop.
		void OnEvents(const std::vector<Event>& a_events);
		// Once everything is at rest: judges the shot and moves the game on.
		ShotResult EndShot(const Sim& a_sim);

	private:
		ShotResult EndSoloShot(const Sim& a_sim, ShotResult a_result);
		ShotResult EndNineShot(const Sim& a_sim, ShotResult a_result);

	public:

		bool InShot() const { return _inShot; }

	private:
		Mode                 _mode{ Mode::kEightBall };
		int                  _shooter{ 0 };
		std::array<Group, 2> _groups{ Group::kNone, Group::kNone };
		bool                 _break{ true };
		bool                 _ballInHand{ false };
		bool                 _over{ false };
		int                  _winner{ -1 };

		// the shot in progress
		bool             _inShot{ false };
		bool             _onEightAtStart{ false };
		int              _called{ -1 };
		int              _firstContact{ -1 };
		bool             _railAfterContact{ false };
		bool             _cueIn{ false };
		std::vector<int> _potted;
		int              _eightPocket{ -1 };
		int              _lowestAtStart{ 0 };
	};
}
