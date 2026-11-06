#pragma once

#include "sim/Ai.h"
#include "sim/Rules.h"
#include "sim/Sim.h"

namespace fp
{
	enum class NpcStep
	{
		kNone,      // not their turn, or waiting for the balls to stop
		kWalking,   // on the way to the shot
		kAiming,    // PoolAim
		kStriking,  // PoolStrike, up to the cue reaching the ball
		kFollow,    // the rest of the strike clip
	};

	// One NPC at the table: who they are and where they are in their shot. Game derives from it, so the code that
	// runs an NPC works on these members directly; a game between two NPCs keeps one per seat and swaps the one it's
	// working on in (Game::LoadSeat).
	struct NpcSlot
	{
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
		bool                _npcHeld{ false };   // SetRestrained(true) is on them
		RE::ObjectRefHandle _npcPathTarget;      // where they were last sent walking
		std::uint32_t       _npcPathId{ 0 };     // that walk's id (Anim::PathResult)
		int                 _npcResends{ 0 };    // times that walk was re-sent after stalling
		RE::ObjectRefHandle _npcApproach;        // where they walk to for a shot: clear of the table, by the spot
		bool                _npcAtApproach{ false };  // got there: the last few steps onto the spot
		bool                _npcTurnWarned{ false };  // logged a big turn off the shot this shot
		RE::NiPoint3        _npcWalkFrom;             // where they were when the current walk was sent
		float               _npcGlideSum{ 0.0f };     // GlideOntoLine: the tip's offset from the ball's line, summed over a few frames
		int                 _npcGlideSamples{ 0 };
		float               _npcGlideMin{ 0.0f };     // ...and the spread of those samples (still blending into the aim: wide)
		float               _npcGlideMax{ 0.0f };
		int                 _npcGlideTries{ 0 };
		float               _npcGlideT{ -1.0f };      // seconds into the glide (-1: not started)
		bool                _npcGlideDone{ false };
		RE::NiPoint3        _npcGlideFrom;
		RE::NiPoint3        _npcGlideBy;
		float               _npcGetUpTimer{ 0.0f };   // seconds before trying to get them out of furniture again
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
		int                 _npcWaitVariant{ 0 }; // 0 PoolWait, 1 PoolWait2
		bool                _npcInTableLogged{ false };
		int                 _npcAimVariant{ 1 };  // this shot's aim (1 PoolAim2, 2 PoolAim3; 0 PoolAim no longer picked): its strike follows it
	};

	struct Marker  // a calibration marker (Game::ToggleCalibrate)
	{
		RE::ObjectRefHandle ref;
		pool::Vec2          pos;
	};

	// Everything about one table's game: the table, its balls and physics, the rules, the log and banners, and the
	// NPCs playing at it (the seat being worked on in NpcSlot, the other in _seats). Game derives from it: the table
	// you're at (or watching) is the one in Game itself; up to kMaxTables - 1 others carry on in Game::_others and
	// are swapped in for their tick (Game::TickOthers).
	struct TableSlot : NpcSlot
	{
		RE::ObjectRefHandle                                 _table;
		std::unique_ptr<pool::Sim>                          _sim;
		std::array<RE::ObjectRefHandle, pool::Sim::kBalls> _balls;
		std::array<float, pool::Sim::kBalls>                _dropTime{};
		std::vector<Marker>                                 _markers;
		std::chrono::steady_clock::time_point               _last{};
		bool                                                _haveLast{ false };
		bool                                                _scratch{ false };
		bool                                                _warned3D{ false };
		bool                                                _tableNoCollide{ false };  // the table's collision is off while an NPC shoots
		std::vector<RE::NiAVObject*>                        _frozen;  // ball 3D roots whose Havok we already switched off
		pool::Rules                                         _rules;
		int                                                 _nextBreaker{ 0 };
		int                                                 _gameType{ 0 };   // this table's game: 0 8-ball, 1 9-ball
		bool                                                _shotLive{ false };  // a shot is rolling and will be judged
		float                                               _overTime{ 0.0f };   // seconds since the game ended
		int                                                 _bet{ 0 };           // caps on this game (you against an NPC)
		int                                                 _manualPocket{ -1 }; // pocket picked with the call key
		int                                                 _autoPocket{ -1 };   // where the guide says the 8 is going
		int                                                 _lookPocket{ -1 };
		int                                                 _dwellPocket{ -1 };
		std::chrono::steady_clock::time_point               _dwellSince{};
		std::string                                         _message;
		std::chrono::steady_clock::time_point               _messageTime{};
		std::array<std::string, 2>                          _log;
		int                                                 _banner{ 0 };
		std::string                                         _bannerTitle;
		std::string                                         _bannerSub;
		std::chrono::steady_clock::time_point               _bannerTime{};
		pool::Vec2                       _strikePos;
		std::array<NpcSlot, 2> _seats;
		int                    _seatIdx{ 1 };
		bool                   _npcGame{ false };
		bool                   _spectating{ false };  // F on their table: the HUD shows their game
		bool                   _clearAfterTick{ false };
		bool                _autoAim{ false };   // their shot's done and it's yours: straight into the shot camera
		std::array<float, pool::Sim::kBalls>                _glow{};  // the glow each ball has now (-1: not set yet)
		std::array<RE::NiAVObject*, pool::Sim::kBalls>      _glow3D{}; // the 3D it was set on (a reload needs it again)
		std::vector<RE::TESFormID> _navmeshes;        // navmeshes near the table (NavmeshAt), found by ProbeNavmesh
	};

