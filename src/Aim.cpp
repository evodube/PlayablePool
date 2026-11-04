// Aim mode: the shot camera, mouse aiming, hold-to-charge power, the guide line and the cue stick.
//
// The camera is the game's own free camera (the console's "tfc"). The game turns it with the mouse; every frame we
// read which way it faces and move it so it orbits the cue ball, so the player aims just by looking.

#include "Game.h"

#include "Settings.h"
#include "Util.h"

namespace fp
{
	namespace
	{
		constexpr float kDegToRad = 0.017453292f;
		constexpr float kOverheadPitch = 89.0f * kDegToRad;
		constexpr float kStrikeShowSeconds = 0.15f;
		constexpr std::size_t kMaxDots = 96;

		RE::FreeCameraState* FreeCamera()
		{
			auto* camera = RE::PlayerCamera::GetSingleton();
			return camera ? static_cast<RE::FreeCameraState*>(camera->cameraStates[RE::CameraStates::kFree].get()) : nullptr;
		}

		bool InFreeCamera()
		{
			auto* camera = RE::PlayerCamera::GetSingleton();
			auto* free = FreeCamera();
			return camera && free && camera->currentState.get() == free;
		}

		float& Component(RE::FreeCameraState* a_state, int a_index)
		{
			return a_index == 0 ? a_state->rotation.x : a_state->rotation.y;
		}

		std::int64_t NowTicks()
		{
			return std::chrono::steady_clock::now().time_since_epoch().count();
		}

		RE::NiPoint3 Normalized(const RE::NiPoint3& a_v)
		{
			const float l = std::sqrt(a_v.x * a_v.x + a_v.y * a_v.y + a_v.z * a_v.z);
			return l > 1e-6f ? RE::NiPoint3{ a_v.x / l, a_v.y / l, a_v.z / l } : RE::NiPoint3{ 0.0f, 0.0f, 0.0f };
		}
	}

	// ---------------------------------------------------------------- any thread

	float Game::PowerNow() const
	{
		if (!_charging) {
			return 0.0f;
		}
		const auto  elapsed = std::chrono::steady_clock::duration(NowTicks() - _chargeStart.load());
		const float seconds = std::chrono::duration<float>(elapsed).count();
		return std::clamp(seconds / Settings::Get().chargeTime, 0.0f, 1.0f);
	}

	Game::HudState Game::Hud() const
	{
		return { _aiming.load(), _charging.load(), _rolling.load(), _overhead.load(), PowerNow() };
	}

	// ---------------------------------------------------------------- render thread

