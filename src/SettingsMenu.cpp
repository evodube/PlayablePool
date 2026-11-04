// Every fo4pool.ini setting on pages in F4SE Menu Framework's menu (fo4pool > HUD, Shot & camera, ...).
//
// One table below drives it: each row is an INI key, the Settings field it sets, a label, a range and a line of
// help. Changes apply straight away (except the table, ball and physics ones, which the next rack picks up) and
// are written to the INI as soon as they're made, so a rack (which re-reads the INI) keeps them.

#include "SettingsMenu.h"

#include "F4SEMenuFramework.h"
#include "Settings.h"

namespace fp::SettingsMenu
{
	namespace
	{
		using namespace ImGuiMCP;

		constexpr auto kIniPath = L"Data\\F4SE\\Plugins\\fo4pool.ini";

		enum class Kind
		{
			kFloat,   // float*
			kDouble,  // double* (the table spec)
			kBool,    // bool*
			kInt,     // int*
			kHex,     // std::uint32_t*, a form ID
			kChoice,  // int*, picked from `choices`
			kKey,     // int*, a virtual-key code, rebound by pressing it
			kText,    // std::string*
		};

		struct Choice
		{
			const char* label;
			int         value;
		};

		struct Entry
		{
			const wchar_t*          section;
			const wchar_t*          key;
			const char*             label;
			Kind                    kind;
			void*                   field;
			float                   min{ 0.0f };
			float                   max{ 1.0f };
			const char*             format{ "%.2f" };
			const char*             help{ nullptr };
			std::vector<Choice>     choices{};
			bool                    nextRack{ false };  // only takes effect on the next rack
		};

		struct Page
		{
			const char*        name;
			const char*        intro;
			std::vector<Entry> entries;
		};

		Settings& S() { return Settings::Get(); }

