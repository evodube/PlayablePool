#include "Hook.h"

#include "Game.h"

namespace fp::Hook
{
	namespace
	{
		using Present_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);

		Present_t       g_originalPresent{ nullptr };
		IDXGISwapChain* g_swapChain{ nullptr };
		bool            g_installed{ false };

		HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags)
		{
			if (a_swapChain == g_swapChain) {
				Game::Get().OnPresent();
			}
			return g_originalPresent(a_swapChain, a_syncInterval, a_flags);
		}
	}

	bool Install()
	{
		if (g_installed) {
			return true;
		}

		auto* data = RE::BSGraphics::GetRendererData();
		if (!data) {
			log::warn("renderer not ready yet");
			return false;
		}
		auto* swapChain = reinterpret_cast<IDXGISwapChain*>(data->renderWindow[0].swapChain);
		if (!swapChain) {
			log::warn("swap chain not ready yet");
			return false;
		}

		auto**        vtable = *reinterpret_cast<void***>(swapChain);
		DWORD         oldProtect = 0;
		constexpr int kPresent = 8;
		if (!::VirtualProtect(&vtable[kPresent], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
			log::error("VirtualProtect failed");
			return false;
		}
		g_originalPresent = reinterpret_cast<Present_t>(vtable[kPresent]);
		vtable[kPresent] = reinterpret_cast<void*>(&HookedPresent);
		::VirtualProtect(&vtable[kPresent], sizeof(void*), oldProtect, &oldProtect);

		g_swapChain = swapChain;
		g_installed = true;
		log::info("Present hook installed");
		return true;
	}
}
