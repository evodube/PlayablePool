#include "sim/Sim.h"

#include <algorithm>
#include <cmath>

namespace pool
{
	namespace
	{
		constexpr double kSqrt2 = 1.4142135623730951;
		constexpr double kRackGap = 0.01;        // gap between racked balls, so the break spreads
		constexpr double kRackJitter = 0.004;     // tiny random offsets so no two breaks are the same
		constexpr double kMaxTravelPerStep = 0.2;  // in ball radii
		constexpr double kMaxStepTime = 1.0 / 240.0;
		constexpr int    kMaxSubSteps = 4000;

		// Small deterministic generator (same results on every compiler, unlike std:: distributions).
		struct Rng
		{
			std::uint32_t state;
			std::uint32_t Next()
			{
				state ^= state << 13;
				state ^= state >> 17;
				state ^= state << 5;
				return state;
			}
			double Unit() { return (Next() & 0xFFFFFF) / double(0x1000000); }  // [0, 1)
		};
	}

	double Vec2::Length() const { return std::sqrt(x * x + y * y); }

	Quat Quat::operator*(const Quat& o) const
	{
		return {
			w * o.w - x * o.x - y * o.y - z * o.z,
			w * o.x + x * o.w + y * o.z - z * o.y,
			w * o.y - x * o.z + y * o.w + z * o.x,
			w * o.z + x * o.y - y * o.x + z * o.w
		};
	}

	void Quat::Normalize()
	{
		const double n = std::sqrt(w * w + x * x + y * y + z * z);
		if (n > 0.0) {
			w /= n;
			x /= n;
			y /= n;
			z /= n;
		} else {
			*this = {};
		}
	}

	Quat Quat::FromAxisAngle(double ax, double ay, double az, double angle)
	{
		const double s = std::sin(angle * 0.5);
		return { std::cos(angle * 0.5), ax * s, ay * s, az * s };
	}

	void Quat::ToMatrix(double m[3][3]) const
	{
		m[0][0] = 1 - 2 * (y * y + z * z);
		m[0][1] = 2 * (x * y - z * w);
		m[0][2] = 2 * (x * z + y * w);
		m[1][0] = 2 * (x * y + z * w);
		m[1][1] = 1 - 2 * (x * x + z * z);
		m[1][2] = 2 * (y * z - x * w);
		m[2][0] = 2 * (x * z - y * w);
		m[2][1] = 2 * (y * z + x * w);
		m[2][2] = 1 - 2 * (x * x + y * y);
	}

	Sim::Sim(const TableSpec& a_spec) :
		_spec(a_spec)
	{
		for (int i = 0; i < kBalls; ++i) {
			_balls[i].number = i;
		}
		BuildCushions();
	}

	void Sim::BuildCushions()
	{
		const double hl = _spec.length * 0.5;
		const double hw = _spec.width * 0.5;
		const double cj = _spec.cornerMouth / kSqrt2;  // how far along each rail the corner opening reaches
		const double sj = _spec.sideMouth * 0.5;

		_cushions.clear();
		for (const double y : { -hw, hw }) {
			_cushions.push_back({ { -hl + cj, y }, { -sj, y } });
			_cushions.push_back({ { sj, y }, { hl - cj, y } });
		}
		for (const double x : { -hl, hl }) {
			_cushions.push_back({ { x, -hw + cj }, { x, hw - cj } });
		}

		// the jaws: from each end of an opening, into the pocket, closing in by the jaw angle
		const double jaw = _spec.jawLength * _spec.ballRadius;
		auto addJaws = [&](Vec2 a_p1, Vec2 a_p2, Vec2 a_inward, double a_angleDeg) {
			if (jaw <= 1e-6) {
				return;
			}
			const double close = std::tan(a_angleDeg * 3.14159265358979 / 180.0) * jaw;
			Vec2         across = a_p2 - a_p1;
			across = across * (1.0 / across.Length());
			_cushions.push_back({ a_p1, a_p1 + a_inward * jaw + across * close });
			_cushions.push_back({ a_p2, a_p2 + a_inward * jaw - across * close });
		};
		for (const double sx : { -1.0, 1.0 }) {
			for (const double sy : { -1.0, 1.0 }) {
				addJaws({ sx * (hl - cj), sy * hw }, { sx * hl, sy * (hw - cj) }, Vec2{ sx, sy } * (1.0 / kSqrt2), _spec.cornerJawAngle);
			}
		}
		for (const double sy : { -1.0, 1.0 }) {
			addJaws({ -sj, sy * hw }, { sj, sy * hw }, { 0.0, sy }, _spec.sideJawAngle);
		}
	}

