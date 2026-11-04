#pragma once

namespace fp::Hud
{
	// Registers the aim-mode HUD with F4SE Menu Framework, if it's installed. Safe to call more than once.
	void Install();

	// The terminal HUD is up (Menu Framework there, its art loaded), so calls don't need the game's own notifications.
	bool Showing();
}
