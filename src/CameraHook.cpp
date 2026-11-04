// Hooks the free camera ("tfc") state so aim mode runs inside the game's own camera code.
//
// The free camera can turn in two places: its mouse handler (OnMouseMoveEvent) and its Update. Both are wrapped:
// we note the angles before the game's code runs, and right after it Game::OnFreeCameraLook decides what the change
// means (aim, or power while charging, in which case the angles are put straight back so the view never moves).
// GetTranslation is where the game reads the camera position for the frame; it is worked out there, from the
// angles as they are at that moment, so position and angle can never come from different frames.

#include "CameraHook.h"

#include "Game.h"

namespace fp::CameraHook
{
	namespace
	{
		using Update_t = void (*)(RE::FreeCameraState*, RE::BSTSmartPointer<RE::TESCameraState>&);
		using GetTranslation_t = void (*)(const RE::FreeCameraState*, RE::NiPoint3&);
		using MouseMove_t = void (*)(RE::FreeCameraState*, const RE::MouseMoveEvent*);

		Update_t         g_update{ nullptr };
		GetTranslation_t g_getTranslation{ nullptr };
		MouseMove_t      g_mouseMove{ nullptr };
		bool             g_installed{ false };

		void HookedMouseMove(RE::FreeCameraState* a_this, const RE::MouseMoveEvent* a_event)
		{
			const auto before = a_this->rotation;
			g_mouseMove(a_this, a_event);
			Game::Get().OnFreeCameraLook(a_this, before);
		}

		void HookedUpdate(RE::FreeCameraState* a_this, RE::BSTSmartPointer<RE::TESCameraState>& a_next)
		{
			const auto before = a_this->rotation;
			g_update(a_this, a_next);
			Game::Get().OnFreeCameraLook(a_this, before);
			Game::Get().OnFreeCameraUpdate(a_this);
		}

		void HookedGetTranslation(const RE::FreeCameraState* a_this, RE::NiPoint3& a_out)
		{
			if (!Game::Get().FreeCameraTranslation(a_this, a_out)) {
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
		g_mouseMove = reinterpret_cast<MouseMove_t>(vtable.write_vfunc(0x06, &HookedMouseMove));
		g_update = reinterpret_cast<Update_t>(vtable.write_vfunc(0x0B, &HookedUpdate));
		g_getTranslation = reinterpret_cast<GetTranslation_t>(vtable.write_vfunc(0x0D, &HookedGetTranslation));
		g_installed = true;
		log::info("free camera hooks installed");
	}
}
