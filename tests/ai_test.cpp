// Computer opponent tests. Build and run on any machine:
//   g++ -std=c++20 -O2 -Isrc tests/ai_test.cpp src/sim/Ai.cpp src/sim/Rules.cpp src/sim/Sim.cpp -o ai_test && ./ai_test

#include "sim/Ai.h"

#include <cstdio>
#include <string>

using namespace pool;

namespace
{
	// the vanilla table, as fo4pool.ini sets it up
	const TableSpec kSpec{ .length = 224.0, .width = 112.0, .ballRadius = 3.51, .cornerMouth = 12.4, .sideMouth = 17.0,
		.rollDecel = 17.5, .ballRestitution = 0.75, .cushionRestitution = 0.75, .stopSpeed = 0.7, .pocketDepth = 0.7,
		.jawLength = 1.3, .cornerJawAngle = 15.0, .sideJawAngle = 0.0 };

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

	// plays a shot for real, as the game does
	ShotResult Play(Sim& a_sim, Rules& a_rules, const AiShot& a_shot)
	{
		if (a_shot.placeCue) {
			a_sim.Place(0, a_shot.cuePos);
			a_rules.PlacedCueBall();
		}
		const int shooter = a_rules.Shooter();
		a_rules.BeginShot(a_sim, a_rules.OnEight(shooter, a_sim) ? a_shot.pocket : -1);
		a_sim.Strike(a_shot.dir, a_shot.speed);
		for (int i = 0; i < 60 * 40 && !a_sim.AtRest(); ++i) {
			a_rules.OnEvents(a_sim.Step(1.0 / 60.0));
		}
		auto r = a_rules.EndShot(a_sim);
		if (r.respotEight) {
			a_sim.RespotOnFoot(8);
		}
		if (r.respotNine) {
			a_sim.RespotOnFoot(9);
		}
		if (a_sim.Balls()[0].state != BallState::kOnTable && !r.ballInHand) {
			a_sim.RespotCue();
		}
		return r;
	}

	void EasyPot()
	{
		std::printf("easy pot\n");
		Sim   sim(kSpec);
		Rules rules;
		sim.Rack(1);
		rules.NewGame(0);
		// get past the break by hand (a legal break, nothing potted: open table), then clear the table and line
		// the 3 up on a corner
		AiParams p;
		rules.BeginShot(sim, -1);
		rules.OnEvents({ { Event::Type::kBallBall, 0, 1, 100.0 }, { Event::Type::kCushion, 1, -1, 50.0 } });
		rules.EndShot(sim);
		sim.Clear();
		Vec2 mouth;
		Vec2 inward;
		sim.PocketMouth(3, mouth, inward);
		sim.Place(3, mouth - inward * 40.0);
		// at an angle: dead straight, a rolling cue ball follows the object ball in
		sim.Place(0, mouth - inward * 100.0 + Vec2{ -inward.y, inward.x } * 25.0);
		sim.Place(9, { -80.0, 30.0 });  // something left for the stripes so the game doesn't end
		sim.Place(8, { -80.0, -30.0 });
		const int  shooter = rules.Shooter();
		const auto shot = BestShot(sim, rules, shooter, p);
		Check(shot.target == 3, "picks the 3, lined up on the pocket");
		Check(shot.checked, "the pot plays out");
		const auto r = Play(sim, rules, shot);
		Check(r.foul == Foul::kNone, "no foul");
		Check(sim.Balls()[shot.target].state == BallState::kPocketed, "the target went in");
	}

