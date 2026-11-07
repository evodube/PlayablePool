#include "Pad.h"

#include <Xinput.h>

namespace fp::Pad
{
	namespace
	{
		using GetState_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
		using SetState_t = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);

		GetState_t g_get{ nullptr };
		SetState_t g_set{ nullptr };
		bool       g_loaded{ false };
		int        g_index{ -1 };
		ULONGLONG  g_nextScan{ 0 };

		void Load()
		{
			if (g_loaded) {
				return;
			}
			g_loaded = true;
			for (const wchar_t* dll : { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" }) {
				if (HMODULE m = ::LoadLibraryW(dll)) {
					g_get = reinterpret_cast<GetState_t>(::GetProcAddress(m, "XInputGetState"));
					g_set = reinterpret_cast<SetState_t>(::GetProcAddress(m, "XInputSetState"));
					if (g_get) {
						return;
					}
				}
			}
		}

		float Trigger(BYTE a_v) { return a_v <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? 0.0f : (a_v - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) / (255.0f - XINPUT_GAMEPAD_TRIGGER_THRESHOLD); }

		// a round dead zone, then 0..1 from its edge to the stick's
		void Stick(SHORT a_x, SHORT a_y, int a_dead, float& a_outX, float& a_outY)
		{
			const float x = a_x / 32767.0f, y = a_y / 32767.0f;
			const float len = std::sqrt(x * x + y * y);
			const float dead = a_dead / 32767.0f;
			if (len <= dead) {
				a_outX = a_outY = 0.0f;
				return;
			}
			const float scaled = std::min(1.0f, (len - dead) / (1.0f - dead));
			a_outX = x / len * scaled;
			a_outY = y / len * scaled;
		}
	}

	State Read()
	{
		Load();
		State out;
		if (!g_get) {
			return out;
		}
		XINPUT_STATE st{};
		if (g_index >= 0 && g_get(static_cast<DWORD>(g_index), &st) != ERROR_SUCCESS) {
			g_index = -1;
		}
		if (g_index < 0) {
			const ULONGLONG now = ::GetTickCount64();
			if (now < g_nextScan) {
				return out;  // asking about an empty slot is slow: not every frame
			}
			g_nextScan = now + 2000;
			for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
				if (g_get(i, &st) == ERROR_SUCCESS) {
					g_index = static_cast<int>(i);
					break;
				}
			}
			if (g_index < 0) {
				return out;
			}
		}
		const auto& p = st.Gamepad;
		out.connected = true;
		out.buttons = p.wButtons;
		out.lt = Trigger(p.bLeftTrigger);
		out.rt = Trigger(p.bRightTrigger);
		Stick(p.sThumbLX, p.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, out.lx, out.ly);
		Stick(p.sThumbRX, p.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE, out.rx, out.ry);
		return out;
	}

	void Rumble(float a_low, float a_high)
	{
		if (!g_set || g_index < 0) {
			return;
		}
		XINPUT_VIBRATION v{ static_cast<WORD>(std::clamp(a_low, 0.0f, 1.0f) * 65535.0f), static_cast<WORD>(std::clamp(a_high, 0.0f, 1.0f) * 65535.0f) };
		g_set(static_cast<DWORD>(g_index), &v);
	}
}
