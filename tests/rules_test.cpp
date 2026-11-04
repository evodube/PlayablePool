// 8-ball rules tests. Build and run on any machine:
//   g++ -std=c++20 -O2 -Isrc tests/rules_test.cpp src/sim/Rules.cpp src/sim/Sim.cpp -o rules_test && ./rules_test

#include "sim/Rules.h"

#include <cstdio>
#include <string>

using namespace pool;

namespace
{
	int g_failed = 0;
	int g_checks = 0;

	void Check(bool a_ok, const std::string& a_what)
	{
		++g_checks;
		if (!a_ok) {
			++g_failed;
			std::printf("    FAIL: %s\n", a_what.c_str());
		}
	}

	// A table with every ball on it, and helpers that make up the events a shot would produce.
	struct Table
	{
		Sim   sim;
		Rules rules;

		Table()
		{
			sim.Rack(1);
			rules.NewGame(0);
		}
		void Pot(int a_ball, int a_pocket = 3)
		{
			sim.BallAt(a_ball).state = BallState::kPocketed;
			sim.BallAt(a_ball).pocket = a_pocket;
		}
		// the shot: what the cue ball hits first, whether anything then touches a cushion, what goes in
		ShotResult Shoot(int a_first, bool a_rail, std::vector<std::pair<int, int>> a_pots, int a_called = -1)
		{
			rules.BeginShot(sim, a_called);
			std::vector<Event> ev;
			if (a_first >= 0) {
				ev.push_back({ Event::Type::kBallBall, 0, a_first, 100.0 });
			}
			if (a_rail) {
				ev.push_back({ Event::Type::kCushion, a_first >= 0 ? a_first : 0, -1, 50.0 });
			}
			for (auto [ball, pocket] : a_pots) {
				ev.push_back({ Event::Type::kPocket, ball, pocket, 40.0 });
				Pot(ball, pocket);
			}
			rules.OnEvents(ev);
			return rules.EndShot(sim);
		}
		// gets past the break with nothing potted, so player 1 is up on an open table
		void Broken() { Shoot(1, true, {}); }
		void Groups(Group a_shooterGroup)
		{
			Broken();  // now player 1
			Shoot(a_shooterGroup == Group::kSolids ? 2 : 10, true, { { a_shooterGroup == Group::kSolids ? 2 : 10, 3 } });
		}
	};

	void Break()
	{
		std::puts("break");
		{
			Table t;
			auto  r = t.Shoot(1, true, { { 3, 0 }, { 12, 2 } });
			Check(r.breakShot && r.foul == Foul::kNone, "a clean break");
			Check(r.turnContinues && t.rules.Shooter() == 0, "potting on the break keeps the breaker at the table");
			Check(t.rules.OpenTable() && !r.groupsSet, "the table stays open after the break");
		}
		{
			Table t;
			auto  r = t.Shoot(1, true, {});
			Check(!r.turnContinues && t.rules.Shooter() == 1 && !r.ballInHand, "dry break: the other player is up, no ball in hand");
		}
		{
			Table t;
			t.rules.BeginShot(t.sim, -1);
			t.rules.OnEvents({ { Event::Type::kBallBall, 0, 1, 500.0 }, { Event::Type::kPocket, 0, 4, 100.0 } });
			auto r = t.rules.EndShot(t.sim);
			Check(r.foul == Foul::kScratch && r.ballInHand && t.rules.Shooter() == 1, "scratch on the break: ball in hand for the other player");
		}
		{
			Table t;
			auto  r = t.Shoot(1, true, { { 8, 5 } });
			Check(r.respotEight && r.end == GameEnd::kNone && !t.rules.Over(), "the 8 on the break is spotted, not a loss");
		}
	}

