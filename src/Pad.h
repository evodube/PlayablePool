#pragma once

// The controller, read straight from XInput (what Fallout 4 itself uses), so it works the same on every runtime and
// whatever the game's own button setup is. Render thread only.

#include <cstdint>

namespace fp::Pad
{
	enum Button : std::uint16_t
	{
		kUp = 0x0001,
		kDown = 0x0002,
		kLeft = 0x0004,
		kRight = 0x0008,
		kStart = 0x0010,
		kBack = 0x0020,
		kLB = 0x0100,
		kRB = 0x0200,
		kA = 0x1000,
		kB = 0x2000,
		kX = 0x4000,
		kY = 0x8000,
	};

	struct State
	{
		bool          connected{ false };
		std::uint16_t buttons{ 0 };
		float         lt{ 0.0f }, rt{ 0.0f };  // 0..1
		float         lx{ 0.0f }, ly{ 0.0f };  // -1..1 past the dead zone; up and right are positive
		float         rx{ 0.0f }, ry{ 0.0f };
	};

	// The first connected controller (looks for one again every couple of seconds while there's none).
	State Read();
	// 0..1 on the two motors; 0, 0 stops it.
	void Rumble(float a_low, float a_high);
}
