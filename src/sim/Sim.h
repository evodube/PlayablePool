#pragma once

// Pool table physics. Plain C++ with no game dependencies, so it can be tested on its own (tests/sim_test.cpp).
//
// Table space: origin at the centre of the playing surface, x along the length (head end at -x, foot end at +x),
// y across the width, z up. Units are game units (about 70 per metre) and seconds.

#include <array>
#include <cstdint>
#include <vector>

namespace pool
{
	struct Vec2
	{
		double x{ 0.0 };
		double y{ 0.0 };

		Vec2 operator+(const Vec2& o) const { return { x + o.x, y + o.y }; }
		Vec2 operator-(const Vec2& o) const { return { x - o.x, y - o.y }; }
		Vec2 operator*(double s) const { return { x * s, y * s }; }
		Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
		Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
		double Dot(const Vec2& o) const { return x * o.x + y * o.y; }
		double Length() const;
		double LengthSq() const { return x * x + y * y; }
	};

	// Unit quaternion: the ball's orientation in table space.
	struct Quat
	{
		double w{ 1.0 };
		double x{ 0.0 };
		double y{ 0.0 };
		double z{ 0.0 };

		Quat operator*(const Quat& o) const;
		void Normalize();
		static Quat FromAxisAngle(double ax, double ay, double az, double angle);
		// Row-major 3x3 rotation matrix.
		void ToMatrix(double out[3][3]) const;
	};

	struct TableSpec
	{
		double length{ 140.0 };            // playing surface, cushion nose to cushion nose (7 ft table is about 140)
		double width{ 70.0 };
		double ballRadius{ 2.0 };          // 57 mm ball
		double cornerMouth{ 8.6 };         // width of a corner pocket opening, measured across the diagonal
		double sideMouth{ 9.6 };           // width of a side pocket opening
		double rollDecel{ 10.0 };          // rolling resistance, units/s^2
		double ballRestitution{ 0.95 };
		double cushionRestitution{ 0.75 };
		double stopSpeed{ 0.4 };           // below this a ball stops
		// Pocket throats. Each opening has two jaws, angled walls running from the cushion ends into the pocket. A
		// ball only drops once its centre is pocketDepth radii past the opening, so one that clips a jaw or comes in
		// too fast and steep bounces off it and can rattle back out.
		double pocketDepth{ 0.7 };         // in ball radii
		double jawLength{ 1.3 };           // in ball radii
		double cornerJawAngle{ 15.0 };     // degrees the corner jaws close in towards the back of the pocket
		double sideJawAngle{ 0.0 };        // the same for the side pockets (0 = parallel)
	};

	enum class BallState : std::uint8_t
	{
		kOnTable,
		kPocketed,
		kOff  // not in this game
	};

	struct Ball
	{
		int       number{ 0 };  // 0 = cue ball, 1..15
		Vec2      pos;
		Vec2      vel;
		Quat      rot;
		BallState state{ BallState::kOff };
		int       pocket{ -1 };  // which pocket, once pocketed
	};

	struct Event
	{
		enum class Type : std::uint8_t
		{
			kBallBall,
			kCushion,
			kPocket
		};

		Type   type;
		int    a{ -1 };       // ball index
		int    b{ -1 };       // other ball index (ball-ball) or pocket index (pocket)
		double speed{ 0.0 };  // impact speed, for sounds
	};

	// Where a shot would go, for the aiming guide. Straight lines only: no friction, spin or later collisions.
	struct Prediction
	{
		bool   hitBall{ false };     // true: first hits a ball; false: first hits a cushion
		int    ball{ -1 };           // the ball it hits
		Vec2   contact;              // where the cue ball's centre is at the first contact
		Vec2   objectDir;            // the hit ball's direction afterwards
		Vec2   cueDir;               // the cue ball's direction afterwards
		double cueSpeed{ 1.0 };      // the cue ball's speed afterwards, as a fraction of its speed before
		double distance{ 0.0 };      // how far the cue ball travels to the contact
	};

	class Sim
	{
	public:
		static constexpr int kBalls = 16;
		static constexpr int kPockets = 6;

		explicit Sim(const TableSpec& a_spec = {});

		const TableSpec& Spec() const { return _spec; }
		const std::array<Ball, kBalls>& Balls() const { return _balls; }
		Ball& BallAt(int a_index) { return _balls[a_index]; }

		// Pocket centres in table space: 0..3 corners (head-left, head-right, foot-left, foot-right), 4..5 sides.
		Vec2 PocketPos(int a_pocket) const;
		// A pocket's opening: its middle and the direction into the pocket.
		void PocketMouth(int a_pocket, Vec2& a_middle, Vec2& a_inward) const;

		Vec2 HeadSpot() const { return { -_spec.length * 0.25, 0.0 }; }
		Vec2 FootSpot() const { return { _spec.length * 0.25, 0.0 }; }

		// Takes every ball off the table.
		void Clear();
		// Cue ball on the head spot, the 15 object balls racked for 8-ball at the foot spot.
		void Rack(std::uint32_t a_seed);
		// Puts one ball on the table at rest.
		void Place(int a_index, Vec2 a_pos);
		// Whether a ball could be put at a_pos: on the felt and not touching another (a_ignore aside).
		bool CanPlace(int a_index, Vec2 a_pos) const;
		// Puts the cue ball back on the head spot, or the nearest free spot behind it.
		void RespotCue();
		// Puts the 8 (or any ball) back on the foot spot, or the nearest free spot behind it.
		void RespotOnFoot(int a_index);

		// Hits the cue ball in direction a_dir (any length) at a_speed units/s.
		void Strike(Vec2 a_dir, double a_speed);

		// The first thing the cue ball would touch if hit in direction a_dir.
		Prediction Predict(Vec2 a_dir) const;

		// Advances the simulation. Splits a_dt into small enough sub-steps internally.
		std::vector<Event> Step(double a_dt);

		bool   AtRest() const;
		double KineticEnergy() const;  // sum of v^2 (equal masses), for tests

	private:
		struct Segment
		{
			Vec2 a;
			Vec2 b;
		};

		void BuildCushions();
		void SubStep(double a_dt, std::vector<Event>& a_events);
		void CollideBalls(std::vector<Event>& a_events);
		void SeparateBalls();
		void CollideCushions(int a_index, std::vector<Event>& a_events);
		void CheckPockets(int a_index, std::vector<Event>& a_events);
		bool SpotFree(Vec2 a_pos, int a_ignore) const;
		void RespotNear(int a_index, Vec2 a_spot, double a_backwards);

		TableSpec                _spec;
		std::array<Ball, kBalls> _balls;
		std::vector<Segment>     _cushions;
	};
}
