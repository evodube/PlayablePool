#include "Settings.h"

#include "SettingsMenu.h"

namespace fp
{
	namespace
	{
		constexpr auto kIniPath = L"Data\\F4SE\\Plugins\\fo4pool.ini";

		std::wstring ReadString(const wchar_t* a_section, const wchar_t* a_key, const std::wstring& a_default)
		{
			wchar_t buf[512]{};
			::GetPrivateProfileStringW(a_section, a_key, a_default.c_str(), buf, static_cast<DWORD>(std::size(buf)), kIniPath);
			std::wstring value = buf;
			// strip inline "; comment" and surrounding whitespace
			for (std::size_t i = 0; i < value.size(); ++i) {
				if (value[i] == L';' && (i == 0 || value[i - 1] == L' ' || value[i - 1] == L'\t')) {
					value.resize(i);
					break;
				}
			}
			while (!value.empty() && (value.back() == L' ' || value.back() == L'\t')) {
				value.pop_back();
			}
			return value;
		}

		std::string Narrow(const std::wstring& a_str)
		{
			if (a_str.empty()) {
				return {};
			}
			const int len = ::WideCharToMultiByte(CP_UTF8, 0, a_str.c_str(), static_cast<int>(a_str.size()), nullptr, 0, nullptr, nullptr);
			std::string out(static_cast<std::size_t>(len), '\0');
			::WideCharToMultiByte(CP_UTF8, 0, a_str.c_str(), static_cast<int>(a_str.size()), out.data(), len, nullptr, nullptr);
			return out;
		}

		long long ReadInt(const wchar_t* a_section, const wchar_t* a_key, long long a_default)
		{
			const auto str = ReadString(a_section, a_key, std::to_wstring(a_default));
			try {
				return std::stoll(str, nullptr, 0);  // accepts 0x800 style
			} catch (...) {
				return a_default;
			}
		}

		float ReadFloat(const wchar_t* a_section, const wchar_t* a_key, float a_default)
		{
			const auto str = ReadString(a_section, a_key, std::to_wstring(a_default));
			try {
				return std::stof(str);
			} catch (...) {
				return a_default;
			}
		}

		bool ReadBool(const wchar_t* a_section, const wchar_t* a_key, bool a_default)
		{
			return ReadInt(a_section, a_key, a_default ? 1 : 0) != 0;
		}
	}