		std::vector<Page>& Pages()
		{
			auto& s = S();
			static std::vector<Page> pages{
				{ "HUD", "The HUD sizes itself to your screen height; the scale makes it bigger or smaller on top of that. Rack a table to see it while you change it.",
					{
						{ L"HUD", L"fScale", "HUD scale", Kind::kFloat, &s.hudScale, 0.3f, 3.0f, "%.2fx", "1.00 = the normal size for your screen" },
						{ L"HUD", L"bNotifications", "Corner messages too", Kind::kBool, &s.notifications, 0, 0, nullptr,
							"also show every call as the game's corner notifications (they queue up and keep coming after a game)" },
					} },
				{ "Shot & camera", "How hard shots go, how pulling the mouse turns into power, and where the shot camera sits.",
					{
						{ L"Shot", L"fMinSpeed", "Softest shot", Kind::kFloat, &s.minSpeed, 5.0f, 200.0f, "%.0f", "cue ball speed at the lowest power (units per second)" },
						{ L"Shot", L"fMaxSpeed", "Hardest shot", Kind::kFloat, &s.maxSpeed, 100.0f, 1500.0f, "%.0f", "cue ball speed at 100% power" },
						{ L"Shot", L"fPullPerDegree", "Pull sensitivity", Kind::kFloat, &s.pullPerDegree, 0.005f, 0.2f, "%.3f",
							"power gained per degree of mouse pull: 0.04 = full power after a 25 degree pull" },
						{ L"Shot", L"bInvertPull", "Push forward for power", Kind::kBool, &s.invertPull, 0, 0, nullptr, "instead of pulling the mouse back" },
						{ L"Camera", L"fDistance", "Camera distance", Kind::kFloat, &s.camDistance, 20.0f, 300.0f, "%.0f", "how far behind the cue ball the shot camera sits" },
						{ L"Camera", L"fPitch", "Starting angle", Kind::kFloat, &s.camPitch, 0.0f, 80.0f, "%.0f deg", "how far the camera looks down when you start aiming" },
						{ L"Camera", L"fMinPitch", "Lowest angle", Kind::kFloat, &s.camMinPitch, 0.0f, 60.0f, "%.0f deg", "how close to the felt the camera can go" },
						{ L"Camera", L"fMaxPitch", "Highest angle", Kind::kFloat, &s.camMaxPitch, 10.0f, 85.0f, "%.0f deg", "how steeply it can look down" },
						{ L"Camera", L"fOverheadHeight", "Top-down height", Kind::kFloat, &s.overheadHeight, 80.0f, 800.0f, "%.0f",
							"how high the top-down view (right mouse) is above the table" },
					} },
				{ "Guide & cue stick", "The aiming dots and the cue stick behind the cue ball.",
					{
						{ L"Guide", L"bEnabled", "Show the aiming guide", Kind::kBool, &s.guide },
						{ L"Ball", L"bGlowOwn", "Your balls glow", Kind::kBool, &s.glowOwn, 0, 0, nullptr, "your group (solids or stripes) glows a little; in hotseat, the shooter's" },
						{ L"Ball", L"fGlowStrength", "Glow strength", Kind::kFloat, &s.glowStrength, 0.0f, 3.0f, "%.2f" },
						{ L"Guide", L"fDotRadius", "Dot size", Kind::kFloat, &s.guideDotRadius, 0.1f, 2.0f, "%.2f" },
						{ L"Guide", L"fSpacing", "Dot spacing", Kind::kFloat, &s.guideSpacing, 1.0f, 20.0f, "%.1f" },
						{ L"Guide", L"fMaxLength", "Cue ball line length", Kind::kFloat, &s.guideMaxLength, 0.0f, 1000.0f, "%.0f", "how far the cue ball's path is shown" },
						{ L"Guide", L"fObjectLength", "Hit ball line length", Kind::kFloat, &s.guideObjectLength, 0.0f, 200.0f, "%.0f", "how far the hit ball's path is shown" },
						{ L"Guide", L"fCueLength", "Cue ball after-line length", Kind::kFloat, &s.guideCueLength, 0.0f, 200.0f, "%.0f", "where the cue ball goes after the hit" },
						{ L"Stick", L"fElevation", "Stick tilt", Kind::kFloat, &s.stickElevation, 0.0f, 30.0f, "%.0f deg", "how far the butt is raised" },
						{ L"Stick", L"fMaxPull", "Stick draw-back", Kind::kFloat, &s.stickMaxPull, 0.0f, 60.0f, "%.0f", "how far the stick pulls back at full power" },
						{ L"Stick", L"fGap", "Tip gap", Kind::kFloat, &s.stickGap, 0.0f, 10.0f, "%.1f", "gap between the tip and the cue ball" },
						{ L"Stick", L"iAxis", "Stick mesh axis", Kind::kChoice, &s.stickAxis, 0, 0, nullptr, "the mesh axis the stick runs along",
							{ { "X", 0 }, { "Y", 1 }, { "Z", 2 } } },
						{ L"Stick", L"fTip", "Tip position", Kind::kFloat, &s.stickTip, -200.0f, 200.0f, "%.1f", "the tip's coordinate on that axis" },
						{ L"Stick", L"fButt", "Butt position", Kind::kFloat, &s.stickButt, -200.0f, 200.0f, "%.1f", "the butt's coordinate on that axis" },
						{ L"Stick", L"sPlugin", "Stick plugin", Kind::kText, &s.stickPlugin, 0, 0, nullptr, nullptr, {}, true },
						{ L"Stick", L"iFormID", "Stick form ID", Kind::kHex, &s.stickFormID, 0, 0, nullptr, "0 = no stick", {}, true },
					} },
				{ "Opponent", "Look at a settler or companion near a racked table and press the challenge key to play them at 8-ball.",
					{
						{ L"Opponent", L"fSkill", "Skill", Kind::kFloat, &s.opponentSkill, 0.0f, 1.0f, "%.2f", "0 = hopeless, 1 = never misses its aim" },
						{ L"Opponent", L"fAimSeconds", "Time to line up", Kind::kFloat, &s.opponentAimSeconds, 0.0f, 10.0f, "%.1f", "seconds they aim before striking" },
						{ L"Opponent", L"bRun", "Run to the shot", Kind::kBool, &s.opponentRun, 0, 0, nullptr, "off = walk" },
						{ L"Opponent", L"bLog", "Status log", Kind::kBool, &s.opponentLog, 0, 0, nullptr,
							"a line about them in fo4pool.log every second: where they are, what they're doing, their AI package" },
						{ L"Opponent", L"fWalkTimeout", "Walk time limit", Kind::kFloat, &s.opponentWalkTimeout, 1.0f, 60.0f, "%.0f", "seconds before they're put at the shot" },
						{ L"Opponent", L"fPickDistance", "Challenge distance", Kind::kFloat, &s.opponentPickDistance, 50.0f, 3000.0f, "%.0f", "how far away the NPC can be" },
						{ L"Anim", L"bEnabled", "Pool animations", Kind::kBool, &s.anim, 0, 0, nullptr, "they lean over the table and strike" },
						{ L"Anim", L"sPlugin", "Idle plugin", Kind::kText, &s.animPlugin, 0, 0, nullptr, nullptr, {}, true },
						{ L"Anim", L"iAimFormID", "Aim idle form ID", Kind::kHex, &s.animAimFormID, 0, 0, nullptr, nullptr, {}, true },
						{ L"Anim", L"iStrikeFormID", "Strike idle form ID", Kind::kHex, &s.animStrikeFormID, 0, 0, nullptr, nullptr, {}, true },
						{ L"Anim", L"iWaitFormID", "Waiting idle form ID", Kind::kHex, &s.animWaitFormID, 0, 0, nullptr, nullptr, {}, true },
					} },
				{ "Rules", "The game mode is used from the next rack.",
					{
						{ L"Rules", L"iGameMode", "Game mode", Kind::kChoice, &s.gameMode, 0, 0, nullptr,
							"8-Ball: two players. Solo Play: pot all 14, then the 8 into the called pocket",
							{ { "8-Ball (two players)", 0 }, { "Solo Play", 1 } }, true },
						{ L"Rules", L"bCallEight", "Call the pocket for the 8", Kind::kBool, &s.callEight, 0, 0, nullptr,
							"the 8 must go in the marked pocket; off = any pocket wins" },
						{ L"Rules", L"fPlaceSpeed", "Ball in hand speed", Kind::kFloat, &s.placeSpeed, 0.005f, 0.5f, "%.3f", "how far the cue ball moves per bit of mouse movement" },
					} },
				{ "Sound", "Pool sounds (Data\\Sound\\fo4pool).",
					{
						{ L"Sound", L"bEnabled", "Sounds on", Kind::kBool, &s.sound, 0, 0, nullptr, "switching them on takes a game restart" },
						{ L"Sound", L"fVolume", "Volume", Kind::kFloat, &s.soundVolume, 0.0f, 1.0f, "%.2f" },
						{ L"Sound", L"fFullDistance", "Full volume within", Kind::kFloat, &s.soundFullDistance, 10.0f, 1000.0f, "%.0f", "distance from the camera" },
						{ L"Sound", L"fMaxDistance", "Silent beyond", Kind::kFloat, &s.soundMaxDistance, 100.0f, 10000.0f, "%.0f" },
						{ L"Sound", L"bBallReturn", "Ball return roll", Kind::kBool, &s.soundReturn, 0, 0, nullptr, "a pocketed ball is heard rolling away under the table" },
						{ L"Sound", L"iBallReturnEnd", "Ball return end", Kind::kChoice, &s.returnEnd, 0, 0, nullptr, "pockets nearer it give a shorter roll",
							{ { "Foot (rack) end", 1 }, { "Head end", -1 } } },
					} },
				{ "Table & pockets", "Where the felt is on the table mesh, and how picky the pockets are. These apply on the next rack.",
					{
						{ L"Table", L"bUseConsolePick", "Console pick first", Kind::kBool, &s.useConsolePick, 0, 0, nullptr,
							"the table clicked in the console is used before the one under the crosshair" },
						{ L"Table", L"fAimDistance", "Table search distance", Kind::kFloat, &s.aimDistance, 50.0f, 2000.0f, "%.0f", "how far away a table under the crosshair can be" },
						{ L"Table", L"iLengthAxis", "Length runs along", Kind::kChoice, &s.lengthAxis, 0, 0, nullptr, "the table mesh's axis",
							{ { "Mesh X", 0 }, { "Mesh Y", 1 } }, true },
						{ L"Table", L"fLength", "Felt length", Kind::kDouble, &s.spec.length, 50.0f, 400.0f, "%.1f", "cushion nose to cushion nose", {}, true },
						{ L"Table", L"fWidth", "Felt width", Kind::kDouble, &s.spec.width, 25.0f, 200.0f, "%.1f", nullptr, {}, true },
						{ L"Table", L"fSurfaceZ", "Felt height", Kind::kFloat, &s.surfaceZ, 0.0f, 200.0f, "%.1f", "above the mesh origin", {}, true },
						{ L"Table", L"fOffsetX", "Felt offset X", Kind::kFloat, &s.offsetX, -50.0f, 50.0f, "%.1f", nullptr, {}, true },
						{ L"Table", L"fOffsetY", "Felt offset Y", Kind::kFloat, &s.offsetY, -50.0f, 50.0f, "%.1f", nullptr, {}, true },
						{ L"Table", L"fCornerMouth", "Corner pocket opening", Kind::kDouble, &s.spec.cornerMouth, 5.0f, 25.0f, "%.1f", "bigger = easier", {}, true },
						{ L"Table", L"fSideMouth", "Side pocket opening", Kind::kDouble, &s.spec.sideMouth, 5.0f, 30.0f, "%.1f", "bigger = easier", {}, true },
						{ L"Table", L"fPocketDepth", "Pocket depth", Kind::kDouble, &s.spec.pocketDepth, 0.0f, 2.0f, "%.2f",
							"how far past the opening a ball must get, in ball radii (0 = the old easy pockets)", {}, true },
						{ L"Table", L"fJawLength", "Jaw length", Kind::kDouble, &s.spec.jawLength, 0.0f, 3.0f, "%.2f", "how far the jaws reach in, in ball radii (0 = none)", {}, true },
						{ L"Table", L"fCornerJawAngle", "Corner jaw angle", Kind::kDouble, &s.spec.cornerJawAngle, -30.0f, 40.0f, "%.0f deg", "more = pickier corners", {}, true },
						{ L"Table", L"fSideJawAngle", "Side jaw angle", Kind::kDouble, &s.spec.sideJawAngle, -30.0f, 40.0f, "%.0f deg", "more = pickier sides", {}, true },
					} },
				{ "Physics", "How the balls behave. These apply on the next rack.",
					{
						{ L"Physics", L"fRollDecel", "Rolling friction", Kind::kDouble, &s.spec.rollDecel, 1.0f, 60.0f, "%.1f", "how quickly rolling balls slow down", {}, true },
						{ L"Physics", L"fBallRestitution", "Ball bounce", Kind::kDouble, &s.spec.ballRestitution, 0.3f, 1.0f, "%.2f",
							"how much of the cue ball's speed a hit ball gets: 0.75 = 87%, 1.00 = all of it", {}, true },
						{ L"Physics", L"fCushionRestitution", "Cushion bounce", Kind::kDouble, &s.spec.cushionRestitution, 0.3f, 1.0f, "%.2f",
							"how lively the cushions are", {}, true },
					} },
				{ "Balls", "The ball objects and their size. These apply on the next rack.",
					{
						{ L"Ball", L"sPlugin", "Ball plugin", Kind::kText, &s.ballPlugin, 0, 0, nullptr, nullptr, {}, true },
						{ L"Ball", L"iCueFormID", "Cue ball form ID", Kind::kHex, &s.cueFormID, 0, 0, nullptr, "ball n is this + n unless set below", {}, true },
						{ L"Ball", L"iFormID", "One form for every ball", Kind::kHex, &s.ballFormID, 0, 0, nullptr, "0 = each number its own", {}, true },
						{ L"Ball", L"fRadius", "Ball radius", Kind::kDouble, &s.spec.ballRadius, 1.0f, 10.0f, "%.2f", nullptr, {}, true },
						{ L"Ball", L"bAutoScale", "Scale the mesh to the radius", Kind::kBool, &s.ballAutoScale, 0, 0, nullptr, nullptr, {}, true },
						{ L"Ball", L"fScale", "Mesh scale", Kind::kFloat, &s.ballScale, 0.1f, 5.0f, "%.2f", "used when the above is off", {}, true },
						{ L"Ball", L"fZOffset", "Height offset", Kind::kFloat, &s.ballZOffset, -5.0f, 5.0f, "%.2f", "if the mesh's centre isn't the ball's centre", {}, true },
					} },
				{ "Keys", "Click a key's button, then press the key you want.",
					{
						{ L"Keys", L"iRack", "Rack", Kind::kKey, &s.keyRack },
						{ L"Keys", L"iShoot", "Aim / stop aiming", Kind::kKey, &s.keyShoot },
						{ L"Keys", L"iClear", "Clear the table", Kind::kKey, &s.keyClear },
						{ L"Keys", L"iCalibrate", "Calibration markers", Kind::kKey, &s.keyCalibrate },
						{ L"Keys", L"iCallPocket", "Change the called pocket", Kind::kKey, &s.keyCallPocket },
						{ L"Keys", L"iChallenge", "Challenge an NPC", Kind::kKey, &s.keyChallenge },
					} },
			};
			static bool added = false;
			if (!added) {
				added = true;
				// per-ball form overrides
				static std::array<std::string, 16> labels;
				auto&                               balls = pages[7].entries;
				static std::array<std::wstring, 16> keys;
				for (int n = 1; n < 16; ++n) {
					labels[n] = std::format("Ball {} form ID", n);
					keys[n] = std::format(L"iFormID{}", n);
					balls.push_back({ L"Ball", keys[n].c_str(), labels[n].c_str(), Kind::kHex, &s.numberFormIDs[n], 0, 0, nullptr, "0 = the default", {}, true });
				}
			}
			return pages;
		}