	// whole games, the computer against itself
	void Games(double a_skill, int a_games, Mode a_mode = Mode::kEightBall, int a_style = 1)
	{
		static const char* kStyles[] = { "safe", "balanced", "aggressive" };
		std::printf("%s games at skill %.1f, %s\n", a_mode == Mode::kNineBall ? "9-ball" : "8-ball", a_skill, kStyles[a_style]);
		int totalShots = 0;
		int totalPots = 0;
		int fouls = 0;
		int finished = 0;
		int checkedGood = 0;
		int checkedShots = 0;
		for (int g = 0; g < a_games; ++g) {
			Sim   sim(kSpec);
			Rules rules;
			if (a_mode == Mode::kNineBall) {
				sim.RackNine(100 + g);
			} else {
				sim.Rack(100 + g);
			}
			rules.NewGame(g % 2, a_mode);
			std::mt19937 rng(7 + g);
			AiParams     p;
			p.skill = a_skill;
			p.style = a_style;
			int shots = 0;
			for (; shots < 300 && !rules.Over(); ++shots) {
				const int  shooter = rules.Shooter();
				const auto shot = ChooseShot(sim, rules, shooter, p, rng);
				const auto r = Play(sim, rules, shot);
				totalPots += r.ownPotted;
				fouls += r.foul != Foul::kNone;
				if (shot.checked && !r.breakShot) {
					++checkedShots;
					checkedGood += r.turnContinues || r.end == GameEnd::kWon;
				}
			}
			finished += rules.Over();
			totalShots += shots;
		}
		std::printf("    %d/%d finished, %.1f shots a game, %.1f own balls potted a game, %.1f fouls a game, %d/%d checked shots kept the turn\n",
			finished, a_games, double(totalShots) / a_games, double(totalPots) / a_games, double(fouls) / a_games, checkedGood, checkedShots);
		Check(finished == a_games, "every game finishes");
		if (a_skill >= 0.99) {
			Check(checkedShots == 0 || checkedGood * 10 >= checkedShots * 7, "a near-perfect player makes most of its checked shots");
		}
	}

	// with a reach limit (the opponent's stance), every pot it goes for can be played from outside the table
	void Reach()
	{
		std::printf("reach\n");
		int shots = 0;
		int pots = 0;
		int potsOver = 0;
		int safeties = 0;
		int safetiesOver = 0;
		for (int g = 0; g < 10; ++g) {
			Sim   sim(kSpec);
			Rules rules;
			sim.Rack(300 + g);
			rules.NewGame(g % 2);
			std::mt19937 rng(11 + g);
			AiParams     p;
			p.skill = 0.8;
			p.stanceBack = 77.0;  // the opponent's stance (Opponent.cpp kStanceBack)
			p.railClear = 20.0;   // and the cue clear of the rail behind the cue ball
			for (int n = 0; n < 300 && !rules.Over(); ++n) {
				const auto shot = ChooseShot(sim, rules, rules.Shooter(), p, rng);
				if (!rules.IsBreak()) {
					AiParams exact = p;  // judged on the shot before the aim error
					const Vec2 cue = shot.placeCue ? shot.cuePos : sim.Balls()[0].pos;
					const bool over = Overreach(sim, cue, shot.dir, exact) > p.reachSlack + 3.0;  // the aim error turns it a little
					if (shot.target >= 0) {
						++pots;
						potsOver += over;
					} else {
						++safeties;
						safetiesOver += over;
					}
				}
				Play(sim, rules, shot);
				++shots;
			}
			Check(rules.Over(), "a reach-limited game still finishes");
		}
		std::printf("    %d shots: %d pots (%d out of reach), %d safeties (%d out of reach)\n", shots, pots, potsOver, safeties, safetiesOver);
		Check(potsOver == 0, "no pot needs them to lean across the table or cue through the rail");
	}
}

int main()
{
	EasyPot();
	Reach();
	Games(1.0, 20);
	Games(0.6, 20);
	Games(0.2, 10);
	Games(0.6, 20, Mode::kNineBall);
	Games(0.2, 10, Mode::kNineBall);
	Games(0.6, 20, Mode::kEightBall, 0);  // play styles: every game still finishes
	Games(0.6, 20, Mode::kEightBall, 2);
	std::printf("%d checks, %d failed\n", g_checks, g_failed);
	return g_failed ? 1 : 0;
}