	void Settings::Load()
	{
		keyRack = static_cast<int>(ReadInt(L"Keys", L"iRack", keyRack));
		keyShoot = static_cast<int>(ReadInt(L"Keys", L"iShoot", keyShoot));
		keyClear = static_cast<int>(ReadInt(L"Keys", L"iClear", keyClear));
		keyCalibrate = static_cast<int>(ReadInt(L"Keys", L"iCalibrate", keyCalibrate));
		keyCallPocket = static_cast<int>(ReadInt(L"Keys", L"iCallPocket", keyCallPocket));
		gameMode = ReadInt(L"Rules", L"iGameMode", gameMode) == 1 ? 1 : 0;
		callEight = ReadBool(L"Rules", L"bCallEight", callEight);
		placeSpeed = std::clamp(ReadFloat(L"Rules", L"fPlaceSpeed", placeSpeed), 0.005f, 1.0f);

		ballPlugin = Narrow(ReadString(L"Ball", L"sPlugin", std::wstring(ballPlugin.begin(), ballPlugin.end())));
		cueFormID = static_cast<std::uint32_t>(ReadInt(L"Ball", L"iCueFormID", cueFormID));
		ballFormID = static_cast<std::uint32_t>(ReadInt(L"Ball", L"iFormID", ballFormID));
		for (int n = 1; n < 16; ++n) {
			const auto key = std::format(L"iFormID{}", n);
			numberFormIDs[n] = static_cast<std::uint32_t>(ReadInt(L"Ball", key.c_str(), 0));
		}
		ballAutoScale = ReadBool(L"Ball", L"bAutoScale", ballAutoScale);
		ballScale = ReadFloat(L"Ball", L"fScale", ballScale);
		ballZOffset = ReadFloat(L"Ball", L"fZOffset", ballZOffset);
		spec.ballRadius = std::max(0.5f, ReadFloat(L"Ball", L"fRadius", static_cast<float>(spec.ballRadius)));

		minSpeed = ReadFloat(L"Shot", L"fMinSpeed", minSpeed);
		maxSpeed = std::max(minSpeed, ReadFloat(L"Shot", L"fMaxSpeed", maxSpeed));
		pullPerDegree = std::max(0.001f, ReadFloat(L"Shot", L"fPullPerDegree", pullPerDegree));
		invertPull = ReadBool(L"Shot", L"bInvertPull", invertPull);

		camDistance = std::max(10.0f, ReadFloat(L"Camera", L"fDistance", camDistance));
		camPitch = ReadFloat(L"Camera", L"fPitch", camPitch);
		camMinPitch = ReadFloat(L"Camera", L"fMinPitch", camMinPitch);
		camMaxPitch = std::max(camMinPitch, ReadFloat(L"Camera", L"fMaxPitch", camMaxPitch));
		overheadHeight = std::max(50.0f, ReadFloat(L"Camera", L"fOverheadHeight", overheadHeight));

		guide = ReadBool(L"Guide", L"bEnabled", guide);
		guideDotRadius = std::max(0.05f, ReadFloat(L"Guide", L"fDotRadius", guideDotRadius));
		guideSpacing = std::max(1.0f, ReadFloat(L"Guide", L"fSpacing", guideSpacing));
		guideMaxLength = ReadFloat(L"Guide", L"fMaxLength", guideMaxLength);
		guideObjectLength = ReadFloat(L"Guide", L"fObjectLength", guideObjectLength);
		guideCueLength = ReadFloat(L"Guide", L"fCueLength", guideCueLength);

		stickPlugin = Narrow(ReadString(L"Stick", L"sPlugin", std::wstring(stickPlugin.begin(), stickPlugin.end())));
		stickFormID = static_cast<std::uint32_t>(ReadInt(L"Stick", L"iFormID", stickFormID));
		stickAxis = std::clamp(static_cast<int>(ReadInt(L"Stick", L"iAxis", stickAxis)), 0, 2);
		stickTip = ReadFloat(L"Stick", L"fTip", stickTip);
		stickButt = ReadFloat(L"Stick", L"fButt", stickButt);
		stickElevation = ReadFloat(L"Stick", L"fElevation", stickElevation);
		stickMaxPull = ReadFloat(L"Stick", L"fMaxPull", stickMaxPull);
		stickGap = ReadFloat(L"Stick", L"fGap", stickGap);

		hudScale = std::clamp(ReadFloat(L"HUD", L"fScale", hudScale), 0.3f, 3.0f);
		sound = ReadBool(L"Sound", L"bEnabled", sound);
		soundVolume = std::clamp(ReadFloat(L"Sound", L"fVolume", soundVolume), 0.0f, 1.0f);
		soundFullDistance = std::max(1.0f, ReadFloat(L"Sound", L"fFullDistance", soundFullDistance));
		soundMaxDistance = std::max(soundFullDistance + 1.0f, ReadFloat(L"Sound", L"fMaxDistance", soundMaxDistance));
		soundReturn = ReadBool(L"Sound", L"bBallReturn", soundReturn);
		returnEnd = ReadInt(L"Sound", L"iBallReturnEnd", returnEnd) < 0 ? -1 : 1;

		useConsolePick = ReadBool(L"Table", L"bUseConsolePick", useConsolePick);
		aimDistance = ReadFloat(L"Table", L"fAimDistance", aimDistance);
		lengthAxis = static_cast<int>(ReadInt(L"Table", L"iLengthAxis", lengthAxis)) == 1 ? 1 : 0;
		offsetX = ReadFloat(L"Table", L"fOffsetX", offsetX);
		offsetY = ReadFloat(L"Table", L"fOffsetY", offsetY);
		surfaceZ = ReadFloat(L"Table", L"fSurfaceZ", surfaceZ);
		spec.length = std::max(20.0f, ReadFloat(L"Table", L"fLength", static_cast<float>(spec.length)));
		spec.width = std::max(10.0f, ReadFloat(L"Table", L"fWidth", static_cast<float>(spec.width)));
		spec.cornerMouth = ReadFloat(L"Table", L"fCornerMouth", static_cast<float>(spec.cornerMouth));
		spec.sideMouth = ReadFloat(L"Table", L"fSideMouth", static_cast<float>(spec.sideMouth));
		spec.pocketDepth = std::max(0.0f, ReadFloat(L"Table", L"fPocketDepth", static_cast<float>(spec.pocketDepth)));
		spec.jawLength = std::max(0.0f, ReadFloat(L"Table", L"fJawLength", static_cast<float>(spec.jawLength)));
		spec.cornerJawAngle = std::clamp(ReadFloat(L"Table", L"fCornerJawAngle", static_cast<float>(spec.cornerJawAngle)), -30.0f, 40.0f);
		spec.sideJawAngle = std::clamp(ReadFloat(L"Table", L"fSideJawAngle", static_cast<float>(spec.sideJawAngle)), -30.0f, 40.0f);

		spec.rollDecel = ReadFloat(L"Physics", L"fRollDecel", static_cast<float>(spec.rollDecel));
		spec.ballRestitution = ReadFloat(L"Physics", L"fBallRestitution", static_cast<float>(spec.ballRestitution));
		spec.cushionRestitution = ReadFloat(L"Physics", L"fCushionRestitution", static_cast<float>(spec.cushionRestitution));

		log::info("settings: ball {}|{:X} r={} autoscale={} | table {}x{} surfaceZ={} axis={} offset=({}, {}) | max shot {}",
			ballPlugin, ballFormID, spec.ballRadius, ballAutoScale, spec.length, spec.width, surfaceZ, lengthAxis, offsetX, offsetY,
			maxSpeed);
	}

	std::string Settings::Key(int a_vk)
	{
		return SettingsMenu::KeyName(a_vk);
	}
}