	// Racks balls on a pool table, runs the physics and moves the balls' 3D, and the shot camera ("aim mode", Aim.cpp).
	// Everything runs on the game's main thread except OnPresent/PollKeys (render thread) and Hud() (UI thread),
	// which only touch the atomics.
	class Game : private TableSlot
	{
	public:
		// Wins and losses, kept in the save (F4SE co-save): every NPC who plays, and you (only games against NPCs; not
		// Solo Play)
		struct Record
		{
			std::uint32_t wins{ 0 };
			std::uint32_t losses{ 0 };
			float         skill{ -1.0f };  // 0-100, NPCs only (grows with every game they play); -1: not set yet
		};
		static float       StartingSkill(RE::TESFormID a_id);  // an NPC's skill before their first game: 10-40
		float              SkillOf(RE::TESFormID a_id) const;  // 0-100
		static const char* SkillName(float a_skill);
		static int         StakeFor(float a_skill);         // caps on a game against them (bBets)
		static int         StyleOf(RE::TESFormID a_id);      // how they play: 0 safe, 1 balanced, 2 aggressive (fixed per NPC)
		static const char* StyleName(int a_style);
		static int         PlayerCaps();
		static bool        BetsWithYou(RE::TESFormID a_id);  // some NPCs only play for fun (fixed per NPC)
		int                StakeOf(RE::Actor* a_actor) const;  // caps on a game against them now (0: for fun)
		std::string        PickLine(RE::Actor* a_actor) const;
		void               SettleBet(int a_winner);           // the game's over: the caps change hands           // BEGINNER, AMATEUR, SEMI-PRO, PRO
		static void SaveRecords(const F4SE::SerializationInterface* a_intfc);
		// the Pool Table Kit (Kit.cpp)
		static void SetUpKit();     // game data ready: the Dugout Inn sells it; listen for it being used
		void        StockKit();     // a save loaded / new game: one in the Dugout Inn's stock now (once a save)
		void        BeginKitPlacement();   // the kit used: the Pip-Boy closes, a table follows where you look
		void        TickKitPlacement();
		void        ConfirmKit();          // F: set it up there
		void        CancelKit(bool a_giveBack);  // X (or a save, a load): the kit back
		void        TurnKit(int a_dir);    // left / right arrows
		std::string KitPrompt() const;
		static bool IsKitPreview(const RE::TESObjectREFR* a_ref);
		std::atomic<bool>   _kitPlacing{ false };
		RE::ObjectRefHandle _kitPreview;
		float               _kitTurn{ 0.0f };     // the player's turn of it (radians) on top of facing you
		float               _kitAngle{ 0.0f };    // where it is now
		RE::NiPoint3        _kitPos;
		bool                _kitOk{ false };      // clear of other tables: F sets it up
		float               _kitMadeAngle{ 0.0f };
		int                 _kitSign{ 0 };        // how the 3D's rotation reads the angle (found from the preview)
		bool                _kitReady{ false };   // preview's collision off and its rotation sign known
		void        PackUpTable(RE::TESObjectREFR* a_table);
		static bool IsKitTable(const RE::TESObjectREFR* a_table);
		static void SaveKit(const F4SE::SerializationInterface* a_intfc);
		static void LoadKit(const F4SE::SerializationInterface* a_intfc, std::uint32_t a_version);
		static void RevertKit();
		static bool IsKitRecord(std::uint32_t a_type);
		static void LoadRecords(const F4SE::SerializationInterface* a_intfc);
		static void RevertRecords(const F4SE::SerializationInterface* a_intfc);
		Record      RecordOf(RE::TESFormID a_id) const;
		void        CountResult(int a_winner);  // the game in play just ended: a win and a loss on the record

