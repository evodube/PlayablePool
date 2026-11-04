// The HUD, drawn through F4SE Menu Framework when it's installed: the 8-ball scoreboard while a table is racked,
// and the power bar and controls while aiming.

#include "Hud.h"

#include "F4SEMenuFramework.h"
#include "Game.h"

namespace fp::Hud
{
	namespace
	{
		using namespace ImGuiMCP;

		constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
		                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

		const char* GroupLabel(int a_group)
		{
			return a_group == 1 ? "solids" : a_group == 2 ? "stripes" : "open table";
		}

		// The scoreboard: both players, their group and balls left, whose shot it is, and the latest call.
		// (A plain stand-in until the bar theme is picked.)
		void DrawScoreboard(const Game::HudState& a_state, const ImGuiViewport* a_viewport)
		{
			SetNextWindowPos(ImVec2{ a_viewport->Pos.x + 24.0f, a_viewport->Pos.y + 24.0f }, ImGuiCond_Always, ImVec2{ 0.0f, 0.0f });
			SetNextWindowBgAlpha(0.6f);
			if (Begin("##fo4pool_score", nullptr, kFlags)) {
				for (int p = 0; p < 2; ++p) {
					const bool up = !a_state.over && a_state.shooter == p;
					const ImVec4 colour = up ? ImVec4{ 1.0f, 0.85f, 0.45f, 1.0f } : ImVec4{ 0.75f, 0.72f, 0.66f, 1.0f };
					if (a_state.group[p] == 0) {
						TextColored(colour, "%s Player %d   %s", up ? ">" : " ", p + 1, GroupLabel(0));
					} else {
						TextColored(colour, "%s Player %d   %s, %d left", up ? ">" : " ", p + 1, GroupLabel(a_state.group[p]), a_state.remaining[p]);
					}
				}
				if (a_state.over) {
					TextColored(ImVec4{ 1.0f, 0.6f, 0.4f, 1.0f }, "Game over: Player %d wins. Numpad 7 racks again", a_state.winner + 1);
				} else if (a_state.breakShot) {
					Text("Break");
				} else if (a_state.onEight) {
					Text(a_state.calling ? "On the 8: the marked pocket is called (Numpad 5 to change)" : "On the 8");
				}
				if (!a_state.message.empty() && a_state.messageAge < 8.0f) {
					Separator();
					TextUnformatted(a_state.message.c_str());
				}
			}
			End();
		}

		void __stdcall Draw()
		{
			const auto state = Game::Get().Hud();
			auto*      viewport = GetMainViewport();
			if (!viewport || !state.game) {
				return;
			}
			DrawScoreboard(state, viewport);
			if (!state.aiming) {
				return;
			}
			const ImVec2 bottom{ viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.86f };
			SetNextWindowPos(bottom, ImGuiCond_Always, ImVec2{ 0.5f, 1.0f });
			SetNextWindowBgAlpha(0.55f);
			if (Begin("##fo4pool_hud", nullptr, kFlags)) {
				if (state.placing) {
					Text("Ball in hand: move the mouse to place the cue ball, left click to set it");
				} else if (state.rolling) {
					Text("Balls rolling...");
				} else if (state.over) {
					Text("Game over. Numpad 7 racks again, Numpad 8 stops aiming");
				} else if (state.charging) {
					char label[16];
					std::snprintf(label, sizeof(label), "%d%%", static_cast<int>(state.power * 100.0f + 0.5f));
					ProgressBar(state.power, ImVec2{ 320.0f, 22.0f }, label);
					Text("Pull back for more, push forward for less. Release to shoot (0%% cancels)");
				} else {
					Text("Hold the left mouse button and pull the mouse back for power, release to shoot");
				}
				if (!state.placing) {
					Text("Right mouse: %s   |   Numpad 8: stop aiming", state.overhead ? "back to the cue ball" : "overhead view");
				}
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