	void Sim::PocketMouth(int a_pocket, Vec2& a_middle, Vec2& a_inward) const
	{
		const double hl = _spec.length * 0.5;
		const double hw = _spec.width * 0.5;
		const double cj = _spec.cornerMouth / kSqrt2;
		if (a_pocket < 4) {
			const double sx = (a_pocket == 2 || a_pocket == 3) ? 1.0 : -1.0;
			const double sy = (a_pocket == 1 || a_pocket == 3) ? 1.0 : -1.0;
			a_middle = { sx * (hl - cj * 0.5), sy * (hw - cj * 0.5) };
			a_inward = Vec2{ sx, sy } * (1.0 / kSqrt2);
		} else {
			const double sy = a_pocket == 5 ? 1.0 : -1.0;
			a_middle = { 0.0, sy * hw };
			a_inward = { 0.0, sy };
		}
	}

	Vec2 Sim::PocketPos(int a_pocket) const
	{
		const double hl = _spec.length * 0.5;
		const double hw = _spec.width * 0.5;
		switch (a_pocket) {
		case 0: return { -hl, -hw };
		case 1: return { -hl, hw };
		case 2: return { hl, -hw };
		case 3: return { hl, hw };
		case 4: return { 0.0, -hw };
		default: return { 0.0, hw };
		}
	}

	void Sim::Clear()
	{
		for (auto& b : _balls) {
			b.state = BallState::kOff;
			b.vel = {};
			b.pocket = -1;
		}
	}

	void Sim::Place(int a_index, Vec2 a_pos)
	{
		auto& b = _balls[a_index];
		b.pos = a_pos;
		b.vel = {};
		b.state = BallState::kOnTable;
		b.pocket = -1;
	}

	void Sim::Rack(std::uint32_t a_seed)
	{
		Clear();
		Rng rng{ a_seed ? a_seed : 0x9E3779B9u };

		// 8-ball: 1 at the apex, 8 in the middle, one solid and one stripe in the back corners, the rest shuffled.
		std::vector<int> solids{ 2, 3, 4, 5, 6, 7 };
		std::vector<int> stripes{ 9, 10, 11, 12, 13, 14, 15 };
		auto take = [&](std::vector<int>& a_from) {
			const auto i = rng.Next() % a_from.size();
			const int  n = a_from[i];
			a_from.erase(a_from.begin() + static_cast<std::ptrdiff_t>(i));
			return n;
		};
		const int cornerSolid = take(solids);
		const int cornerStripe = take(stripes);
		std::vector<int> rest = solids;
		rest.insert(rest.end(), stripes.begin(), stripes.end());

		const double r = _spec.ballRadius;
		const double d = 2.0 * r + kRackGap;
		const double rowStep = d * std::sqrt(3.0) * 0.5;
		const Vec2   apex = FootSpot();
		const bool   solidLeft = (rng.Next() & 1) != 0;

		for (int row = 0; row < 5; ++row) {
			for (int j = 0; j <= row; ++j) {
				int number;
				if (row == 0) {
					number = 1;
				} else if (row == 2 && j == 1) {
					number = 8;
				} else if (row == 4 && j == 0) {
					number = solidLeft ? cornerSolid : cornerStripe;
				} else if (row == 4 && j == 4) {
					number = solidLeft ? cornerStripe : cornerSolid;
				} else {
					number = take(rest);
				}
				const Vec2 jitter{ (rng.Unit() - 0.5) * kRackJitter, (rng.Unit() - 0.5) * kRackJitter };
				Place(number, Vec2{ apex.x + row * rowStep, (j - row * 0.5) * d } + jitter);
			}
		}
		Place(0, HeadSpot());
	}

	bool Sim::SpotFree(Vec2 a_pos, int a_ignore) const
	{
		const double min = 2.0 * _spec.ballRadius + 0.05;
		for (int i = 0; i < kBalls; ++i) {
			const auto& b = _balls[i];
			if (i != a_ignore && b.state == BallState::kOnTable && (b.pos - a_pos).LengthSq() < min * min) {
				return false;
			}
		}
		return true;
	}

