#pragma once

#include "sim/Sim.h"

namespace fp
{
	// Data\F4SE\Plugins\fo4pool.ini. Read at startup and again on every rack or calibrate, so edits apply without
	// restarting the game.
	struct Settings
	{
		static Settings& Get()
		{
			static Settings instance;
			return instance;
		}

		void Load();

		// a key's name for messages ("F7", "Numpad 7")
		static std::string Key(int a_vk);

		// [Keys] virtual-key codes
		int keyRack{ 0x67 };       // Numpad 7
		int   keyPlay{ 0 };                 // off; a key here: look at an NPC by a pool table and press it to play them
		float playDistance{ 300.0f };       // ...close enough to talk to
		float playNearTable{ 700.0f };      // ...and this close to a pool table
		bool  activateTable{ true };        // the game's activate key (F) on a pool table racks / takes your shot
		float activateDistance{ 260.0f };   // ...within this far
		int keyShoot{ 0x68 };      // Numpad 8 (Shift = soft, Ctrl = hard)
		int keyClear{ 0x69 };      // Numpad 9
		int keyCalibrate{ 0x64 };  // Numpad 4
		int keyCallPocket{ 0x65 }; // Numpad 5: pick the pocket for the 8
		int keyChallenge{ 0x66 };  // Numpad 6: challenge the NPC you're looking at (again: they stop playing)
		int keyForfeit{ 0x58 };    // X (twice): give up the game in play
		int keyTables{ 0x6F };     // Numpad /: Playable Pool off at every table (no prompts, F is the game's), or back on
		int keyStopAll{ 0x60 };    // Numpad 0: every game at every table stops (no results), watchers go home

		// [Rules] 8-ball
		int   gameType{ 0 };        // 0 = 8-ball, 1 = 9-ball (what F starts; left/right at a table changes it)
		int   gameMode{ 1 };        // 0 = 8-Ball (two players), 1 = Solo Play (the default; a challenged NPC always plays 8-ball)
		bool  callEight{ true };    // the 8 must go in the called pocket (the one marked on the table)
		float placeSpeed{ 0.06f };  // ball in hand: how far the cue ball moves per mouse count

		// [Ball] base objects. By default fo4pool.esl: the cue ball at 0x800 and ball n at 0x800 + n.
		std::string                   ballPlugin{ "fo4pool.esl" };
		std::uint32_t                 cueFormID{ 0x800 };
		std::uint32_t                 ballFormID{ 0 };   // nonzero: every numbered ball uses this one object
		std::array<std::uint32_t, 16> numberFormIDs{};  // [1..15]; nonzero overrides one ball

		std::uint32_t FormIDFor(int a_number) const
		{
			if (a_number == 0) {
				return cueFormID;
			}
			if (numberFormIDs[a_number]) {
				return numberFormIDs[a_number];
			}
			return ballFormID ? ballFormID : cueFormID + static_cast<std::uint32_t>(a_number);
		}
		bool          ballAutoScale{ true };  // scale the mesh so its size matches fRadius
		float         ballScale{ 1.0f };      // used when bAutoScale=0
		float         ballZOffset{ 0.0f };    // if the mesh's centre isn't the ball's centre
		bool          glowOwn{ false };       // off by default. On: your group's balls glow a little (in hotseat: the shooter's)
		float         glowStrength{ 0.6f };   // how much

		// [Shot] hold the left mouse button (the camera freezes), pull the mouse back for power, release to shoot
		float minSpeed{ 28.0f };      // cue ball speed at the smallest power, units per second
		float maxSpeed{ 462.0f };     // at 100% (401.5 + 15% in 0.9.12; 365 + 10% in 0.8.1)
		float pullPerDegree{ 0.04f }; // power gained per degree the mouse tries to tilt the view (0.04 = full at 25)
		bool  invertPull{ false };    // true: push the mouse forward for power instead

		// [Controller]
		bool  padEnabled{ true };      // read an Xbox-style controller (XInput)
		int   padPowerMode{ 0 };       // 0: hold RT and pull the right stick back; 1: squeeze RT
		float padFullPull{ 0.9f };     // how far the stick (or trigger) goes for full power, 0..1
		bool  padInvertPull{ false };  // true: push the right stick forward for power instead
		float padPlaceSpeed{ 900.0f }; // ball in hand: how fast the left stick moves the cue ball
		bool  padRumble{ true };       // a short rumble when the cue strikes

		// [Camera] the shot camera orbits the cue ball
		float camDistance{ 75.0f };
		float camPitch{ 20.0f };      // degrees below horizontal when aiming starts
		float camMinPitch{ 4.0f };
		float camMaxPitch{ 80.0f };
		float overheadHeight{ 240.0f };

		// [Guide]
		bool  guide{ true };
		float guideDotRadius{ 0.45f };
		float guideSpacing{ 5.0f };
		float guideMaxLength{ 400.0f };  // cue ball path before the first contact
		float guideObjectLength{ 45.0f }; // the hit ball's path after it
		float guideCueLength{ 25.0f };    // the cue ball's path after it (scaled by how much speed it keeps)

		// [Stick] the cue stick. Off until iFormID is set.
		std::string   stickPlugin{ "fo4pool.esl" };
		std::uint32_t stickFormID{ 0x810 };  // a static made from the vanilla Weapons\PoolCue\PoolCue.nif
		int           stickAxis{ 1 };        // mesh axis the stick runs along: 0 X, 1 Y, 2 Z
		float         stickTip{ 83.0f };     // tip's coordinate on that axis
		float         stickButt{ -11.0f };   // butt's coordinate on that axis
		float         stickElevation{ 6.0f };  // degrees the butt is raised
		float         stickMaxPull{ 18.0f };   // how far it draws back at full power
		float         stickGap{ 1.5f };        // gap between tip and cue ball at rest

