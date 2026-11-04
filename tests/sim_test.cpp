// Physics tests. Build and run on any machine:
//   g++ -std=c++20 -O2 -Isrc tests/sim_test.cpp src/sim/Sim.cpp -o sim_test && ./sim_test

#include "sim/Sim.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

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

	std::string Fmt(const char* a_fmt, double a, double b = 0.0, double c = 0.0)
	{
		char buf[256];
		std::snprintf(buf, sizeof(buf), a_fmt, a, b, c);
		return buf;
	}

	constexpr double kFrame = 1.0 / 60.0;

	// Runs at 60 fps until everything stops or a_maxSeconds pass. Returns all events; a_onFrame sees each frame.
	std::vector<Event> RunUntilRest(Sim& a_sim, double a_maxSeconds, const std::function<void()>& a_onFrame = {})
	{
		std::vector<Event> all;
		for (double t = 0.0; t < a_maxSeconds && !a_sim.AtRest(); t += kFrame) {
			auto ev = a_sim.Step(kFrame);
			all.insert(all.end(), ev.begin(), ev.end());
			if (a_onFrame) {
				a_onFrame();
			}
		}
		return all;
	}

	int Count(const std::vector<Event>& a_events, Event::Type a_type)
	{
		int n = 0;
		for (const auto& e : a_events) {
			n += e.type == a_type;
		}
		return n;
	}

	bool Finite(const Sim& a_sim)
	{
		for (const auto& b : a_sim.Balls()) {
			if (!std::isfinite(b.pos.x) || !std::isfinite(b.pos.y) || !std::isfinite(b.vel.x) || !std::isfinite(b.vel.y) ||
				!std::isfinite(b.rot.w)) {
				return false;
			}
		}
		return true;
	}

	void HeadOnCollision()
	{
		std::puts("head-on: cue stops, object ball takes the speed");
		Sim sim;
		sim.Place(0, { -20.0, 0.0 });
		sim.Place(1, { 0.0, 0.0 });
		sim.Strike({ 1.0, 0.0 }, 300.0);

		bool hit = false;
		for (int f = 0; f < 120 && !hit; ++f) {
			for (const auto& e : sim.Step(kFrame)) {
				hit |= e.type == Event::Type::kBallBall;
			}
		}
		Check(hit, "the balls collided");
		const auto& cue = sim.Balls()[0];
		const auto& obj = sim.Balls()[1];
		Check(std::abs(cue.vel.x) < 20.0, Fmt("cue ball nearly stopped (%.1f)", cue.vel.x));
		Check(obj.vel.x > 250.0, Fmt("object ball took most of the speed (%.1f)", obj.vel.x));
		Check(std::abs(obj.vel.y) < 1.0, Fmt("object ball went straight (vy %.2f)", obj.vel.y));
	}

	void CutShot()
	{
		std::puts("cut shot: balls separate at roughly right angles");
		Sim sim;
		sim.Place(0, { -20.0, 0.0 });
		sim.Place(1, { 0.0, 2.0 });  // half-ball hit
		sim.Strike({ 1.0, 0.0 }, 300.0);
		bool hit = false;
		for (int f = 0; f < 120 && !hit; ++f) {
			for (const auto& e : sim.Step(kFrame)) {
				hit |= e.type == Event::Type::kBallBall;
			}
		}
		Check(hit, "the balls collided");
		const auto&  a = sim.Balls()[0].vel;
		const auto&  b = sim.Balls()[1].vel;
		const double cosAngle = a.Dot(b) / (a.Length() * b.Length());
		Check(std::abs(cosAngle) < 0.1, Fmt("about 90 degrees apart (cos %.3f)", cosAngle));
		Check(b.y > 0.0, "object ball goes away from the contact side");
	}

	void CushionBounce()
	{
		std::puts("cushion: bounces back with lost speed");
		Sim sim;
		sim.Place(0, { 30.0, 20.0 });
		sim.Strike({ 0.0, 1.0 }, 200.0);
		double before = 0.0;
		bool   bounced = false;
		for (int f = 0; f < 120 && !bounced; ++f) {
			before = sim.Balls()[0].vel.y;
			for (const auto& e : sim.Step(kFrame)) {
				bounced |= e.type == Event::Type::kCushion;
			}
		}
		Check(bounced, "hit the cushion");
		const auto& b = sim.Balls()[0];
		Check(b.vel.y < 0.0, "now moving away from it");
		const double ratio = -b.vel.y / before;
		Check(ratio > 0.6 && ratio < 0.8, Fmt("lost about a quarter of its speed (kept %.2f)", ratio));
		Check(b.pos.y <= sim.Spec().width * 0.5 - sim.Spec().ballRadius + 1e-6, "stayed on the table");
	}

	void CornerPocket()
	{
		std::puts("corner pocket: a ball sent at the corner goes in");
		Sim sim;
		sim.Place(1, { 40.0, 10.0 });
		sim.Strike({ 0.0, 0.0 }, 0.0);
		sim.BallAt(1).vel = Vec2{ 30.0, 25.0 } * (250.0 / Vec2{ 30.0, 25.0 }.Length());
		auto ev = RunUntilRest(sim, 10.0);
		Check(sim.Balls()[1].state == BallState::kPocketed, "pocketed");
		Check(sim.Balls()[1].pocket == 3, Fmt("in the foot-right corner (got %.0f)", sim.Balls()[1].pocket));
		Check(Count(ev, Event::Type::kPocket) == 1, "one pocket event");
	}

	void SidePocket()
	{
		std::puts("side pocket: a ball sent straight at it goes in");
		Sim sim;
		sim.Place(1, { 0.0, 0.0 });
		sim.BallAt(1).vel = { 0.0, -150.0 };
		RunUntilRest(sim, 10.0);
		Check(sim.Balls()[1].state == BallState::kPocketed, "pocketed");
		Check(sim.Balls()[1].pocket == 4, Fmt("in the left side pocket (got %.0f)", sim.Balls()[1].pocket));
	}

	void SidePocketTooSteep()
	{
		std::puts("side pocket: a ball rolling along the rail past it stays on the table");
		Sim sim;
		const double y = sim.Spec().width * 0.5 - sim.Spec().ballRadius - 0.5;
		sim.Place(1, { -40.0, y });
		sim.BallAt(1).vel = { 35.0, 0.0 };  // rolls about 60 units: past the side pocket, stops before the corner
		RunUntilRest(sim, 10.0);
		Check(sim.Balls()[1].state == BallState::kOnTable, "still on the table");
		Check(sim.Balls()[1].pos.x > 10.0, Fmt("rolled past the pocket (x %.1f)", sim.Balls()[1].pos.x));
	}

	void Rolling()
	{
		std::puts("rolling: one full turn over one circumference");
		TableSpec spec;
		spec.rollDecel = 0.0;
		Sim sim(spec);
		sim.Place(0, { -40.0, 0.0 });
		sim.Strike({ 1.0, 0.0 }, 2.0 * 3.14159265358979 * spec.ballRadius);  // one circumference per second
		for (int f = 0; f < 60; ++f) {
			sim.Step(kFrame);
		}
		const auto& q = sim.Balls()[0].rot;
		Check(std::abs(std::abs(q.w) - 1.0) < 1e-3, Fmt("back to the starting orientation (w %.4f)", q.w));
		Check(std::abs(sim.Balls()[0].pos.x - (-40.0 + 2.0 * 3.14159265358979 * spec.ballRadius)) < 1e-6, "moved one circumference");

		// half a turn: the top of the ball is now at the bottom, turned about the y axis for motion along +x
		Sim half(spec);
		half.Place(0, { -40.0, 0.0 });
		half.Strike({ 1.0, 0.0 }, 3.14159265358979 * spec.ballRadius);
		for (int f = 0; f < 60; ++f) {
			half.Step(kFrame);
		}
		double m[3][3];
		half.Balls()[0].rot.ToMatrix(m);
		Check(m[2][2] < -0.999, Fmt("top now faces down (zz %.4f)", m[2][2]));
		Check(std::abs(half.Balls()[0].rot.y) > 0.999, "turned about the y axis");
	}

	void StopsByFriction()
	{
		std::puts("friction: a gentle shot stops where expected");
		Sim sim;
		sim.Place(0, { -40.0, 0.0 });
		sim.Strike({ 1.0, 0.0 }, 30.0);
		RunUntilRest(sim, 10.0);
		const double expected = 30.0 * 30.0 / (2.0 * sim.Spec().rollDecel);  // v^2 / 2a
		const double moved = sim.Balls()[0].pos.x + 40.0;
		Check(sim.AtRest(), "came to rest");
		Check(std::abs(moved - expected) < 1.0, Fmt("travelled %.2f, expected about %.2f", moved, expected));
	}

	void Break(std::uint32_t a_seed, double a_speed)
	{
		std::printf("break (seed %u, speed %.0f): everything stays sane\n", a_seed, a_speed);
		Sim sim;
		sim.Rack(a_seed);

		// the rack itself: 16 balls, no overlaps
		int onTable = 0;
		for (const auto& b : sim.Balls()) {
			onTable += b.state == BallState::kOnTable;
		}
		Check(onTable == 16, "16 balls on the table");

		sim.Strike(sim.FootSpot() - sim.HeadSpot(), a_speed);

		const auto& spec = sim.Spec();
		const double hl = spec.length * 0.5 - spec.ballRadius + 1e-6;
		const double hw = spec.width * 0.5 - spec.ballRadius + 1e-6;
		double lastEnergy = sim.KineticEnergy();
		bool   energyRose = false;
		bool   outOfBounds = false;
		double worstOverlap = 0.0;

		auto ev = RunUntilRest(sim, 60.0, [&] {
			const double e = sim.KineticEnergy();
			energyRose |= e > lastEnergy * 1.0001 + 1e-9;
			lastEnergy = e;
			const auto& balls = sim.Balls();
			for (int i = 0; i < Sim::kBalls; ++i) {
				const auto& a = balls[i];
				if (a.state != BallState::kOnTable) {
					continue;
				}
				// a ball can be past the cushion line only inside a pocket opening; just check it isn't far off
				outOfBounds |= std::abs(a.pos.x) > hl + 2.0 * spec.ballRadius || std::abs(a.pos.y) > hw + 2.0 * spec.ballRadius;
				for (int j = i + 1; j < Sim::kBalls; ++j) {
					const auto& b = balls[j];
					if (b.state == BallState::kOnTable) {
						worstOverlap = std::max(worstOverlap, 2.0 * spec.ballRadius - (a.pos - b.pos).Length());
					}
				}
			}
		});

		Check(Finite(sim), "no NaNs");
		Check(sim.AtRest(), "everything came to rest within a minute");
		Check(!energyRose, "energy never increased");
		Check(!outOfBounds, "no ball left the table except into a pocket");
		Check(worstOverlap < 0.1, Fmt("balls never sank into each other (worst %.3f)", worstOverlap));
		Check(Count(ev, Event::Type::kBallBall) > 15, "the rack was hit and spread");

		int moved = 0;
		int pocketed = 0;
		for (const auto& b : sim.Balls()) {
			pocketed += b.state == BallState::kPocketed;
			moved += b.state == BallState::kPocketed || std::abs(b.pos.x - sim.FootSpot().x) > 15.0 || std::abs(b.pos.y) > 15.0;
		}
		std::printf("    %d balls pocketed, %d moved well away from the rack, %d ball-ball contacts\n", pocketed, moved,
			Count(ev, Event::Type::kBallBall));
		Check(moved >= 8, "the break spread the balls");
	}

	void Deterministic()
	{
		std::puts("determinism: same seed and shot, same result");
		auto run = [] {
			Sim sim;
			sim.Rack(1234);
			sim.Strike(sim.FootSpot() - sim.HeadSpot(), 700.0);
			RunUntilRest(sim, 60.0);
			return sim.Balls();
		};
		const auto a = run();
		const auto b = run();
		bool same = true;
		for (int i = 0; i < Sim::kBalls; ++i) {
			same &= a[i].pos.x == b[i].pos.x && a[i].pos.y == b[i].pos.y && a[i].state == b[i].state;
		}
		Check(same, "identical");
	}

	void Respot()
	{
		std::puts("respot: cue ball goes back to the head spot, or next to it if taken");
		Sim sim;
		sim.Place(5, sim.HeadSpot());
		sim.BallAt(0).state = BallState::kPocketed;
		sim.RespotCue();
		const auto& cue = sim.Balls()[0];
		Check(cue.state == BallState::kOnTable, "back on the table");
		Check((cue.pos - sim.HeadSpot()).Length() > 3.9, "not on top of the ball sitting on the spot");
		Check(std::abs(cue.pos.y) < 1e-9, "on the long axis");
	}
}

int main()
{
	HeadOnCollision();
	CutShot();
	CushionBounce();
	CornerPocket();
	SidePocket();
	SidePocketTooSteep();
	Rolling();
	StopsByFriction();
	for (std::uint32_t seed : { 1u, 42u, 777u, 31337u }) {
		Break(seed, 800.0);
	}
	Break(5, 300.0);
	Break(6, 1400.0);
	Deterministic();
	Respot();

	std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
	return g_failed == 0 ? 0 : 1;
}