	void OpenTable()
	{
		std::puts("open table");
		{
			Table t;
			t.Broken();
			auto r = t.Shoot(8, true, {});
			Check(r.foul == Foul::kWrongBallFirst && r.ballInHand, "hitting the 8 first on an open table is a foul");
		}
		{
			Table t;
			t.Broken();
			auto r = t.Shoot(4, true, { { 11, 2 } });
			Check(r.groupsSet, "the first legal pot decides the groups");
			Check(t.rules.GroupOf(1) == Group::kStripes && t.rules.GroupOf(0) == Group::kSolids, "potting a stripe makes the shooter stripes");
			Check(r.turnContinues && t.rules.Shooter() == 1, "and they carry on");
		}
		{
			Table t;
			t.Broken();
			t.rules.BeginShot(t.sim, -1);
			t.rules.OnEvents({ { Event::Type::kBallBall, 0, 4, 50.0 }, { Event::Type::kPocket, 4, 2, 30.0 }, { Event::Type::kPocket, 0, 2, 30.0 } });
			t.Pot(4, 2);
			auto r = t.rules.EndShot(t.sim);
			Check(!r.groupsSet && t.rules.OpenTable(), "a pot on a foul doesn't decide the groups");
		}
	}

	void Fouls()
	{
		std::puts("fouls and turns");
		{
			Table t;
			t.Groups(Group::kSolids);  // player 1 has solids, still shooting
			auto r = t.Shoot(12, true, {});
			Check(r.foul == Foul::kWrongBallFirst && r.ballInHand && t.rules.Shooter() == 0, "hitting the opponent's ball first: foul, ball in hand");
		}
		{
			Table t;
			t.Groups(Group::kSolids);
			auto r = t.Shoot(3, false, {});
			Check(r.foul == Foul::kNoRail, "nothing potted and no cushion after contact: foul");
		}
		{
			Table t;
			t.Groups(Group::kSolids);
			auto r = t.Shoot(-1, false, {});
			Check(r.foul == Foul::kNoContact, "missing everything: foul");
		}
		{
			Table t;
			t.Groups(Group::kSolids);
			auto r = t.Shoot(3, true, {});
			Check(r.foul == Foul::kNone && !r.turnContinues && !r.ballInHand && t.rules.Shooter() == 0, "a legal miss passes the turn, no ball in hand");
		}
		{
			Table t;
			t.Groups(Group::kSolids);
			auto r = t.Shoot(3, true, { { 13, 1 } });
			Check(r.foul == Foul::kNone && !r.turnContinues, "potting only the opponent's ball passes the turn");
			Check(t.sim.Balls()[13].state == BallState::kPocketed, "and it stays down");
		}
		{
			Table t;
			t.Groups(Group::kSolids);
			auto r = t.Shoot(3, false, { { 3, 0 } });
			Check(r.foul == Foul::kNone && r.turnContinues, "a pot counts even without a cushion");
		}
		{
			Table t;
			t.Groups(Group::kSolids);
			auto r = t.Shoot(5, true, { { 5, 0 }, { 0, 1 } });
			Check(r.foul == Foul::kScratch && !r.turnContinues && r.ballInHand, "potting your ball and the cue ball: foul");
		}
	}

