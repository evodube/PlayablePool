// Hooks the free camera ("tfc") state so aim mode runs inside the game's own camera code.
//
// Mouse movement reaches the view through the player's LookHandler (which feeds look input and a smoothing spring
// that keeps turning the view for a moment after the mouse stops) and possibly the free camera's own mouse handler.
// While a shot is being charged both handlers drop the movement (it becomes power instead), and before each camera
// update the look input and the spring are zeroed, so nothing can turn the view. Whatever still slips through is
// undone right after the update. The rest of the time the game turns the view as usual and Game::OnFreeCameraLook
// keeps the pitch in range and learns how far it turns per mouse count, so a pull gives power at the player's own
// sensitivity.
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

		using LookMouse_t = void (*)(RE::LookHandler*, const RE::MouseMoveEvent*);
		LookMouse_t g_lookMouse{ nullptr };

		// The player's look handler: where FO4 turns mouse movement into look input (and a smoothing spring).
		void HookedLookMouse(RE::LookHandler* a_this, const RE::MouseMoveEvent* a_event)
		{
			if (Game::Get().OnLookMouse(a_event)) {
				return;  // charging: the movement became power and never reaches the camera
			}
			g_lookMouse(a_this, a_event);
		}

		void HookedMouseMove(RE::FreeCameraState* a_this, const RE::MouseMoveEvent* a_event)
		{
			if (Game::Get().OnLookMouse(a_event)) {
				return;
			}
			const auto before = a_this->rotation;
			g_mouseMove(a_this, a_event);
			Game::Get().OnFreeCameraLook(a_this, before);
		}

		void HookedUpdate(RE::FreeCameraState* a_this, RE::BSTSmartPointer<RE::TESCameraState>& a_next)
		{
			Game::Get().BeforeFreeCameraUpdate(a_this);
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

	namespace
	{
		// a vtable slot swapped for ours: the old one back
		std::uintptr_t SwapSlot(void** a_vtable, std::size_t a_index, const void* a_hook)
		{
			DWORD old = 0;
			if (!::VirtualProtect(&a_vtable[a_index], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
				return 0;
			}
			const auto was = reinterpret_cast<std::uintptr_t>(a_vtable[a_index]);
			a_vtable[a_index] = const_cast<void*>(a_hook);
			::VirtualProtect(&a_vtable[a_index], sizeof(void*), old, &old);
			return was;
		}
	}

	void Install()
	{
		if (g_installed) {
			return;
		}
		if (REX::FModule::IsRuntimeOG()) {
			// 1.10.163: the vtables by their Address Library IDs
			REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE::FreeCameraState[0] };
			g_mouseMove = reinterpret_cast<MouseMove_t>(vtable.write_vfunc(0x06, &HookedMouseMove));
			g_update = reinterpret_cast<Update_t>(vtable.write_vfunc(0x0B, &HookedUpdate));
			g_getTranslation = reinterpret_cast<GetTranslation_t>(vtable.write_vfunc(0x0D, &HookedGetTranslation));
			REL::Relocation<std::uintptr_t> look{ RE::VTABLE::LookHandler[0] };
			g_lookMouse = reinterpret_cast<LookMouse_t>(look.write_vfunc(0x06, &HookedLookMouse));
		} else {
			// next-gen / Anniversary: the library only has the old game's vtable IDs, so the vtables are read off the
			// live objects instead (the player camera's free camera state, the player's look handler)
			auto* camera = RE::PlayerCamera::GetSingleton();
			auto* controls = RE::PlayerControls::GetSingleton();
			auto* free = camera ? camera->cameraStates[RE::CameraStates::kFree].get() : nullptr;
			auto* look = controls ? controls->lookHandler : nullptr;
			if (!free || !look) {
				log::error("free camera hooks: the camera or look handler isn't there yet");
				return;
			}
			auto** freeTable = *reinterpret_cast<void***>(free);
			auto** lookTable = *reinterpret_cast<void***>(look);
			g_mouseMove = reinterpret_cast<MouseMove_t>(SwapSlot(freeTable, 0x06, reinterpret_cast<const void*>(&HookedMouseMove)));
			g_update = reinterpret_cast<Update_t>(SwapSlot(freeTable, 0x0B, reinterpret_cast<const void*>(&HookedUpdate)));
			g_getTranslation = reinterpret_cast<GetTranslation_t>(SwapSlot(freeTable, 0x0D, reinterpret_cast<const void*>(&HookedGetTranslation)));
			g_lookMouse = reinterpret_cast<LookMouse_t>(SwapSlot(lookTable, 0x06, reinterpret_cast<const void*>(&HookedLookMouse)));
			if (!g_mouseMove || !g_update || !g_getTranslation || !g_lookMouse) {
				log::error("free camera hooks: couldn't write the vtables");
				return;
			}
		}
		g_installed = true;
		log::info("free camera hooks installed");
	}
}
