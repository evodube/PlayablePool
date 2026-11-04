#pragma once

namespace fp::Anim
{
	// The player's pool idles (fo4pool.esl, [Anim] in the INI). Call on the game's main thread.
	void PlayAim();     // PoolAim: lined up over the table, loops
	void PlayStrike();  // PoolStrike: the shot; Tick() goes back to PoolAim after fStrikeSeconds
	void Stop();        // back to normal
	void Tick(float a_dt, bool a_aiming);
}
