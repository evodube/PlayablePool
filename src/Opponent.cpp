// The NPC opponent: look at a settler or companion and press the challenge key, and they play you at 8-ball as
// Player 2. On their turn they pick a shot (sim/Ai), walk to it (Papyrus PathToReference to a marker placed where
// they have to stand), line it up (PoolAim), strike (PoolStrike) and the cue ball goes on the frame the cue reaches
// it.

#include "Game.h"

#include "Anim.h"
#include "Audio.h"
#include "Settings.h"
#include "Util.h"

namespace fp
{
	namespace
	{
		// The pool clips' shot geometry, in the actor's own space (root at the origin, facing +Y): the cue runs
		// along x = 8.5 and the cue ball sits 66.1 ahead of the root (tools/anim/README.md).
		constexpr float kStanceBack = 66.1f;
		constexpr float kStanceSide = 8.5f;
		constexpr float kContactSeconds = 35.0f / 30.0f;  // PoolStrike: the cue reaches the ball on frame 35
		constexpr float kStrikeLength = 60.0f / 30.0f;
		constexpr float kSettleSeconds = 0.4f;   // after being put on the spot, before the aim idle starts
		constexpr float kThinkSeconds = 0.8f;    // after the balls stop, before they set off
		constexpr float kArrivedDistance = 30.0f;
		constexpr float kTableMargin = 22.0f;    // the rails and some room for the body, beyond the felt
		constexpr float kQuitDistance = 4000.0f; // the opponent is this far from the table: the game's off
		constexpr float kWatchDistance = 1500.0f;
		constexpr RE::TESFormID kXMarkerHeading = 0x00000034;
		constexpr float kGripFromButt = 12.0f;   // where the grip hand holds the cue
		constexpr float kWaitRadius = 80.0f;     // further from the wait spot than this: back they go
		constexpr float kRepathSeconds = 3.0f;
		constexpr float kWaitFromCentre = 170.0f; // the first wait spot: this far from the table's middle, towards them

		float Distance2D(const RE::NiPoint3& a, const RE::NiPoint3& b)
		{
			return std::hypot(a.x - b.x, a.y - b.y);
		}

		void RemoveRef(RE::ObjectRefHandle& a_handle)
		{
			if (auto ref = a_handle.get()) {
				ref->Disable();
				ref->SetWantsDelete(true);
			}
			a_handle = {};
		}
	}

	std::string Game::PlayerName(int a_player) const
	{
		if (a_player == 1 && _opponent) {
			return _opponentName;
		}
		return a_player == 0 ? "Player 1" : "Player 2";
	}

	RE::Actor* Game::Opponent() const
	{
		auto ref = _opponent.get();
		return ref ? ref->As<RE::Actor>() : nullptr;
	}

	bool Game::OpponentsTurn() const
	{
		return _sim && _opponent && _rules.GameMode() == pool::Mode::kEightBall && _rules.Shooter() == 1 && !_rules.Over();
	}

	RE::Actor* Game::PickActor() const
	{
		auto*       player = RE::PlayerCharacter::GetSingleton();
		const auto& s = Settings::Get();
		auto usable = [&](RE::TESObjectREFR* a_ref) -> RE::Actor* {
			auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
			if (!actor || actor == player || actor->IsDisabled() || actor->IsDead(false) || !actor->Get3D()) {
				return nullptr;
			}
			return actor;
		};
		if (s.useConsolePick) {
			if (auto picked = RE::Console::GetPickRef().get()) {
				if (auto* actor = usable(picked.get())) {
					return actor;
				}
			}
		}

		auto* camera = RE::PlayerCamera::GetSingleton();
		if (!player || !camera || !camera->cameraRoot) {
			return nullptr;
		}
		const auto         origin = camera->cameraRoot->GetWorldTranslate();
		const float        pitch = player->data.angle.x;
		const float        yaw = player->data.angle.z;
		const RE::NiPoint3 dir{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) };

