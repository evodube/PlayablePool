#include "Audio.h"
#include "CameraHook.h"
#include "Game.h"
#include "Hook.h"
#include "Hud.h"
#include "Settings.h"

namespace
{
	constexpr auto kName = "fo4pool";

	void OnMessage(F4SE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		switch (a_msg->type) {
		case F4SE::MessagingInterface::kPostPostLoad:
			fp::Hud::Install();  // F4SE Menu Framework is loaded by now, if it's installed
			break;
		case F4SE::MessagingInterface::kGameDataReady:
			fp::log::info("game data ready");
			fp::Settings::Get().Load();
			fp::Hook::Install();
			fp::CameraHook::Install();
			fp::Audio::Init();
			fp::Game::ListenForLoading();
			break;
		case F4SE::MessagingInterface::kPreLoadGame:
			fp::Game::Get().Forget();
			break;
		case F4SE::MessagingInterface::kPreSaveGame:
			fp::Game::Get().ClearForSave();
			break;
		case F4SE::MessagingInterface::kPostLoadGame:
		case F4SE::MessagingInterface::kNewGame:
			fp::Hook::Install();  // no-op if already hooked
			break;
		default:
			break;
		}
	}
}

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = kName;
	a_info->version = 1;

	if (a_f4se->IsEditor()) {
		return false;
	}
	return a_f4se->RuntimeVersion() >= F4SE::RUNTIME_1_10_163;
}

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
	// fo4pool.log is this run; fo4pool.1.log .. .3.log the runs before (so a freeze's log survives the restart)
	F4SE::Init(a_f4se, { .log = true, .logName = kName, .logRotate = 3, .trampoline = false });

	fp::log::info("fo4pool loading on runtime {} ({})", a_f4se->RuntimeVersion().string(),
		REX::FModule::IsRuntimeOG() ? "OG" : REX::FModule::IsRuntimeNG() ? "NG" : "AE");

	auto* messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage)) {
		fp::log::error("could not register F4SE message listener");
		return false;
	}

	// wins and losses go in the co-save
	if (auto* serialization = F4SE::GetSerializationInterface()) {
		serialization->SetUniqueID(0x46504F4C);  // 'FPOL'
		serialization->SetSaveCallback(fp::Game::SaveRecords);
		serialization->SetLoadCallback(fp::Game::LoadRecords);
		serialization->SetRevertCallback(fp::Game::RevertRecords);
	} else {
		fp::log::warn("no F4SE serialization: win/loss records won't be saved");
	}

	fp::log::info("loaded; waiting for game data");
	return true;
}
