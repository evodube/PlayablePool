#include "Game.h"

#include "Anim.h"
#include "Audio.h"
#include "Hud.h"

#include "Settings.h"
#include "Util.h"

namespace fp
{
	namespace
	{
		constexpr float kDropSeconds = 0.3f;     // a pocketed ball sinks out of sight over this long
		constexpr double kReturnShortest = 1.5;  // seconds of rolling from the pockets nearest the ball return
		constexpr double kReturnLongest = 5.5;   // from the far corners (the whole sound)
		constexpr float kMaxFrame = 0.05f;    // longer frames (hitches) are clamped so balls don't jump through things

		using util::FromRows;
		using util::GameWindow;

		using util::ForEachLoadedCell;

		// Table x/y -> mesh x/y. With iLengthAxis=1 the table's length runs along the mesh's Y axis.
		RE::NiMatrix3 AxisSwap(int a_lengthAxis)
		{
			if (a_lengthAxis == 1) {
				return RE::NiMatrix3(
					0.0f, -1.0f, 0.0f, 0.0f,
					1.0f, 0.0f, 0.0f, 0.0f,
					0.0f, 0.0f, 1.0f, 0.0f);
			}
			RE::NiMatrix3 m;
			m.MakeIdentity();
			return m;
		}

		const char* BallName(int a_index)
		{
			static const std::array<std::string, pool::Sim::kBalls> names = [] {
				std::array<std::string, pool::Sim::kBalls> n;
				n[0] = "cue ball";
				for (int i = 1; i < pool::Sim::kBalls; ++i) {
					n[i] = std::format("{}-ball", i);
				}
				return n;
			}();
			return names[a_index].c_str();
		}
	}

	// ---------------------------------------------------------------- render thread

	void Game::OnPresent()
	{
		PollKeys();
		PollMouse();
		if (_tickPending.exchange(true)) {
			return;
		}
		auto* tasks = F4SE::GetTaskInterface();
		if (!tasks) {
			_tickPending = false;
			return;
		}
		tasks->AddTask([this]() {
			Tick();
			_tickPending = false;
		});
	}

	void Game::PollKeys()
	{
		const HWND window = GameWindow();
		if (window && ::GetForegroundWindow() != window) {
			return;
		}
		const auto&              s = Settings::Get();
		const std::array<int, 6> keys{ s.keyRack, s.keyShoot, s.keyClear, s.keyCalibrate, s.keyCallPocket, s.keyChallenge };
		for (std::size_t i = 0; i < keys.size(); ++i) {
			if (keys[i] <= 0) {
				continue;
			}
			const bool down = (::GetAsyncKeyState(keys[i]) & 0x8000) != 0;
			if (down && !_keyWasDown[i]) {
				if (auto* tasks = F4SE::GetTaskInterface()) {
					tasks->AddTask([this, i]() {
						if (IsGamePaused()) {
							return;  // menus, console (typing numbers), loading
						}
						switch (i) {
						case 0: Rack(); break;
						case 1: ToggleAim(); break;
						case 2:
							if (_opponent) {
								EndChallenge("");
							}
							Clear();
							break;
						case 3: ToggleCalibrate(); break;
						case 4: CyclePocket(); break;
						default: Challenge(); break;
						}
					});
				}
			}
			_keyWasDown[i] = down;
		}
	}

	// ---------------------------------------------------------------- main thread

