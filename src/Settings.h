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

		// [Keys] virtual-key codes
		int keyRack{ 0x67 };       // Numpad 7
		int keyShoot{ 0x68 };      // Numpad 8 (Shift = soft, Ctrl = hard)
		int keyClear{ 0x69 };      // Numpad 9
		int keyCalibrate{ 0x64 };  // Numpad 4

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

		// [Shot]
		float shotSpeed{ 880.0f };  // units per second

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
			.sideMouth = 20.8,
			.rollDecel = 17.5,
			.ballRestitution = 0.95,
			.cushionRestitution = 0.75,
			.stopSpeed = 0.7
		};
	};
}
