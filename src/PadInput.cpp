// The controller at the table. Read on the render thread next to the keyboard and mouse (PollKeys, PollMouse);
// anything that changes the game goes to the main thread as a task, like the keys do.
//
//   A        the activate key: play / take your shot (hold for an opponent); ball in hand: set the cue ball down;
//            squeeze-RT power: shoot now
//   B        leave the table (shot camera), close the who-plays list, put a kit away again
//   RT       hold and pull the right stick back for power, let go to shoot (or: squeeze it, let go to shoot)
//   Y        overhead view
//   D-pad    the who-plays list (up/down pick, left/right 8- or 9-ball), turning a kit table, the called pocket
//            (left/right), and once the game's over, up racks again and down clears the table
//   Back     held: forfeit
//   Left     stick: the cue ball, with ball in hand
// The right stick aims through the game's own free camera, the same way the mouse does.

#include "Game.h"
#include "Pad.h"
#include "Settings.h"
#include "Util.h"

namespace fp
{
	namespace
	{
		constexpr float kRtDown = 0.55f;     // RT counts as pressed past this (stick power)
		constexpr float kRtUp = 0.30f;       // and let go under this
		constexpr float kSqueezeUp = 0.06f;  // squeeze power: let go
		constexpr auto  kRecent = std::chrono::milliseconds(150);  // letting go keeps the power of the moment before
		constexpr auto  kBackHold = std::chrono::milliseconds(1000);

		template <class F>
		void Later(F&& a_fn)
		{
			if (auto* tasks = F4SE::GetTaskInterface()) {
				tasks->AddTask(std::forward<F>(a_fn));
			}
		}
	}

	float Game::PadPullNow(float a_rt, float a_ry) const
	{
		const auto& s = Settings::Get();
		const float raw = s.padPowerMode == 1 ? a_rt : (s.padInvertPull ? a_ry : -a_ry);
		return std::clamp(raw / std::max(0.05f, s.padFullPull), 0.0f, 1.0f);
	}

	void Game::PadStartCharge()
	{
		_power = 0.0f;
		_chargeMouseEvents = 0;
		_chargeInputs = 0;
		_padRecent = {};
		_padCharge = true;
		_charging = true;  // the main thread freezes the camera, as for the mouse
	}

	void Game::PadEndCharge(bool a_shoot)
	{
		// the power a moment ago: the stick (or trigger) springs back as you let go
		const auto now = std::chrono::steady_clock::now();
		float      power = _power.load();
		for (const auto& [at, p] : _padRecent) {
			if (now - at <= kRecent) {
				power = std::max(power, p);
			}
		}
		_charging = false;
		_padCharge = false;
		if (!a_shoot || power < 0.02f) {
			return;  // let go without pulling: no shot (a way to call it off)
		}
		if (Settings::Get().padRumble) {
			_padRumbleUntil = now + std::chrono::milliseconds(90 + static_cast<int>(power * 120.0f));
			Pad::Rumble(0.25f + 0.5f * power, 0.35f + 0.5f * power);
			_padRumbling = true;
		}
		Later([this, power]() { ShootAim(power); });
	}

