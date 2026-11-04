// The aim-mode HUD: power bar and controls, drawn through F4SE Menu Framework when it's installed.

#include "Hud.h"

#include "F4SEMenuFramework.h"
#include "Game.h"

namespace fp::Hud
{
	namespace
	{
		using namespace ImGuiMCP;

		void __stdcall Draw()
		{
			const auto state = Game::Get().Hud();
			if (!state.aiming) {
				return;
			}
			auto* viewport = GetMainViewport();
			if (!viewport) {
				return;
			}
			const ImVec2 bottom{ viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.86f };
			SetNextWindowPos(bottom, ImGuiCond_Always, ImVec2{ 0.5f, 1.0f });
			SetNextWindowBgAlpha(0.55f);
			const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
			                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
			if (Begin("##fo4pool_hud", nullptr, flags)) {
				if (state.rolling) {
					Text("Balls rolling...");
				} else if (state.charging) {
					char label[16];
					std::snprintf(label, sizeof(label), "%d%%", static_cast<int>(state.power * 100.0f + 0.5f));
					ProgressBar(state.power, ImVec2{ 320.0f, 22.0f }, label);
					Text("Pull back for more, push forward for less. Release to shoot (0%% cancels)");
				} else {
					Text("Hold the left mouse button and pull the mouse back for power, release to shoot");
				}
				Text("Right mouse: %s   |   Numpad 8: stop aiming", state.overhead ? "back to the cue ball" : "overhead view");
			}
			End();
		}

		F4SEMenuFramework::Model::HudElement* g_element{ nullptr };
	}

	void Install()
	{
		if (g_element) {
			return;
		}
		if (!F4SEMenuFramework::IsInstalled()) {
			log::warn("F4SE Menu Framework not found: no power bar on screen (aiming still works)");
			return;
		}
		g_element = F4SEMenuFramework::AddHudElement(Draw);
		log::info("HUD element registered");
	}
}