		// ------------------------------------------------------------ saving

		void Write(const Entry& a_e)
		{
			std::wstring value;
			switch (a_e.kind) {
			case Kind::kFloat:
				value = std::format(L"{}", *static_cast<float*>(a_e.field));
				break;
			case Kind::kDouble:
				value = std::format(L"{}", *static_cast<double*>(a_e.field));
				break;
			case Kind::kBool:
				value = *static_cast<bool*>(a_e.field) ? L"1" : L"0";
				break;
			case Kind::kInt:
			case Kind::kChoice:
				value = std::format(L"{}", *static_cast<int*>(a_e.field));
				break;
			case Kind::kKey:
				value = std::format(L"0x{:02X}", *static_cast<int*>(a_e.field));
				break;
			case Kind::kHex:
				value = std::format(L"0x{:X}", *static_cast<std::uint32_t*>(a_e.field));
				break;
			case Kind::kText:
				{
					const auto& str = *static_cast<std::string*>(a_e.field);
					value.assign(str.begin(), str.end());
				}
				break;
			}
			if (!::WritePrivateProfileStringW(a_e.section, a_e.key, value.c_str(), kIniPath)) {
				log::warn("settings menu: could not write [{}] to the INI", static_cast<int>(a_e.kind));
			}
		}

		// The same field in a freshly made Settings: the default.
		void* DefaultOf(const Entry& a_e)
		{
			static Settings defaults{};
			const auto offset = static_cast<char*>(a_e.field) - reinterpret_cast<char*>(&S());
			return reinterpret_cast<char*>(&defaults) + offset;
		}