		// A loading screen is starting (fast travel, a door, a save loading): every game ends now, while their tables
		// (and the balls, cues and markers on them) are still loaded and can be taken away
		void OnLoadingStarts();
		static void ListenForLoading();

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
			bool  noOverhead{ false };  // against a SEMI-PRO or PRO: no top view
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
			std::string             prompt;           // looking at a pool table, no shot camera: "F) PLAY POOL" etc.
			std::array<std::string, 2> names;         // the players, capitals: you (your character's name) or an NPC
			std::array<std::string, 2> stats;         // "3-1 · PRO" (NPCs), "5-2" (you); empty: no record kept
			bool                    npcGame{ false };  // two NPCs playing each other (you're watching)
			bool                    nine{ false };     // 9-ball
			int                     lowest{ 0 };       // 9-ball: the ball to hit first
			std::vector<std::string> pick;            // F on a table with people by it: who to play (held F: their names)
			int                     pickSel{ 0 };
			std::string             pickHint;         // "UP/DOWN PICK   F PLAY"
			std::string             pickTitle;        // "WHO PLAYS? · 8-BALL"
			int                     activateVk{ 'F' };
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

		using Marker = fp::Marker;

		void PollKeys();
		void PollMouse();
		void Tick();
		// Several tables at once: the one in Game is "loaded" (yours, or the one you're watching); the others are in
		// _others and each gets its tick swapped in (TickOthers). A background tick must leave you alone: _bgTick
		// makes the aim, HUD and UI-sound code skip it. The camera hooks lock _tableMutex, so they never see a table
		// mid-swap.
		static constexpr std::size_t kMaxTables = 4;
		std::vector<TableSlot> _others;
		bool                   _bgTick{ false };
		std::mutex             _tableMutex;
		void TickTable();                              // the loaded table's frame (balls, rules, NPCs)
		void TickOthers();
		void SwapWith(std::size_t a_other);            // _others[a_other] <-> the loaded table
		int  SlotOf(const RE::TESObjectREFR* a_table) const;  // 0 loaded, i + 1 = _others[i], -1 none
		bool Busy(const TableSlot& a_slot) const;      // a game's on there (racked, or NPCs in it)
		std::size_t RunningGames() const;
		bool FocusTable(RE::TESObjectREFR* a_table);   // make a_table the loaded one (an NPC game here goes to the background); false: no room
		bool IsPlaying(const RE::Actor* a_actor) const; // in a game at any table
		std::string SlotNames(const TableSlot& a_slot) const;  // "HEATHER VS PIPER"
		void PlaySounds(const Frame& a_frame, const std::vector<pool::Event>& a_events);