	void Sim::RespotCue()
	{
		const double r = _spec.ballRadius;
		const double hl = _spec.length * 0.5;
		Vec2         spot = HeadSpot();
		// walk back towards the head rail, then forwards, until there's room
		for (int i = 0; i < 200; ++i) {
			const double offset = (i % 2 == 0 ? -1.0 : 1.0) * (i / 2) * r * 0.5;
			const Vec2   p{ spot.x + offset, 0.0 };
			if (std::abs(p.x) < hl - r && SpotFree(p, 0)) {
				spot = p;
				break;
			}
		}
		Place(0, spot);
	}

	void Sim::Strike(Vec2 a_dir, double a_speed)
	{
		auto& cue = _balls[0];
		const double len = a_dir.Length();
		if (cue.state != BallState::kOnTable || len <= 0.0) {
			return;
		}
		cue.vel = a_dir * (a_speed / len);
	}

	Prediction Sim::Predict(Vec2 a_dir) const
	{
		Prediction  out;
		const auto& cue = _balls[0];
		const double len = a_dir.Length();
		if (cue.state != BallState::kOnTable || len <= 0.0) {
			return out;
		}
		const Vec2   d = a_dir * (1.0 / len);
		const Vec2   p = cue.pos;
		const double r = _spec.ballRadius;

		// the cushions, as the lines the cue ball's centre can't cross
		const double hl = _spec.length * 0.5 - r;
		const double hw = _spec.width * 0.5 - r;
		double       best = 1e30;
		Vec2         cushionNormal;
		auto wall = [&](double a_pos, double a_limit, double a_dir2, Vec2 a_normal) {
			if (a_dir2 > 1e-12) {
				const double t = (a_limit - a_pos) / a_dir2;
				if (t >= 0.0 && t < best) {
					best = t;
					cushionNormal = a_normal;
				}
			}
		};
		wall(p.x, hl, d.x, { -1.0, 0.0 });
		wall(-p.x, hl, -d.x, { 1.0, 0.0 });
		wall(p.y, hw, d.y, { 0.0, -1.0 });
		wall(-p.y, hw, -d.y, { 0.0, 1.0 });

		// balls: the cue ball's centre comes within two radii of theirs
		const double reach = 2.0 * r;
		for (int i = 1; i < kBalls; ++i) {
			const auto& b = _balls[i];
			if (b.state != BallState::kOnTable) {
				continue;
			}
			const Vec2   m = p - b.pos;
			const double bq = m.Dot(d);
			const double c = m.LengthSq() - reach * reach;
			const double disc = bq * bq - c;
			if (disc < 0.0) {
				continue;
			}
			const double t = -bq - std::sqrt(disc);
			if (t >= 0.0 && t < best) {
				best = t;
				out.hitBall = true;
				out.ball = i;
			}
		}
		if (best >= 1e29) {
			return out;
		}

		out.distance = best;
		out.contact = p + d * best;
		if (out.hitBall) {
			const Vec2 toBall = _balls[out.ball].pos - out.contact;
			const double l = toBall.Length();
			out.objectDir = l > 0.0 ? toBall * (1.0 / l) : d;
			// the cue ball keeps its motion along the tangent, plus what the imperfect bounce leaves of the rest
			const double along = d.Dot(out.objectDir);
			const Vec2   after = d - out.objectDir * (along * 0.5 * (1.0 + _spec.ballRestitution));
			const double al = after.Length();
			out.cueSpeed = al;
			out.cueDir = al > 1e-9 ? after * (1.0 / al) : Vec2{};
		} else {
			out.cueDir = d - cushionNormal * (2.0 * d.Dot(cushionNormal));
		}
		return out;
	}

	bool Sim::AtRest() const
	{
		return std::ranges::none_of(_balls, [](const Ball& b) {
			return b.state == BallState::kOnTable && (b.vel.x != 0.0 || b.vel.y != 0.0);
		});
	}

	double Sim::KineticEnergy() const
	{
		double e = 0.0;
		for (const auto& b : _balls) {
			if (b.state == BallState::kOnTable) {
				e += b.vel.LengthSq();
			}
		}
		return e;
	}

