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
		const double hl = _spec.length * 0.5;
		const double hw = _spec.width * 0.5;

		// The cushions block everything except the pocket openings, so a ball whose centre gets past a cushion line
		// went in. Also take one sitting deep in a corner.
		bool in = std::abs(b.pos.x) > hl || std::abs(b.pos.y) > hw;
		int  nearest = 0;
		double best = 1e30;
		for (int p = 0; p < kPockets; ++p) {
			const double d2 = (b.pos - PocketPos(p)).LengthSq();
			if (d2 < best) {
				best = d2;
				nearest = p;
			}
		}
		if (!in && nearest < 4 && best < _spec.ballRadius * _spec.ballRadius) {
			in = true;
		}
		if (!in) {
			return;
		}

		a_events.push_back({ Event::Type::kPocket, a_index, nearest, b.vel.Length() });
		b.state = BallState::kPocketed;
		b.pocket = nearest;
		b.pos = PocketPos(nearest);
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
				const double dist = std::sqrt(dist2);
				const Vec2   n = dist > 1e-9 ? d * (1.0 / dist) : Vec2{ 1.0, 0.0 };

				// separate them
				const double overlap = r2 - dist;
				a.pos -= n * (overlap * 0.5);
				b.pos += n * (overlap * 0.5);

				// equal masses: exchange the normal component (scaled by restitution)
				const double vn = (a.vel - b.vel).Dot(n);
				if (vn > 0.0) {
					const double impulse = 0.5 * (1.0 + _spec.ballRestitution) * vn;
					a.vel -= n * impulse;
					b.vel += n * impulse;
					a_events.push_back({ Event::Type::kBallBall, i, j, vn });
				}
			}
		}
	}
}