		// the 8-ball game
		void JudgeShot();
		void Say(const std::string& a_message);  // the HUD's message line, and a notification
		void Banner(int a_kind, const std::string& a_title, const std::string& a_sub);
		void UpdateHud();
		int  CalledPocket() const;
		void CyclePocket();
		// X twice: you give up the game in play. Against an NPC it's their win (and, later, a loss on your record);
		// Solo Play just ends
		void Forfeit();
		void StopAllGames();
		bool WithinEarshot() const;  // you're near enough the loaded table to hear its turn and rack sounds  // [Keys] iStopAll: every table's game ends (nothing counted), watchers sent home
		std::chrono::steady_clock::time_point _forfeitAsked{};
		void StartGame();  // the rules side of a rack
		void StartPlacement();
		void ConfirmPlacement();
		void TickPlacement(const Frame& a_frame);
		std::string PlayerName(int a_player) const;  // your name, the NPC's, or "Player 2"
		RE::Actor*  SeatActor(int a_seat) const;     // the NPC in that seat, if it's one

		// the NPC opponent (Opponent.cpp)
		using NpcStep = fp::NpcStep;
		void        Challenge(RE::Actor* a_actor = nullptr, RE::TESObjectREFR* a_table = nullptr);  // a_actor at a_table (racked first if need be)
		void        StartNpcGame(RE::Actor* a_first, RE::Actor* a_second, RE::TESObjectREFR* a_table);  // two NPCs play each other (at the loaded table)
		void        StartNpcGameAt(RE::Actor* a_first, RE::Actor* a_second, RE::TESObjectREFR* a_table);  // ...wherever: the loaded table or one of its own
		bool        TableFree(const RE::TESObjectREFR* a_table) const;  // no game on there (or only a finished one)
		void        EndChallenge(const std::string& a_why);
		RE::Actor*  Opponent() const;
		bool        OpponentsTurn() const;  // Player 2 is the NPC and it's their shot
		RE::Actor*  PickActor(bool a_crosshairOnly = false) const;
		RE::TESObjectREFR* PoolTableNear(const RE::NiPoint3& a_pos, float a_within) const;
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
		std::vector<RE::TESFormID> NavmeshesNear(const RE::NiPoint3& a_pos) const;
		const std::vector<RE::TESFormID>& NavmeshesFor(const RE::TESObjectREFR* a_table, const RE::NiPoint3& a_origin);  // cached per table
		std::unordered_map<RE::TESFormID, std::pair<RE::NiPoint3, std::vector<RE::TESFormID>>> _navCache;
		bool        FrameOf(RE::TESObjectREFR* a_table, Frame& a_out) const;
		// a spot for a watcher to stand, well clear of the table (no shot is taken from there), on navmesh, not
		// another watcher's, nearest a_from; facing the table
		RE::ObjectRefHandle WatchSpot(RE::TESObjectREFR* a_table, const RE::NiPoint3& a_from, const RE::NiPoint3* a_avoid = nullptr);
		void        StepAsideFrom(const RE::NiPoint3& a_shot);  // watchers (and whoever's waiting) by that shot spot move to another spot
		bool        KeepWalking(RE::Actor* a_actor, const RE::ObjectRefHandle& a_target, float a_walkRun, float a_dt, int a_tries);  // true: gave up
		void        OpponentLooksAtPlayer(RE::Actor* a_actor, bool a_look);
		void        ReleaseOpponent();  // everything we set on them undone (before a save, when the game ends)
		void        LogOpponent(RE::Actor* a_actor, float a_dt);  // the status line, once a second and on every change
		void        TickWaiting(RE::Actor* a_actor, const Frame& a_frame, float a_dt);
		RE::NiPoint3 StanceFor(const Frame& a_frame, pool::Vec2 a_cue, pool::Vec2 a_dir, float& a_heading) const;

		void Rack(RE::TESObjectREFR* a_table = nullptr);  // the table under the crosshair, or a_table
		bool RerackInPlace();  // a new game on the table already in use, keeping the balls and aim mode
		void Clear();
		void ToggleCalibrate();
		void ClearMarkers();

