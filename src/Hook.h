#pragma once

namespace fp::Hook
{
	// Hooks IDXGISwapChain::Present on the game's swap chain, to get a call every frame. Safe to call more than once.
	bool Install();
}