	bool Game::IsGamePaused()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return false;
		}
		if (ui->menuMode != 0 || ui->freezeFramePause != 0) {
			return true;
		}
		static const std::array<RE::BSFixedString, 6> menus{
			RE::BSFixedString("PauseMenu"), RE::BSFixedString("PipboyMenu"), RE::BSFixedString("LoadingMenu"),
			RE::BSFixedString("MainMenu"), RE::BSFixedString("Console"), RE::BSFixedString("WorkshopMenu")
		};
		return std::ranges::any_of(menus, [&](const RE::BSFixedString& a_name) { return ui->GetMenuOpen(a_name); });
	}

	void Game::Notify(const std::string& a_message)
	{
		log::info("{}", a_message);
		RE::SendHUDMessage::ShowHUDMessage(a_message.c_str(), nullptr, false, false);
	}

	void Game::Tell(const std::string& a_message)
	{
		if (Settings::Get().notifications || !Hud::Showing()) {
			Notify(a_message);
		}
	}

	void Game::Forget()
	{
		_table = {};
		_sim.reset();
		_balls.fill({});
		_markers.clear();
		_frozen.clear();
		_glow3D.fill(nullptr);
		_haveLast = false;
		_scratch = false;
		// the camera is reset with the load; just drop our side of aim mode
		BlockControls(false);
		_aiming = false;
		_charging = false;
		_overhead = false;
		_rolling = false;
		_dots.clear();
		_stick = {};
		// a load: the opponent's still in the world, but the game's gone
		_opponent = {};
		_npcMarker = {};
		_npcWaitMarker = {};
		_npcStep = NpcStep::kNone;
		_autoAim = false;
		_npcHeld = false;
		_npcLooking = false;
		_npcStick = {};
		_npcStickReady = false;
		_npcWaitIdle = false;
		_npcPathTarget = {};
		_npcReleasedForOver = false;
		_watching = false;
	}

	void Game::ClearForSave()
	{
		ReleaseOpponent();  // SetRestrained is saved with the game: never leave it on in a save
		if (_sim || !_markers.empty()) {
			Clear();
			Tell("Pool: balls cleared before saving");
		}
	}

	bool Game::IsOurs(const RE::TESObjectREFR* a_ref) const
	{
		if (!a_ref) {
			return false;
		}
		for (const auto& h : _balls) {
			if (h && h.get().get() == a_ref) {
				return true;
			}
		}
		for (const auto& m : _markers) {
			if (m.ref && m.ref.get().get() == a_ref) {
				return true;
			}
		}
		for (const auto& h : _dots) {
			if (h && h.get().get() == a_ref) {
				return true;
			}
		}
		return (_stick && _stick.get().get() == a_ref) || (_npcStick && _npcStick.get().get() == a_ref);
	}

	RE::TESObjectREFR* Game::PickTable() const
	{
		const auto& s = Settings::Get();
		if (s.useConsolePick) {
			if (auto picked = RE::Console::GetPickRef().get(); picked && !IsOurs(picked.get()) && picked->Get3D()) {
				return picked.get();
			}
		}

		// what the crosshair points at: the nearest table-sized object along the view line
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* camera = RE::PlayerCamera::GetSingleton();
		if (!player || !camera || !camera->cameraRoot) {
			return nullptr;
		}
		const auto         origin = camera->cameraRoot->GetWorldTranslate();
		const float        pitch = player->data.angle.x;
		const float        yaw = player->data.angle.z;
		const RE::NiPoint3 dir{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) };

		// A pool table mesh wins over anything else that's table sized.
		RE::TESObjectREFR* best = nullptr;
		float              bestScore = std::numeric_limits<float>::max();
		ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(origin, s.aimDistance + 300.0f, [&](RE::TESObjectREFR* a_ref) {
				auto* root = a_ref && !a_ref->IsActor() && !a_ref->IsDisabled() && !IsOurs(a_ref) ? a_ref->Get3D() : nullptr;
				if (!root) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const auto& b = root->worldBound;
				const bool  pool = IsPoolTable(a_ref);
				if (!pool && (b.fRadius < 40.0f || b.fRadius > 300.0f)) {  // roughly table sized
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const auto  oc = b.center - origin;
				const float t = oc.Dot(dir);
				const float miss2 = oc.SqrLength() - t * t;
				// the view line passes near the object's middle, within reach
				if (t > 0.0f && t < s.aimDistance + b.fRadius && miss2 <= b.fRadius * b.fRadius * 0.5f) {
					const float score = t + (pool ? 0.0f : 100000.0f);
					if (score < bestScore) {
						best = a_ref;
						bestScore = score;
					}
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		return best;
	}

	void Game::LogTable(RE::TESObjectREFR* a_table, const Frame& a_frame)
	{
		auto*       base = a_table->GetObjectReference();
		const auto& r = a_frame.tableRot;
		log::info("table {:08X} base {:08X} '{}' pool={} | pos ({:.1f}, {:.1f}, {:.1f}) angle z {:.1f} deg | world scale {:.3f}",
			a_table->GetFormID(), base ? base->GetFormID() : 0, ModelPath(a_table), IsPoolTable(a_table), a_frame.origin.x,
			a_frame.origin.y, a_frame.origin.z, a_table->data.angle.z * 57.29578f, a_frame.scale);
		log::info("  rotation rows ({:.3f} {:.3f} {:.3f}) ({:.3f} {:.3f} {:.3f}) ({:.3f} {:.3f} {:.3f})", r[0].x, r[0].y, r[0].z,
			r[1].x, r[1].y, r[1].z, r[2].x, r[2].y, r[2].z);

		// every piece of the table's mesh, in the table's own space: tells us which mesh this really is
		const auto& inv = a_frame.tableRot;  // undoes the table's rotation (see Frame)
		const float s = a_frame.scale != 0.0f ? a_frame.scale : 1.0f;
		if (auto* root = a_table->Get3D()) {
			RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* a_geom) {
				const auto& b = a_geom->worldBound;
				const auto  c = (inv * (b.center - a_frame.origin)) * (1.0f / s);
				log::info("  shape '{}': centre ({:.1f}, {:.1f}, {:.1f}) radius {:.1f}", a_geom->name.c_str(), c.x, c.y, c.z, b.fRadius / s);
				return RE::BSVisitControl::kContinue;
			});
		}
	}

	std::string Game::ModelPath(const RE::TESObjectREFR* a_ref)
	{
		auto* base = a_ref ? a_ref->GetObjectReference() : nullptr;
		auto* model = base ? base->As<RE::TESModel>() : nullptr;
		const char* path = model ? model->GetModel() : nullptr;
		return path ? path : "";
	}

	bool Game::IsPoolTable(const RE::TESObjectREFR* a_ref)
	{
		auto path = ModelPath(a_ref);
		std::ranges::transform(path, path.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
		return path.find("pooltable") != std::string::npos && path.find("rack") == std::string::npos;
	}

	RE::TESBoundObject* Game::BallForm(int a_number) const
	{
		const auto& s = Settings::Get();
		return util::ResolveForm(s.ballPlugin, s.FormIDFor(a_number));
	}

	RE::TESBoundObject* Game::StickForm() const
	{
		const auto& s = Settings::Get();
		return s.stickFormID ? util::ResolveForm(s.stickPlugin, s.stickFormID) : nullptr;
	}

	void Game::FreezePhysics(RE::TESObjectREFR* a_ref)
	{
		// fo4pool's ball meshes have no collision. If [Ball] points at an object that does have Havok, make it
		// keyframed so the physics engine doesn't move the ball while we do.
		auto* root = a_ref ? a_ref->Get3D() : nullptr;
		if (!root || std::ranges::find(_frozen, root) != _frozen.end()) {
			return;
		}
		_frozen.push_back(root);
		int count = 0;
		RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNPCollisionObject* a_collision) {
			a_collision->SetMotionType(RE::hknpMotionPropertiesId::Preset::kKeyframed);
			++count;
			return RE::BSVisitControl::kContinue;
		});
		if (count) {
			log::info("{:08X}: switched {} collision object(s) to keyframed", a_ref->GetFormID(), count);
		}
	}

	bool Game::GetFrame(Frame& a_out) const
	{
		auto table = _table.get();
		auto* root = table ? table->Get3D() : nullptr;
		if (!root) {
			return false;
		}
		const auto& w = root->GetWorldTransform();
		a_out.tableRot = w.rotate;
		a_out.rot = AxisSwap(Settings::Get().lengthAxis).Transpose() * w.rotate;
		a_out.origin = w.translate;
		a_out.scale = w.scale;
		return true;
	}

	RE::NiPoint3 Game::ToWorld(const Frame& a_frame, pool::Vec2 a_pos, float a_height) const
	{
		const auto&  s = Settings::Get();
		RE::NiPoint3 mesh;
		if (s.lengthAxis == 1) {
			mesh = { s.offsetX - static_cast<float>(a_pos.y), s.offsetY + static_cast<float>(a_pos.x), s.surfaceZ + a_height };
		} else {
			mesh = { s.offsetX + static_cast<float>(a_pos.x), s.offsetY + static_cast<float>(a_pos.y), s.surfaceZ + a_height };
		}
		return a_frame.origin + a_frame.tableRot.Transpose() * (mesh * a_frame.scale);
	}

	RE::ObjectRefHandle Game::Spawn(RE::TESBoundObject* a_form, RE::TESObjectREFR* a_table, const RE::NiPoint3& a_pos) const
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		auto* cell = a_table->GetParentCell();
		if (!dh || !cell) {
			return {};
		}
		RE::NEW_REFR_DATA data;
		data.location = a_pos;
		data.direction = { 0.0f, 0.0f, 0.0f };
		data.object = a_form;
		if (cell->IsInterior()) {
			data.interior = cell;
		} else {
			data.world = cell->worldSpace;
		}
		data.initializeScripts = false;
		return dh->CreateReferenceAtLocation(data);
	}

	float Game::BallScale([[maybe_unused]] RE::NiAVObject* a_root)
	{
		const auto& s = Settings::Get();
		if (!s.ballAutoScale) {
			return s.ballScale;
		}
		// Until a real ball has been measured (its first frame), the INI's scale (1.0 = the vanilla balls' size).
		return _modelRadius > 0.0f ? static_cast<float>(s.spec.ballRadius) / _modelRadius : s.ballScale;
	}

	void Game::MeasureBall(RE::NiAVObject* a_root)
	{
		// Measured from a ball right after it was placed and updated, so its bound and scale belong together. Guide
		// dots (shrunk cue balls) never get here.
		if (_modelRadius > 0.0f || !a_root || a_root->GetAppCulled()) {
			return;
		}
		const float scale = a_root->GetWorldScale();
		const float bound = a_root->worldBound.fRadius;
		if (scale <= 0.0f || bound <= 0.0f) {
			return;
		}
		const float radius = bound / scale;
		if (radius < 0.5f || radius > 50.0f) {
			log::warn("ball mesh measured an odd size ({:.3f}); not using it", radius);
			return;
		}
		_modelRadius = radius;
		log::info("ball mesh radius {:.3f} -> scale {:.3f} for a {:.2f} ball", _modelRadius,
			static_cast<float>(Settings::Get().spec.ballRadius) / _modelRadius, Settings::Get().spec.ballRadius);
	}

	void Game::PlaceNode(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos, const RE::NiMatrix3& a_rot, float a_scale, bool a_hidden)
	{
		auto* root = a_ref ? a_ref->Get3D() : nullptr;
		if (!root) {
			return;
		}
		FreezePhysics(a_ref);
		if (root->GetAppCulled() != a_hidden) {
			root->SetAppCulled(a_hidden);
		}
		if (a_hidden) {
			return;
		}

		// we know the world transform we want; the node stores it relative to its parent
		RE::NiTransform local;
		if (auto* parent = root->parent) {
			const auto& p = parent->GetWorldTransform();
			const float ps = p.scale != 0.0f ? p.scale : 1.0f;
			local.rotate = a_rot * p.rotate.Transpose();
			local.translate = (p.rotate * (a_pos - p.translate)) * (1.0f / ps);
			local.scale = a_scale / ps;
		} else {
			local.rotate = a_rot;
			local.translate = a_pos;
			local.scale = a_scale;
		}
		root->SetLocalTransform(local);

		RE::NiUpdateData update;
		root->Update(update);

		a_ref->data.location.x = a_pos.x;
		a_ref->data.location.y = a_pos.y;
		a_ref->data.location.z = a_pos.z;
	}

	bool Game::RerackInPlace()
	{
		auto* table = _table.get().get();
		if (!_sim || !table) {
			return false;
		}
		const auto& s = Settings::Get();
		for (int i = 0; i < pool::Sim::kBalls; ++i) {
			auto ref = _balls[i].get();
			if (!ref || ref->GetObjectReference() != BallForm(i)) {
				return false;  // a ball's gone, or the INI now names other balls: rack from scratch
			}
		}
		Frame frame;
		if (!GetFrame(frame)) {
			return false;
		}
		_sim = std::make_unique<pool::Sim>(s.spec);
		_sim->Rack(static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
		_dropTime.fill(0.0f);
		_scratch = false;
		_haveLast = false;
		_modelRadius = 0.0f;
		log::info("re-racked on the same table");
		StartGame();
		return true;
	}

	void Game::Rack()
	{
		auto& s = Settings::Get();
		s.Load();

		// Same table (or no other one in view, or in the shot camera, where there's no crosshair): just start again.
		auto* picked = _aiming ? nullptr : PickTable();
		if (_sim && (!picked || picked == _table.get().get()) && RerackInPlace()) {
			return;
		}

		auto* table = picked ? picked : PickTable();
		if (!table) {
			Notify("Pool: look at a pool table (or click it in the console) first");
			return;
		}
		std::array<RE::TESBoundObject*, pool::Sim::kBalls> forms{};
		for (int i = 0; i < pool::Sim::kBalls; ++i) {
			forms[i] = BallForm(i);
			if (!forms[i]) {
				Notify(std::format("Pool: ball {} ({}|{:X}) not found. Is {} installed? See fo4pool.ini [Ball]", i, s.ballPlugin,
					s.FormIDFor(i), s.ballPlugin));
				return;
			}
		}

		Clear();
		_table = table->GetHandle();
		Frame frame;
		if (!GetFrame(frame)) {
			Notify("Pool: that table has no 3D loaded");
			_table = {};
			return;
		}

		LogTable(table, frame);
		_sim = std::make_unique<pool::Sim>(s.spec);
		_sim->Rack(static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
		_dropTime.fill(0.0f);
		_scratch = false;
		_haveLast = false;
		_warned3D = false;

		int spawned = 0;
		for (int i = 0; i < pool::Sim::kBalls; ++i) {
			const auto& b = _sim->Balls()[i];
			_balls[i] = Spawn(forms[i], table, ToWorld(frame, b.pos, static_cast<float>(s.spec.ballRadius)));
			spawned += static_cast<bool>(_balls[i]);
		}
		log::info("racked on {:08X} '{}' ({}x{} felt): {} balls spawned", table->GetFormID(), ModelPath(table), s.spec.length,
			s.spec.width, spawned);
		if (spawned != pool::Sim::kBalls) {
			Notify(std::format("Pool: only {} of 16 balls could be placed", spawned));
		}
		_modelRadius = 0.0f;
		StartGame();
	}

	void Game::Clear()
	{
		ExitAim();
		if (auto* actor = Opponent(); actor && (_npcStep != NpcStep::kNone || _npcWaitIdle)) {
			Anim::Stop(actor);  // no balls, no shot: don't leave them bent over the table or waiting with a cue
		}
		_npcWaitIdle = false;
		_npcFacing = false;  // the next game's wait spot holds them again (and starts PoolWait)
		HideOpponentStick();
		_npcStep = NpcStep::kNone;
		if (auto marker = _npcMarker.get()) {
			marker->Disable();
			marker->SetWantsDelete(true);
		}
		_npcMarker = {};
		if (auto marker = _npcWaitMarker.get()) {
			marker->Disable();
			marker->SetWantsDelete(true);
		}
		_npcWaitMarker = {};
		RemoveAimObjects();
		auto remove = [](RE::ObjectRefHandle& a_handle) {
			if (auto ref = a_handle.get()) {
				ref->Disable();
				ref->SetWantsDelete(true);
			}
			a_handle = {};
		};
		for (auto& h : _balls) {
			remove(h);
		}
		for (auto& m : _markers) {
			remove(m.ref);
		}
		_markers.clear();
		_frozen.clear();
		_glow3D.fill(nullptr);
		_sim.reset();
		_table = {};
		_shotLive = false;
		_placing = false;
		_message.clear();
		_log = {};
		_banner = 0;
		UpdateHud();
	}

	void Game::ClearMarkers()
	{
		for (auto& m : _markers) {
			if (auto ref = m.ref.get()) {
				ref->Disable();
				ref->SetWantsDelete(true);
			}
		}
		_markers.clear();
	}

	void Game::ToggleCalibrate()
	{
		if (!_markers.empty()) {
			ClearMarkers();
			Notify("Pool: calibration markers removed");
			return;
		}

		auto& s = Settings::Get();
		s.Load();
		auto* table = PickTable();
		auto* ball = BallForm(0);
		if (!table || !ball) {
			Notify(!table ? "Pool: look at a pool table (or click it in the console) first" : "Pool: ball form not found (see fo4pool.ini [Ball])");
			return;
		}
		if (!_sim) {
			_table = table->GetHandle();
		} else if (_table.get().get() != table) {
			Notify("Pool: clear the current game first");
			return;
		}

		// the felt corners (= corner pockets), the side pockets, the head and foot spots and the centre
		pool::Sim probe(s.spec);
		for (int p = 0; p < pool::Sim::kPockets; ++p) {
			_markers.push_back({ {}, probe.PocketPos(p) });
		}
		_markers.push_back({ {}, probe.HeadSpot() });
		_markers.push_back({ {}, probe.FootSpot() });
		_markers.push_back({ {}, { 0.0, 0.0 } });

		Frame frame;
		if (!GetFrame(frame)) {
			_markers.clear();
			return;
		}
		LogTable(table, frame);
		for (auto& m : _markers) {
			const auto p = ToWorld(frame, m.pos, static_cast<float>(s.spec.ballRadius));
			log::info("  marker table ({:.1f}, {:.1f}) -> world ({:.1f}, {:.1f}, {:.1f})", m.pos.x, m.pos.y, p.x, p.y, p.z);
			m.ref = Spawn(ball, table, p);
		}
		Notify("Pool: markers at the felt corners, side pockets, spots and centre. Edit [Table] in fo4pool.ini, then press again twice");
	}

	void Game::Tick()
	{
		if (!_sim && _markers.empty()) {
			return;
		}
		Frame frame;
		if (!GetFrame(frame)) {
			if (!_warned3D) {
				log::warn("table 3D not loaded; balls on hold");
				_warned3D = true;
			}
			return;
		}

		const auto& s = Settings::Get();
		const float r = static_cast<float>(s.spec.ballRadius);

		for (auto& m : _markers) {
			if (auto ref = m.ref.get()) {
				PlaceNode(ref.get(), ToWorld(frame, m.pos, r + s.ballZOffset), frame.rot, BallScale(ref->Get3D()) * frame.scale, false);
			}
		}
		if (!_sim) {
			return;
		}

		const auto now = std::chrono::steady_clock::now();
		float      dt = _haveLast ? std::chrono::duration<float>(now - _last).count() : 0.0f;
		_last = now;
		_haveLast = true;
		if (IsGamePaused()) {
			dt = 0.0f;
		}
		dt = std::clamp(dt, 0.0f, kMaxFrame);

		if (dt > 0.0f) {
			const auto events = _sim->Step(dt);
			PlaySounds(frame, events);
			if (_shotLive) {
				_rules.OnEvents(events);
			}
			for (const auto& e : events) {
				if (e.type == pool::Event::Type::kPocket) {
					if (e.a == 0) {
						_scratch = true;
						Say("scratch!");
					} else {
						Say(std::format("{} in", BallName(e.a)));
					}
				}
			}
			for (int i = 0; i < pool::Sim::kBalls; ++i) {
				if (_sim->Balls()[i].state == pool::BallState::kPocketed) {
					_dropTime[i] += dt;
				}
			}
		}

		if (_scratch && _sim->AtRest()) {
			_scratch = false;
			_sim->RespotCue();  // ball in hand moves it from there
			_dropTime[0] = 0.0f;
		}
		if (_shotLive && _sim->AtRest()) {
			JudgeShot();
		}
		TickOpponent(frame, dt);

		for (int i = 0; i < pool::Sim::kBalls; ++i) {
			auto ref = _balls[i].get();
			if (!ref) {
				continue;
			}
			const auto& b = _sim->Balls()[i];
			const bool  pocketed = b.state == pool::BallState::kPocketed;
			const bool  hidden = b.state == pool::BallState::kOff || (pocketed && _dropTime[i] >= kDropSeconds);
			const float sink = pocketed ? (_dropTime[i] / kDropSeconds) * 2.5f * r : 0.0f;

			double m[3][3];
			b.rot.ToMatrix(m);
			PlaceNode(ref.get(), ToWorld(frame, b.pos, r + s.ballZOffset - sink), FromRows(m).Transpose() * frame.rot, BallScale(ref->Get3D()) * frame.scale,
				hidden);
			MeasureBall(ref->Get3D());
		}

		UpdateGlow();
		_rolling = !_sim->AtRest();
		if (_aiming) {
			TickAim(frame, dt);
		}
		UpdateHud();
	}

	// ---------------------------------------------------------------- your balls glow

	void Game::SetGlow(RE::NiAVObject* a_root, float a_strength)
	{
		static const RE::NiColor kGreen{ 0.35f, 1.0f, 0.45f };  // the HUD's green
		RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geom) {
			auto* prop = RE::netimmerse_cast<RE::BSLightingShaderProperty*>(a_geom->properties[1].get());
			if (!prop) {
				return RE::BSVisitControl::kContinue;
			}
			if (!_glowLogged) {
				_glowLogged = true;
				log::info("glow: ball shader on '{}': emit colour {}, own emit {}, scale {:.2f}", a_geom->name.c_str(),
					prop->emitColor ? "yes" : "none", prop->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kOwnEmit), prop->emitColorScale);
			}
			if (a_strength > 0.0f) {
				prop->SetFlag(RE::BSShaderProperty::EShaderPropertyFlag::kOwnEmit, true);
				if (prop->emitColor) {
					*prop->emitColor = kGreen;  // may be shared by every ball: the others keep scale 0, so stay dark
				}
			}
			prop->emitColorScale = a_strength;
			return RE::BSVisitControl::kContinue;
		});
	}

	void Game::UpdateGlow()
	{
		const auto& s = Settings::Get();
		// whose balls: yours against an NPC, the shooter's in hotseat; none in Solo Play or before groups are set
		const bool solo = _rules.GameMode() == pool::Mode::kSolo;
		const int  who = _opponent ? 0 : _rules.Shooter();
		const auto group = _rules.GroupOf(who);
		const bool onEight = !solo && _rules.OnEight(who, *_sim);
		for (int i = 1; i < pool::Sim::kBalls; ++i) {
			auto  ref = _balls[i].get();
			auto* root = ref ? ref->Get3D() : nullptr;
			if (!root) {
				continue;
			}
			const bool  mine = s.glowOwn && !solo && !_rules.Over() &&
			                  ((group != pool::Group::kNone && pool::GroupOfBall(i) == group) || (onEight && i == 8));
			const float want = mine ? s.glowStrength : 0.0f;
			if (_glow3D[i] == root && _glow[i] == want) {
				continue;
			}
			SetGlow(root, want);
			_glow[i] = want;
			_glow3D[i] = root;
		}
	}

	void Game::PlaySounds(const Frame& a_frame, const std::vector<pool::Event>& a_events)
	{
		// The loudest few per frame: a break makes dozens of contacts at once, and they'd only blur together.
		struct Sound
		{
			Audio::Bank  bank;
			RE::NiPoint3 pos;
			float        gain;
			float        seconds{ 0.0f };
		};
		std::vector<Sound> sounds;
		const float        r = static_cast<float>(Settings::Get().spec.ballRadius);
		auto loudness = [](double a_speed, double a_quiet, double a_loud) {
			if (a_speed <= a_quiet) {
				return 0.0f;
			}
			return static_cast<float>(std::pow(std::min(1.0, (a_speed - a_quiet) / (a_loud - a_quiet)), 0.7));
		};
		for (const auto& e : a_events) {
			const auto& balls = _sim->Balls();
			switch (e.type) {
			case pool::Event::Type::kBallBall:
				{
					const pool::Vec2 mid = (balls[e.a].pos + balls[e.b].pos) * 0.5;
					sounds.push_back({ Audio::Bank::kHit, ToWorld(a_frame, mid, r), loudness(e.speed, 2.0, 450.0) });
				}
				break;
			case pool::Event::Type::kCushion:
				sounds.push_back({ Audio::Bank::kCushion, ToWorld(a_frame, balls[e.a].pos, r), 0.8f * loudness(std::abs(e.speed), 4.0, 450.0) });
				break;
			case pool::Event::Type::kPocket:
				{
					const auto pos = ToWorld(a_frame, _sim->PocketPos(e.b), 0.0f);
					sounds.push_back({ Audio::Bank::kPocket, pos, 0.5f + 0.5f * loudness(e.speed, 0.0, 250.0) });
					if (Settings::Get().soundReturn) {
						// The ball runs under the table to the return at the foot end (where the balls are racked):
						// the further the pocket from it, the longer the roll.
						const auto&      spec = Settings::Get().spec;
						const double     end = Settings::Get().returnEnd >= 0 ? 1.0 : -1.0;
						const pool::Vec2 collect{ end * spec.length * 0.5, 0.0 };
						const double     nearest = spec.width * 0.5;                                          // a foot corner
						const double     farthest = std::sqrt(spec.length * spec.length + nearest * nearest);  // a head corner
						const double     t = std::clamp(((_sim->PocketPos(e.b) - collect).Length() - nearest) / (farthest - nearest), 0.0, 1.0);
						const float      seconds = static_cast<float>(kReturnShortest + (kReturnLongest - kReturnShortest) * t);
						sounds.push_back({ Audio::Bank::kReturn, pos, 0.45f, seconds });
					}
				}
				break;
			}
		}
		std::ranges::sort(sounds, [](const Sound& a, const Sound& b) { return a.gain > b.gain; });
		int played = 0;
		for (const auto& snd : sounds) {
			if (played >= 8 || snd.gain < 0.02f) {
				break;
			}
			Audio::Play(snd.bank, snd.pos, snd.gain, snd.seconds);
			++played;
		}
	}
}