		// aim mode (Aim.cpp)
		void  ToggleAim();
		void  EnterAim();
		void  ExitAim();
		void  ToggleOverhead();
		void  QuietNpcs(bool a_quiet);  // while you're playing: NPCs don't come up to say hello or comment
		bool  _npcsQuiet{ false };
		float CeilingAbove(const Frame& a_frame, float a_upTo) const;
		void  SetTableCollision(bool a_on);
		// a point in table space (felt units) and whether it's within a_beyond of the felt; and the nearest point
		// a_beyond out from it if so (else the point itself)
		pool::Vec2   TablePoint(const Frame& a_frame, const RE::NiPoint3& a_pos) const;
		bool         InTable(const Frame& a_frame, const RE::NiPoint3& a_pos, float a_beyond) const;
		RE::NiPoint3 OutOfTable(const Frame& a_frame, const RE::NiPoint3& a_pos, float a_beyond) const;
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
		// The stick in their hands. a_side: how far the tip is to the side of the line through the cue ball's middle
		// (+ = to the line's left), for lining them up.
		bool  PlaceStickInHands(RE::Actor* a_actor, float* a_side = nullptr);
		void  GlideOntoLine(RE::Actor* a_actor, const Frame& a_frame, float a_dt);  // ease them sideways so the cue's on the ball
		bool  PlaceStickUpright(RE::Actor* a_actor);
		bool  PlaceStickCarried(RE::Actor* a_actor);  // walking: in the right hand, nearly upright
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
		float                                               _modelRadius{ 0.0f };
		bool                                                _glowLogged{ false };
		// On the 8: aim at a pocket for 2 s (the 8 heading into it, or the cue ball pointing at it) to call it; look
		// at another for 2 s to change; or F calls it at once
		void                                                TickPocketCall();
		void                                                CallLookedAtPocket();
		bool                                                _overheadBeforePlacing{ false };
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

		// the NPC opponent, main thread: their state is NpcSlot (the base class), one per seat (_seats)
		// Two NPCs playing each other ([Opponent] bNpcGames): both seats are NPCs. The seat being worked on is in the
		// base class (_seatIdx); the other waits in _seats. Against you, it's always seat 1 (you're 0)
		bool                   _rackingNpcGame{ false };  // StartNpcGame's own rack
		std::atomic<bool>      _building{ false };        // the workshop menu is open: nothing of ours runs (no keys either)
		std::chrono::steady_clock::time_point _lastNpcGame{};
		bool                   _inWorkshop{ false };  // build mode is open: everything holds still
		RE::NiPoint3           _lastPlayerPos;
		bool                   _haveLastPlayerPos{ false };
		void                   EndGamesAway(bool a_travelled);  // games you've travelled from or gone 125 m from end (NPC games: a random winner)
		void                   EndThisGameAway(const char* a_why);
		bool                   _quietStart{ false };
		std::atomic<int>       _wantType{ -1 };          // the game you'll rack (-1: [Rules] iGameType); left/right arrows
		std::atomic<bool>      _canSwitchType{ false };  // looking at a free table, or the list is up: the arrows switch it
		int                    _npcRackType{ 0 };        // StartNpcGame's pick
		int                    WantType() const;
		int                    RackType() const { return _rackingNpcGame ? _npcRackType : WantType(); }
		void                   SwitchType();      // a rack that's about to be done again (Challenge): no start sound  // the NPC game ended mid-tick: the table clears once it's done
		void                   LoadSeat(int a_seat);
		int                    OwnSlot() const { return _npcGame ? _seatIdx : 0; }  // the quest alias this NPC walks with
		void                   EndSeat(const std::string& a_why);
		void                   EndNpcGame(const std::string& a_why);
		void                   ForBothSeats(const std::function<void()>& a_do);  // each seat's NPC loaded in turn (NPC games), else the one
		std::mt19937        _npcRng{ std::random_device{}() };
		bool                             _controlsBlocked{ false };

		// shared with the render and UI threads
		std::atomic<bool>          _aiming{ false };
		std::atomic<bool>          _watching{ false };
		std::atomic<bool>          _charging{ false };
		std::atomic<bool>          _picking{ false };  // the who-to-play list is up (arrow keys go to it)
		std::atomic<bool>          _overhead{ false };
		std::atomic<bool>          _noOverhead{ false };  // this aim: no top view (playing a SEMI-PRO or PRO)
		bool                       _ceilingTooLow{ false };  // this aim: no top view because a ceiling is in the way
		std::atomic<bool>          _noGuide{ false };     // this aim: no guide line (playing a PRO)
		std::atomic<float>         _overheadCap{ 100000.0f };  // this table: the top view no higher than this above the felt (a ceiling)
		std::atomic<bool>          _rolling{ false };
		std::atomic<float>         _power{ 0.0f };     // 0..1 while charging
		std::atomic<bool>          _placing{ false };  // ball in hand: the mouse moves the cue ball

