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
		int keyShoot{ 0x68 };      // Numpad 8 (Shift = soft, Ctrl = hard)
		int keyClear{ 0x69 };      // Numpad 9
		int keyCalibrate{ 0x64 };  // Numpad 4
		int keyCallPocket{ 0x65 }; // Numpad 5: pick the pocket for the 8

		// [Rules] 8-ball
		int   gameMode{ 0 };        // 0 = 8-Ball (two players), 1 = Solo Play
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

		// [Shot] hold the left mouse button (the camera freezes), pull the mouse back for power, release to shoot
		float minSpeed{ 28.0f };      // cue ball speed at the smallest power, units per second
		float maxSpeed{ 365.0f };     // at 100% (what 50% was up to 0.3.2)
		float pullPerDegree{ 0.04f }; // power gained per degree the mouse tries to tilt the view (0.04 = full at 25)
		bool  invertPull{ false };    // true: push the mouse forward for power instead

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

		// [HUD] the terminal HUD (needs F4SE Menu Framework)
		float hudScale{ 1.0f };  // on top of the automatic scale (screen height / 720)
		bool  notifications{ false };  // also show calls ("3 in", "Player 2's shot") as the game's corner messages

		// [Sound] Data\Sound\fo4pool\*.wav, played by the plugin itself
		bool  sound{ true };
		float soundVolume{ 0.8f };        // 0..1, on top of how hard each hit was
		float soundFullDistance{ 150.0f }; // full volume within this distance of the camera
		float soundMaxDistance{ 3000.0f }; // silent beyond this
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