		// [Opponent] an NPC playing you at 8-ball
		float opponentSkill{ 0.6f };        // 0 = hopeless, 1 = never misses its aim
		float opponentAimSeconds{ 2.5f };   // how long they line the shot up (PoolAim) before striking
		float opponentPickDistance{ 600.0f }; // how far away the NPC you challenge can be
		float opponentWalkTimeout{ 15.0f }; // a last resort: seconds of failed walking before they're put at the shot
		bool  opponentRun{ false };         // run to the shot instead of walking
		bool  opponentLog{ true };          // a status line about them in fo4pool.log every second
		bool  offers{ true };               // a settler hanging round the table asks you for a game now and then
		float offerEvery{ 180.0f };         // ...at most this often (seconds)
		float offerWait{ 30.0f };           // ...and waits this long for you to press F at the table
		float carryTilt{ 50.0f };           // the cue they carry while walking: degrees up from level, tip forward
		float carryGrip{ 0.3f };            // ...held this far up from the butt (0..1)
		float carryIn{ 1.2f };              // ...this far in from the finger joint, on the palm side
		float carryOut{ 0.12f };            // ...tip leaning out to the side this much (keeps it off the body)
		bool  npcGames{ true };             // two settlers hanging round a table now and then play each other
		float npcGameChance{ 100.0f };      // ...the chance (%) that two at a table start one (a miss: again a minute later)
		float npcGameDelay{ 3.0f };         // ...this long (seconds) after the second gets there
		float watchDistance{ 110.0f };      // watchers stand this far out from the felt's edge
		bool  bets{ true };                 // games against NPCs are played for caps (by their skill: 25 / 50 / 150 / 300)
		bool  wander{ true };               // settlers wander over to a pool table near you now and then
		float wanderEvery{ 45.0f };         // about this often (seconds)
		int   wanderMax{ 3 };               // at most this many at once
		int   wanderWorkers{ 1 };           // settlers with a settlement job: 0 never come over, 1 only off duty, 2 any time
		float wanderStay{ 180.0f };         // each hangs round about this long
		float wanderRange{ 2500.0f };       // from this far from the table
		bool  opponentOwnPackage{ true };   // walk them with fo4pool.esl's own AI package (outranks settler sandboxes); off = PathToReference

		// [Anim] the pool idles (fo4pool.esl) the opponent plays
		bool          anim{ true };
		std::string   animPlugin{ "fo4pool.esl" };
		std::uint32_t animAimFormID{ 0x811 };     // PoolAim: lined up, practice strokes, loops
		std::uint32_t animStrikeFormID{ 0x812 };  // PoolStrike: the shot
		std::uint32_t animWaitFormID{ 0x813 };    // PoolWait: standing at the table with the cue, between shots
		std::uint32_t animAim2FormID{ 0x819 };    // PoolAim2: another aim (each shot picks one of the two)
		std::uint32_t animWait2FormID{ 0x81A };   // PoolWait2: another wait (picked each time they settle)
		std::uint32_t animStrike2FormID{ 0x81B }; // PoolStrike2: the strike from PoolAim2's stance
		std::uint32_t animAim3FormID{ 0x81C };    // PoolAim3: a third aim
		std::uint32_t animStrike3FormID{ 0x81D }; // PoolStrike3: the strike from PoolAim3's stance

		// [HUD] the terminal HUD (needs F4SE Menu Framework)
		float hudScale{ 1.0f };  // on top of the automatic scale (screen height / 720)
		bool  notifications{ false };  // also show calls ("3 in", "Player 2's shot") as the game's corner messages

		// [Sound] Data\Sound\fo4pool\*.wav, played by the plugin itself
		bool  sound{ true };
		float soundVolume{ 0.8f };        // 0..1, on top of how hard each hit was
		float soundFullDistance{ 150.0f }; // full volume within this distance of the camera
		float soundMaxDistance{ 700.0f };  // silent beyond this (10 m); the turn and rack sounds too
		bool  soundReturn{ true };         // the rolling-away sound after a ball drops
		int   returnEnd{ 1 };              // which end the ball return is: 1 the foot (rack) end, -1 the head end

		// [Table] the felt, in the table mesh's own space (before the table's scale). The defaults are measured
		// from the vanilla SetDressing\PoolTable\PoolTable01.nif.
		bool  useConsolePick{ true };  // the object clicked in the console wins over the crosshair
		float aimDistance{ 400.0f };
		int   lengthAxis{ 1 };         // 0: the table's length runs along its mesh X axis, 1: along Y
		float offsetX{ 0.0f };         // centre of the felt relative to the mesh origin
		float offsetY{ 0.0f };
		float surfaceZ{ 70.4f };       // height of the felt above the mesh origin

		pool::TableSpec spec{
			.length = 224.0,
			.width = 112.0,
			.ballRadius = 3.51,
			.cornerMouth = 12.4,
			.sideMouth = 17.0,
			.rollDecel = 17.5,
			.ballRestitution = 0.75,
			.cushionRestitution = 0.75,
			.stopSpeed = 0.7,
			.pocketDepth = 0.7,
			.jawLength = 1.3,
			.cornerJawAngle = 15.0,
			.sideJawAngle = 0.0
		};
	};
}