	void Game::PollMouse()
	{
		if (!_aiming || !util::GameHasFocus()) {
			_lmbWasDown = _rmbWasDown = false;
			_charging = false;
			return;
		}
		const bool lmb = (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
		const bool rmb = (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
		auto*      tasks = F4SE::GetTaskInterface();

		if (lmb && !_lmbWasDown && !_rolling) {
			_chargeStart = NowTicks();
			_charging = true;
		}
		if (!lmb && _lmbWasDown && _charging) {
			const float power = PowerNow();
			_charging = false;
			if (tasks) {
				tasks->AddTask([this, power]() { ShootAim(power); });
			}
		}
		if (rmb && !_rmbWasDown && tasks) {
			tasks->AddTask([this]() { ToggleOverhead(); });
		}
		_lmbWasDown = lmb;
		_rmbWasDown = rmb;
	}

	// ---------------------------------------------------------------- main thread

	void Game::ToggleAim()
	{
		if (_aiming) {
			ExitAim();
			Notify("Pool: stopped aiming");
		} else {
			EnterAim();
		}
	}

	void Game::EnterAim()
	{
		if (!_sim) {
			Notify("Pool: rack first (Numpad 7)");
			return;
		}
		if (!_sim->AtRest()) {
			Notify("Pool: wait for the balls to stop");
			return;
		}
		Frame frame;
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* camera = RE::PlayerCamera::GetSingleton();
		if (!player || !camera || !FreeCamera() || !GetFrame(frame)) {
			return;
		}

		if (!InFreeCamera()) {
			camera->ToggleFreeCameraMode(false);
		}
		_entryYaw = player->data.angle.z;
		_entryPitch = player->data.angle.x;
		_camMapped = false;
		_mapFrames = 0;
		_haveCamTarget = false;
		_overhead = false;
		_charging = false;
		_strikeShow = 0.0f;
		_aiming = true;
		log::info("aim mode on (player yaw {:.3f}, pitch {:.3f})", _entryYaw, _entryPitch);
		Notify("Pool: aim with the mouse. Hold left button to charge, release to shoot. Right button: overhead. Numpad 8: stop");
	}

	void Game::ExitAim()
	{
		if (!_aiming) {
			return;
		}
		_aiming = false;
		_charging = false;
		_overhead = false;
		if (InFreeCamera()) {
			if (auto* camera = RE::PlayerCamera::GetSingleton()) {
				camera->ToggleFreeCameraMode(false);
			}
		}
		HideAimObjects();
		log::info("aim mode off");
	}

	void Game::ToggleOverhead()
	{
		if (!_aiming) {
			return;
		}
		_overhead = !_overhead;
		if (!_overhead && _camMapped) {
			if (auto* free = FreeCamera()) {
				Component(free, 1 - _yawIndex) = _pitchSign * Settings::Get().camPitch * kDegToRad;
			}
		}
	}

	void Game::ShootAim(float a_power)
	{
		if (!_aiming || !_sim || !_sim->AtRest() || IsGamePaused()) {
			return;
		}
		const auto& s = Settings::Get();
		const float speed = s.minSpeed + (s.maxSpeed - s.minSpeed) * std::pow(a_power, 1.5f);
		_strikePos = _sim->Balls()[0].pos;
		_strikeShow = kStrikeShowSeconds;
		_sim->Strike(_lastDir, speed);
		log::info("shot: power {:.0f}% speed {:.0f} table dir ({:.3f}, {:.3f})", a_power * 100.0f, speed, _lastDir.x, _lastDir.y);
	}

	void Game::TickAim(const Frame& a_frame, float a_dt)
	{
		auto* free = FreeCamera();
		if (!free || !InFreeCamera()) {
			// something else took the camera back (a menu, a scene): leave aim mode quietly
			log::info("free camera ended outside fo4pool; leaving aim mode");
			_aiming = false;
			_charging = false;
			_overhead = false;
			HideAimObjects();
			return;
		}

		const auto& s = Settings::Get();
		const float r = static_cast<float>(s.spec.ballRadius);

		// The free camera keeps two angles; work out once which one is yaw by comparing with where the player
		// faced when aiming started (the free camera starts there).
		if (!_camMapped) {
			const float a = free->rotation.x;
			const float b = free->rotation.y;
			if (a == 0.0f && b == 0.0f && ++_mapFrames < 10) {
				return;  // not set up yet
			}
			const float errA = std::abs(util::AngleDiff(a, _entryYaw));
			const float errB = std::abs(util::AngleDiff(b, _entryYaw));
			_yawIndex = errA <= errB ? 0 : 1;
			const float pitchValue = _yawIndex == 0 ? b : a;
			_pitchSign = 1.0f;
			if (std::abs(_entryPitch) > 0.05f && std::abs(pitchValue) > 0.05f && (pitchValue > 0.0f) != (_entryPitch > 0.0f)) {
				_pitchSign = -1.0f;
			}
			log::info("free camera rotation ({:.3f}, {:.3f}) vs player yaw {:.3f} pitch {:.3f}: yaw is value {}, pitch sign {}", a, b,
				_entryYaw, _entryPitch, _yawIndex, _pitchSign);
			Component(free, 1 - _yawIndex) = _pitchSign * s.camPitch * kDegToRad;
			_camMapped = true;
		}

		const float yaw = Component(free, _yawIndex);
		float       pitch = _pitchSign * Component(free, 1 - _yawIndex);
		const float wanted = _overhead ? kOverheadPitch : std::clamp(pitch, s.camMinPitch * kDegToRad, s.camMaxPitch * kDegToRad);
		if (wanted != pitch) {
			pitch = wanted;
			Component(free, 1 - _yawIndex) = _pitchSign * pitch;
		}

		// what the camera looks at
		const bool   atRest = _sim->AtRest();
		const auto&  cue = _sim->Balls()[0];
		RE::NiPoint3 desired = _overhead ? ToWorld(a_frame, { 0.0, 0.0 }, 0.0f) : ToWorld(a_frame, cue.pos, r);
		if (!_haveCamTarget) {
			_camTarget = desired;
			_haveCamTarget = true;
		} else if (atRest || _overhead) {
			const float k = std::min(1.0f, a_dt * 5.0f);
			_camTarget = _camTarget + (desired - _camTarget) * k;
		}
		const float        distance = _overhead ? s.overheadHeight : s.camDistance;
		const RE::NiPoint3 forward{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) };
		free->translation = _camTarget - forward * distance;

		// aim along where the camera faces, flattened onto the table
		const RE::NiPoint3 world{ std::sin(yaw), std::cos(yaw), 0.0f };
		const RE::NiPoint3 table = a_frame.rot * world;
		if (std::abs(table.x) + std::abs(table.y) > 1e-4f) {
			_lastDir = pool::Vec2{ table.x, table.y } * (1.0 / pool::Vec2{ table.x, table.y }.Length());
		}

		if (atRest && cue.state == pool::BallState::kOnTable) {
			if (s.guide) {
				PlaceGuide(a_frame, _lastDir);
			}
			PlaceStick(a_frame, cue.pos, _lastDir, PowerNow() * s.stickMaxPull, true);
		} else {
			for (auto& h : _dots) {
				if (auto ref = h.get()) {
					PlaceNode(ref.get(), {}, {}, 1.0f, true);
				}
			}
			const bool striking = _strikeShow > 0.0f;
			_strikeShow = std::max(0.0f, _strikeShow - a_dt);
			PlaceStick(a_frame, _strikePos, _lastDir, 0.0f, striking);
		}
	}

	void Game::PlaceGuide(const Frame& a_frame, pool::Vec2 a_dir)
	{
		const auto& s = Settings::Get();
		const auto  prediction = _sim->Predict(a_dir);
		const auto& cue = _sim->Balls()[0];
		const double r = s.spec.ballRadius;

		struct Dot
		{
			pool::Vec2 pos;
			float      radius;
		};
		std::vector<Dot> dots;
		const float      small = s.guideDotRadius;
		auto line = [&](pool::Vec2 a_from, pool::Vec2 a_dir2, double a_start, double a_length) {
			for (double t = a_start; t <= a_length && dots.size() < kMaxDots - 1; t += s.guideSpacing) {
				dots.push_back({ a_from + a_dir2 * t, small });
			}
		};

		const double travel = std::min(prediction.distance, static_cast<double>(s.guideMaxLength));
		line(cue.pos, a_dir, r + s.guideSpacing, travel - r);
		if (prediction.distance <= s.guideMaxLength) {
			dots.push_back({ prediction.contact, small * 2.5f });  // where the cue ball will be at contact
			if (prediction.hitBall) {
				const auto& target = _sim->Balls()[prediction.ball].pos;
				line(target, prediction.objectDir, r + s.guideSpacing, r + s.guideObjectLength);
				line(prediction.contact, prediction.cueDir, r + s.guideSpacing, r + s.guideCueLength * prediction.cueSpeed);
			} else {
				line(prediction.contact, prediction.cueDir, r + s.guideSpacing, r + s.guideCueLength);
			}
		}

		// enough dot objects for this frame
		auto* table = _table.get().get();
		auto* form = BallForm(0);
		while (_dots.size() < dots.size() && table && form) {
			_dots.push_back(Spawn(form, table, ToWorld(a_frame, cue.pos, small)));
		}

		for (std::size_t i = 0; i < _dots.size(); ++i) {
			auto ref = _dots[i].get();
			if (!ref) {
				continue;
			}
			if (i < dots.size()) {
				const float scale = BallScale(ref->Get3D()) * (dots[i].radius / static_cast<float>(r)) * a_frame.scale;
				PlaceNode(ref.get(), ToWorld(a_frame, dots[i].pos, dots[i].radius), a_frame.rot, scale, false);
			} else {
				PlaceNode(ref.get(), {}, {}, 1.0f, true);
			}
		}
	}

	void Game::PlaceStick(const Frame& a_frame, pool::Vec2 a_cue, pool::Vec2 a_dir, float a_pull, bool a_show)
	{
		auto* form = StickForm();
		if (!form) {
			return;
		}
		const auto& s = Settings::Get();
		const float r = static_cast<float>(s.spec.ballRadius);
		if (!_stick) {
			if (auto* table = _table.get().get()) {
				_stick = Spawn(form, table, ToWorld(a_frame, a_cue, r));
			}
		}
		auto ref = _stick.get();
		if (!ref) {
			return;
		}
		if (!a_show) {
			PlaceNode(ref.get(), {}, {}, 1.0f, true);
			return;
		}

		// the tip sits behind the cue ball at its centre height, the butt raised a little
		const pool::Vec2   tipTable = a_cue - a_dir * (r + s.stickGap + a_pull);
		const RE::NiPoint3 tip = ToWorld(a_frame, tipTable, r);
		const RE::NiPoint3 aimWorld = a_frame.rot.Transpose() * RE::NiPoint3{ static_cast<float>(a_dir.x), static_cast<float>(a_dir.y), 0.0f };
		const float        e = s.stickElevation * kDegToRad;
		const RE::NiPoint3 toTip = Normalized(RE::NiPoint3{ aimWorld.x * std::cos(e), aimWorld.y * std::cos(e), -std::sin(e) });

		// the mesh axis the stick runs along, pointing to whichever end its +direction reaches
		const RE::NiPoint3 axis = s.stickTip >= s.stickButt ? toTip : toTip * -1.0f;
		const RE::NiPoint3 up{ 0.0f, 0.0f, 1.0f };
		RE::NiPoint3       x, y, z;
		switch (s.stickAxis) {
		case 0:
			x = axis;
			y = Normalized(up.Cross(x));
			z = x.Cross(y);
			break;
		case 2:
			z = axis;
			x = Normalized(z.Cross(up));
			y = z.Cross(x);
			break;
		default:
			y = axis;
			x = Normalized(y.Cross(up));
			z = x.Cross(y);
			break;
		}
		const RE::NiPoint3 origin = tip - axis * (s.stickTip * a_frame.scale);
		PlaceNode(ref.get(), origin, util::FromAxes(x, y, z), a_frame.scale, false);
	}

	void Game::HideAimObjects()
	{
		for (auto& h : _dots) {
			if (auto ref = h.get()) {
				PlaceNode(ref.get(), {}, {}, 1.0f, true);
			}
		}
		if (auto ref = _stick.get()) {
			PlaceNode(ref.get(), {}, {}, 1.0f, true);
		}
	}

	void Game::RemoveAimObjects()
	{
		auto remove = [](RE::ObjectRefHandle& a_handle) {
			if (auto ref = a_handle.get()) {
				ref->Disable();
				ref->SetWantsDelete(true);
			}
			a_handle = {};
		};
		for (auto& h : _dots) {
			remove(h);
		}
		_dots.clear();
		remove(_stick);
	}
}