	std::vector<Event> Sim::Step(double a_dt)
	{
		std::vector<Event> events;
		if (a_dt <= 0.0) {
			return events;
		}

		double maxSpeed = 0.0;
		for (const auto& b : _balls) {
			if (b.state == BallState::kOnTable) {
				maxSpeed = std::max(maxSpeed, b.vel.Length());
			}
		}
		if (maxSpeed == 0.0) {
			return events;
		}

		const double maxTravel = kMaxTravelPerStep * _spec.ballRadius;
		int          steps = static_cast<int>(std::ceil(std::max(maxSpeed * a_dt / maxTravel, a_dt / kMaxStepTime)));
		steps = std::clamp(steps, 1, kMaxSubSteps);
		const double h = a_dt / steps;
		for (int i = 0; i < steps; ++i) {
			SubStep(h, events);
		}
		return events;
	}

	void Sim::SubStep(double a_dt, std::vector<Event>& a_events)
	{
		const double r = _spec.ballRadius;

		for (int i = 0; i < kBalls; ++i) {
			auto& b = _balls[i];
			if (b.state != BallState::kOnTable) {
				continue;
			}
			const double speed = b.vel.Length();
			if (speed == 0.0) {
				continue;
			}

			// roll: rotate about the horizontal axis perpendicular to the motion (z up)
			const double dist = speed * a_dt;
			const auto   roll = Quat::FromAxisAngle(-b.vel.y / speed, b.vel.x / speed, 0.0, dist / r);
			b.rot = roll * b.rot;
			b.rot.Normalize();

			b.pos += b.vel * a_dt;

			// rolling resistance
			const double newSpeed = speed - _spec.rollDecel * a_dt;
			if (newSpeed <= _spec.stopSpeed) {
				b.vel = {};
			} else {
				b.vel = b.vel * (newSpeed / speed);
			}

			CheckPockets(i, a_events);
			if (b.state == BallState::kOnTable) {
				CollideCushions(i, a_events);
			}
		}

		CollideBalls(a_events);
		// Pushing one pair apart can push a ball into a third (the rack is packed tight), so settle positions a few
		// more times. Velocities were already handled above.
		for (int pass = 0; pass < 3; ++pass) {
			SeparateBalls();
		}
	}

	void Sim::SeparateBalls()
	{
		const double r2 = 2.0 * _spec.ballRadius;
		for (int i = 0; i < kBalls; ++i) {
			auto& a = _balls[i];
			if (a.state != BallState::kOnTable) {
				continue;
			}
			for (int j = i + 1; j < kBalls; ++j) {
				auto& b = _balls[j];
				if (b.state != BallState::kOnTable) {
					continue;
				}
				const Vec2   d = b.pos - a.pos;
				const double dist2 = d.LengthSq();
				if (dist2 >= r2 * r2) {
					continue;
				}
				const double dist = std::sqrt(dist2);
				const Vec2   n = dist > 1e-9 ? d * (1.0 / dist) : Vec2{ 1.0, 0.0 };
				const double overlap = r2 - dist;
				a.pos -= n * (overlap * 0.5);
				b.pos += n * (overlap * 0.5);
			}
		}
	}

	void Sim::CheckPockets(int a_index, std::vector<Event>& a_events)
	{
		auto&        b = _balls[a_index];
		const double r = _spec.ballRadius;
		const double hl = _spec.length * 0.5;
		const double hw = _spec.width * 0.5;

		// In once the centre is far enough past an opening; the jaws decide whether it gets that far.
		int    pocket = -1;
		double deepest = _spec.pocketDepth * r;
		for (int p = 0; p < kPockets; ++p) {
			Vec2 middle, inward;
			PocketMouth(p, middle, inward);
			const Vec2   rel = b.pos - middle;
			const double depth = rel.Dot(inward);
			const double half = (p < 4 ? _spec.cornerMouth : _spec.sideMouth) * 0.5;
			const Vec2   side = rel - inward * depth;
			if (depth >= deepest && side.Length() <= half) {
				deepest = depth;
				pocket = p;
			}
		}
		// Safety net: a ball well past a cushion line anywhere (it can't really get there) goes in the nearest pocket.
		if (pocket < 0 && (std::abs(b.pos.x) > hl + r || std::abs(b.pos.y) > hw + r)) {
			double best = 1e30;
			for (int p = 0; p < kPockets; ++p) {
				const double d2 = (b.pos - PocketPos(p)).LengthSq();
				if (d2 < best) {
					best = d2;
					pocket = p;
				}
			}
		}
		if (pocket < 0) {
			return;
		}

		a_events.push_back({ Event::Type::kPocket, a_index, pocket, b.vel.Length() });
		b.state = BallState::kPocketed;
		b.pocket = pocket;
		b.pos = PocketPos(pocket);
		b.vel = {};
	}

