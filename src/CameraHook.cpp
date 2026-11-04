// Hooks the free camera ("tfc") state so aim mode runs inside the game's own camera update.
//
// FreeCameraState::Update applies the mouse; right after it, Game::OnFreeCameraUpdate turns that into aim or power
// and places the camera. GetTranslation is where the game reads the camera position for the frame, so while aiming
// it returns ours. Doing this from a separate task (as before) left the position a frame behind the angle, which
// showed as a stutter whenever the mouse moved.

#include "CameraHook.h"

#include "Game.h"

namespace fp::CameraHook
{
	namespace
	{
		using Update_t = void (*)(RE::FreeCameraState*, RE::BSTSmartPointer<RE::TESCameraState>&);
		using GetTranslation_t = void (*)(const RE::FreeCameraState*, RE::NiPoint3&);

		Update_t         g_update{ nullptr };
		GetTranslation_t g_getTranslation{ nullptr };
		bool             g_installed{ false };

		void HookedUpdate(RE::FreeCameraState* a_this, RE::BSTSmartPointer<RE::TESCameraState>& a_next)
		{
			g_update(a_this, a_next);
			Game::Get().OnFreeCameraUpdate(a_this);
		}

		void HookedGetTranslation(const RE::FreeCameraState* a_this, RE::NiPoint3& a_out)
		{
			if (!Game::Get().FreeCameraTranslation(a_out)) {
				g_getTranslation(a_this, a_out);
			}
		}
	}

	void Install()
	{
		if (g_installed) {
			return;
		}
		REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE::FreeCameraState[0] };
		g_update = reinterpret_cast<Update_t>(vtable.write_vfunc(0x0B, &HookedUpdate));
		g_getTranslation = reinterpret_cast<GetTranslation_t>(vtable.write_vfunc(0x0D, &HookedGetTranslation));
		g_installed = true;
		log::info("free camera hooks installed");
	}
}