	void TheEight()
	{
		std::puts("the 8");
		{
			Table t;
			t.Groups(Group::kSolids);
			auto r = t.Shoot(3, true, { { 8, 2 } });
			Check(r.end == GameEnd::kEightEarly && t.rules.Over() && r.winner == 0, "the 8 before clearing your group loses");
		}
		auto cleared = [](Table& t) {
			t.Groups(Group::kSolids);  // player 1 solids, 2 already down
			for (int n : { 1, 3, 4, 5, 6, 7 }) {
				t.Pot(n);
			}
		};
		{
			Table t;
			cleared(t);
			Check(t.rules.OnEight(1, t.sim) && t.rules.Remaining(1, t.sim) == 0, "group cleared: on the 8");
			auto r = t.Shoot(8, true, { { 8, 5 } }, 5);
			Check(r.end == GameEnd::kWon && r.winner == 1, "the 8 into the called pocket wins");
		}
		{
			Table t;
			cleared(t);
			auto r = t.Shoot(8, true, { { 8, 4 } }, 5);
			Check(r.end == GameEnd::kEightWrongPocket && r.winner == 0, "the 8 into another pocket loses");
		}
		{
			Table t;
			cleared(t);
			auto r = t.Shoot(8, true, { { 8, 5 }, { 0, 2 } }, 5);
			Check(r.end == GameEnd::kEightOnFoul && r.winner == 0, "the 8 with a scratch loses");
		}
		{
			Table t;
			cleared(t);
			auto r = t.Shoot(8, true, { { 8, 1 } });
			Check(r.end == GameEnd::kWon, "without calling, any pocket wins");
		}
		{
			Table t;
			cleared(t);
			auto r = t.Shoot(12, true, {});
			Check(r.foul == Foul::kWrongBallFirst, "on the 8, hitting anything else first is a foul");
		}
		{
			Table t;
			t.Groups(Group::kSolids);
			for (int n : { 1, 3, 4, 5, 6 }) {
				t.Pot(n);
			}
			auto r = t.Shoot(7, true, { { 7, 0 }, { 8, 3 } });
			Check(r.end == GameEnd::kEightEarly, "your last ball and the 8 on the same shot loses");
		}
	}

	// whole games with random shots through the real physics: they always end, and the bookkeeping holds up
	void RandomGames()
	{
		std::puts("random games through the physics");
		TableSpec spec{ .length = 224.0, .width = 112.0, .ballRadius = 3.51, .cornerMouth = 12.4, .sideMouth = 17.0, .rollDecel = 17.5,
			.ballRestitution = 0.75, .stopSpeed = 0.7 };
		int finished = 0;
		int longest = 0;
		std::uint32_t rng = 12345;
		auto next = [&] {
			rng ^= rng << 13;
			rng ^= rng >> 17;
			rng ^= rng << 5;
			return rng;
		};
		for (int game = 0; game < 40; ++game) {
			Sim   sim(spec);
			Rules rules;
			sim.Rack(next());
			rules.NewGame(game % 2);
			int shots = 0;
			while (!rules.Over() && shots < 600) {
				if (sim.Balls()[0].state != BallState::kOnTable) {
					sim.RespotCue();
				}
				if (rules.BallInHand()) {
					rules.PlacedCueBall();
				}
				// aim at a random ball of the shooter's (or any), a bit off, at a random speed
				int target = 1 + static_cast<int>(next() % 15);
				for (int tries = 0; tries < 30 && sim.Balls()[target].state != BallState::kOnTable; ++tries) {
					target = 1 + static_cast<int>(next() % 15);
				}
				const Vec2 to = sim.Balls()[target].pos - sim.Balls()[0].pos;
				const double wobble = (static_cast<double>(next() % 1000) / 1000.0 - 0.5) * 0.2;
				const Vec2 dir{ to.x - to.y * wobble, to.y + to.x * wobble };
				rules.BeginShot(sim, static_cast<int>(next() % 6));
				sim.Strike(dir, 80.0 + static_cast<double>(next() % 300));
				for (double t = 0.0; t < 60.0 && !sim.AtRest(); t += 1.0 / 60.0) {
					rules.OnEvents(sim.Step(1.0 / 60.0));
				}
				const auto r = rules.EndShot(sim);
				if (r.respotEight) {
					sim.RespotOnFoot(8);
				}
				++shots;
			}
			finished += rules.Over();
			longest = std::max(longest, shots);
			if (rules.Over()) {
				Check(rules.Winner() == 0 || rules.Winner() == 1, "a winner");
			}
		}
		std::printf("    %d of 40 random games finished (longest %d shots)\n", finished, longest);
		Check(finished >= 38, "random games finish");
	}
}

int main()
{
	Break();
	OpenTable();
	Fouls();
	TheEight();
	RandomGames();
	std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
	return g_failed == 0 ? 0 : 1;
}