		// render thread
		std::array<bool, 9> _keyWasDown{};
		RE::ObjectRefHandle _lookActor;            // an NPC by a pool table under the crosshair: the play key challenges them
		RE::ObjectRefHandle _lookActorTable;       // ...at this table
		// The activate key (F by default; read from the game's controls): on a pool table it racks or takes your shot
		std::atomic<int>    _activateVk{ 'F' };
		bool                _activateWasDown{ false };
		bool                _activateHeld{ false };     // this press already counted as a hold
		std::chrono::steady_clock::time_point _activateDownAt{};
		static constexpr int kHoldMs = 500;
		RE::ObjectRefHandle _lookTable;            // the pool table under the crosshair (UpdateLook), if any
		std::chrono::steady_clock::time_point _lookTime{};
		void                UpdateLook();          // main thread: which table you're looking at, and the prompt

		// Settlers wandering over to hang round a pool table now and then ([Opponent] bWander), so there's someone
		// to ask for a game
		struct Watcher
		{
			RE::TESFormID id{ 0 };
			float         left{ 0.0f };     // seconds before they go back to their own business
			bool          offered{ false };  // asked you for a game already (or decided not to)
			RE::ObjectRefHandle table;       // the table they hang round
			RE::ObjectRefHandle spot;        // where they hang round (WatchSpot)
		};
		// Someone hanging round the table asking you for a game ([Opponent] bOffers): F at that table plays them
		RE::TESFormID                         _offerID{ 0 };
		RE::ObjectRefHandle                   _offerTable;
		std::chrono::steady_clock::time_point _offerUntil{};
		std::chrono::steady_clock::time_point _lastOffer{};
		RE::Actor*                            Offerer(RE::TESObjectREFR* a_table) const;  // still asking, at this table
		// F on a table with people by it: a list, Solo first, to pick who to play (arrow keys, F)
		struct Pick
		{
			RE::ObjectRefHandle              table;
			std::vector<RE::ObjectRefHandle> who;  // [0] empty: Solo
			std::vector<std::string>         names;
			int                              sel{ 0 };
			std::chrono::steady_clock::time_point touched{};
		};
		std::optional<Pick>        _pick;
		std::array<bool, 4>        _arrowWasDown{};
		std::vector<RE::Actor*>    PlayersNear(RE::TESObjectREFR* a_table) const;  // who you could play at it
		void                       MovePick(int a_by);
		void                       ClosePick();
		std::vector<Watcher> _watchers;
		bool                 _watchersStale{ true };  // a load: whoever the collection holds, the plugin doesn't know
		std::chrono::steady_clock::time_point _watchTime{};
		std::vector<RE::ObjectRefHandle> _watchTables;  // the pool tables near you (TickWatchers), nearest first
		std::vector<std::pair<RE::ObjectRefHandle, std::chrono::steady_clock::time_point>> _pairSince;  // tables with two watchers there, since when
		std::vector<std::pair<RE::ObjectRefHandle, std::chrono::steady_clock::time_point>> _aloneSince;  // ...with one, since when
		bool SendWatcher(const RE::ObjectRefHandle& a_table, const char* a_why);  // someone near comes over to a_table
		RE::ObjectRefHandle  _watchNavTable;           // _watchNavmeshes are round this one
		std::vector<RE::TESFormID> _watchNavmeshes;
		float                _watchScan{ 0.0f };
		void                 TickWatchers();
		void                 DropWatcher(RE::TESFormID a_id);
		void                OnActivate(bool a_hold);  // main thread: the activate key tapped (on letting go) or held
		bool                ActorInView(const RE::NiPoint3& a_origin, const RE::NiPoint3& a_dir, float a_upTo) const;
		bool                _lmbWasDown{ false };
		bool                _rmbWasDown{ false };
		std::atomic<bool>   _tickPending{ false };
	};
}
