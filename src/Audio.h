#pragma once

namespace fp::Audio
{
	enum class Bank
	{
		kCue,      // the stick hitting the cue ball
		kHit,      // ball on ball
		kCushion,  // ball on a rail
		kPocket,   // a ball dropping in
		kReturn,   // a pocketed ball rolling away under the table
		kCount
	};

	// Loads Data\Sound\fo4pool\*.wav and starts XAudio2. Safe to call more than once.
	void Init();

	// Plays one of the bank's sounds at a_pos in the world. a_gain 0..1 is how hard the hit was; distance from the
	// camera and the [Sound] volume are applied on top.
	void Play(Bank a_bank, const RE::NiPoint3& a_pos, float a_gain);

	// Which way the listener faces (radians, the game's yaw: 0 = north/+Y). Set each frame while aiming.
	void SetListenerYaw(float a_yaw, bool a_valid);
}
