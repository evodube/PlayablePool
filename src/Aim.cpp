// Aim mode: the shot camera, mouse aiming, hold-to-charge power, the guide line and the cue stick.
//
// The camera is the game's own free camera (the console's "tfc"). The game turns it with the mouse; every frame we
// read which way it faces and move it so it orbits the cue ball, so the player aims just by looking.

#include "Game.h"
#include "Anim.h"

#include "Audio.h"

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

		RE::NiPoint3 Normalized(const RE::NiPoint3& a_v)
		{
			const float l = std::sqrt(a_v.x * a_v.x + a_v.y * a_v.y + a_v.z * a_v.z);
			return l > 1e-6f ? RE::NiPoint3{ a_v.x / l, a_v.y / l, a_v.z / l } : RE::NiPoint3{ 0.0f, 0.0f, 0.0f };
		}
	}

	// ---------------------------------------------------------------- any thread

	float Game::PowerNow() const
	{
		return _charging ? _power.load() : 0.0f;
	}

	Game::HudState Game::Hud() const
	{
		HudState h;
		{
			std::scoped_lock lock(_hudMutex);
			h = _hud;
		}
		h.aiming = _aiming;
		h.charging = _charging;
		h.rolling = _rolling;
		h.overhead = _overhead;
		h.power = PowerNow();
		h.placing = _placing;
		return h;
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

		if (lmb && !_lmbWasDown && _placing) {
			if (tasks) {
				tasks->AddTask([this]() { ConfirmPlacement(); });
			}
		} else if (lmb && !_lmbWasDown && !_rolling) {
			_power = 0.0f;
			_chargeMouseEvents = 0;
			_chargeInputs = 0;
			_charging = true;  // the main thread freezes the camera and turns mouse pulls into power
		}
		if (!lmb && _lmbWasDown && _charging) {
			const float power = PowerNow();
			_charging = false;
			if (tasks) {
				tasks->AddTask([this, power]() { ShootAim(power); });
			}
		}
		if (rmb && !_rmbWasDown && tasks && !_placing) {
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
			Tell("Pool: stopped aiming");
		} else {
			EnterAim();
		}
	}

	void Game::EnterAim()
	{
		if (!_sim) {
			Notify(std::format("Pool: rack first ({})", Settings::Key(Settings::Get().keyRack)));
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
		if (auto* third = static_cast<RE::ThirdPersonState*>(camera->cameraStates[RE::CameraStates::k3rdPerson].get())) {
			_entryFreeRotation = third->freeRotation;
		}
		_haveEntryView = true;
		{
			std::scoped_lock lock(_camMutex);
			_camMapped = false;
			_mapFrames = 0;
			_haveCamTarget = false;
			_haveCamDesired = false;
			_haveCamUpdate = false;
		}
		_dotsWanted = 0;
		_overhead = false;
		_charging = false;
		_strikeShow = 0.0f;
		_aiming = true;
		BlockControls(true);
		log::info("aim mode on (player yaw {:.3f}, pitch {:.3f})", _entryYaw, _entryPitch);
		Tell(std::format("Pool: aim with the mouse. Hold left button and pull the mouse back for power, release to shoot. Right button: overhead. {}: stop",
			Settings::Key(Settings::Get().keyShoot)));
		if (_rules.BallInHand() && !_rules.Over()) {
			StartPlacement();
		}
		Anim::PlayAim();
	}

	void Game::ExitAim()
	{
		if (!_aiming) {
			return;
		}
		_aiming = false;
		_charging = false;
		_overhead = false;
		_placing = false;  // ball in hand is still owed: it starts again on the next aim
		Audio::SetListenerYaw(0.0f, false);
		HoldPlayerView();  // back exactly as it was when aiming started
		_haveEntryView = false;
		BlockControls(false);
		if (InFreeCamera()) {
			if (auto* camera = RE::PlayerCamera::GetSingleton()) {
				camera->ToggleFreeCameraMode(false);
			}
		}
		RemoveAimObjects();  // the stick is a real Pool Cue: don't leave it lying on the table
		Anim::Stop();
		log::info("aim mode off");
	}

	void Game::ToggleOverhead()
	{
		if (!_aiming || _placing) {
			return;
		}
		_overhead = !_overhead;
		std::scoped_lock lock(_camMutex);
		if (!_overhead && _camMapped) {
			if (auto* free = FreeCamera()) {
				Component(free, 1 - _yawIndex) = _pitchSign * Settings::Get().camPitch * kDegToRad;
			}
		}
	}

	void Game::ShootAim(float a_power)
	{
		if (!_aiming || !_sim || !_sim->AtRest() || IsGamePaused() || _placing || _shotLive) {
			return;
		}
		if (_rules.Over()) {
			Notify(std::format("Pool: the game's over. {} racks again", Settings::Key(Settings::Get().keyRack)));
			return;
		}
		if (a_power < 0.02f) {
			Tell("Pool: no power, no shot");
			return;
		}
		const auto& s = Settings::Get();
		const float speed = s.minSpeed + (s.maxSpeed - s.minSpeed) * std::pow(a_power, 1.5f);
		_strikePos = _sim->Balls()[0].pos;
		_strikeShow = kStrikeShowSeconds;
		if (Frame frame; GetFrame(frame)) {
			Audio::Play(Audio::Bank::kCue, ToWorld(frame, _strikePos, static_cast<float>(s.spec.ballRadius)), 0.25f + 0.75f * a_power);
		}
		_rules.BeginShot(*_sim, CalledPocket());
		_shotLive = true;
		_sim->Strike(_lastDir, speed);
		Anim::PlayStrike();
		log::info("shot: power {:.0f}% speed {:.0f} table dir ({:.3f}, {:.3f}); charge input: {} mouse events, {} camera turns; {:.4f} deg/count",
			a_power * 100.0f, speed, _lastDir.x, _lastDir.y, _chargeMouseEvents, _chargeInputs, DegreesPerCount());
	}

	void Game::TickAim(const Frame& a_frame, [[maybe_unused]] float a_dt)
	{
		auto* free = FreeCamera();
		if (!free || !InFreeCamera()) {
			// something else took the camera back (a menu, a scene): leave aim mode quietly
			log::info("free camera ended outside fo4pool; leaving aim mode");
			Audio::SetListenerYaw(0.0f, false);
			HoldPlayerView();
			_haveEntryView = false;
			_aiming = false;
			_charging = false;
			_overhead = false;
			_placing = false;
			BlockControls(false);
			RemoveAimObjects();
			Anim::Stop();
			return;
		}

		const auto& s = Settings::Get();
		const float r = static_cast<float>(s.spec.ballRadius);

		// What the camera looks at. The camera itself is moved in OnFreeCameraUpdate, inside the game's own camera
		// update, so its position and angle always come from the same frame.
		const bool   atRest = _sim->AtRest();
		const auto&  cue = _sim->Balls()[0];
		const RE::NiPoint3 desired = _overhead ? ToWorld(a_frame, { 0.0, 0.0 }, 0.0f) : ToWorld(a_frame, cue.pos, r);
		{
			std::scoped_lock lock(_camMutex);
			_camDesired = desired;
			_camFollow = atRest || _overhead;  // don't chase the cue ball while it rolls
			_haveCamDesired = true;
		}

		if (_placing) {
			TickPlacement(a_frame);
		}

		// The camera update places the stick and the guide (so they move in step with the view) but doesn't create
		// objects; that happens here, a frame ahead of when they're needed.
		auto* table = _table.get().get();
		if (table && atRest && cue.state == pool::BallState::kOnTable) {
			if (!_stick) {
				if (auto* form = StickForm()) {
					_stick = Spawn(form, table, ToWorld(a_frame, cue.pos, r));
				} else if (!_stickWarned && s.stickFormID != 0) {
					_stickWarned = true;
					Notify(std::format("Pool: no cue stick: {}|{:X} not found. Is the new fo4pool.esl installed?", s.stickPlugin, s.stickFormID));
				}
			}
			auto* dotForm = BallForm(0);
			while (s.guide && dotForm && _dots.size() < std::min(_dotsWanted + 8, kMaxDots)) {
				_dots.push_back(Spawn(dotForm, table, ToWorld(a_frame, cue.pos, s.guideDotRadius)));
			}
		}
	}

	void Game::PlaceAimObjects(const Frame& a_frame, float a_yaw, float a_dt)
	{
		const auto& s = Settings::Get();

		// aim along where the camera faces, flattened onto the table
		const RE::NiPoint3 world{ std::sin(a_yaw), std::cos(a_yaw), 0.0f };
		const RE::NiPoint3 table = a_frame.rot * world;
		if (std::abs(table.x) + std::abs(table.y) > 1e-4f) {
			_lastDir = pool::Vec2{ table.x, table.y } * (1.0 / pool::Vec2{ table.x, table.y }.Length());
		}

		const auto& cue = _sim->Balls()[0];
		if (_placing) {
			HideAimObjects();
			return;
		}
		if (_sim->AtRest() && cue.state == pool::BallState::kOnTable) {
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

	// ---------------------------------------------------------------- the game's camera update

	namespace
	{
		// until something has been learned (charging straight away without having aimed)
		constexpr double kDefaultDegreesPerCount = 0.05;
	}

	void Game::AddPull(double a_degrees)
	{
		const auto& s = Settings::Get();
		const float degrees = static_cast<float>(a_degrees) * (s.invertPull ? -1.0f : 1.0f);
		_power = std::clamp(_power.load() + degrees * s.pullPerDegree, 0.0f, 1.0f);
	}

	double Game::DegreesPerCount() const
	{
		if (_sumAbsCounts < 300.0 || _sumAbsTilt < 1e-3) {
			return kDefaultDegreesPerCount;
		}
		const double slope = _sumAbsTilt / _sumAbsCounts;
		return _sumCross < 0.0 ? -slope : slope;
	}

	bool Game::OnLookMouse(const RE::MouseMoveEvent* a_event)
	{
		if (!_aiming || !a_event) {
			return false;
		}
		std::scoped_lock lock(_camMutex);
		if (!_camMapped) {
			return false;
		}
		// the same event can reach both handlers; count it once
		const bool repeat = a_event == _lastMouseEvent && a_event->timeCode == _lastMouseTime;
		_lastMouseEvent = a_event;
		_lastMouseTime = a_event->timeCode;

		if (_placing) {
			if (!repeat) {
				_placeDX += a_event->mouseInputX;
				_placeDY += a_event->mouseInputY;
			}
			return true;  // the mouse moves the cue ball, not the view
		}
		if (!_charging) {
			if (!repeat) {
				_frameCounts += a_event->mouseInputY;
			}
			return false;
		}
		if (!repeat) {
			++_chargeMouseEvents;
			AddPull(static_cast<double>(a_event->mouseInputY) * DegreesPerCount());  // back = view would tilt down = more
		}
		return true;
	}

	void Game::BeforeFreeCameraUpdate(RE::FreeCameraState* a_free)
	{
		if (!_aiming || !(_charging || _placing) || !a_free) {
			return;
		}
		if (auto* camera = a_free->camera) {
			camera->rotationInput.x = 0.0f;
			camera->rotationInput.y = 0.0f;
		}
		if (auto* controls = RE::PlayerControls::GetSingleton()) {
			auto& d = controls->data;
			d.lookInputVec = {};
			d.lookInputVecNormalized = {};
			d.prevLookVec = {};
			d.rotationSpeedSpring.position = {};
			d.rotationSpeedSpring.velocity = {};
		}
	}

	void Game::OnFreeCameraLook(RE::FreeCameraState* a_free, const RE::BSTPoint2<float>& a_before)
	{
		if (!_aiming || !a_free) {
			return;
		}
		std::scoped_lock lock(_camMutex);
		if (!_camMapped) {
			return;
		}
		const auto& s = Settings::Get();
		const int   pitchIndex = 1 - _yawIndex;
		const float before = pitchIndex == 0 ? a_before.x : a_before.y;
		const double tilted = _pitchSign * (Component(a_free, pitchIndex) - before) / kDegToRad;

		if (_placing) {
			a_free->rotation = a_before;  // the view holds still, looking straight down, while the cue ball is moved
			Component(a_free, pitchIndex) = _pitchSign * kOverheadPitch;
			return;
		}
		if (_charging) {
			// If the mouse never reached a handler we could catch, the turn that got through is the pull instead
			// (how 0.2 did it). Either way the view goes back to where it was.
			if (_chargeMouseEvents == 0 && tilted != 0.0 && std::abs(tilted) < 30.0) {
				++_chargeInputs;
				AddPull(tilted);
			}
			a_free->rotation = a_before;
			return;
		}

		if (std::abs(tilted) < 30.0) {
			_frameTilt += tilted;
		}
		const float pitch = _pitchSign * Component(a_free, pitchIndex);
		const float wanted = _overhead ? kOverheadPitch : std::clamp(pitch, s.camMinPitch * kDegToRad, s.camMaxPitch * kDegToRad);
		if (wanted != pitch) {
			Component(a_free, pitchIndex) = _pitchSign * wanted;
		}
	}

	bool Game::CamTranslation(const RE::FreeCameraState* a_free, RE::NiPoint3& a_out) const
	{
		if (!_camMapped || !_haveCamTarget) {
			return false;
		}
		const auto& s = Settings::Get();
		const float yaw = _yawIndex == 0 ? a_free->rotation.x : a_free->rotation.y;
		const float pitch = _pitchSign * (_yawIndex == 0 ? a_free->rotation.y : a_free->rotation.x);
		const float distance = _overhead ? s.overheadHeight : s.camDistance;
		const RE::NiPoint3 forward{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) };
		a_out = _camTarget - forward * distance;
		return true;
	}

	void Game::OnFreeCameraUpdate(RE::FreeCameraState* a_free)
	{
		if (!_aiming || !a_free) {
			return;
		}
		const auto& s = Settings::Get();
		float       yaw = 0.0f;
		float       camDt = 0.0f;
		{
			std::scoped_lock lock(_camMutex);

			// The free camera keeps two angles; work out once which one is yaw by comparing with where the player
			// faced when aiming started (the free camera starts there).
			if (!_camMapped) {
				const float a = a_free->rotation.x;
				const float b = a_free->rotation.y;
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
				Component(a_free, 1 - _yawIndex) = _pitchSign * s.camPitch * kDegToRad;
				_camMapped = true;
			}

			// ease the point the camera orbits towards the cue ball (it moves after each shot)
			const auto  now = std::chrono::steady_clock::now();
			const float dt = _haveCamUpdate ? std::min(0.1f, std::chrono::duration<float>(now - _lastCamUpdate).count()) : 0.0f;
			_lastCamUpdate = now;
			_haveCamUpdate = true;
			if (_haveCamDesired) {
				if (!_haveCamTarget) {
					_camTarget = _camDesired;
					_haveCamTarget = true;
				} else if (_camFollow) {
					_camTarget = _camTarget + (_camDesired - _camTarget) * std::min(1.0f, dt * 5.0f);
				}
			}
			camDt = dt;

			// learn the mouse sensitivity from this frame's movement and tilt (only while not charging)
			if (!_charging && (_frameCounts != 0.0 || _frameTilt != 0.0)) {
				constexpr double kForget = 0.995;
				_sumAbsCounts = _sumAbsCounts * kForget + std::abs(_frameCounts);
				_sumAbsTilt = _sumAbsTilt * kForget + std::abs(_frameTilt);
				_sumCross = _sumCross * kForget + _frameCounts * _frameTilt;
			}
			_frameCounts = 0.0;
			_frameTilt = 0.0;

			yaw = Component(a_free, _yawIndex);
			_camYaw = yaw;
			Audio::SetListenerYaw(yaw, true);
			RE::NiPoint3 pos;
			if (CamTranslation(a_free, pos)) {
				a_free->translation = pos;  // the game's own movement never gets a say
			}
		}

		// The mouse that turns the shot camera also reaches the player's own look controls; undo that every frame so
		// the player (and the view they get back) doesn't drift while aiming.
		HoldPlayerView();

		// The stick and the guide follow the aim here, in the same update as the camera, so they never trail it.
		Frame frame;
		if (_sim && GetFrame(frame)) {
			PlaceAimObjects(frame, yaw, camDt);
		}
	}

	bool Game::FreeCameraTranslation(const RE::FreeCameraState* a_free, RE::NiPoint3& a_out)
	{
		if (!_aiming || !a_free) {
			return false;
		}
		std::scoped_lock lock(_camMutex);
		return CamTranslation(a_free, a_out);
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
		const float      dotRadius = s.guideDotRadius;
		auto line = [&](pool::Vec2 a_from, pool::Vec2 a_dir2, double a_start, double a_length) {
			for (double t = a_start; t <= a_length && dots.size() < kMaxDots - 1; t += s.guideSpacing) {
				dots.push_back({ a_from + a_dir2 * t, dotRadius });
			}
		};

		const double travel = std::min(prediction.distance, static_cast<double>(s.guideMaxLength));
		line(cue.pos, a_dir, r + s.guideSpacing, travel - r);
		if (prediction.distance <= s.guideMaxLength) {
			dots.push_back({ prediction.contact, dotRadius * 2.5f });  // where the cue ball will be at contact
			if (prediction.hitBall) {
				const auto& target = _sim->Balls()[prediction.ball].pos;
				line(target, prediction.objectDir, r + s.guideSpacing, r + s.guideObjectLength);
				line(prediction.contact, prediction.cueDir, r + s.guideSpacing, r + s.guideCueLength * prediction.cueSpeed);
			} else {
				line(prediction.contact, prediction.cueDir, r + s.guideSpacing, r + s.guideCueLength);
			}
		}

		// On the 8: the pocket it's heading for is the called one (unless one was picked with the key); mark it.
		if (prediction.hitBall && prediction.ball == 8) {
			const auto& eight = _sim->Balls()[8].pos;
			double      best = -2.0;
			for (int p = 0; p < pool::Sim::kPockets; ++p) {
				pool::Vec2 mouth, inward;
				_sim->PocketMouth(p, mouth, inward);
				const pool::Vec2 to = mouth - eight;
				const double     l = to.Length();
				const double     c = l > 0.0 ? to.Dot(prediction.objectDir) / l : -1.0;
				if (c > best) {
					best = c;
					_autoPocket = p;
				}
			}
		}
		if (const int called = CalledPocket(); called >= 0) {
			pool::Vec2 mouth, inward;
			_sim->PocketMouth(called, mouth, inward);
			if (dots.size() >= kMaxDots) {
				dots.pop_back();
			}
			dots.push_back({ mouth - inward * r, dotRadius * 4.0f });  // a big dot just in front of the opening
		}

		_dotsWanted = dots.size();  // the tick makes more dot objects if these aren't enough

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
		const auto& s = Settings::Get();
		const float r = static_cast<float>(s.spec.ballRadius);
		auto        ref = _stick.get();  // made by the tick
		if (!ref) {
			return;
		}
		if (!_stickLogged && ref->Get3D()) {
			_stickLogged = true;
			LogShape(ref.get(), "cue stick");
			// weapon meshes carry a blood overlay shape for when they've hit someone; a pool cue doesn't need it
			RE::BSVisit::TraverseScenegraphGeometries(ref->Get3D(), [](RE::BSGeometry* a_geom) {
				std::string name = a_geom->name.c_str();
				std::ranges::transform(name, name.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
				if (name.find("blood") != std::string::npos) {
					a_geom->SetAppCulled(true);
				}
				return RE::BSVisitControl::kContinue;
			});
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

	void Game::HoldPlayerView()
	{
		if (!_haveEntryView) {
			return;
		}
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			player->data.angle.x = _entryPitch;
			player->data.angle.z = _entryYaw;
		}
		if (auto* camera = RE::PlayerCamera::GetSingleton()) {
			if (auto* third = static_cast<RE::ThirdPersonState*>(camera->cameraStates[RE::CameraStates::k3rdPerson].get())) {
				third->freeRotation = _entryFreeRotation;
			}
		}
	}

	void Game::BlockControls(bool a_block)
	{
		if (a_block == _controlsBlocked) {
			return;
		}
		auto* manager = RE::BSInputEnableManager::GetSingleton();
		if (!manager) {
			return;
		}
		if (!_inputLayer && !manager->AllocateNewLayer(_inputLayer, "fo4pool aiming")) {
			log::warn("could not get an input layer; controls stay on while aiming");
			return;
		}
		// Mouse look stays on (it aims). Everything that would draw a weapon, fire, walk off or open the Pip-Boy
		// is off while aiming.
		using Flag = RE::UserEvents::USER_EVENT_FLAG;
		const auto flags = static_cast<Flag>(
			std::to_underlying(Flag::kMovement) | std::to_underlying(Flag::kActivate) | std::to_underlying(Flag::kPOVSwitch) |
			std::to_underlying(Flag::kFighting) | std::to_underlying(Flag::kSneaking) | std::to_underlying(Flag::kMainFour) |
			std::to_underlying(Flag::kWheelZoom) | std::to_underlying(Flag::kJumping) | std::to_underlying(Flag::kVATS));
		manager->EnableUserEvent(_inputLayer->layerID, flags, !a_block, RE::UserEvents::SENDER_ID::kGameplay);
		_controlsBlocked = a_block;
		log::info("player controls {}", a_block ? "blocked for aiming" : "restored");
	}

	void Game::LogShape(RE::TESObjectREFR* a_ref, const char* a_what)
	{
		auto* root = a_ref ? a_ref->Get3D() : nullptr;
		if (!root) {
			return;
		}
		const auto& w = root->GetWorldTransform();
		const float s = w.scale != 0.0f ? w.scale : 1.0f;
		log::info("{}: base {:08X} '{}'", a_what, a_ref->GetObjectReference() ? a_ref->GetObjectReference()->GetFormID() : 0,
			ModelPath(a_ref));
		RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* a_geom) {
			const auto& b = a_geom->worldBound;
			const auto  c = (w.rotate * (b.center - w.translate)) * (1.0f / s);
			const auto& gw = a_geom->GetWorldTransform();
			const auto  gl = (w.rotate * (gw.translate - w.translate)) * (1.0f / s);
			log::info("  shape '{}': bound centre ({:.2f}, {:.2f}, {:.2f}) radius {:.2f} | node at ({:.2f}, {:.2f}, {:.2f})",
				a_geom->name.c_str(), c.x, c.y, c.z, b.fRadius / s, gl.x, gl.y, gl.z);
			return RE::BSVisitControl::kContinue;
		});
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
		_stickLogged = false;
	}
}
