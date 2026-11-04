#pragma once

#include "sim/Ai.h"
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
			bool  watching{ false };  // playing an NPC and near the table, not aiming: the HUD shows anyway
			bool  charging{ false };
			bool  rolling{ false };
			bool  overhead{ false };
			float power{ 0.0f };

			// the 8-ball game, while a table is racked
			bool                    game{ false };
			bool                    solo{ false };     // Solo Play
			std::string             opponent;          // the NPC playing you (empty: "Player 2")
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
			std::array<std::string, 2> log;           // the last two lines for the terminal log, oldest first
			int                     calledPocket{ -1 };
			// the big centre banner: 0 none, 1 foul (red, blinks), 2 won (green), 3 lost (red)
			int                     banner{ 0 };
			std::string             bannerTitle;
			std::string             bannerSub;
			float                   bannerAge{ 0.0f };
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
		void Banner(int a_kind, const std::string& a_title, const std::string& a_sub);
		void UpdateHud();
		int  CalledPocket() const;
		void CyclePocket();
		void StartGame();  // the rules side of a rack
		void StartPlacement();
		void ConfirmPlacement();
		void TickPlacement(const Frame& a_frame);
		std::string PlayerName(int a_player) const;  // "Player 1", "Player 2", or the opponent's name

		// the NPC opponent (Opponent.cpp)
		enum class NpcStep
		{
			kNone,      // not their turn, or waiting for the balls to stop
			kWalking,   // on the way to the shot
			kAiming,    // PoolAim
			kStriking,  // PoolStrike, up to the cue reaching the ball
			kFollow,    // the rest of the strike clip
		};
		void        Challenge();
		void        EndChallenge(const std::string& a_why);
		RE::Actor*  Opponent() const;
		bool        OpponentsTurn() const;  // Player 2 is the NPC and it's their shot
		RE::Actor*  PickActor() const;
		void        TickOpponent(const Frame& a_frame, float a_dt);
		void        PlanOpponentShot(const Frame& a_frame);
		void        OpponentStrike(const Frame& a_frame);
		void        SetWaitSpot(const Frame& a_frame, const RE::NiPoint3& a_pos);
		void        WaitByTable();  // a wait spot at the table on the side they're on
		void        HoldOpponent(RE::Actor* a_actor, bool a_hold);       // SetRestrained, only when it changes
		void        PathOpponent(RE::Actor* a_actor, const RE::ObjectRefHandle& a_target, float a_walkRun, bool a_force = false);
		// each tick of a walk: re-sends it when they stop getting closer (their sandbox package took over)
		void        KeepFacingShot(RE::Actor* a_actor);                  // lined up: turned back onto the shot every frame
		void        GetOutOfFurniture(RE::Actor* a_actor, float a_dt);   // out of any chair they've taken while playing
		void        ProbeNavmesh(const Frame& a_frame);                 // logs how much navmesh is around the table
		std::string NavmeshAt(const RE::NiPoint3& a_pos) const;         // "on navmesh <id>" / "NO navmesh" (for the log)
		bool        KeepWalking(RE::Actor* a_actor, const RE::ObjectRefHandle& a_target, float a_walkRun, float a_dt, int a_tries);  // true: gave up
		void        OpponentLooksAtPlayer(RE::Actor* a_actor, bool a_look);
		void        ReleaseOpponent();  // everything we set on them undone (before a save, when the game ends)
		void        LogOpponent(RE::Actor* a_actor, float a_dt);  // the status line, once a second and on every change
		void        TickWaiting(RE::Actor* a_actor, const Frame& a_frame, float a_dt);
		RE::NiPoint3 StanceFor(const Frame& a_frame, pool::Vec2 a_cue, pool::Vec2 a_dir, float& a_heading) const;

		void Rack();
		bool RerackInPlace();  // a new game on the table already in use, keeping the balls and aim mode
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
		// a stick object with its tip at a_tip pointing along a_toTip (world, unit), mesh scale a_scale
		void  PlaceStickAlong(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_tip, const RE::NiPoint3& a_toTip, float a_scale);
		static void PrepareStick(RE::TESObjectREFR* a_ref);  // once its 3D is in: hides the blood overlay
		// the opponent's own cue: in their hands on a shot (butt in the right, shaft over the left bridge), upright with
		// its butt on the floor while they wait (in the right hand); false = no hands found
		bool  PlaceStickInHands(RE::Actor* a_actor);
		bool  PlaceStickUpright(RE::Actor* a_actor);
		void  HideOpponentStick();
		void  HideAimObjects();
		void  RemoveAimObjects();
		void  BlockControls(bool a_block);
		void  HoldPlayerView();  // keeps the player facing where they did when aiming started
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
		void                MeasureBall(RE::NiAVObject* a_root);  // once per rack, from a real ball
		void                UpdateGlow();                          // your balls' glow, when it changes
		void                SetGlow(RE::NiAVObject* a_root, float a_strength);
		void                PlaceNode(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos, const RE::NiMatrix3& a_rot, float a_scale, bool a_hidden);
		bool                IsOurs(const RE::TESObjectREFR* a_ref) const;
		static std::string  ModelPath(const RE::TESObjectREFR* a_ref);
		static void         LogTable(RE::TESObjectREFR* a_table, const Frame& a_frame);
		static bool         IsPoolTable(const RE::TESObjectREFR* a_ref);
		static bool         IsGamePaused();
		static void         Notify(const std::string& a_message);  // always: problems and instructions
		static void         Tell(const std::string& a_message);    // only without the HUD, or with bNotifications=1

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
		std::array<float, pool::Sim::kBalls>                _glow{};  // the glow each ball has now (-1: not set yet)
		std::array<RE::NiAVObject*, pool::Sim::kBalls>      _glow3D{}; // the 3D it was set on (a reload needs it again)
		bool                                                _glowLogged{ false };
		pool::Rules                                         _rules;
		int                                                 _nextBreaker{ 0 };
		bool                                                _shotLive{ false };  // a shot is rolling and will be judged
		float                                               _overTime{ 0.0f };   // seconds since the game ended
		int                                                 _manualPocket{ -1 }; // pocket picked with the call key
		int                                                 _autoPocket{ -1 };   // where the guide says the 8 is going
		bool                                                _overheadBeforePlacing{ false };
		std::string                                         _message;
		std::chrono::steady_clock::time_point               _messageTime{};
		std::array<std::string, 2>                          _log;
		int                                                 _banner{ 0 };
		std::string                                         _bannerTitle;
		std::string                                         _bannerSub;
		std::chrono::steady_clock::time_point               _bannerTime{};
		mutable std::mutex                                  _hudMutex;
		HudState                                            _hud;  // under _hudMutex

		// aim mode, main thread
		bool                             _camMapped{ false };  // worked out which free-camera value is yaw
		int                              _mapFrames{ 0 };
		int                              _yawIndex{ 1 };
		float                            _pitchSign{ 1.0f };
		float                            _entryYaw{ 0.0f };
		float                            _entryPitch{ 0.0f };
		RE::NiPoint2                     _entryFreeRotation{};  // third person's free-look offset
		bool                             _haveEntryView{ false };
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

		// the NPC opponent, main thread
		RE::ObjectRefHandle _opponent;     // empty: two players at one keyboard (or Solo Play)
		std::string         _opponentName;
		NpcStep             _npcStep{ NpcStep::kNone };
		float               _npcTimer{ 0.0f };
		pool::AiShot        _npcShot;
		RE::NiPoint3        _npcStance;
		float               _npcHeading{ 0.0f };
		RE::ObjectRefHandle _npcMarker;    // an XMarkerHeading at the shot, for PathToReference / MoveTo
		RE::ObjectRefHandle _npcWaitMarker; // where they wait for their turn, by the table
		float               _npcWaitHeading{ 0.0f };
		bool                _npcFacing{ false }; // turned to the table since arriving at the wait spot
		bool                _autoAim{ false };   // their shot's done and it's yours: straight into the shot camera
		bool                _npcHeld{ false };   // SetRestrained(true) is on them
		RE::ObjectRefHandle _npcPathTarget;      // where they were last sent walking
		std::uint32_t       _npcPathId{ 0 };     // that walk's id (Anim::PathResult)
		int                 _npcResends{ 0 };    // times that walk was re-sent after stalling
		RE::ObjectRefHandle _npcApproach;        // where they walk to for a shot: clear of the table, by the spot
		bool                _npcAtApproach{ false };  // got there: the last few steps onto the spot
		bool                _npcTurnWarned{ false };  // logged a big turn off the shot this shot
		float               _npcGetUpTimer{ 0.0f };   // seconds before trying to get them out of furniture again
		std::vector<RE::TESFormID> _navmeshes;        // navmeshes near the table (NavmeshAt), found by ProbeNavmesh
		float               _npcUnheldAge{ 10.0f };   // seconds since SetRestrained came off (walks wait for it)
		float               _npcPathAge{ 0.0f }; // seconds since
		bool                _npcReleasedForOver{ false };  // the game's over: we let them go until the next rack
		float               _npcLogTimer{ 0.0f };
		std::string         _npcLogLast;          // the last line's state part, to log changes at once
		RE::NiPoint3        _npcHeldAt;           // where they were when the hold went on
		RE::NiPoint3        _npcLastPos;          // for their speed
		bool                _npcMovedWarned{ false };
		RE::ObjectRefHandle _npcStick;            // their own cue (yours is _stick), so you can both hold one
		bool                _npcStickReady{ false };
		bool                _npcWaitIdle{ false }; // PoolWait is playing
		float               _npcWaitWalk{ 0.0f };  // seconds they've been on their way to the wait spot
		float               _npcBestDist{ 0.0f };  // closest they've got to where they're walking
		float               _npcStall{ 0.0f };     // seconds without getting closer
		bool                _npcLooking{ false }; // SetLookAt(player) is on them
		std::mt19937        _npcRng{ std::random_device{}() };
		bool                             _controlsBlocked{ false };

		// shared with the render and UI threads
		std::atomic<bool>          _aiming{ false };
		std::atomic<bool>          _watching{ false };
		std::atomic<bool>          _charging{ false };
		std::atomic<bool>          _overhead{ false };
		std::atomic<bool>          _rolling{ false };
		std::atomic<float>         _power{ 0.0f };     // 0..1 while charging
		std::atomic<bool>          _placing{ false };  // ball in hand: the mouse moves the cue ball

		// render thread
		std::array<bool, 6> _keyWasDown{};
		bool                _lmbWasDown{ false };
		bool                _rmbWasDown{ false };
		std::atomic<bool>   _tickPending{ false };
	};
}