	void Sim::CollideCushions(int a_index, std::vector<Event>& a_events)
	{
		auto&        b = _balls[a_index];
		const double r = _spec.ballRadius;

		for (const auto& s : _cushions) {
			const Vec2   ab = s.b - s.a;
			const double t = std::clamp((b.pos - s.a).Dot(ab) / ab.LengthSq(), 0.0, 1.0);
			const Vec2   closest = s.a + ab * t;
			const Vec2   d = b.pos - closest;
			const double dist2 = d.LengthSq();
			if (dist2 >= r * r || dist2 == 0.0) {
				continue;
			}
			const double dist = std::sqrt(dist2);
			const Vec2   n = d * (1.0 / dist);  // from the cushion (or a pocket jaw) towards the ball
			const double vn = b.vel.Dot(n);
			b.pos = closest + n * r;
			if (vn < 0.0) {
				b.vel -= n * ((1.0 + _spec.cushionRestitution) * vn);
				a_events.push_back({ Event::Type::kCushion, a_index, -1, -vn });
			}
		}
	}

	void Sim::CollideBalls(std::vector<Event>& a_events)
	{
		const double r2 = 2.0 * _spec.ballRadius;
		for (int i = 0; i < kBalls; ++i) {
			auto& a = _balls[i];
			if (a.state != BallState::kOnTable) {
				continue;
			}
			for (int j = i + 1; j < kBalls; ++j) {
				auto& b = _balls[j];
				if (b.state != BallState::kOnTable) {
					continue;
				}
				const Vec2   d = b.pos - a.pos;
				const double dist2 = d.LengthSq();
				if (dist2 >= r2 * r2) {
					continue;
				}
				// They overlap because the step overshot. Rewind both to the moment they touched, so the contact
				// direction (and with it the angle the balls leave at) is exact, then replay the rest of the step.
				const Vec2   rel = a.vel - b.vel;  // a's motion relative to b
				double       rewind = 0.0;
				const double rv2 = rel.LengthSq();
				if (rv2 > 1e-12) {
					// |d + rel * s| = r2, smallest s >= 0 (d grows back to r2 as we go back in time)
					const double bq = d.Dot(rel);
					const double c = dist2 - r2 * r2;
					const double disc = bq * bq - rv2 * c;
					if (disc >= 0.0) {
						rewind = std::max(0.0, (-bq + std::sqrt(disc)) / rv2);
						rewind = std::min(rewind, 0.05);  // never further back than a few frames' worth
					}
				}
				a.pos -= a.vel * rewind;
				b.pos -= b.vel * rewind;

				Vec2         dd = b.pos - a.pos;
				const double dist = dd.Length();
				const Vec2   n = dist > 1e-9 ? dd * (1.0 / dist) : Vec2{ 1.0, 0.0 };

				// equal masses: exchange the normal component (scaled by restitution)
				const double vn = (a.vel - b.vel).Dot(n);
				if (vn > 0.0) {
					const double impulse = 0.5 * (1.0 + _spec.ballRestitution) * vn;
					a.vel -= n * impulse;
					b.vel += n * impulse;
					a_events.push_back({ Event::Type::kBallBall, i, j, vn });
				}

				a.pos += a.vel * rewind;
				b.pos += b.vel * rewind;

				// anything still overlapping (resting contact, packed rack) is pushed apart
				dd = b.pos - a.pos;
				const double after = dd.Length();
				if (after < r2) {
					const Vec2   nn = after > 1e-9 ? dd * (1.0 / after) : n;
					const double overlap = r2 - after;
					a.pos -= nn * (overlap * 0.5);
					b.pos += nn * (overlap * 0.5);
				}
			}
		}
	}
}
