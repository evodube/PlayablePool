#pragma once

#include "sim/Sim.h"

namespace fp
{
	// Test build: rack balls on a table with a hotkey, shoot the cue ball where you look, watch the physics.
	// Everything except OnPresent runs on the game's main thread.
	class Game
	{
	public:
		static Game& Get()
		{
			static Game instance;
			return instance;
		}

		// Render thread, every frame: reads the hotkeys and queues the main-thread tick.
		void OnPresent();

		// A save is about to load: the balls go away with the old game, so just forget them.
		void Forget();
		// A save is about to be written: take the balls off so they never end up in a save.
		void ClearForSave();

	private:
		struct Frame  // the table's felt in the world
		{
			RE::NiMatrix3 rot;        // world rotation of the table, times the length-axis swap
			RE::NiMatrix3 tableRot;   // world rotation of the table alone
			RE::NiPoint3  origin;     // the table's world position
			float         scale{ 1.0f };
		};

		struct Marker
		{
			RE::ObjectRefHandle ref;
			pool::Vec2          pos;
		};

		void PollKeys();
		void Tick();

		void Rack();
		void Shoot(float a_power);
		void Clear();
		void ToggleCalibrate();
		void ClearMarkers();

		RE::TESObjectREFR*  PickTable() const;
		RE::TESBoundObject* BallForm(int a_number) const;
		void                FreezePhysics(RE::TESObjectREFR* a_ref);
		bool                GetFrame(Frame& a_out) const;
		RE::NiPoint3        ToWorld(const Frame& a_frame, pool::Vec2 a_pos, float a_height) const;
		RE::ObjectRefHandle Spawn(RE::TESBoundObject* a_form, RE::TESObjectREFR* a_table, const RE::NiPoint3& a_pos) const;
		float               BallScale(RE::NiAVObject* a_root);
		void                PlaceNode(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos, const RE::NiMatrix3& a_rot, float a_scale, bool a_hidden);
		bool                IsOurs(const RE::TESObjectREFR* a_ref) const;
		static std::string  ModelPath(const RE::TESObjectREFR* a_ref);
		static void         LogTable(RE::TESObjectREFR* a_table, const Frame& a_frame);
		static bool         IsPoolTable(const RE::TESObjectREFR* a_ref);
		static bool         IsGamePaused();
		static void         Notify(const std::string& a_message);

		// main thread
		RE::ObjectRefHandle                        _table;
		std::unique_ptr<pool::Sim>                 _sim;
		std::array<RE::ObjectRefHandle, pool::Sim::kBalls> _balls;
		std::array<float, pool::Sim::kBalls>       _dropTime{};
		std::vector<Marker>                        _markers;
		float                                      _modelRadius{ 0.0f };
		std::chrono::steady_clock::time_point      _last{};
		bool                                       _haveLast{ false };
		bool                                       _scratch{ false };
		bool                                       _warned3D{ false };
		std::vector<RE::NiAVObject*>               _frozen;  // ball 3D roots whose Havok we already switched off

		// render thread
		std::array<bool, 4> _keyWasDown{};
		std::atomic<bool>   _tickPending{ false };
	};
}
