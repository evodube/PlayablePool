#include "sim/Ai.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace pool
{
	namespace
	{
		constexpr double kPi = 3.14159265358979323846;
		constexpr double kMinCutCos = 0.26;  // no cuts thinner than about 75 degrees

		Vec2 Unit(Vec2 a_v)
		{
			const double l = a_v.Length();
			return l > 1e-9 ? a_v * (1.0 / l) : Vec2{ 1.0, 0.0 };
		}

		Vec2 Rotate(Vec2 a_v, double a_radians)
		{
			const double c = std::cos(a_radians);
			const double s = std::sin(a_radians);
			return { a_v.x * c - a_v.y * s, a_v.x * s + a_v.y * c };
		}

		// distance from point p to the segment a..b
		double SegmentDistance(Vec2 a_p, Vec2 a_a, Vec2 a_b)
		{
			const Vec2   ab = a_b - a_a;
			const double len2 = ab.LengthSq();
			const double t = len2 > 1e-12 ? std::clamp((a_p - a_a).Dot(ab) / len2, 0.0, 1.0) : 0.0;
			return (a_p - (a_a + ab * t)).Length();
		}

		bool OnTable(const Sim& a_sim, int a_index)
		{
			return a_sim.Balls()[a_index].state == BallState::kOnTable;
		}

		// the balls a_player may (and should) hit first
		std::vector<int> Targets(const Sim& a_sim, const Rules& a_rules, int a_player)
		{
			std::vector<int> out;
			if (a_rules.OnEight(a_player, a_sim)) {
				if (OnTable(a_sim, 8)) {
					out.push_back(8);
				}
				return out;
			}
			const Group g = a_rules.GroupOf(a_player);
			for (int i = 1; i < Sim::kBalls; ++i) {
				if (i == 8 || !OnTable(a_sim, i)) {
					continue;
				}
				if (g == Group::kNone || GroupOfBall(i) == g) {
					out.push_back(i);
				}
			}
			return out;
		}

		// speed to give the cue ball so the object ball still rolls a_objectDistance (+ a margin) after a contact
		// a_cueDistance away, cut at a_cutCos
		double SpeedFor(const Sim& a_sim, double a_cueDistance, double a_objectDistance, double a_cutCos, const AiParams& a_p)
		{
			const double decel = std::max(1.0, a_sim.Spec().rollDecel);
			const double objectSpeed = std::sqrt(2.0 * decel * (a_objectDistance + 25.0)) * 1.35 + 15.0;
			const double atContact = objectSpeed / std::max(0.25, a_cutCos);
			const double speed = std::sqrt(atContact * atContact + 2.0 * decel * a_cueDistance);
			return std::clamp(speed, a_p.minSpeed, a_p.maxSpeed);
		}

		struct Played
		{
			bool good{ false };  // potted something of ours (or won), no foul, game not lost
			bool won{ false };
		};

		// plays a shot out on copies of the table and the rules
		Played PlayOut(const Sim& a_sim, const Rules& a_rules, int a_player, const AiShot& a_shot)
		{
			Sim   sim = a_sim;
			Rules rules = a_rules;
			if (a_shot.placeCue) {
				sim.Place(0, a_shot.cuePos);
				rules.PlacedCueBall();
			}
			const int called = a_rules.OnEight(a_player, a_sim) ? a_shot.pocket : -1;
			rules.BeginShot(sim, called);
			sim.Strike(a_shot.dir, a_shot.speed);
			constexpr double kDt = 1.0 / 60.0;
			for (int i = 0; i < 60 * 40 && !sim.AtRest(); ++i) {
				rules.OnEvents(sim.Step(kDt));
			}
			const ShotResult r = rules.EndShot(sim);
			Played out;
			out.won = r.end == GameEnd::kWon && r.winner == a_player;
			out.good = out.won || (r.end == GameEnd::kNone && r.foul == Foul::kNone && r.turnContinues);
			return out;
		}

		// every pot that's geometrically on from the cue ball where it is, best first (no play-out yet)
		std::vector<AiShot> Candidates(const Sim& a_sim, const Rules& a_rules, int a_player, const AiParams& a_p)
		{
			std::vector<AiShot> out;
			const auto&         balls = a_sim.Balls();
			if (!OnTable(a_sim, 0)) {
				return out;
			}
			const double r = a_sim.Spec().ballRadius;
			const Vec2   cue = balls[0].pos;

			for (const int t : Targets(a_sim, a_rules, a_player)) {
				const Vec2 obj = balls[t].pos;
				for (int p = 0; p < Sim::kPockets; ++p) {
					Vec2 mouth;
					Vec2 inward;
					a_sim.PocketMouth(p, mouth, inward);
					// a little inside the mouth, so balls aimed at it drop rather than graze a jaw
					const Vec2 aimPoint = mouth + inward * (r * 0.8);
					const Vec2 toPocket = Unit(aimPoint - obj);
					if (toPocket.Dot(inward) < 0.35) {
						continue;  // coming at the mouth from the side: it hits the jaw
					}
					const Vec2   ghost = obj - toPocket * (2.0 * r);
					const Vec2   aim = Unit(ghost - cue);
					const double cutCos = aim.Dot(toPocket);
					if (cutCos < kMinCutCos) {
						continue;
					}
					const auto pred = a_sim.Predict(aim);
					if (!pred.hitBall || pred.ball != t) {
						continue;  // something's in the way, or the contact isn't where we think
					}
					// the object ball's way to the pocket
					bool blocked = false;
					for (int i = 1; i < Sim::kBalls && !blocked; ++i) {
						if (i != t && OnTable(a_sim, i) && SegmentDistance(balls[i].pos, obj, aimPoint) < 2.0 * r) {
							blocked = true;
						}
					}
					if (blocked) {
						continue;
					}
					const double d1 = (ghost - cue).Length();
					const double d2 = (aimPoint - obj).Length();
					AiShot s;
					s.dir = aim;
					s.target = t;
					s.pocket = p;
					s.speed = SpeedFor(a_sim, d1, d2, cutCos, a_p);
					s.score = cutCos * cutCos / (1.0 + (d1 + 2.0 * d2) / 250.0);
					out.push_back(s);
				}
			}
			std::sort(out.begin(), out.end(), [](const AiShot& a, const AiShot& b) { return a.score > b.score; });
			return out;
		}

		// no pot on: hit a legal ball full on, firmly enough to reach a cushion
		AiShot Safety(const Sim& a_sim, const Rules& a_rules, int a_player, const AiParams& a_p)
		{
			AiShot      best;
			double      bestDist = 1e18;
			const auto& balls = a_sim.Balls();
			const Vec2  cue = balls[0].pos;
			for (const int t : Targets(a_sim, a_rules, a_player)) {
				// straight at it, then a few slightly off-centre hits in case something is in the way
				for (const double off : { 0.0, 0.03, -0.03, 0.07, -0.07, 0.12, -0.12 }) {
					const Vec2 dir = Rotate(Unit(balls[t].pos - cue), off);
					const auto pred = a_sim.Predict(dir);
					if (pred.hitBall && pred.ball == t && pred.distance < bestDist) {
						bestDist = pred.distance;
						best.dir = dir;
						best.target = -1;
						best.speed = std::clamp(SpeedFor(a_sim, pred.distance, 120.0, 0.8, a_p), a_p.minSpeed, a_p.maxSpeed);
						break;
					}
				}
			}
			if (bestDist > 1e17) {  // nothing hittable directly: at least send it somewhere
				best.dir = Unit(a_sim.FootSpot() - cue);
				best.speed = (a_p.minSpeed + a_p.maxSpeed) * 0.5;
			}
			return best;
		}

		AiShot Break(const Sim& a_sim, const AiParams& a_p)
		{
			AiShot s;
			s.dir = Unit(a_sim.FootSpot() - a_sim.Balls()[0].pos);
			s.speed = a_p.maxSpeed * 0.95;
			s.checked = true;
			return s;
		}

		// the best pot from where the cue ball is: the top candidates played out, else the best on paper
		AiShot BestFromHere(const Sim& a_sim, const Rules& a_rules, int a_player, const AiParams& a_p, bool a_validate)
		{
			auto cands = Candidates(a_sim, a_rules, a_player, a_p);
			if (cands.empty()) {
				return Safety(a_sim, a_rules, a_player, a_p);
			}
			if (a_validate) {
				const int n = std::min<int>(static_cast<int>(cands.size()), std::max(1, a_p.validate));
				for (int i = 0; i < n; ++i) {
					// a softer and a firmer version too: the speed guess is only a guess
					for (const double k : { 1.0, 1.25, 0.8, 0.65 }) {
						AiShot s = cands[i];
						s.speed = std::clamp(s.speed * k, a_p.minSpeed, a_p.maxSpeed);
						const auto played = PlayOut(a_sim, a_rules, a_player, s);
						if (played.good) {
							s.checked = true;
							return s;
						}
					}
				}
			}
			return cands.front();
		}
	}

	AiShot BestShot(const Sim& a_sim, const Rules& a_rules, int a_player, const AiParams& a_params)
	{
		if (a_rules.IsBreak()) {
			return Break(a_sim, a_params);
		}
		if (!a_rules.BallInHand()) {
			return BestFromHere(a_sim, a_rules, a_player, a_params, true);
		}

		// ball in hand: try the cue ball on a grid over the table, keep the spots with the best pots on paper,
		// then play those out
		const auto&  spec = a_sim.Spec();
		const double r = spec.ballRadius;
		struct Spot
		{
			Vec2   pos;
			double score;
		};
		std::vector<Spot> spots;
		constexpr int     kNX = 10;
		constexpr int     kNY = 5;
		for (int ix = 0; ix < kNX; ++ix) {
			for (int iy = 0; iy < kNY; ++iy) {
				const Vec2 pos{ -spec.length * 0.5 + 3.0 * r + (spec.length - 6.0 * r) * ix / (kNX - 1),
					-spec.width * 0.5 + 3.0 * r + (spec.width - 6.0 * r) * iy / (kNY - 1) };
				if (!a_sim.CanPlace(0, pos)) {
					continue;
				}
				Sim sim = a_sim;
				sim.Place(0, pos);
				const auto cands = Candidates(sim, a_rules, a_player, a_params);
				spots.push_back({ pos, cands.empty() ? 0.0 : cands.front().score });
			}
		}
		std::sort(spots.begin(), spots.end(), [](const Spot& a, const Spot& b) { return a.score > b.score; });

		AiShot fallback;
		bool   haveFallback = false;
		for (std::size_t i = 0; i < std::min<std::size_t>(spots.size(), 4); ++i) {
			Sim sim = a_sim;
			sim.Place(0, spots[i].pos);
			AiShot s = BestFromHere(sim, a_rules, a_player, a_params, true);
			s.placeCue = true;
			s.cuePos = spots[i].pos;
			if (s.checked) {
				return s;
			}
			if (!haveFallback) {
				fallback = s;
				haveFallback = true;
			}
		}
		if (haveFallback) {
			return fallback;
		}
		// nowhere to put it (can't really happen): where it is now
		AiShot s = BestFromHere(a_sim, a_rules, a_player, a_params, false);
		s.placeCue = true;
		s.cuePos = a_sim.Balls()[0].pos;
		return s;
	}

	AiShot ChooseShot(const Sim& a_sim, const Rules& a_rules, int a_player, const AiParams& a_params, std::mt19937& a_rng)
	{
		AiShot       s = BestShot(a_sim, a_rules, a_player, a_params);
		const double skill = std::clamp(a_params.skill, 0.0, 1.0);
		// aim error: about 2.5 degrees (1 sigma) for a beginner, 0.15 for the best
		const double aimSigma = (0.15 + 2.35 * (1.0 - skill)) * kPi / 180.0;
		const double speedSigma = 0.02 + 0.13 * (1.0 - skill);
		std::normal_distribution<double> aimErr(0.0, aimSigma);
		std::normal_distribution<double> speedErr(0.0, speedSigma);
		if (!a_rules.IsBreak()) {
			s.dir = Rotate(s.dir, aimErr(a_rng));
		}
		s.speed = std::clamp(s.speed * (1.0 + speedErr(a_rng)), a_params.minSpeed, a_params.maxSpeed);
		return s;
	}
}