	void Game::PollPad()
	{
		const auto& s = Settings::Get();
		const auto  now = std::chrono::steady_clock::now();
		const float dt = _padLast.time_since_epoch().count() == 0 ? 0.0f : std::min(0.1f, std::chrono::duration<float>(now - _padLast).count());
		_padLast = now;

		if (_padRumbling && now >= _padRumbleUntil) {
			Pad::Rumble(0.0f, 0.0f);
			_padRumbling = false;
		}
		const Pad::State pad = s.padEnabled ? Pad::Read() : Pad::State{};
		const HWND       window = util::GameWindow();
		if (!pad.connected || (window && ::GetForegroundWindow() != window) || _building.load()) {
			if (_padCharge) {
				PadEndCharge(false);
			}
			_padWas = pad.connected ? pad.buttons : 0;
			_padRtWas = pad.rt;
			_padAHeld = true;  // a press already going when we come back isn't ours
			return;
		}
		const std::uint16_t b = pad.buttons;
		const auto pressed = [&](std::uint16_t a_button) { return (b & a_button) && !(_padWas & a_button); };
		const auto released = [&](std::uint16_t a_button) { return !(b & a_button) && (_padWas & a_button); };
		if (b != _padWas || pad.rt > 0.2f || pad.lt > 0.2f || std::abs(pad.lx) + std::abs(pad.ly) + std::abs(pad.rx) + std::abs(pad.ry) > 0.3f) {
			_padActive = true;
		}

		const bool aiming = _aiming.load();
		const bool placing = _placing.load();

		// ---- the shot camera
		if (aiming) {
			if (placing) {
				if (pad.lx != 0.0f || pad.ly != 0.0f) {
					std::scoped_lock lock(_camMutex);
					_placeDX += pad.lx * s.padPlaceSpeed * dt;
					_placeDY -= pad.ly * s.padPlaceSpeed * dt;  // stick up = up the screen (as the mouse going forward)
				}
				if (pressed(Pad::kA)) {
					Later([this]() { ConfirmPlacement(); });
				}
			} else {
				const bool squeeze = s.padPowerMode == 1;
				if (!_charging && !_rolling) {
					const bool start = squeeze ? (pad.rt > 0.10f && _padRtWas <= 0.10f) : (pad.rt >= kRtDown && _padRtWas < kRtDown);
					if (start) {
						PadStartCharge();
					}
				}
				if (_padCharge) {
					const float p = PadPullNow(pad.rt, pad.ry);
					_power = p;
					_padRecent[_padRecentAt++ % _padRecent.size()] = { now, p };
					if (squeeze ? (pad.rt <= kSqueezeUp || pressed(Pad::kA)) : pad.rt <= kRtUp) {
						PadEndCharge(true);
					}
				}
				if (pressed(Pad::kY) && !_noOverhead) {
					Later([this]() { ToggleOverhead(); });
				}
				if (pressed(Pad::kLeft) || pressed(Pad::kRight)) {
					Later([this]() { CyclePocket(); });
				}
			}
			if (pressed(Pad::kB)) {
				if (_padCharge) {
					PadEndCharge(false);  // B while pulling: call the shot off
				} else {
					Later([this]() {
						if (_aiming) {
							ExitAim();
							Tell("Pool: stopped aiming");
						}
					});
				}
			}
			if (pressed(Pad::kUp) || pressed(Pad::kDown)) {
				const bool up = pressed(Pad::kUp);
				Later([this, up]() {
					if (!_sim || !_rules.Over()) {
						return;  // only once the game's over: rack again / clear the table
					}
					if (up) {
						Rack();
					} else {
						if (_opponent) {
							EndChallenge("");
						}
						Clear();
					}
				});
			}
		} else {
			if (_padCharge) {
				PadEndCharge(false);
			}
			// ---- the who-plays list, a kit table being set up
			if (_picking) {
				if (pressed(Pad::kUp) || pressed(Pad::kDown)) {
					const int by = pressed(Pad::kUp) ? -1 : 1;
					Later([this, by]() { MovePick(by); });
				}
				if (pressed(Pad::kLeft) || pressed(Pad::kRight)) {
					Later([this]() { SwitchType(); });
				}
				if (pressed(Pad::kB)) {
					Later([this]() { ClosePick(); });
				}
			} else if (_kitPlacing) {
				if (pressed(Pad::kLeft) || pressed(Pad::kRight)) {
					const int dir = pressed(Pad::kLeft) ? -1 : 1;
					Later([this, dir]() { TurnKit(dir); });
				}
				if (pressed(Pad::kB)) {
					Later([this]() { CancelKit(true); });
				}
			}
			// ---- A: the activate key's tap (on letting go) and hold
			if (pressed(Pad::kA)) {
				_padADownAt = now;
				_padAHeld = false;
			}
			if ((b & Pad::kA) && !_padAHeld && now - _padADownAt >= std::chrono::milliseconds(kHoldMs)) {
				_padAHeld = true;
				Later([this]() { OnActivate(true); });
			} else if (released(Pad::kA) && !_padAHeld) {
				_padAHeld = true;
				Later([this]() { OnActivate(false); });
			}
		}

		// ---- Back held: forfeit (holding it is the "are you sure")
		if (pressed(Pad::kBack)) {
			_padBackDownAt = now;
			_padBackDone = false;
		}
		if ((b & Pad::kBack) && !_padBackDone && now - _padBackDownAt >= kBackHold) {
			_padBackDone = true;
			Later([this]() {
				if (!_picking && !_kitPlacing) {
					Forfeit(true);
				}
			});
		}

		_padWas = b;
		_padRtWas = pad.rt;
	}

	void Game::UpdatePadBlock()
	{
		// Pad users at the table: the D-pad and the menu buttons are ours, so the game's favourites and menus stay shut
		// (the keyboard has no such clash: our keys aren't the game's).
		const bool want = _padActive.load() && Settings::Get().padEnabled && (_aiming.load() || _picking.load() || _kitPlacing.load());
		if (want == _padBlocked) {
			return;
		}
		auto* manager = RE::BSInputEnableManager::GetSingleton();
		if (!manager) {
			return;
		}
		if (!_padLayer && !manager->AllocateNewLayer(_padLayer, "fo4pool controller")) {
			return;
		}
		using Flag = RE::UserEvents::USER_EVENT_FLAG;
		const auto flags = static_cast<Flag>(std::to_underlying(Flag::kMenu) | std::to_underlying(Flag::kMainFour) | std::to_underlying(Flag::kJumping));
		manager->EnableUserEvent(_padLayer->layerID, flags, !want, RE::UserEvents::SENDER_ID::kGameplay);
		_padBlocked = want;
	}
}
