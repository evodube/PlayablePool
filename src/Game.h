#pragma once

#include "sim/Rules.h"
#include "sim/Sim.h"

namespace fp
{
	// Racks balls on a pool table, runs the physics and moves the balls' 3D, and the shot camera ("aim mode", Aim.cpp).
	// Everything runs on the game's main thread except OnPresent/PollKeys (render thread) and Hud() (UI thread),
	// which only touch the atomics.
	class Game
	{
	public:
		static Game& Get()
		{
			static Game instance;
			return instance;
		}

		// Render thread, every frame: reads the hotkeys and mouse buttons and queues the main-thread tick.
		void OnPresent();

		// A save is about to load: the balls go away with the old game, so just forget them.
		void Forget();
		// A save is about to be written: take the balls off so they never end up in a save.
		void ClearForSave();

		// What the HUD shows. Safe from any thread.
		struct HudState
		{
			bool  aiming{ false };
			bool  charging{ false };
			bool  rolling{ false };
			bool  overhead{ false };
			float power{ 0.0f };

			// the 8-ball game, while a table is racked
			bool                    game{ false };
			int                     shooter{ 0 };
			std::array<int, 2>      group{};        // 0 open table, 1 solids, 2 stripes
			std::array<int, 2>      remaining{};
			std::array<bool, 16>    onTable{};      // which balls are still on the table
			bool                    breakShot{ false };
			bool                    onEight{ false };
			bool                    calling{ false };  // the shooter is on the 8 and calls a pocket
			bool                    ballInHand{ false };
			bool                    placing{ false };
			bool                    over{ false };
			int                     winner{ -1 };
			std::string             message;          // the latest thing that happened ("Foul: scratch", ...)
			float                   messageAge{ 0.0f };  // seconds since it
		};
		HudState Hud() const;

		// Mouse movement, from the player's look handler or the free camera's (CameraHook.cpp). While charging it
		// becomes power and true is returned: the hook then drops it, so the view can't turn.
		bool OnLookMouse(const RE::MouseMoveEvent* a_event);
		// Before the free camera update: while charging, empties the queued look input and the look spring.
		void BeforeFreeCameraUpdate(RE::FreeCameraState* a_free);
		// Right after the game's free camera code turned the view: keeps the pitch in range and notes how far it
		// tilted (to learn the mouse sensitivity). While charging it puts the angles back to a_before instead.
		void OnFreeCameraLook(RE::FreeCameraState* a_free, const RE::BSTPoint2<float>& a_before);
		// Right after the game's free camera update: moves the camera, the cue stick and the guide for this frame.
		void OnFreeCameraUpdate(RE::FreeCameraState* a_free);
		// The camera position while aiming, from the camera's angles right now; false = leave the game's own.
		bool FreeCameraTranslation(const RE::FreeCameraState* a_free, RE::NiPoint3& a_out);

	private:
		// The game stores a node's rotation with the object's axes as the ROWS of the matrix: a point p in the
		// object's own space is at translate + scale * (rotate.Transpose() * p) in the world, and rotate * v turns a
		// world direction back into the object's space. Ball rotations are built the same way (rows = axes).
		struct Frame  // the table's felt in the world
		{
			RE::NiMatrix3 rot;        // the table's rotation with the length-axis swap: rows = table x, y, z in the world
			RE::NiMatrix3 tableRot;   // the table node's own world rotation
			RE::NiPoint3  origin;     // the table's world position
			float         scale{ 1.0f };
		};

		struct Marker
		{
			RE::ObjectRefHandle ref;
			pool::Vec2          pos;
		};

		void PollKeys();
		void PollMouse();
		void Tick();
		void PlaySounds(const Frame& a_frame, const std::vector<pool::Event>& a_events);

		// the 8-ball game
		void JudgeShot();
		void Say(const std::string& a_message);  // the HUD's message line, and a notification
		void UpdateHud();
		int  CalledPocket() const;
		void CyclePocket();
		void StartPlacement();
		void ConfirmPlacement();
		void TickPlacement(const Frame& a_frame);
		static std::string PlayerName(int a_player) { return a_player == 0 ? "Player 1" : "Player 2"; }

		void Rack();
		void Clear();
		void ToggleCalibrate();
		void ClearMarkers();

		// aim mode (Aim.cpp)
		void  ToggleAim();
		void  EnterAim();
		void  ExitAim();
		void  ToggleOverhead();
		void  ShootAim(float a_power);
		void  TickAim(const Frame& a_frame, float a_dt);
		void  PlaceAimObjects(const Frame& a_frame, float a_yaw, float a_dt);
		bool  CamTranslation(const RE::FreeCameraState* a_free, RE::NiPoint3& a_out) const;  // _camMutex held
		void  PlaceGuide(const Frame& a_frame, pool::Vec2 a_dir);
		void  PlaceStick(const Frame& a_frame, pool::Vec2 a_cue, pool::Vec2 a_dir, float a_pull, bool a_show);
		void  HideAimObjects();
		void  RemoveAimObjects();
		void  BlockControls(bool a_block);
		float PowerNow() const;
		void  AddPull(double a_degrees);  // _camMutex held
		static void LogShape(RE::TESObjectREFR* a_ref, const char* a_what);