		void Reset(const Entry& a_e)
		{
			void* def = DefaultOf(a_e);
			switch (a_e.kind) {
			case Kind::kFloat:
				*static_cast<float*>(a_e.field) = *static_cast<float*>(def);
				break;
			case Kind::kDouble:
				*static_cast<double*>(a_e.field) = *static_cast<double*>(def);
				break;
			case Kind::kBool:
				*static_cast<bool*>(a_e.field) = *static_cast<bool*>(def);
				break;
			case Kind::kInt:
			case Kind::kChoice:
			case Kind::kKey:
				*static_cast<int*>(a_e.field) = *static_cast<int*>(def);
				break;
			case Kind::kHex:
				*static_cast<std::uint32_t*>(a_e.field) = *static_cast<std::uint32_t*>(def);
				break;
			case Kind::kText:
				*static_cast<std::string*>(a_e.field) = *static_cast<std::string*>(def);
				break;
			}
			Write(a_e);
		}

		// ------------------------------------------------------------ keys

		const Entry* g_rebinding{ nullptr };
		std::array<bool, 256> g_keyWasDown{};

		// while waiting for a key: the first key newly pressed (mouse buttons and Escape aside)
		int NewKeyPress()
		{
			int pressed = 0;
			for (int vk = 0x08; vk < 0xFF; ++vk) {
				const bool down = (::GetAsyncKeyState(vk) & 0x8000) != 0;
				if (down && !g_keyWasDown[vk] && !pressed && vk != VK_LBUTTON && vk != VK_RBUTTON && vk != VK_MBUTTON && vk != VK_XBUTTON1 &&
					vk != VK_XBUTTON2 && vk != VK_SHIFT && vk != VK_CONTROL && vk != VK_MENU) {
					pressed = vk;
				}
				g_keyWasDown[vk] = down;
			}
			return pressed;
		}

