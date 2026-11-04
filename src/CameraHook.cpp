// Hooks the free camera ("tfc") state so aim mode runs inside the game's own camera code.
//
// The free camera can turn in two places: its mouse handler (OnMouseMoveEvent) and its Update (which applies the
// camera's queued look input). While a shot is being charged the mouse movement is taken before either sees it, so
// the game never turns the view at all, not even for one frame. The rest of the time the game turns it as usual and
// Game::OnFreeCameraLook keeps the pitch in range and learns how far the view turns per unit of mouse movement,
// which is what converts a pull into power at the player's own mouse sensitivity.
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
			if (a_event && Game::Get().OnFreeCameraMouse(a_event->mouseInputX, a_event->mouseInputY)) {
				return;  // charging: the movement became power, the view stays put
			}
			const auto before = a_this->rotation;
			g_mouseMove(a_this, a_event);
			Game::Get().OnFreeCameraLook(a_this, before, a_event ? static_cast<float>(a_event->mouseInputX) : 0.0f,
				a_event ? static_cast<float>(a_event->mouseInputY) : 0.0f, Game::LookSource::kMouse);
		}

		void HookedUpdate(RE::FreeCameraState* a_this, RE::BSTSmartPointer<RE::TESCameraState>& a_next)
		{
			RE::BSTPoint2<float> input{};
			if (auto* camera = a_this->camera) {
				input = camera->rotationInput;
				if (Game::Get().OnFreeCameraInput(input.x, input.y)) {
					camera->rotationInput.x = 0.0f;  // charging: the look input became power
					camera->rotationInput.y = 0.0f;
				}
			}
			const auto before = a_this->rotation;
			g_update(a_this, a_next);
			Game::Get().OnFreeCameraLook(a_this, before, input.x, input.y, Game::LookSource::kUpdate);
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