		RE::TESObjectREFR*  PickTable() const;
		RE::TESBoundObject* BallForm(int a_number) const;
		RE::TESBoundObject* StickForm() const;
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
		RE::ObjectRefHandle                                 _table;
		std::unique_ptr<pool::Sim>                          _sim;
		std::array<RE::ObjectRefHandle, pool::Sim::kBalls> _balls;
		std::array<float, pool::Sim::kBalls>                _dropTime{};
		std::vector<Marker>                                 _markers;
		float                                               _modelRadius{ 0.0f };
		std::chrono::steady_clock::time_point               _last{};
		bool                                                _haveLast{ false };
		bool                                                _scratch{ false };
		bool                                                _warned3D{ false };
		std::vector<RE::NiAVObject*>                        _frozen;  // ball 3D roots whose Havok we already switched off
		pool::Rules                                         _rules;
		int                                                 _nextBreaker{ 0 };
		bool                                                _shotLive{ false };  // a shot is rolling and will be judged
		int                                                 _manualPocket{ -1 }; // pocket picked with the call key
		int                                                 _autoPocket{ -1 };   // where the guide says the 8 is going
		bool                                                _overheadBeforePlacing{ false };
		std::string                                         _message;
		std::chrono::steady_clock::time_point               _messageTime{};
		mutable std::mutex                                  _hudMutex;
		HudState                                            _hud;  // under _hudMutex

		// aim mode, main thread
		bool                             _camMapped{ false };  // worked out which free-camera value is yaw
		int                              _mapFrames{ 0 };
		int                              _yawIndex{ 1 };
		float                            _pitchSign{ 1.0f };
		float                            _entryYaw{ 0.0f };
		float                            _entryPitch{ 0.0f };
		RE::NiPoint3                     _camTarget;
		bool                             _haveCamTarget{ false };
		float                            _strikeShow{ 0.0f };  // the stick stays visible this much longer after a shot
		pool::Vec2                       _lastDir{ 1.0, 0.0 };
		pool::Vec2                       _strikePos;
		std::vector<RE::ObjectRefHandle> _dots;
		RE::ObjectRefHandle              _stick;
		bool                             _stickLogged{ false };
		bool                             _stickWarned{ false };
		// mouse sensitivity, learned while aiming: degrees the view tilts per vertical mouse count
		double                           _sumAbsCounts{ 0.0 };
		double                           _sumAbsTilt{ 0.0 };
		double                           _sumCross{ 0.0 };   // sign: does moving the mouse back tilt the view down
		double                           _frameCounts{ 0.0 };
		double                           _frameTilt{ 0.0 };
		const void*                      _lastMouseEvent{ nullptr };
		std::uint32_t                    _lastMouseTime{ 0 };
		double                           DegreesPerCount() const;
		int                              _chargeMouseEvents{ 0 };
		int                              _chargeInputs{ 0 };
		std::size_t                      _dotsWanted{ 0 };  // guide dots the camera update needed; the tick spawns them
		std::chrono::steady_clock::time_point _lastCamUpdate{};
		bool                             _haveCamUpdate{ false };
		RE::NiPoint3                     _camDesired;  // where the tick says the camera should orbit
		bool                             _haveCamDesired{ false };
		bool                             _camFollow{ true };
		double                           _placeDX{ 0.0 };  // mouse counts waiting to move the cue ball (ball in hand)
		double                           _placeDY{ 0.0 };
		std::mutex                       _camMutex;  // camera update thread vs the tick
		float                            _camYaw{ 0.0f };
		RE::BSTSmartPointer<RE::BSInputEnableLayer> _inputLayer;
		bool                             _controlsBlocked{ false };

		// shared with the render and UI threads
		std::atomic<bool>          _aiming{ false };
		std::atomic<bool>          _charging{ false };
		std::atomic<bool>          _overhead{ false };
		std::atomic<bool>          _rolling{ false };
		std::atomic<float>         _power{ 0.0f };     // 0..1 while charging
		std::atomic<bool>          _placing{ false };  // ball in hand: the mouse moves the cue ball

		// render thread
		std::array<bool, 5> _keyWasDown{};
		bool                _lmbWasDown{ false };
		bool                _rmbWasDown{ false };
		std::atomic<bool>   _tickPending{ false };
	};
}