		RE::Actor* best = nullptr;
		float      bestT = std::numeric_limits<float>::max();
		util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(origin, s.opponentPickDistance + 100.0f, [&](RE::TESObjectREFR* a_ref) {
				auto* actor = usable(a_ref);
				if (!actor) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const auto& b = actor->Get3D()->worldBound;
				const auto  oc = b.center - origin;
				const float t = oc.Dot(dir);
				const float miss2 = oc.SqrLength() - t * t;
				const float reach = std::max(30.0f, b.fRadius * 0.7f);
				if (t > 0.0f && t < s.opponentPickDistance + b.fRadius && miss2 <= reach * reach && t < bestT) {
					bestT = t;
					best = actor;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		return best;
	}

	void Game::Challenge()
	{
		const auto& s = Settings::Get();
		if (!_sim) {
			Notify(std::format("Pool: rack a table first ({}), then look at who you want to play and press {}", Settings::Key(s.keyRack),
				Settings::Key(s.keyChallenge)));
			return;
		}
		auto* actor = PickActor();
		auto* current = Opponent();
		if (!actor || actor == current) {
			if (current) {
				EndChallenge(std::format("{} stops playing", _opponentName));
			} else {
				Notify("Pool: look at a settler or companion to challenge them");
			}
			return;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (actor->IsInCombat() || (player && actor->GetHostileToActor(player))) {
			Notify(std::format("Pool: {} isn't in the mood for pool", actor->GetDisplayFullName()));
			return;
		}
		if (current) {
			EndChallenge(std::format("{} stops playing", _opponentName));
		}

		ExitAim();
		_opponent = actor->GetHandle();
		_opponentName = actor->GetDisplayFullName();
		if (_opponentName.empty()) {
			_opponentName = "Player 2";
		}
		_nextBreaker = 0;  // you break the first rack
		log::info("challenge: {} ({:08X}), skill {:.2f}", _opponentName, actor->GetFormID(), s.opponentSkill);
		if (!RerackInPlace()) {
			Rack();
		}
		Say(std::format("{} plays you at 8-ball. You break", _opponentName));
	}

	void Game::HoldOpponent(RE::Actor* a_actor, bool a_hold)
	{
		if (a_actor && a_hold != _npcHeld) {
			_npcHeld = a_hold;
			Anim::SetDontMove(a_actor, a_hold);
		}
	}

	void Game::OpponentLooksAtPlayer(RE::Actor* a_actor, bool a_look)
	{
		if (!a_actor || a_look == _npcLooking) {
			return;
		}
		_npcLooking = a_look;
		if (a_look) {
			Anim::LookAt(a_actor, RE::PlayerCharacter::GetSingleton());
		} else {
			Anim::ClearLookAt(a_actor);
		}
	}

	void Game::ReleaseOpponent()
	{
		auto* actor = Opponent();
		if (!actor) {
			return;
		}
		if (_npcStep != NpcStep::kNone) {
			Anim::Stop(actor);
			_npcStep = NpcStep::kNone;
		}
		HoldOpponent(actor, false);
		OpponentLooksAtPlayer(actor, false);
		Anim::SetHeadTracking(actor, true);
		_npcFacing = false;
	}

	void Game::EndChallenge(const std::string& a_why)
	{
		ReleaseOpponent();
		RemoveRef(_npcMarker);
		RemoveRef(_npcWaitMarker);
		_autoAim = false;
		_opponent = {};
		_npcStep = NpcStep::kNone;
		_npcTimer = 0.0f;
		_watching = false;
		_manualPocket = -1;
		if (!a_why.empty()) {
			Say(a_why);
		}
		log::info("challenge over: {}", a_why);
	}

	RE::NiPoint3 Game::StanceFor(const Frame& a_frame, pool::Vec2 a_cue, pool::Vec2 a_dir, float& a_heading) const
	{
		const auto&  s = Settings::Get();
		const double scale = std::max(0.01f, a_frame.scale);

		// how far back along the shot line the root goes: the clip's distance, or further if that's still over
		// the table (they lean in from the rail and the cue reaches less far than the clip shows)
		double       back = kStanceBack / scale;
		const double hx = s.spec.length * 0.5 + kTableMargin;
		const double hy = s.spec.width * 0.5 + kTableMargin;
		auto inside = [&](double k) {
			const pool::Vec2 p = a_cue - a_dir * k;
			return std::abs(p.x) < hx && std::abs(p.y) < hy;
		};
		while (inside(back) && back < 400.0) {
			back += 2.0;
		}

		const RE::NiPoint3 dirWorld = util::Normalized(a_frame.rot.Transpose() * RE::NiPoint3{ static_cast<float>(a_dir.x), static_cast<float>(a_dir.y), 0.0f });
		const RE::NiPoint3 flat = util::Normalized(RE::NiPoint3{ dirWorld.x, dirWorld.y, 0.0f });
		const RE::NiPoint3 right{ flat.y, -flat.x, 0.0f };  // the actor's right, facing along the shot
		a_heading = std::atan2(flat.x, flat.y);

		RE::NiPoint3 root = ToWorld(a_frame, a_cue - a_dir * back, 0.0f) - right * kStanceSide;
		root.z = a_frame.origin.z;  // the clip has the felt 70.4 above the feet: the table's own height
		return root;
	}

	void Game::SetWaitSpot(const Frame& a_frame, const RE::NiPoint3& a_pos)
	{
		auto* table = _table.get().get();
		RemoveRef(_npcWaitMarker);
		auto* form = RE::TESForm::GetFormByID(kXMarkerHeading);
		auto* bound = form && form->IsBoundObject() ? static_cast<RE::TESBoundObject*>(form) : nullptr;
		if (!bound || !table) {
			return;
		}
		RE::NiPoint3 pos = a_pos;
		pos.z = a_frame.origin.z;
		// facing the middle of the table while they wait
		_npcWaitHeading = std::atan2(a_frame.origin.x - pos.x, a_frame.origin.y - pos.y);
		_npcWaitMarker = Spawn(bound, table, pos);
		if (auto marker = _npcWaitMarker.get()) {
			marker->data.angle = { 0.0f, 0.0f, _npcWaitHeading };
		}
		_npcRepath = kRepathSeconds;  // check straight away
		_npcFacing = false;
	}

	void Game::WaitByTable()
	{
		auto*  actor = Opponent();
		Frame  frame;
		if (!actor || !GetFrame(frame)) {
			return;
		}
		const RE::NiPoint3 away = util::Normalized(RE::NiPoint3{ actor->data.location.x - frame.origin.x, actor->data.location.y - frame.origin.y, 0.0f });
		SetWaitSpot(frame, frame.origin + away * (kWaitFromCentre * frame.scale));
	}

	void Game::TickWaiting(RE::Actor* a_actor, [[maybe_unused]] const Frame& a_frame, float a_dt)
	{
		// while it's your shot they watch you
		OpponentLooksAtPlayer(a_actor, !OpponentsTurn() && !_rules.Over());

		auto marker = _npcWaitMarker.get();
		if (!marker) {
			return;
		}
		_npcRepath += a_dt;
		const float away = Distance2D(a_actor->data.location, marker->data.location);
		if (away > kWaitRadius) {
			// pushed or pulled off the spot: walk back (they can't walk while held)
			_npcFacing = false;
			HoldOpponent(a_actor, false);
			if (_npcRepath >= kRepathSeconds) {
				_npcRepath = 0.0f;
				Anim::PathTo(a_actor, marker.get(), 0.0f);
			}
		} else if (!_npcFacing && away < kWaitRadius * 0.5f) {
			// there: face the table and stay put, so their sandbox package can't walk them off
			_npcFacing = true;
			a_actor->SetHeading(_npcWaitHeading);
			HoldOpponent(a_actor, true);
		}
	}

	bool Game::PlaceStickInHands(RE::Actor* a_actor)
	{
		auto* root = a_actor ? a_actor->Get3D() : nullptr;
		if (!root || !_stick.get()) {
			return false;
		}
		auto node = [&](const char* a_name) -> RE::NiAVObject* {
			return root->GetObjectByName(RE::BSFixedString{ a_name });
		};
		auto* rHand = node("RArm_Hand");
		auto* lHand = node("LArm_Hand");
		if (!rHand || !lHand) {
			return false;
		}
		// grip: the right palm (between the wrist and the middle finger's base); bridge: the left hand's V between
		// thumb and index finger
		auto mid = [&](RE::NiAVObject* a_a, RE::NiAVObject* a_b) {
			return a_b ? (a_a->world.translate + a_b->world.translate) * 0.5f : a_a->world.translate;
		};
		const RE::NiPoint3 grip = mid(rHand, node("RArm_Finger31"));
		auto*              thumb = node("LArm_Finger11");
		auto*              index = node("LArm_Finger21");
		const RE::NiPoint3 bridge = thumb && index ? (thumb->world.translate + index->world.translate) * 0.5f : lHand->world.translate;
		const RE::NiPoint3 along = bridge - grip;
		if (along.Length() < 10.0f) {
			return false;  // hands together: not a pool pose
		}
		const RE::NiPoint3 dir = util::Normalized(along);
		// the cue (Weapons\PoolCue) is 94 long; the grip hand holds it about 12 from the butt
		const auto&        s = Settings::Get();
		const float        length = std::abs(s.stickTip - s.stickButt);
		const RE::NiPoint3 tip = grip + dir * (length - kGripFromButt);
		PlaceStickAlong(tip, dir, 1.0f);
		return true;
	}

	void Game::PlanOpponentShot(const Frame& a_frame)
	{
		const auto&    s = Settings::Get();
		pool::AiParams p;
		p.skill = s.opponentSkill;
		p.minSpeed = s.minSpeed;
		p.maxSpeed = s.maxSpeed;
		_npcShot = pool::ChooseShot(*_sim, _rules, 1, p, _npcRng);
		if (_npcShot.placeCue) {
			_sim->Place(0, _npcShot.cuePos);
			_rules.PlacedCueBall();
			_dropTime[0] = 0.0f;
			Say(std::format("{} places the cue ball", _opponentName));
		}
		_manualPocket = _rules.OnEight(1, *_sim) ? _npcShot.pocket : -1;  // the HUD marks their call
		log::info("{}: shot at ball {} into pocket {}{}, speed {:.0f}, dir ({:.3f}, {:.3f})", _opponentName, _npcShot.target, _npcShot.pocket,
			_npcShot.checked ? " (checked)" : "", _npcShot.speed, _npcShot.dir.x, _npcShot.dir.y);

		_npcStance = StanceFor(a_frame, _sim->Balls()[0].pos, _npcShot.dir, _npcHeading);

		auto* actor = Opponent();
		auto* table = _table.get().get();
		RemoveRef(_npcMarker);
		auto* markerForm = RE::TESForm::GetFormByID(kXMarkerHeading);
		auto* bound = markerForm && markerForm->IsBoundObject() ? static_cast<RE::TESBoundObject*>(markerForm) : nullptr;
		if (bound && table) {
			_npcMarker = Spawn(bound, table, _npcStance);
		}
		auto marker = _npcMarker.get();
		if (marker) {
			marker->data.angle = { 0.0f, 0.0f, _npcHeading };
		}
		_npcTimer = 0.0f;
		OpponentLooksAtPlayer(actor, false);
		HoldOpponent(actor, false);  // free to walk to the shot
		_npcFacing = false;
		if (actor && marker && Anim::PathTo(actor, marker.get(), s.opponentRun ? 1.0f : 0.0f)) {
			_npcStep = NpcStep::kWalking;
		} else {
			// can't make them walk: straight to lining up the shot
			if (actor && marker) {
				Anim::MoveTo(actor, marker.get());
			}
			_npcStep = NpcStep::kAiming;
		}
	}

	void Game::OpponentStrike(const Frame& a_frame)
	{
		const auto& s = Settings::Get();
		const int   called = _rules.OnEight(1, *_sim) ? _npcShot.pocket : -1;
		_strikePos = _sim->Balls()[0].pos;
		const float power = static_cast<float>(std::clamp((_npcShot.speed - s.minSpeed) / std::max(1.0f, s.maxSpeed - s.minSpeed), 0.0, 1.0));
		Audio::Play(Audio::Bank::kCue, ToWorld(a_frame, _strikePos, static_cast<float>(s.spec.ballRadius)), 0.25f + 0.75f * power);
		_rules.BeginShot(*_sim, called);
		_shotLive = true;
		_sim->Strike(_npcShot.dir, _npcShot.speed);
		log::info("{} shoots: speed {:.0f}", _opponentName, _npcShot.speed);
	}

	void Game::TickOpponent(const Frame& a_frame, float a_dt)
	{
		if (!_opponent) {
			_watching = false;
			return;
		}
		auto* actor = Opponent();
		auto* table = _table.get().get();
		if (!actor || actor->IsDead(false) || actor->IsDisabled() || !table) {
			EndChallenge(std::format("{} can't play any more", _opponentName));
			return;
		}
		if (Distance2D(actor->data.location, a_frame.origin) > kQuitDistance) {
			EndChallenge(std::format("{} wandered off", _opponentName));
			return;
		}
		if (actor->IsInCombat()) {
			EndChallenge(std::format("{} has better things to do", _opponentName));
			return;
		}
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			_watching = Distance2D(player->data.location, a_frame.origin) < kWatchDistance;
		}
		if (a_dt <= 0.0f) {
			return;  // paused
		}

		if (!OpponentsTurn()) {
			if (_npcStep != NpcStep::kNone && _npcStep != NpcStep::kFollow) {
				Anim::Stop(actor);  // a re-rack or the game ended while they were at it
				_npcStep = NpcStep::kNone;
			}
		}
		if (OpponentsTurn() && _aiming && _npcStep == NpcStep::kNone) {
			ExitAim();  // it's their table now
		}

		const auto& s = Settings::Get();
		auto        marker = _npcMarker.get();
		_npcTimer += a_dt;
		switch (_npcStep) {
		case NpcStep::kNone:
			TickWaiting(actor, a_frame, a_dt);
			if (_autoAim && !OpponentsTurn() && !_shotLive && _sim->AtRest()) {
				_autoAim = false;
				if (_watching && !_aiming && !_rules.Over()) {
					EnterAim();  // your shot: straight to the cue ball
				}
			}
			if (!OpponentsTurn() || _shotLive || _scratch || !_sim->AtRest()) {
				_npcTimer = 0.0f;
				break;
			}
			if (_npcTimer >= kThinkSeconds) {
				PlanOpponentShot(a_frame);
			}
			break;

		case NpcStep::kWalking:
			if (Distance2D(actor->data.location, _npcStance) < kArrivedDistance || _npcTimer > s.opponentWalkTimeout) {
				if (_npcTimer > s.opponentWalkTimeout) {
					log::info("{} didn't reach the shot in {:.0f} s: putting them there", _opponentName, s.opponentWalkTimeout);
				}
				if (marker) {
					Anim::MoveTo(actor, marker.get());  // exactly on the spot, facing the shot
				}
				HoldOpponent(actor, true);
				Anim::SetHeadTracking(actor, false);  // eyes on the shot, not on whoever walks by
				_npcStep = NpcStep::kAiming;
				_npcTimer = 0.0f;
			}
			break;

		case NpcStep::kAiming:
			if (_npcTimer >= kSettleSeconds && _npcTimer - a_dt < kSettleSeconds) {
				actor->SetHeading(_npcHeading);
				Anim::PlayAim(actor);
			}
			if (_npcTimer < kSettleSeconds || !PlaceStickInHands(actor)) {
				PlaceStick(a_frame, _sim->Balls()[0].pos, _npcShot.dir, 0.0f, true);
			}
			if (_npcTimer >= kSettleSeconds + s.opponentAimSeconds) {
				Anim::PlayStrike(actor);
				_npcStep = NpcStep::kStriking;
				_npcTimer = 0.0f;
			}
			break;

		case NpcStep::kStriking:
			if (!PlaceStickInHands(actor)) {
				PlaceStick(a_frame, _sim->Balls()[0].pos, _npcShot.dir, 0.0f, true);
			}
			if (_npcTimer >= kContactSeconds) {
				OpponentStrike(a_frame);
				_npcStep = NpcStep::kFollow;
				_npcTimer = 0.0f;
			}
			break;

		case NpcStep::kFollow:
			if (!PlaceStickInHands(actor)) {
				PlaceStick(a_frame, _strikePos, _npcShot.dir, 0.0f, _npcTimer < 0.6f);
			}
			if (_npcTimer >= kStrikeLength - kContactSeconds) {
				PlaceStick(a_frame, _strikePos, _npcShot.dir, 0.0f, false);
				Anim::Stop(actor);
				Anim::SetHeadTracking(actor, true);
				_npcStep = NpcStep::kNone;
				_npcTimer = 0.0f;
				SetWaitSpot(a_frame, actor->data.location);  // stay where they shot from
			}
			break;
		}

		// the cue stick, made here when nobody's aiming (aim mode makes its own)
		if (_npcStep == NpcStep::kAiming && !_stick && table) {
			if (auto* form = StickForm()) {
				_stick = Spawn(form, table, ToWorld(a_frame, _sim->Balls()[0].pos, static_cast<float>(s.spec.ballRadius)));
			}
		}
	}
}
