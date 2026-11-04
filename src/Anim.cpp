#include "Anim.h"

#include "Settings.h"
#include "Util.h"

namespace fp::Anim
{
	namespace
	{
		bool  g_playing{ false };    // one of our idles was started and not stopped
		float g_strikeLeft{ 0.0f };  // seconds of PoolStrike still to play
		bool  g_warned{ false };

		RE::TESIdleForm* Idle(std::uint32_t a_rawID)
		{
			const auto& s = Settings::Get();
			auto*       form = a_rawID ? util::ResolveAnyForm(s.animPlugin, a_rawID) : nullptr;
			auto*       idle = form ? form->As<RE::TESIdleForm>() : nullptr;
			if (!idle && !g_warned) {
				g_warned = true;
				log::warn("pool idle {}|{:X} not found (is the new fo4pool.esl installed?)", s.animPlugin, a_rawID);
			}
			return idle;
		}

		bool Play(std::uint32_t a_rawID, const char* a_name)
		{
			if (!Settings::Get().anim) {
				return false;
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* idle = Idle(a_rawID);
			if (!player || !player->currentProcess || !idle) {
				return false;
			}
			const bool ok = player->currentProcess->PlayIdle(*player, idle, nullptr);
			log::info("play idle {} ({:08X}): {}", a_name, idle->GetFormID(), ok ? "started" : "refused");
			g_playing = g_playing || ok;
			return ok;
		}
	}

	void PlayAim()
	{
		g_strikeLeft = 0.0f;
		Play(Settings::Get().animAimFormID, "PoolAim");
	}

	void PlayStrike()
	{
		if (Play(Settings::Get().animStrikeFormID, "PoolStrike")) {
			g_strikeLeft = Settings::Get().animStrikeSeconds;
		}
	}

	void Stop()
	{
		g_strikeLeft = 0.0f;
		if (!g_playing) {
			return;
		}
		g_playing = false;
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (player && player->currentProcess) {
			player->currentProcess->StopCurrentIdle(player, false, true);
			log::info("pool idle stopped");
		}
	}

	void Tick(float a_dt, bool a_aiming)
	{
		if (g_strikeLeft <= 0.0f) {
			return;
		}
		g_strikeLeft -= a_dt;
		if (g_strikeLeft <= 0.0f) {
			if (a_aiming) {
				PlayAim();
			} else {
				Stop();
			}
		}
	}
}