		// ------------------------------------------------------------ drawing

		void Help(const Entry& a_e)
		{
			if (a_e.help && IsItemHovered()) {
				SetTooltip("%s", a_e.help);
			}
		}

		void Row(const Entry& a_e)
		{
			PushID(a_e.key);
			SetNextItemWidth(260.0f);
			bool save = false;
			switch (a_e.kind) {
			case Kind::kFloat:
				{
					auto& v = *static_cast<float*>(a_e.field);
					if (SliderFloat(a_e.label, &v, a_e.min, a_e.max, a_e.format)) {
						v = std::clamp(v, a_e.min, a_e.max);
					}
					save = IsItemDeactivatedAfterEdit();
				}
				break;
			case Kind::kDouble:
				{
					auto& v = *static_cast<double*>(a_e.field);
					float f = static_cast<float>(v);
					if (SliderFloat(a_e.label, &f, a_e.min, a_e.max, a_e.format)) {
						v = std::clamp(f, a_e.min, a_e.max);
					}
					save = IsItemDeactivatedAfterEdit();
				}
				break;
			case Kind::kBool:
				save = Checkbox(a_e.label, static_cast<bool*>(a_e.field));
				break;
			case Kind::kInt:
				{
					auto& v = *static_cast<int*>(a_e.field);
					if (InputInt(a_e.label, &v)) {
						v = std::clamp(v, static_cast<int>(a_e.min), static_cast<int>(a_e.max));
					}
					save = IsItemDeactivatedAfterEdit();
				}
				break;
			case Kind::kChoice:
				{
					auto& v = *static_cast<int*>(a_e.field);
					int   current = 0;
					std::vector<const char*> labels;
					for (std::size_t i = 0; i < a_e.choices.size(); ++i) {
						labels.push_back(a_e.choices[i].label);
						if (a_e.choices[i].value == v) {
							current = static_cast<int>(i);
						}
					}
					if (Combo(a_e.label, &current, labels.data(), static_cast<int>(labels.size()))) {
						v = a_e.choices[current].value;
						save = true;
					}
				}
				break;
			case Kind::kHex:
				{
					auto& v = *static_cast<std::uint32_t*>(a_e.field);
					char  buf[16];
					std::snprintf(buf, sizeof(buf), "%X", v);
					if (InputText(a_e.label, buf, sizeof(buf), ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue) ||
						IsItemDeactivatedAfterEdit()) {
						v = static_cast<std::uint32_t>(std::strtoul(buf, nullptr, 16));
						save = true;
					}
				}
				break;
			case Kind::kText:
				{
					auto& str = *static_cast<std::string*>(a_e.field);
					char  buf[128];
					std::snprintf(buf, sizeof(buf), "%s", str.c_str());
					if (InputText(a_e.label, buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue) || IsItemDeactivatedAfterEdit()) {
						str = buf;
						save = true;
					}
				}
				break;
			case Kind::kKey:
				{
					auto& v = *static_cast<int*>(a_e.field);
					const bool waiting = g_rebinding == &a_e;
					const std::string caption = waiting ? "press a key... (Esc cancels)" : KeyName(v);
					if (Button(std::format("{}##key", caption).c_str(), ImVec2{ 260.0f, 0.0f }) && !waiting) {
						g_rebinding = &a_e;
						NewKeyPress();  // forget whatever is held now
					}
					SameLine();
					Text("%s", a_e.label);
					if (waiting) {
						if (const int vk = NewKeyPress()) {
							if (vk != VK_ESCAPE) {
								v = vk;
								save = true;
							}
							g_rebinding = nullptr;
						}
					}
				}
				break;
			}
			Help(a_e);
			if (a_e.kind != Kind::kKey && a_e.kind != Kind::kBool) {
				SameLine();
				if (SmallButton("reset")) {
					Reset(a_e);
				}
			}
			if (save) {
				Write(a_e);
			}
			PopID();
		}

		template <std::size_t I>
		void __stdcall DrawPage()
		{
			auto& page = Pages()[I];
			TextWrapped("%s", page.intro);
			Spacing();
			bool rackNote = false;
			for (const auto& e : page.entries) {
				Row(e);
				rackNote |= e.nextRack;
			}
			Spacing();
			if (Button("Reset this page to the defaults")) {
				for (const auto& e : page.entries) {
					Reset(e);
				}
			}
			if (rackNote) {
				TextDisabled("Some of these take effect on the next rack (Numpad 7).");
			}
			TextDisabled("Saved to Data\\F4SE\\Plugins\\fo4pool.ini as you change them.");
		}

		template <std::size_t... I>
		void AddPages(std::index_sequence<I...>)
		{
			(F4SEMenuFramework::AddSectionItem(Pages()[I].name, &DrawPage<I>), ...);
		}
	}

	std::string KeyName(int a_vk)
	{
		if (a_vk <= 0) {
			return "none";
		}
		if (a_vk >= VK_NUMPAD0 && a_vk <= VK_NUMPAD9) {
			return std::format("Numpad {}", a_vk - VK_NUMPAD0);
		}
		const UINT scan = ::MapVirtualKeyW(static_cast<UINT>(a_vk), MAPVK_VK_TO_VSC);
		LONG       lparam = static_cast<LONG>(scan << 16);
		switch (a_vk) {
		case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
		case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_DIVIDE:
			lparam |= 1 << 24;  // extended keys
			break;
		default:
			break;
		}
		wchar_t name[64]{};
		if (::GetKeyNameTextW(lparam, name, 64) > 0) {
			std::string out;
			for (const wchar_t* c = name; *c; ++c) {
				out.push_back(*c < 128 ? static_cast<char>(*c) : '?');
			}
			return out;
		}
		return std::format("key 0x{:02X}", a_vk);
	}

	void Install()
	{
		static bool installed = false;
		if (installed || !F4SEMenuFramework::IsInstalled()) {
			return;
		}
		installed = true;
		Settings::Get().Load();  // the menu shows the INI's values from the start
		F4SEMenuFramework::SetSection("fo4pool");
		AddPages(std::make_index_sequence<9>{});
		log::info("settings pages registered in F4SE Menu Framework ({} pages)", Pages().size());
	}
}
