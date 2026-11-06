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
		// Where the cue is at the moment it meets the ball (PoolStrike frame 35), in the actor's own space (root at the
		// origin, facing +Y), as the game places it from their hands: the tip, and the cue's direction as a yaw (+ =
		// to their right). Measured in Blender for the clips as shipped (tools/anim/README.md): right hand 8.8 out and
		// 9 up, fingers wrapped round the cue, which slopes 7.3 degrees down to the tip.
		// PoolAim2 / PoolStrike2 (variant 1), measured the same way at their contact (frame 35): the bridge hand ~30
		// further forward, so the tip is further ahead of the root, and the cue turned a little more to their left
		// PoolAim3 / PoolStrike3 (variant 2): the cue nearly straight ahead, the tip furthest out of the three
		constexpr int   kAims = 3;
		constexpr float kTipX[kAims] = { 0.59f, -3.46f, 8.06f };
		constexpr float kTipY[kAims] = { 73.37f, 85.56f, 92.02f };
		constexpr float kCueYawOf[kAims] = { -10.85f * 0.017453292f, -14.49f * 0.017453292f, -3.59f * 0.017453292f };
		constexpr float kTipGap = 1.5f;  // between the tip and the ball at contact
		// ...so, along the shot line, the ball is this far ahead of the root, and the root this far to the line's left
		float StanceBackOf(int v) { return kTipX[v] * std::sin(kCueYawOf[v]) + kTipY[v] * std::cos(kCueYawOf[v]) + 3.51f + kTipGap; }
		float StanceSideOf(int v) { return kTipX[v] * std::cos(kCueYawOf[v]) - kTipY[v] * std::sin(kCueYawOf[v]); }
		constexpr float kContactSeconds = 35.0f / 30.0f;  // PoolStrike: the cue reaches the ball on frame 35
		constexpr float kStrikeLength = 60.0f / 30.0f;
		constexpr float kSettleSeconds = 0.4f;   // after being put on the spot, before the aim idle starts
		constexpr float kThinkSeconds = 0.8f;    // after the balls stop, before they set off
		constexpr float kArrivedDistance = 30.0f;
		constexpr float kAlignAfter = 1.0f;      // into the aim pose (fully blended in: earlier the hands are still moving) before the cue is measured
		constexpr float kGlideSeconds = 0.6f;    // the ease sideways onto the ball's line
		constexpr float kRailClear = 20.0f;      // the cue ball this far from the cushion behind it, or the cue goes through the rail
		constexpr float kTableMargin = 22.0f;    // the rails and some room for the body, beyond the felt
		constexpr float kQuitDistance = 4000.0f; // the opponent is this far from the table: the game's off
		constexpr float kWatchDistance = 1500.0f;
		constexpr RE::TESFormID kXMarkerHeading = 0x00000034;
		constexpr float kGripFromButt = 12.0f;   // where the grip hand holds the cue
		// where the cue runs through the right hand, in the hand's own frame: along the wrist -> finger-bases line,
		// across it (towards the index finger), and out of the palm; from the clips (tools/anim/README.md)
		constexpr float kGripAlong = 6.478f;
		constexpr float kGripAcross = 0.133f;
		constexpr float kGripOut = -1.488f;
		constexpr float kBridgeLift = 3.0f;      // the cue rests this far above the bridge hand's thumb-index V (clear of the knuckles)
		constexpr float kWaitRadius = 80.0f;     // further from the wait spot than this: back they go
		constexpr float kRepathSeconds = 12.0f;  // the same walk isn't re-sent sooner than this while it's making progress
		constexpr float kNotStartedSeconds = 1.5f;  // a walk they haven't set off on in this long is sent again (AI reset)
		constexpr float kStallSeconds = 4.0f;    // ...but is, once they've gone this long without getting closer (going round the table can take a while)
		constexpr float kProgress = 10.0f;       // closer by this much counts as progress
		constexpr float kWaitBeyondRail = 55.0f; // the first wait spot: this far outside the felt, on their side
		constexpr float kInTable = 30.0f;        // a root nearer the felt than this is standing in the rail (the body overlaps it)
		constexpr float kWaitClear = 42.0f;      // a wait spot in there is moved out to this far from the felt
		constexpr float kOutOfTableSeconds = 3.0f;  // walking out of the table: put on the spot if not out by then
		// Walks go to a spot this far outside the felt (clear of the table, which blocks the navmesh around it: a
		// walk to the shot spot itself, right by the rail, fails), and they step from there onto the spot
		constexpr float kApproachBeyondFelt = 55.0f;
		constexpr float kPartnerAfter = 10.0f;  // one waiting at a free table this long: someone comes over to play them
		constexpr float kStepAside = 90.0f;  // a watcher (or the NPC waiting their turn) this close to where a shot's taken from moves
		constexpr float kStepIn = 75.0f;         // the last hop onto the spot can't be walked: this close, they're set on it
		constexpr float kStrayed = 400.0f;       // this much further than their best: their own package has them, send the walk again
		constexpr int   kWalkTries = 3;          // a walk that fails is sent again this many times before giving up (then they're put there: tight spaces round a table can't be walked)
		constexpr float kUnholdSeconds = 0.3f;   // after SetRestrained comes off, before the walk is sent (a restrained actor can't walk)
		constexpr float kAlreadyThere = 120.0f;  // at the start, this close to that spot already: they wait where they are

		float Distance2D(const RE::NiPoint3& a, const RE::NiPoint3& b)
		{
			return std::hypot(a.x - b.x, a.y - b.y);
		}

		// the AI package they're on, and which plugin it's from, for the log
		std::string PackageName(RE::Actor* a_actor)
		{
			auto* proc = a_actor ? a_actor->currentProcess : nullptr;
			auto* pkg = proc ? proc->currentPackage.package : nullptr;
			if (!pkg) {
				return "none";
			}
			auto* file = pkg->GetFile(0);
			return std::format("{:08X} ({})", pkg->GetFormID(), file ? std::string(file->GetFilename()) : std::string("?"));
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
		if (_npcGame) {
			const NpcSlot& seat = a_player == _seatIdx ? static_cast<const NpcSlot&>(*this) : _seats[a_player];
			return seat._opponentName.empty() ? std::format("Player {}", a_player + 1) : seat._opponentName;
		}
		if (a_player == 1) {
			return _opponent ? _opponentName : std::string("Player 2");
		}
		auto*             player = RE::PlayerCharacter::GetSingleton();
		const std::string name = player ? std::string(player->GetDisplayFullName()) : std::string();
		return name.empty() ? std::string("Player 1") : name;
	}

	RE::Actor* Game::SeatActor(int a_seat) const
	{
		if (!_npcGame && a_seat == 0) {
			return nullptr;  // you
		}
		const NpcSlot& seat = a_seat == _seatIdx ? static_cast<const NpcSlot&>(*this) : _seats[a_seat];
		auto           ref = seat._opponent.get();
		return ref ? ref->As<RE::Actor>() : nullptr;
	}

	RE::Actor* Game::Opponent() const
	{
		auto ref = _opponent.get();
		return ref ? ref->As<RE::Actor>() : nullptr;
	}

	bool Game::OpponentsTurn() const
	{
		// the NPC being worked on (seat 1 against you; either seat in an NPC game) is up
		return _sim && _opponent && !pool::IsSolo(_rules.GameMode()) && _rules.Shooter() == _seatIdx && !_rules.Over();
	}

	void Game::LoadSeat(int a_seat)
	{
		if (a_seat == _seatIdx || a_seat < 0 || a_seat > 1) {
			return;
		}
		_seats[_seatIdx] = static_cast<NpcSlot&>(*this);
		static_cast<NpcSlot&>(*this) = _seats[a_seat];
		_seatIdx = a_seat;
	}

	void Game::ForBothSeats(const std::function<void()>& a_do)
	{
		if (!_npcGame) {
			a_do();
			return;
		}
		const int back = _seatIdx;
		for (int i = 0; i < 2; ++i) {
			LoadSeat(i);
			a_do();
		}
		LoadSeat(back);
	}

	bool Game::TableFree(const RE::TESObjectREFR* a_table) const
	{
		const int slot = SlotOf(a_table);
		if (slot < 0) {
			return true;
		}
		const TableSlot& t = slot == 0 ? static_cast<const TableSlot&>(*this) : _others[static_cast<std::size_t>(slot - 1)];
		return !t._opponent && !t._npcGame && (!t._sim || t._rules.Over());
	}

	void Game::StartNpcGameAt(RE::Actor* a_first, RE::Actor* a_second, RE::TESObjectREFR* a_table)
	{
		// at the loaded table if that's free (nothing of yours on), otherwise as a table of its own in the background
		const int slot = SlotOf(a_table);
		if (slot == 0 || (slot < 0 && !Busy(*this) && !_aiming)) {
			StartNpcGame(a_first, a_second, a_table);
			return;
		}
		if (slot > 0 || _others.size() + 1 >= kMaxTables) {
			return;
		}
		std::scoped_lock lock(_tableMutex);
		_others.emplace_back();
		const std::size_t i = _others.size() - 1;
		SwapWith(i);
		_bgTick = true;
		StartNpcGame(a_first, a_second, a_table);
		_bgTick = false;
		SwapWith(i);
	}

	void Game::StartNpcGame(RE::Actor* a_first, RE::Actor* a_second, RE::TESObjectREFR* a_table)
	{
		if (!a_first || !a_second || !a_table || a_first == a_second) {
			return;
		}
		if (_opponent) {
			EndChallenge("");
		}
		ExitAim();
		DropWatcher(a_first->GetFormID());
		DropWatcher(a_second->GetFormID());
		_npcGame = true;
		_spectating = false;
		_lastNpcGame = std::chrono::steady_clock::now();
		const std::array<RE::Actor*, 2> who{ a_first, a_second };
		for (int i = 0; i < 2; ++i) {
			LoadSeat(i);
			static_cast<NpcSlot&>(*this) = NpcSlot{};
			_opponent = who[i]->GetHandle();
			_opponentName = who[i]->GetDisplayFullName();
			if (_opponentName.empty()) {
				_opponentName = std::format("Player {}", i + 1);
			}
			Anim::JoinOwnPackage(who[i], i);
		}
		LoadSeat(1);
		_nextBreaker = std::uniform_int_distribution<int>(0, 1)(_npcRng);
		log::info("npc game: {} ({:08X}) and {} ({:08X}) at table {:08X}", PlayerName(0), a_first->GetFormID(), PlayerName(1),
			a_second->GetFormID(), a_table->GetFormID());
		_npcRackType = std::uniform_int_distribution<int>(0, 1)(_npcRng);  // 8-ball or 9-ball
		_rackingNpcGame = true;
		Rack(a_table);
		_rackingNpcGame = false;
		Notify(std::format("{} and {} are playing pool. {} at the table to watch", PlayerName(0), PlayerName(1), Settings::Key(_activateVk.load())));
		if (Frame frame; GetFrame(frame)) {
			ProbeNavmesh(frame);
		}
	}

	RE::TESObjectREFR* Game::PoolTableNear(const RE::NiPoint3& a_pos, float a_within) const
	{
		RE::TESObjectREFR* best = nullptr;
		float              bestD = a_within;
		util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(a_pos, a_within, [&](RE::TESObjectREFR* a_ref) {
				if (a_ref && !a_ref->IsActor() && !a_ref->IsDisabled() && !IsOurs(a_ref) && IsPoolTable(a_ref)) {
					if (const float d = (a_ref->data.location - a_pos).Length(); d < bestD) {
						bestD = d;
						best = a_ref;
					}
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		return best;
	}

	RE::Actor* Game::PickActor(bool a_crosshairOnly) const
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
		if (s.useConsolePick && !a_crosshairOnly) {
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

	void Game::Challenge(RE::Actor* a_actor, RE::TESObjectREFR* a_table)
	{
		const auto& s = Settings::Get();
		if (a_table && !FocusTable(a_table)) {
			return;
		}
		if (_npcGame) {
			EndNpcGame("");
			Clear();
		}
		// asked with the play key: their table racked first, if it isn't the one in play
		if (a_actor && a_table && (!_sim || _table.get().get() != a_table)) {
			if (_opponent) {
				EndChallenge(std::format("{} stops playing", _opponentName));
			}
			_quietStart = true;  // racked again below, once they're in: one start sound
			Rack(a_table);
			_quietStart = false;
		}
		if (!_sim) {
			Notify(std::format("Pool: rack a table first ({}), then look at who you want to play and press {}", Settings::Key(s.keyRack),
				Settings::Key(s.keyChallenge)));
			return;
		}
		auto* actor = a_actor ? a_actor : PickActor();
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
		Anim::JoinOwnPackage(actor, OwnSlot());  // their walks go by fo4pool.esl's package from here on
		_opponentName = actor->GetDisplayFullName();
		if (_opponentName.empty()) {
			_opponentName = "Player 2";
		}
		_nextBreaker = 0;  // you break the first rack
		log::info("challenge: {} ({:08X}), skill {:.0f} ({})", _opponentName, actor->GetFormID(), SkillOf(actor->GetFormID()), SkillName(SkillOf(actor->GetFormID())));
		if (!RerackInPlace()) {
			Rack();
		}
		Say(std::format("{} plays you at 8-ball. You break", _opponentName));
		if (Frame frame; GetFrame(frame)) {
			ProbeNavmesh(frame);  // for the log: can they walk round this table at all?
			log::info("{}: standing {}", _opponentName, NavmeshAt(actor->data.location));
		}
	}

	void Game::HoldOpponent(RE::Actor* a_actor, bool a_hold)
	{
		if (a_actor && a_hold != _npcHeld) {
			_npcHeld = a_hold;
			_npcHeldAt = a_actor->data.location;
			_npcMovedWarned = false;
			log::info("{}: hold {}", _opponentName, a_hold ? "on" : "off");
			Anim::SetRestrained(a_actor, a_hold);
		}
	}

	bool Game::KeepWalking(RE::Actor* a_actor, const RE::ObjectRefHandle& a_target, float a_walkRun, float a_dt, int a_tries)
	{
		auto target = a_target.get();
		if (!a_actor || !target) {
			return true;
		}
		if (a_target != _npcPathTarget || _npcPathId == 0) {
			PathOpponent(a_actor, a_target, a_walkRun, a_target == _npcPathTarget);  // a new walk, or one waiting for the hold to come off
			return false;
		}
		const char* where = a_target == _npcWaitMarker ? "wait spot" : a_target == _npcApproach ? "shot (clear of the table)" : "shot";
		const float d = Distance2D(a_actor->data.location, target->data.location);
		if (d < _npcBestDist - kProgress) {
			_npcBestDist = d;
			_npcStall = 0.0f;
		} else {
			_npcStall += a_dt;
		}

		// is the walk working? PathToReference says false when there's no path or the walk was cut short (their own
		// package can take them back); then it's sent again: no putting them there while they can still walk it
		std::string why;
		const int   result = Anim::PathResult(_npcPathId);
		if (result == 0) {
			why = "PathToReference failed";
		} else if (result == 1 && d > kArrivedDistance * 2.0f) {
			why = std::format("PathToReference says they got there, but they're {:.0f} away", d);
		} else if (d > _npcBestDist + kStrayed) {
			why = std::format("they've wandered off ({:.0f} away, got to {:.0f})", d, _npcBestDist);
		} else if (_npcStall >= kNotStartedSeconds && Distance2D(a_actor->data.location, _npcWalkFrom) < 5.0f) {
			why = std::format("they haven't set off in {:.1f} s ({:.0f} away)", _npcStall, d);
		} else if (_npcStall >= kStallSeconds) {
			why = std::format("no closer in {:.0f} s ({:.0f} away)", _npcStall, d);
		}
		if (why.empty()) {
			return false;
		}
		if (_npcResends >= a_tries) {
			log::warn("{}: the walk to the {} isn't working: {}; gave up after {} tries. Package {}", _opponentName, where, why, _npcResends + 1,
				PackageName(a_actor));
			_npcPathTarget = {};
			_npcPathId = 0;
			return true;
		}
		++_npcResends;
		log::info("{}: the walk to the {}: {}; sending it again ({} of {}). Package {}", _opponentName, where, why, _npcResends, a_tries,
			PackageName(a_actor));
		_npcStall = 0.0f;
		_npcBestDist = d;
		PathOpponent(a_actor, a_target, a_walkRun, true);
		return false;
	}

	namespace
	{
		const char* StepName(int a_step)
		{
			static constexpr std::array<const char*, 5> names{ "none", "walking", "aiming", "striking", "follow" };
			return a_step >= 0 && a_step < 5 ? names[a_step] : "?";
		}
	}

	void Game::LogOpponent(RE::Actor* a_actor, float a_dt)
	{
		if (!Settings::Get().opponentLog || !a_actor) {
			return;
		}
		const auto pos = a_actor->data.location;
		if (_npcHeld && !_npcMovedWarned && Distance2D(pos, _npcHeldAt) > 25.0f) {
			_npcMovedWarned = true;
			log::warn("{}: moved {:.0f} while held (SetRestrained isn't stopping them)", _opponentName, Distance2D(pos, _npcHeldAt));
		}

		// what they're doing, and which AI package the game has them on
		auto*         proc = a_actor->currentProcess;
		const auto    wait = _npcWaitMarker.get();
		const auto    shot = _npcMarker.get();
		const char*   turn = _rules.Over() ? "over" : OpponentsTurn() ? "theirs" : "yours";
		const char*   pathTo = !_npcPathTarget ? "-" : _npcPathTarget == _npcWaitMarker ? "wait" : _npcPathTarget == _npcMarker ? "shot" : _npcPathTarget == _npcApproach ? "approach" : "old";
		const std::string state = std::format("step {} turn {} held {} look {} path {} own pkg {} pkg {} level {} combat {} furniture {} facing {}",
			StepName(static_cast<int>(_npcStep)), turn, _npcHeld, _npcLooking, pathTo, Anim::RunningOwnPackage(a_actor) ? "running" : Anim::InOwnPackage(a_actor) ? "in alias" : "no", PackageName(a_actor),
			proc ? proc->processLevel : 255, a_actor->IsInCombat(), proc && static_cast<bool>(proc->GetOccupiedFurniture()) ? "yes" : "no",
			_npcStep == NpcStep::kAiming || _npcStep == NpcStep::kStriking || _npcStep == NpcStep::kFollow ?
				std::format("{:.0f} deg off the shot", util::AngleDiff(a_actor->data.angle.z, _npcHeading) / 0.017453292f) :
				std::string("-"));

		_npcLogTimer += a_dt;
		if (state == _npcLogLast && _npcLogTimer < 1.0f) {
			return;
		}
		const float speed = _npcLogTimer > 0.0f ? Distance2D(pos, _npcLastPos) / _npcLogTimer : 0.0f;
		log::info("npc {}: {} | at ({:.0f}, {:.0f}) speed {:.0f} | to wait spot {} | to shot {} | walk sent {:.1f}s ago",
			_opponentName, state, pos.x, pos.y, speed,
			wait ? std::format("{:.0f}", Distance2D(pos, wait->data.location)) : std::string("-"),
			shot ? std::format("{:.0f}", Distance2D(pos, shot->data.location)) : std::string("-"),
			_npcPathAge);
		_npcLogLast = state;
		_npcLogTimer = 0.0f;
		_npcLastPos = pos;
	}

	void Game::PathOpponent(RE::Actor* a_actor, const RE::ObjectRefHandle& a_target, float a_walkRun, bool a_force)
	{
		auto target = a_target.get();
		if (!a_actor || !target) {
			return;
		}
		// PathToReference is latent: sending it again while a walk is under way and working fights the walk already
		// going, so the same target is only re-sent after a good while, or when KeepWalking sees it isn't working
		if (!a_force && a_target == _npcPathTarget && _npcPathAge < kRepathSeconds) {
			return;
		}
		const bool again = a_target == _npcPathTarget && _npcPathId != 0;
		if (a_target != _npcPathTarget) {
			_npcBestDist = Distance2D(a_actor->data.location, target->data.location);
			_npcStall = 0.0f;
			_npcResends = 0;
		}
		if (_npcWaitIdle) {
			_npcWaitIdle = false;
			Anim::Stop(a_actor);  // out of PoolWait before walking off
		}
		if (_npcHeld) {
			HoldOpponent(a_actor, false);  // a restrained actor can't walk: let go first, and walk a moment later
			_npcUnheldAge = 0.0f;
		}
		_npcPathTarget = a_target;
		if (_npcUnheldAge < kUnholdSeconds) {
			_npcPathId = 0;  // KeepWalking sends it once the hold's off
			return;
		}
		log::info("{}: walk to the {} ({:.0f} away){}; the spot is {}, they're {}", _opponentName,
			a_target == _npcWaitMarker ? "wait spot" : a_target == _npcApproach ? "shot (clear of the table)" : "shot",
			Distance2D(a_actor->data.location, target->data.location), again ? ", again" : "", NavmeshAt(target->data.location),
			NavmeshAt(a_actor->data.location));
		_npcPathAge = 0.0f;
		_npcWalkFrom = a_actor->data.location;
		Anim::JoinOwnPackage(a_actor, OwnSlot());  // if they aren't in the Opponent alias yet; this walk may still be PathToReference
		_npcPathId = Anim::PathTo(a_actor, target.get(), a_walkRun, again);
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
		Anim::ReleaseOwnPackage(actor);
		_npcFacing = false;
		if (_npcWaitIdle) {
			_npcWaitIdle = false;
			Anim::Stop(actor);
		}
		HideOpponentStick();
	}

	void Game::EndChallenge(const std::string& a_why)
	{
		if (_npcGame) {
			EndNpcGame(a_why);
			return;
		}
		EndSeat(a_why);
	}

	void Game::EndNpcGame(const std::string& a_why)
	{
		if (!_npcGame) {
			return;
		}
		for (int i = 0; i < 2; ++i) {
			LoadSeat(i);
			EndSeat("");
		}
		LoadSeat(1);
		_seats = {};
		_npcGame = false;
		_spectating = false;
		if (!_bgTick) {
			_watching = false;
		}
		if (!a_why.empty()) {
			Say(a_why);
		}
		log::info("npc game over: {}", a_why);
	}

	void Game::EndSeat(const std::string& a_why)
	{
		ReleaseOpponent();
		RemoveRef(_npcMarker);
		RemoveRef(_npcApproach);
		RemoveRef(_npcWaitMarker);
		RemoveRef(_npcStick);
		_npcStickReady = false;
		_autoAim = false;
		_opponent = {};
		_npcStep = NpcStep::kNone;
		_npcTimer = 0.0f;
		if (!_bgTick) {
			_watching = false;
		}
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

		// how far back along the shot line the cue ball is from the root: the clip's distance, or further if that's
		// over the table (they lean in from the rail and the cue reaches less far than the clip shows; the shot
		// picker avoids those shots where it can)
		const int    v = std::clamp(_npcAimVariant, 0, kAims - 1);
		double       back = StanceBackOf(v) / scale;
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
		const RE::NiPoint3 left{ -flat.y, flat.x, 0.0f };
		// they face a little off the shot line, so that the cue (which points kCueYawOf[v] off their facing) lies along it
		a_heading = std::atan2(flat.x, flat.y) - kCueYawOf[v];

		RE::NiPoint3 root = ToWorld(a_frame, a_cue - a_dir * back, 0.0f) + left * StanceSideOf(v);
		root.z = a_frame.origin.z;  // the clip has the felt 70.4 above the feet: the table's own height
		return root;
	}

	void Game::SetTableCollision(bool a_on)
	{
		if (_tableNoCollide == !a_on) {
			return;
		}
		_tableNoCollide = !a_on;
		auto  table = _table.get();
		auto* root = table ? table->Get3D() : nullptr;
		if (!root) {
			return;  // (unloaded: it comes back with its collision)
		}
		RE::bhkWorld::EnableCollision(root, a_on, true, true);
		log::info("table {:08X}: collision {}", table->GetFormID(), a_on ? "back on" : "off while they shoot");
	}

	pool::Vec2 Game::TablePoint(const Frame& a_frame, const RE::NiPoint3& a_pos) const
	{
		const RE::NiPoint3 rel = (a_pos - a_frame.origin) * (1.0f / std::max(0.01f, a_frame.scale));
		const RE::NiPoint3 t = a_frame.rot * RE::NiPoint3{ rel.x, rel.y, 0.0f };
		return { t.x, t.y };
	}

	bool Game::InTable(const Frame& a_frame, const RE::NiPoint3& a_pos, float a_beyond) const
	{
		const auto&      s = Settings::Get();
		const pool::Vec2 p = TablePoint(a_frame, a_pos);
		return std::abs(p.x) < s.spec.length * 0.5 + a_beyond && std::abs(p.y) < s.spec.width * 0.5 + a_beyond;
	}

	RE::NiPoint3 Game::OutOfTable(const Frame& a_frame, const RE::NiPoint3& a_pos, float a_beyond) const
	{
		if (!InTable(a_frame, a_pos, a_beyond)) {
			return a_pos;
		}
		const auto&  s = Settings::Get();
		const double hx = s.spec.length * 0.5 + a_beyond;
		const double hy = s.spec.width * 0.5 + a_beyond;
		pool::Vec2   p = TablePoint(a_frame, a_pos);
		if (hx - std::abs(p.x) < hy - std::abs(p.y)) {  // out across the nearer edge
			p.x = p.x < 0 ? -hx : hx;
		} else {
			p.y = p.y < 0 ? -hy : hy;
		}
		RE::NiPoint3 out = ToWorld(a_frame, p, 0.0f);
		out.z = a_pos.z;
		return out;
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
		// never in the table: a spot in the rail (where they shot from, say) is moved out past it
		RE::NiPoint3 pos = OutOfTable(a_frame, a_pos, kWaitClear);
		pos.z = a_frame.origin.z;
		// facing the middle of the table while they wait
		_npcWaitHeading = std::atan2(a_frame.origin.x - pos.x, a_frame.origin.y - pos.y);
		// one of the two waits, at random (both clips face the root's forward, so the same heading for either)
		_npcWaitVariant = std::uniform_int_distribution<int>(0, 1)(_npcRng);
		_npcWaitMarker = Spawn(bound, table, pos);
		if (auto marker = _npcWaitMarker.get()) {
			marker->data.angle = { 0.0f, 0.0f, _npcWaitHeading };
		}
		_npcFacing = false;
	}

	void Game::WaitByTable()
	{
		auto*  actor = Opponent();
		Frame  frame;
		if (!actor || !GetFrame(frame)) {
			return;
		}
		// where they are, in table space; then the nearest point just outside the felt on that side
		const auto&        s = Settings::Get();
		const RE::NiPoint3 rel = (actor->data.location - frame.origin) * (1.0f / std::max(0.01f, frame.scale));
		const RE::NiPoint3 t = frame.rot * RE::NiPoint3{ rel.x, rel.y, 0.0f };
		const double       hx = s.spec.length * 0.5 + kWaitBeyondRail;
		const double       hy = s.spec.width * 0.5 + kWaitBeyondRail;
		pool::Vec2         p{ std::clamp<double>(t.x, -hx, hx), std::clamp<double>(t.y, -hy, hy) };
		if (std::abs(p.x) < hx && std::abs(p.y) < hy) {  // inside the outline: out to the nearest edge
			if (hx - std::abs(p.x) < hy - std::abs(p.y)) {
				p.x = p.x < 0 ? -hx : hx;
			} else {
				p.y = p.y < 0 ? -hy : hy;
			}
		}
		RE::NiPoint3 spot = ToWorld(frame, p, 0.0f);
		spot.z = frame.origin.z;
		if (Distance2D(actor->data.location, spot) < kAlreadyThere && !InTable(frame, actor->data.location, kInTable)) {
			spot = actor->data.location;  // near enough (and clear of the table): no walk, they're held where they stand
		}
		SetWaitSpot(frame, spot);
	}

	void Game::TickWaiting(RE::Actor* a_actor, const Frame& a_frame, float a_dt)
	{
		// while it's your shot they watch you (two NPCs: not you)
		OpponentLooksAtPlayer(a_actor, !_npcGame && !OpponentsTurn() && !_rules.Over());

		auto marker = _npcWaitMarker.get();
		if (!marker || _npcReleasedForOver) {
			return;
		}
		// standing in the table (where they shot from, with its collision off; the wait spot itself is always clear of
		// it): they turn and walk out to the spot, and if that doesn't get them out in a few seconds they're put there
		const bool  inTable = !_tableNoCollide && InTable(a_frame, a_actor->data.location, kInTable);
		const float away = Distance2D(a_actor->data.location, marker->data.location);
		if (away > kWaitRadius || inTable) {
			// not there yet (or pulled off it): walk there, and if the walk doesn't get them there in time (their
			// sandbox package can keep winning), put them there
			if (inTable && !_npcInTableLogged) {
				_npcInTableLogged = true;
				log::info("{}: standing in the table: walking out to the wait spot", _opponentName);
			}
			if (_npcFacing && _npcWaitIdle) {
				Anim::Stop(a_actor);  // out of the wait pose to walk
				_npcWaitIdle = false;
			}
			_npcFacing = false;
			_npcWaitWalk += a_dt;
			if (_npcWaitWalk > (inTable ? kOutOfTableSeconds : Settings::Get().opponentWalkTimeout)) {
				log::warn("{}: didn't reach the wait spot in {:.0f} s ({:.0f} away; package {}): putting them there", _opponentName, _npcWaitWalk, away,
					PackageName(a_actor));
				_npcWaitWalk = 0.0f;
				Anim::MoveTo(a_actor, marker.get());
				return;
			}
			if (KeepWalking(a_actor, _npcWaitMarker, 0.0f, a_dt, kWalkTries)) {
				_npcWaitWalk = 0.0f;
				Anim::MoveTo(a_actor, marker.get());
			}
		} else if (!_npcFacing) {
			// there (anywhere within kWaitRadius: there's no gap between "walk back" and "hold")
			_npcWaitWalk = 0.0f;
			_npcInTableLogged = false;
			// there: face the table and stay put, so their sandbox package can't walk them off
			_npcFacing = true;
			// standing with the cue until their turn (the wait picked with the spot)
			a_actor->SetHeading(_npcWaitHeading);
			HoldOpponent(a_actor, true);
			_npcWaitIdle = Anim::PlayWait(a_actor, _npcWaitVariant);
			log::info("{}: waiting ({}), facing the table: heading {:.0f} deg (now {:.0f})", _opponentName, _npcWaitVariant == 1 ? "PoolWait2" : "PoolWait",
				_npcWaitHeading * 57.29578f, a_actor->data.angle.z * 57.29578f);
		} else if (_npcWaitIdle) {
			// and kept that way: the end of their walk, or the hold, can turn them back to the marker's old facing
			float diff = std::remainder(a_actor->data.angle.z - _npcWaitHeading, 6.2831853f);
			if (std::abs(diff) > 0.05f) {
				a_actor->SetHeading(_npcWaitHeading);
			}
		}
	}

	bool Game::PlaceStickInHands(RE::Actor* a_actor, float* a_side)
	{
		auto* root = a_actor ? a_actor->Get3D() : nullptr;
		auto  stick = _npcStick.get();
		if (!root || !stick || !stick->Get3D()) {
			return false;
		}
		if (!_npcStickReady) {
			_npcStickReady = true;
			PrepareStick(stick.get());
		}
		auto node = [&](const char* a_name) -> RE::NiAVObject* {
			return root->GetObjectByName(RE::BSFixedString{ a_name });
		};
		auto* rHand = node("RArm_Hand");
		auto* lHand = node("LArm_Hand");
		if (!rHand || !lHand) {
			return false;
		}
		// grip: a fixed point in the right hand (inside the fingers wrapped round the cue), from the hand's frame:
		// the wrist, the line to the fingers' bases, and the line across them from the little finger to the index;
		// the fingers themselves move in the clip, so they don't decide it. Bridge: on top of the left hand, over the
		// V between thumb and index finger
		RE::NiPoint3 grip = rHand->world.translate;
		auto*        rIndex = node("RArm_Finger21");
		auto*        rLittle = node("RArm_Finger51");
		RE::NiPoint3 bases{};
		int          found = 0;
		for (const char* n : { "RArm_Finger21", "RArm_Finger31", "RArm_Finger41", "RArm_Finger51" }) {
			if (auto* j = node(n)) {
				bases += j->world.translate;
				++found;
			}
		}
		if (found == 4 && rIndex && rLittle) {
			bases = bases * 0.25f;
			const RE::NiPoint3 e1 = util::Normalized(bases - rHand->world.translate);
			const RE::NiPoint3 side = rIndex->world.translate - rLittle->world.translate;
			const RE::NiPoint3 e2 = util::Normalized(side - e1 * side.Dot(e1));
			const RE::NiPoint3 e3 = e1.Cross(e2);
			grip = rHand->world.translate + e1 * kGripAlong + e2 * kGripAcross + e3 * kGripOut;
		}
		auto*        thumb = node("LArm_Finger11");
		auto*        index = node("LArm_Finger21");
		RE::NiPoint3 bridge = thumb && index ? (thumb->world.translate + index->world.translate) * 0.5f : lHand->world.translate;
		bridge.z += kBridgeLift;
		const RE::NiPoint3 along = bridge - grip;
		if (along.Length() < 10.0f) {
			return false;  // hands together: not a pool pose
		}
		const RE::NiPoint3 dir = util::Normalized(along);
		// the cue (Weapons\PoolCue) is 94 long; the grip hand holds it about 12 from the butt
		const auto&  s = Settings::Get();
		const float  reach = std::abs(s.stickTip - s.stickButt) - kGripFromButt;
		const RE::NiPoint3 tip = grip + dir * reach;

		Frame frame;
		if (a_side && _sim && GetFrame(frame)) {
			const RE::NiPoint3 shotW = util::Normalized(frame.rot.Transpose() * RE::NiPoint3{ static_cast<float>(_npcShot.dir.x), static_cast<float>(_npcShot.dir.y), 0.0f });
			const RE::NiPoint3 shot = util::Normalized(RE::NiPoint3{ shotW.x, shotW.y, 0.0f });
			const RE::NiPoint3 across{ -shot.y, shot.x, 0.0f };
			const RE::NiPoint3 ball = ToWorld(frame, _sim->Balls()[0].pos, static_cast<float>(s.spec.ballRadius));
			*a_side = (ball - tip).Dot(across);
		}
		PlaceStickAlong(stick.get(), tip, dir, 1.0f);
		return true;
	}

	void Game::GlideOntoLine(RE::Actor* a_actor, const Frame& a_frame, float a_dt)
	{
		// Bodies differ from the one the stance was measured on, so the hands put the cue a few units to one side of
		// the cue ball's line. Measured from the hands once the aim pose is in (averaged over a few frames), then they
		// ease sideways onto it over kGlideSeconds while they settle into the aim: a small shuffle, not a jump.
		float side = 0.0f;
		if (!PlaceStickInHands(a_actor, &side) || _npcGlideDone) {
			return;
		}
		if (_npcGlideT < 0.0f) {
			if (_npcGlideSamples == 0) {
				_npcGlideMin = _npcGlideMax = side;
			}
			_npcGlideSum += side;
			_npcGlideMin = std::min(_npcGlideMin, side);
			_npcGlideMax = std::max(_npcGlideMax, side);
			if (++_npcGlideSamples < 8) {
				return;
			}
			if (_npcGlideMax - _npcGlideMin > 2.0f) {
				// the hands are still moving (the aim still blending in, or the body settling): measure again
				log::info("{}: the cue's still moving ({:.1f} .. {:.1f} off the line): measuring again", _opponentName, _npcGlideMin, _npcGlideMax);
				_npcGlideSum = 0.0f;
				_npcGlideSamples = 0;
				if (++_npcGlideTries >= 4) {
					_npcGlideDone = true;  // never settled: leave them where the stance put them
				}
				return;
			}
			const float avg = _npcGlideSum / _npcGlideSamples;
			log::info("{}: the hands put the cue {:.1f} off the cue ball's line: easing over", _opponentName, avg);
			if (std::abs(avg) < 0.6f || std::abs(avg) > 18.0f) {
				_npcGlideDone = true;  // (more than that is a bad measurement, not a body that's a bit different)
				return;
			}
			const RE::NiPoint3 shotW = util::Normalized(a_frame.rot.Transpose() * RE::NiPoint3{ static_cast<float>(_npcShot.dir.x), static_cast<float>(_npcShot.dir.y), 0.0f });
			const RE::NiPoint3 shot = util::Normalized(RE::NiPoint3{ shotW.x, shotW.y, 0.0f });
			_npcGlideFrom = a_actor->data.location;
			_npcGlideBy = RE::NiPoint3{ -shot.y, shot.x, 0.0f } * avg;
			_npcGlideT = 0.0f;
		}
		_npcGlideT += a_dt;
		const float t = std::min(1.0f, _npcGlideT / kGlideSeconds);
		const float ease = t * t * (3.0f - 2.0f * t);  // smoothstep
		Anim::SetPosition(a_actor, _npcGlideFrom + _npcGlideBy * ease);
		if (t >= 1.0f) {
			_npcGlideDone = true;
		}
	}

	namespace
	{
		// The grip: a fixed point in the right hand (inside the fingers wrapped round the cue), from the hand's frame:
		// the wrist, the line to the fingers' bases, and the line across them from the little finger to the index
		bool RightGrip(RE::NiAVObject* a_root, RE::NiPoint3& a_out)
		{
			auto node = [&](const char* a_name) { return a_root->GetObjectByName(RE::BSFixedString{ a_name }); };
			auto* rHand = node("RArm_Hand");
			auto* rIndex = node("RArm_Finger21");
			auto* rLittle = node("RArm_Finger51");
			if (!rHand) {
				return false;
			}
			a_out = rHand->world.translate;
			RE::NiPoint3 bases{};
			int          found = 0;
			for (const char* n : { "RArm_Finger21", "RArm_Finger31", "RArm_Finger41", "RArm_Finger51" }) {
				if (auto* j = node(n)) {
					bases += j->world.translate;
					++found;
				}
			}
			if (found == 4 && rIndex && rLittle) {
				bases = bases * 0.25f;
				const RE::NiPoint3 e1 = util::Normalized(bases - rHand->world.translate);
				const RE::NiPoint3 side = rIndex->world.translate - rLittle->world.translate;
				const RE::NiPoint3 e2 = util::Normalized(side - e1 * side.Dot(e1));
				const RE::NiPoint3 e3 = e1.Cross(e2);
				a_out = rHand->world.translate + e1 * kGripAlong + e2 * kGripAcross + e3 * kGripOut;
			}
			return true;
		}
	}

	bool Game::PlaceStickUpright(RE::Actor* a_actor)
	{
		auto* root = a_actor ? a_actor->Get3D() : nullptr;
		auto  stick = _npcStick.get();
		if (!root || !stick || !stick->Get3D()) {
			return false;
		}
		if (!_npcStickReady) {
			_npcStickReady = true;
			PrepareStick(stick.get());
		}
		// through the grip (the same point in the hand as when they shoot), from the butt on the floor: PoolWait holds
		// the cue out in front, its grip at about (11.5, 24.5, 87.5) in the root's space, the butt below and a little
		// further out, so the cue stands clear of the leg and chest; PoolWait2 holds it out to their right, its grip at
		// about (33, 9.5, 78.6)
		RE::NiPoint3 palm;
		if (!RightGrip(root, palm)) {
			return false;
		}
		const float        h = a_actor->data.angle.z;
		const RE::NiPoint3 right{ std::cos(h), -std::sin(h), 0.0f };
		const RE::NiPoint3 fwd{ std::sin(h), std::cos(h), 0.0f };
		const bool         lean = _npcWaitVariant == 1;
		RE::NiPoint3       butt = a_actor->data.location + right * (lean ? 36.0f : 14.5f) + fwd * (lean ? 13.0f : 30.5f);
		butt.z = a_actor->data.location.z + 0.6f;
		const RE::NiPoint3 dir = util::Normalized(palm - butt);
		const auto&        s = Settings::Get();
		PlaceStickAlong(stick.get(), butt + dir * std::abs(s.stickTip - s.stickButt), dir, 1.0f);
		return true;
	}

	bool Game::PlaceStickCarried(RE::Actor* a_actor)
	{
		// Walking about: the cue in the right hand like a walking stick carried at the side, tip forward and up, the
		// fingers closed round it: it crosses the palm through the curl of the middle finger (not along the fingers,
		// which put it inside the hand), a little in on the palm side
		auto* root = a_actor ? a_actor->Get3D() : nullptr;
		auto  stick = _npcStick.get();
		if (!root || !stick || !stick->Get3D()) {
			return false;
		}
		if (!_npcStickReady) {
			_npcStickReady = true;
			PrepareStick(stick.get());
		}
		auto* rHand = root->GetObjectByName(RE::BSFixedString{ "RArm_Hand" });
		auto* rMid = root->GetObjectByName(RE::BSFixedString{ "RArm_Finger31" });
		if (!rHand) {
			return false;
		}
		const auto&  s = Settings::Get();
		auto*        rMid2 = root->GetObjectByName(RE::BSFixedString{ "RArm_Finger32" });
		auto*        rIndex = root->GetObjectByName(RE::BSFixedString{ "RArm_Finger21" });
		auto*        rLittle = root->GetObjectByName(RE::BSFixedString{ "RArm_Finger51" });
		const float  h = a_actor->data.angle.z;
		const RE::NiPoint3 right{ std::cos(h), -std::sin(h), 0.0f };
		const RE::NiPoint3 fwd{ std::sin(h), std::cos(h), 0.0f };
		// where the fingers close: the middle finger's second joint (or between the palm and its first joint)
		RE::NiPoint3 grip = rMid2 ? rMid2->world.translate : rMid ? rMid->world.translate : rHand->world.translate;
		// the palm's face: across the knuckles x down the fingers, turned to face the body (a hanging hand's palm does)
		if (rMid && rIndex && rLittle) {
			const RE::NiPoint3 down = util::Normalized(rMid->world.translate - rHand->world.translate);
			const RE::NiPoint3 across = util::Normalized(rIndex->world.translate - rLittle->world.translate);
			RE::NiPoint3       face = util::Normalized(down.Cross(across));
			const RE::NiPoint3 toBody = a_actor->data.location + RE::NiPoint3{ 0.0f, 0.0f, grip.z - a_actor->data.location.z } - grip;
			if (face.Dot(toBody) < 0.0f) {
				face = face * -1.0f;
			}
			grip = grip + face * s.carryIn;
		}
		const float        tilt = s.carryTilt * 0.017453292f;  // up from level
		const RE::NiPoint3 dir = util::Normalized(fwd * std::cos(tilt) + RE::NiPoint3{ 0.0f, 0.0f, std::sin(tilt) } + right * s.carryOut);
		const float        length = std::abs(s.stickTip - s.stickButt);
		const RE::NiPoint3 butt = grip - dir * (length * s.carryGrip);
		PlaceStickAlong(stick.get(), butt + dir * length, dir, 1.0f);
		return true;
	}

	void Game::HideOpponentStick()
	{
		if (auto stick = _npcStick.get()) {
			PlaceNode(stick.get(), {}, {}, 1.0f, true);
		}
	}

	void Game::PlanOpponentShot(const Frame& a_frame)
	{
		const auto&    s = Settings::Get();
		pool::AiParams p;
		// their own skill (0-100, from how much they've played): a beginner misses a lot, a pro hardly ever
		p.skill = std::clamp(0.2 + 0.78 * SkillOf(Opponent() ? Opponent()->GetFormID() : 0) / 100.0, 0.0, 1.0);
		p.minSpeed = s.minSpeed;
		p.maxSpeed = s.maxSpeed;
		// only shots they can stand behind: no leaning across the table (table units: the table may be scaled)
		// (with the longest reach of the aims, PoolAim3's: the aim is picked to suit the shot below)
		p.stanceBack = StanceBackOf(2) / std::max(0.01f, a_frame.scale);
		p.tableMargin = kTableMargin;
		p.railClear = kRailClear;  // and the cue (sloping down to the ball) clear of the rail behind the cue ball
		_npcShot = pool::ChooseShot(*_sim, _rules, _seatIdx, p, _npcRng);
		if (const double over = pool::Overreach(*_sim, _npcShot.placeCue ? _npcShot.cuePos : _sim->Balls()[0].pos, _npcShot.dir, p); over > p.reachSlack) {
			log::info("{}: no shot they can stand behind from here; this one needs {:.0f} more reach", _opponentName, over);
		}
		if (_npcShot.placeCue) {
			_sim->Place(0, _npcShot.cuePos);
			_rules.PlacedCueBall();
			_dropTime[0] = 0.0f;
			Say(std::format("{} places the cue ball", _opponentName));
		}
		_manualPocket = _rules.OnEight(_seatIdx, *_sim) ? _npcShot.pocket : -1;  // the HUD marks their call
		log::info("{}: shot at ball {} into pocket {}{}, speed {:.0f}, dir ({:.3f}, {:.3f})", _opponentName, _npcShot.target, _npcShot.pocket,
			_npcShot.checked ? " (checked)" : "", _npcShot.speed, _npcShot.dir.x, _npcShot.dir.y);

		// which aim: PoolAim2 or PoolAim3 (PoolAim, the first, isn't used any more), whichever reaches this shot, at
		// random if both do; if neither does, the one that reaches furthest
		{
			int fits[kAims];
			int n = 0;
			for (int v = 1; v < kAims; ++v) {
				pool::AiParams pv = p;
				pv.stanceBack = StanceBackOf(v) / std::max(0.01f, a_frame.scale);
				if (pool::Overreach(*_sim, _sim->Balls()[0].pos, _npcShot.dir, pv) <= pv.reachSlack) {
					fits[n++] = v;
				}
			}
			_npcAimVariant = n > 0 ? fits[std::uniform_int_distribution<int>(0, n - 1)(_npcRng)] : kAims - 1;
		}
		_npcStance = StanceFor(a_frame, _sim->Balls()[0].pos, _npcShot.dir, _npcHeading);
		StepAsideFrom(_npcStance);

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
		// the walk goes to a spot clear of the table, straight out from the shot spot
		RemoveRef(_npcApproach);
		{
			const RE::NiPoint3 rel = (_npcStance - a_frame.origin) * (1.0f / std::max(0.01f, a_frame.scale));
			const RE::NiPoint3 t = a_frame.rot * RE::NiPoint3{ rel.x, rel.y, 0.0f };
			const double       hx = s.spec.length * 0.5 + kApproachBeyondFelt;
			const double       hy = s.spec.width * 0.5 + kApproachBeyondFelt;
			pool::Vec2         p{ t.x, t.y };
			if (std::abs(p.x) < hx && std::abs(p.y) < hy) {
				if (hx - std::abs(p.x) < hy - std::abs(p.y)) {
					p.x = p.x < 0 ? -hx : hx;
				} else {
					p.y = p.y < 0 ? -hy : hy;
				}
			}
			RE::NiPoint3 approach = ToWorld(a_frame, p, 0.0f);
			approach.z = a_frame.origin.z;
			if (bound && table && Distance2D(approach, _npcStance) > 15.0f) {
				_npcApproach = Spawn(bound, table, approach);
				if (auto a = _npcApproach.get()) {
					a->data.angle = { 0.0f, 0.0f, _npcHeading };
				}
			}
		}
		_npcTimer = 0.0f;
		OpponentLooksAtPlayer(actor, false);
		_npcFacing = false;
		if (actor && marker) {
			_npcAtApproach = false;
			_npcTurnWarned = false;
			_npcGlideSum = 0.0f;
			_npcGlideSamples = 0;
			_npcGlideT = -1.0f;
			_npcGlideDone = false;
			_npcGlideTries = 0;
			PathOpponent(actor, _npcApproach ? _npcApproach : _npcMarker, s.opponentRun ? 1.0f : 0.0f);
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
		const int   called = _rules.OnEight(_seatIdx, *_sim) ? _npcShot.pocket : -1;
		_strikePos = _sim->Balls()[0].pos;
		const float power = static_cast<float>(std::clamp((_npcShot.speed - s.minSpeed) / std::max(1.0f, s.maxSpeed - s.minSpeed), 0.0, 1.0));
		Audio::Play(Audio::Bank::kCue, ToWorld(a_frame, _strikePos, static_cast<float>(s.spec.ballRadius)), 0.25f + 0.75f * power);
		_rules.BeginShot(*_sim, called);
		_shotLive = true;
		_sim->Strike(_npcShot.dir, _npcShot.speed);
		log::info("{} shoots: speed {:.0f}", _opponentName, _npcShot.speed);
	}

	namespace
	{
		// a_pos on this navmesh: inside one of its triangles seen from above, with the triangle within a step of
		// a_pos's height. Vertices are in world space.
		bool OnNavmesh(RE::NavMesh* a_nav, const RE::NiPoint3& a_pos, float a_height = 60.0f)
		{
			const auto& v = a_nav->vertices;
			for (const auto& tri : a_nav->triangles) {
				if (tri.vertices[0] >= v.size() || tri.vertices[1] >= v.size() || tri.vertices[2] >= v.size()) {
					continue;
				}
				const auto& a = v[tri.vertices[0]].location;
				const auto& b = v[tri.vertices[1]].location;
				const auto& c = v[tri.vertices[2]].location;
				const float d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
				if (std::abs(d) < 1e-6f) {
					continue;
				}
				const float l1 = ((b.y - c.y) * (a_pos.x - c.x) + (c.x - b.x) * (a_pos.y - c.y)) / d;
				const float l2 = ((c.y - a.y) * (a_pos.x - c.x) + (a.x - c.x) * (a_pos.y - c.y)) / d;
				const float l3 = 1.0f - l1 - l2;
				if (l1 < 0.0f || l2 < 0.0f || l3 < 0.0f) {
					continue;
				}
				const float z = a.z * l1 + b.z * l2 + c.z * l3;
				if (std::abs(z - a_pos.z) <= a_height) {
					return true;
				}
			}
			return false;
		}
	}

	void Game::ProbeNavmesh(const Frame& a_frame)
	{
		// Pathing only goes where there's navmesh. Every navmesh with a vertex within 2500 of the table, then rings
		// of points round the felt (a point every ~25 units) at a few distances out: how many have navmesh under them
		const auto start = std::chrono::steady_clock::now();
		_navmeshes = NavmeshesFor(_table.get().get(), a_frame.origin);
		const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
		std::string ids;
		for (const auto id : _navmeshes) {
			auto* form = RE::TESForm::GetFormByID(id);
			auto* file = form ? form->GetFile(0) : nullptr;
			ids += std::format(" {:08X}({})", id, file ? std::string(file->GetFilename()) : std::string("?"));
		}
		log::info("navmesh: {} near the table (found in {:.0f} ms):{}", _navmeshes.size(), ms, ids);

		const auto& s = Settings::Get();
		for (const float out : { 10.0f, 22.0f, 40.0f, 55.0f, 80.0f, 120.0f, 200.0f }) {
			const double hx = s.spec.length * 0.5 + out;
			const double hy = s.spec.width * 0.5 + out;
			const double perimeter = 4.0 * (hx + hy);
			const int    n = static_cast<int>(perimeter / 25.0);
			int          covered = 0;
			std::string  gaps;
			for (int i = 0; i < n; ++i) {
				// walk the rectangle's outline
				double     t = perimeter * i / n;
				pool::Vec2 p;
				if (t < 2 * hx) {
					p = { -hx + t, -hy };
				} else if ((t -= 2 * hx) < 2 * hy) {
					p = { hx, -hy + t };
				} else if ((t -= 2 * hy) < 2 * hx) {
					p = { hx - t, hy };
				} else {
					t -= 2 * hx;
					p = { -hx, hy - t };
				}
				RE::NiPoint3 w = ToWorld(a_frame, p, 0.0f);
				w.z = a_frame.origin.z;
				bool on = false;
				for (const auto id : _navmeshes) {
					auto* form = RE::TESForm::GetFormByID(id);
					if (form && form->GetFormType() == RE::ENUM_FORM_ID::kNAVM && OnNavmesh(static_cast<RE::NavMesh*>(form), w)) {
						on = true;
						break;
					}
				}
				covered += on;
				if (!on && gaps.size() < 160) {
					gaps += std::format(" ({:.0f},{:.0f})", p.x, p.y);
				}
			}
			log::info("navmesh: {:.0f} outside the felt: {}/{} points on navmesh{}{}", out, covered, n, gaps.empty() ? "" : "; none at (table space)", gaps);
		}
	}

	std::vector<RE::TESFormID> Game::NavmeshesNear(const RE::NiPoint3& a_pos) const
	{
		// every navmesh in memory is looked at: slow with a big load order, so each table's list is kept
		// (NavmeshesFor) and this runs once per table
		const auto                 start = std::chrono::steady_clock::now();
		std::vector<RE::TESFormID> out;
		const auto& [map, lock] = RE::TESForm::GetAllForms();
		RE::BSAutoReadLock l{ lock };
		if (map) {
			for (const auto& [id, form] : *map) {
				if (!form || form->GetFormType() != RE::ENUM_FORM_ID::kNAVM) {
					continue;
				}
				auto* nav = static_cast<RE::NavMesh*>(form);
				for (const auto& vert : nav->vertices) {
					if (Distance2D(vert.location, a_pos) < 2500.0f && std::abs(vert.location.z - a_pos.z) < 400.0f) {
						out.push_back(id);
						break;
					}
				}
			}
		}
		const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
		log::info("navmesh scan: {} near ({:.0f}, {:.0f}) in {:.0f} ms", out.size(), a_pos.x, a_pos.y, ms);
		return out;
	}

	const std::vector<RE::TESFormID>& Game::NavmeshesFor(const RE::TESObjectREFR* a_table, const RE::NiPoint3& a_origin)
	{
		const RE::TESFormID id = a_table ? a_table->GetFormID() : 0;
		auto                it = _navCache.find(id);
		if (it == _navCache.end() || (it->second.first - a_origin).Length() > 50.0f) {  // (a table moved in the workshop)
			it = _navCache.insert_or_assign(id, std::make_pair(a_origin, NavmeshesNear(a_origin))).first;
		}
		return it->second.second;
	}

	RE::ObjectRefHandle Game::WatchSpot(RE::TESObjectREFR* a_table, const RE::NiPoint3& a_from, const RE::NiPoint3* a_avoid)
	{
		Frame frame;
		auto* markerForm = RE::TESForm::GetFormByID(kXMarkerHeading);
		auto* bound = markerForm && markerForm->IsBoundObject() ? static_cast<RE::TESBoundObject*>(markerForm) : nullptr;
		if (!a_table || !bound || !FrameOf(a_table, frame)) {
			return {};
		}
		if (_watchNavTable.get().get() != a_table) {
			_watchNavTable = a_table->GetHandle();
			_watchNavmeshes = NavmeshesFor(a_table, frame.origin);
		}
		const auto& s = Settings::Get();
		// round the felt, [Opponent] fWatchDistance beyond its edges: past where anyone stands to shoot
		const double hx = s.spec.length * 0.5 + Settings::Get().watchDistance;
		const double hy = s.spec.width * 0.5 + Settings::Get().watchDistance;
		std::vector<pool::Vec2> spots{ { -hx, -hy }, { hx, -hy }, { hx, hy }, { -hx, hy }, { 0.0, -hy }, { 0.0, hy }, { -hx, 0.0 }, { hx, 0.0 },
			{ -hx * 0.5, -hy }, { hx * 0.5, -hy }, { -hx * 0.5, hy }, { hx * 0.5, hy } };
		RE::NiPoint3 best{};
		float        bestD = std::numeric_limits<float>::max();
		for (const auto& p : spots) {
			RE::NiPoint3 w = ToWorld(frame, p, 0.0f);
			w.z = frame.origin.z;
			bool on = _watchNavmeshes.empty();
			for (const auto id : _watchNavmeshes) {
				auto* form = RE::TESForm::GetFormByID(id);
				if (form && form->GetFormType() == RE::ENUM_FORM_ID::kNAVM && OnNavmesh(static_cast<RE::NavMesh*>(form), w)) {
					on = true;
					break;
				}
			}
			const bool taken = std::ranges::any_of(_watchers, [&](const Watcher& a_w) {
				auto ref = a_w.spot.get();
				return ref && Distance2D(ref->data.location, w) < 80.0f;
			});
			const bool inTheWay = a_avoid && Distance2D(w, *a_avoid) < kStepAside + 40.0f;
			if (on && !taken && !inTheWay && Distance2D(w, a_from) < bestD) {
				bestD = Distance2D(w, a_from);
				best = w;
			}
		}
		if (bestD == std::numeric_limits<float>::max()) {
			return {};
		}
		auto spot = Spawn(bound, a_table, best);
		if (auto ref = spot.get()) {
			const RE::NiPoint3 to = frame.origin - best;
			ref->data.angle = { 0.0f, 0.0f, std::atan2(to.x, to.y) };
		}
		return spot;
	}

	void Game::StepAsideFrom(const RE::NiPoint3& a_shot)
	{
		auto table = _table.get();
		if (!table) {
			return;
		}
		// watchers at this table standing where the shot's taken from (or on the way to it)
		for (auto& w : _watchers) {
			if (w.table != _table) {
				continue;
			}
			auto* actor = RE::TESForm::GetFormByID<RE::Actor>(w.id);
			auto  spot = w.spot.get();
			if (!actor || !spot) {
				continue;
			}
			if (Distance2D(spot->data.location, a_shot) > kStepAside && Distance2D(actor->data.location, a_shot) > kStepAside) {
				continue;
			}
			auto moved = WatchSpot(table.get(), actor->data.location, &a_shot);
			if (auto ref = moved.get()) {
				log::info("{}: in the way of the shot; watching from another spot", actor->GetDisplayFullName());
				auto old = w.spot;
				w.spot = moved;
				RemoveRef(old);
				Anim::AddWatcher(actor, ref.get());
			}
		}
		// two NPCs: the one waiting their turn, if they're there
		if (_npcGame) {
			NpcSlot& other = _seats[1 - _seatIdx];
			auto     ref = other._opponent.get();
			auto*    actor = ref ? ref->As<RE::Actor>() : nullptr;
			auto     wait = other._npcWaitMarker.get();
			Frame    frame;
			if (actor && GetFrame(frame) && ((wait && Distance2D(wait->data.location, a_shot) < kStepAside) || Distance2D(actor->data.location, a_shot) < kStepAside)) {
				auto moved = WatchSpot(table.get(), actor->data.location, &a_shot);
				if (auto spot = moved.get()) {
					log::info("{}: waiting in the way of the shot; moving", other._opponentName);
					const int me = _seatIdx;
					const RE::NiPoint3 at = spot->data.location;
					RemoveRef(moved);
					LoadSeat(1 - me);
					SetWaitSpot(frame, at);
					HoldOpponent(actor, false);  // walk there (TickWaiting)
					if (_npcWaitIdle) {
						_npcWaitIdle = false;
						Anim::Stop(actor);
					}
					LoadSeat(me);
				}
			}
		}
	}

	std::string Game::NavmeshAt(const RE::NiPoint3& a_pos) const
	{
		for (const auto id : _navmeshes) {
			auto* form = RE::TESForm::GetFormByID(id);
			if (form && form->GetFormType() == RE::ENUM_FORM_ID::kNAVM && OnNavmesh(static_cast<RE::NavMesh*>(form), a_pos)) {
				return std::format("on navmesh {:08X}", id);
			}
		}
		return _navmeshes.empty() ? "navmesh not probed" : "NO navmesh there";
	}

	void Game::KeepFacingShot(RE::Actor* a_actor)
	{
		// lined up: facing so the cue runs along the shot line. Anything that turned them (their package, a look-at)
		// is undone every frame; a big turn is logged, it means something is fighting the hold
		const float off = util::AngleDiff(a_actor->data.angle.z, _npcHeading);
		if (std::abs(off) > 0.02f) {
			if (std::abs(off) > 0.17f && !_npcTurnWarned) {
				_npcTurnWarned = true;
				log::warn("{}: turned {:.0f} deg off the shot while lining it up (package {}); turned back", _opponentName,
					off / 0.017453292f, PackageName(a_actor));
			}
			a_actor->SetHeading(_npcHeading);
		}
	}

	void Game::GetOutOfFurniture(RE::Actor* a_actor, float a_dt)
	{
		// playing pool, not sitting about: out of any chair or workstation they've taken (their sandbox's doing)
		auto* proc = a_actor->currentProcess;
		_npcGetUpTimer = std::max(0.0f, _npcGetUpTimer - a_dt);
		if (!proc || !proc->GetOccupiedFurniture() || _npcGetUpTimer > 0.0f) {
			return;
		}
		_npcGetUpTimer = 3.0f;
		auto furniture = proc->GetOccupiedFurniture().get();
		log::info("{}: in furniture {:08X}: getting up (package {})", _opponentName, furniture ? furniture->GetFormID() : 0u, PackageName(a_actor));
		a_actor->InitiateGetUpPackage();
	}

	void Game::TickOpponent(const Frame& a_frame, float a_dt)
	{
		if (!_opponent) {
			if (!_bgTick) {
				_watching = false;
			}
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
			const float fromTable = Distance2D(player->data.location, a_frame.origin);
			if (!_bgTick) {
				_watching = (!_npcGame || _spectating) && fromTable < kWatchDistance;
			}
			// (you going 125 m away, or travelling, ends it: Game::EndGamesAway)
		}
		if (a_dt <= 0.0f) {
			return;  // paused
		}
		LogOpponent(actor, a_dt);
		if (!_npcReleasedForOver) {
			GetOutOfFurniture(actor, a_dt);
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
		_npcPathAge += a_dt;
		_npcUnheldAge += a_dt;

		// the game's over: let them go about their business until the next rack
		if (_rules.Over() && _npcStep == NpcStep::kNone && !_npcReleasedForOver) {
			_npcReleasedForOver = true;
			HoldOpponent(actor, false);
			OpponentLooksAtPlayer(actor, false);
			if (_npcWaitIdle) {
				_npcWaitIdle = false;
				Anim::Stop(actor);
			}
			HideOpponentStick();
			Anim::ReleaseOwnPackage(actor);
			log::info("{}: game over, released", _opponentName);
		}
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
			{
				// to the spot clear of the table, then the last few steps onto the shot spot
				auto        approach = _npcApproach.get();
				const float toSpot = Distance2D(actor->data.location, _npcStance);
				if (approach && !_npcAtApproach && Distance2D(actor->data.location, approach->data.location) < kArrivedDistance) {
					_npcAtApproach = true;
					log::info("{}: clear of the table by the shot after {:.1f} s: the last steps ({:.0f})", _opponentName, _npcTimer, toSpot);
				}
				const bool lastSteps = !approach || _npcAtApproach;
				const bool there = toSpot < kArrivedDistance;
				const bool late = _npcTimer > s.opponentWalkTimeout;
				bool       failed = false;
				if (!there && !late) {
					// the last steps, right by the rail, may have no path: then they're set on the spot from there
					failed = KeepWalking(actor, lastSteps ? _npcMarker : _npcApproach, s.opponentRun ? 1.0f : 0.0f, a_dt,
						lastSteps && toSpot < kStepIn ? 0 : kWalkTries);
					if (!failed) {
						break;
					}
				}
				if (there) {
					log::info("{}: on the spot ({:.0f}) after {:.1f} s", _opponentName, toSpot, _npcTimer);
				} else if (failed && toSpot < kStepIn) {
					log::info("{}: {:.0f} from the spot, by the rail: set on it", _opponentName, toSpot);
				} else {
					log::warn("{}: never got to the shot ({:.0f} away after {:.0f} s; package {}): putting them there", _opponentName, toSpot, _npcTimer,
						PackageName(actor));
				}
				_npcPathTarget = {};
				_npcPathId = 0;
				RemoveRef(_npcApproach);  // and the walk there (if it's still going) has nowhere to go
				// the table's collision off while they shoot (on again after the follow-through: TickTable), or the
				// rail pushes them back off the spot and the cue falls short of the ball
				SetTableCollision(false);
				if (marker) {
					Anim::MoveTo(actor, marker.get());  // exactly on the spot, facing the shot
				}
				// and kept there, facing the shot, from now: the walk package's destination comes off (or it keeps
				// stepping and turning them about the spot), the hold goes on, and the heading is set every frame
				Anim::Arrived(actor);
				HoldOpponent(actor, true);
				actor->SetHeading(_npcHeading);
				// eyes on the ball they're going for (the cue ball on a safety), not on whoever walks by
				{
					const int look = _npcShot.target > 0 ? _npcShot.target : 0;
					if (auto ball = _balls[look].get()) {
						Anim::SetHeadTracking(actor, true);
						Anim::LookAt(actor, ball.get());
						_npcLooking = false;  // not looking at the player
					} else {
						Anim::SetHeadTracking(actor, false);
					}
				}
				_npcStep = NpcStep::kAiming;
				_npcTimer = 0.0f;
				break;
			}

		case NpcStep::kAiming:
			KeepFacingShot(actor);  // from the moment they're on the spot, before the aim starts
			if (_npcTimer >= kSettleSeconds && _npcTimer - a_dt < kSettleSeconds) {
				HoldOpponent(actor, true);  // (already on since they stepped onto the spot)
				// exactly on the spot before the aim shows (MoveTo can leave them a little off, the table can push)
				if (const float off = Distance2D(actor->data.location, _npcStance); off > 1.5f) {
					log::info("{}: {:.1f} off the spot before the aim: put on it", _opponentName, off);
					Anim::SetPosition(actor, RE::NiPoint3{ _npcStance.x, _npcStance.y, actor->data.location.z });
				}
				Anim::PlayAim(actor, _npcAimVariant);  // the aim the stance was set for (PlanOpponentShot)
			}
			if (_npcTimer >= kSettleSeconds + kAlignAfter) {
				GlideOntoLine(actor, a_frame, a_dt);  // also places the stick
			} else if (_npcTimer < kSettleSeconds ? !PlaceStickCarried(actor) : !PlaceStickInHands(actor)) {
				HideOpponentStick();  // (carried in the hand while they settle onto the spot)
			}
			if (_npcTimer >= kSettleSeconds + s.opponentAimSeconds) {
				Anim::PlayStrike(actor, _npcAimVariant);  // the strike that goes with their aim
				_npcStep = NpcStep::kStriking;
				_npcTimer = 0.0f;
			}
			break;

		case NpcStep::kStriking:
			KeepFacingShot(actor);
			if (!PlaceStickInHands(actor)) {
				HideOpponentStick();
			}
			if (_npcTimer >= kContactSeconds) {
				OpponentStrike(a_frame);
				_npcStep = NpcStep::kFollow;
				_npcTimer = 0.0f;
			}
			break;

		case NpcStep::kFollow:
			KeepFacingShot(actor);
			if (!PlaceStickInHands(actor)) {
				HideOpponentStick();
			}
			if (_npcTimer >= kStrikeLength - kContactSeconds) {
				Anim::Stop(actor);
				Anim::ClearLookAt(actor);  // done looking at the ball they went for
				Anim::SetHeadTracking(actor, true);
				_npcStep = NpcStep::kNone;
				_npcTimer = 0.0f;
				SetWaitSpot(a_frame, actor->data.location);  // stay where they shot from
			}
			break;
		}

		// their cue: made when first needed; upright while PoolWait plays, put away while they walk
		const bool wantStick = _npcWaitIdle || _npcStep == NpcStep::kAiming || _npcStep == NpcStep::kStriking || _npcStep == NpcStep::kFollow;
		if (wantStick && !_npcStick && table) {
			if (auto* form = StickForm()) {
				_npcStick = Spawn(form, table, actor->data.location);
				_npcStickReady = false;
			}
		}
		if (_npcStep == NpcStep::kNone) {
			// at the table, the cue upright by their foot; walking about, carried in the hand
			if (!(_npcWaitIdle ? PlaceStickUpright(actor) : PlaceStickCarried(actor))) {
				HideOpponentStick();
			}
		} else if (_npcStep == NpcStep::kWalking) {
			if (!PlaceStickCarried(actor)) {
				HideOpponentStick();
			}
		}
	}

	void Game::DropWatcher(RE::TESFormID a_id)
	{
		std::erase_if(_watchers, [&](const Watcher& w) {
			if (w.id != a_id) {
				return false;
			}
			if (auto* actor = RE::TESForm::GetFormByID<RE::Actor>(a_id)) {
				Anim::RemoveWatcher(actor);
			}
			auto spot = w.spot;
			RemoveRef(spot);
			return true;
		});
	}

	void Game::TickWatchers()
	{
		// once a second: watchers whose time is up (or who left, died, or are playing you) go back to their own
		// business; now and then a settler near the pool table closest to you wanders over
		const auto now = std::chrono::steady_clock::now();
		const float dt = std::min(5.0f, std::chrono::duration<float>(now - _watchTime).count());
		if (dt < 1.0f || IsGamePaused()) {
			return;
		}
		_watchTime = now;
		const auto& s = Settings::Get();
		if (_watchersStale && Anim::OwnPackageReady()) {
			_watchersStale = false;
			Anim::ClearWatchers();
		}
		if (!s.wander) {
			while (!_watchers.empty()) {
				DropWatcher(_watchers.back().id);
			}
			return;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		// the pool tables near you, nearest first, up to kMaxTables (looked for every 5 s)
		_watchScan -= dt;
		if (_watchScan <= 0.0f) {
			_watchScan = 5.0f;
			std::vector<std::pair<float, RE::TESObjectREFR*>> found;
			util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
				a_cell->ForEachReferenceInRange(player->data.location, 2500.0f, [&](RE::TESObjectREFR* a_ref) {
					if (a_ref && !a_ref->IsActor() && !a_ref->IsDisabled() && IsPoolTable(a_ref) &&
						std::ranges::none_of(found, [&](const auto& f) { return f.second == a_ref; })) {
						found.emplace_back((a_ref->data.location - player->data.location).Length(), a_ref);
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			});
			std::ranges::sort(found, {}, &std::pair<float, RE::TESObjectREFR*>::first);
			_watchTables.clear();
			for (std::size_t i = 0; i < found.size() && i < kMaxTables; ++i) {
				_watchTables.push_back(found[i].second->GetHandle());
			}
		}
		auto nearYou = [&](const RE::ObjectRefHandle& a_table) {
			return std::ranges::any_of(_watchTables, [&](const RE::ObjectRefHandle& t) { return t == a_table; });
		};

		for (std::size_t i = _watchers.size(); i-- > 0;) {
			auto&      w = _watchers[i];
			auto*      actor = RE::TESForm::GetFormByID<RE::Actor>(w.id);
			auto       table = w.table.get();
			w.left -= dt;
			const bool gone = !actor || actor->IsDead(false) || actor->IsDisabled() || !table || !nearYou(w.table) ||
			                  (actor->data.location - table->data.location).Length() > s.wanderRange + 1000.0f;
			if (actor && IsPlaying(actor)) {
				log::info("{}: playing now, not just watching", actor->GetDisplayFullName());
				DropWatcher(w.id);  // their walks are the Players collection's now
			} else if (gone || w.left <= 0.0f || actor->IsInCombat()) {
				if (actor) {
					log::info("{}: done hanging round the table", actor->GetDisplayFullName());
				}
				DropWatcher(w.id);
			}
		}
		// anyone who's got into one of the tables near you (a table set up from a kit has no navmesh cut out for it, so
		// walks can go straight across it; and settlers sandbox round tables too): put just outside it, on the nearest
		// side. Not those playing at it: the one shooting stands at the rail on purpose, the other has TickWaiting
		for (const auto& handle : _watchTables) {
			auto  table = handle.get();
			Frame frame;
			if (!table || !FrameOf(table.get(), frame)) {
				continue;
			}
			util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
				a_cell->ForEachReferenceInRange(frame.origin, 250.0f, [&](RE::TESObjectREFR* a_ref) {
					auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
					if (!actor || actor == player || actor->IsDead(false) || actor->IsDisabled() || IsPlaying(actor) ||
						!InTable(frame, actor->data.location, kInTable)) {
						return RE::BSContainer::ForEachResult::kContinue;
					}
					log::info("{}: in the table: put just outside it", actor->GetDisplayFullName());
					Anim::SetPosition(actor, OutOfTable(frame, actor->data.location, kWaitClear));
					return RE::BSContainer::ForEachResult::kContinue;
				});
			});
		}
		// those standing at their spot: turned to the table (their package faces the spot's marker, which faces it;
		// this catches anything that turned them since)
		for (const auto& w : _watchers) {
			auto* actor = RE::TESForm::GetFormByID<RE::Actor>(w.id);
			auto  spot = w.spot.get();
			auto  table = w.table.get();
			if (!actor || !spot || !table || Distance2D(actor->data.location, spot->data.location) > 40.0f || actor->IsInCombat()) {
				continue;
			}
			const RE::NiPoint3 to = table->data.location - actor->data.location;
			const float        facing = std::atan2(to.x, to.y);
			if (std::abs(util::AngleDiff(actor->data.angle.z, facing)) > 0.35f) {
				actor->SetHeading(facing);
			}
		}
		static std::mt19937 rng{ std::random_device{}() };
		auto arrived = [&](const Watcher& w) {
			auto* actor = RE::TESForm::GetFormByID<RE::Actor>(w.id);
			auto  spot = w.spot.get();
			return actor && spot && !actor->IsInCombat() && Distance2D(actor->data.location, spot->data.location) < 120.0f;
		};

		// now and then one who's got to a table with no game on asks you for a game: F at that table plays them
		if (_offerID && now > _offerUntil) {
			_offerID = 0;
		}
		if (s.offers && !_offerID && !_aiming && now - _lastOffer > std::chrono::duration<float>(s.offerEvery)) {
			for (auto& w : _watchers) {
				auto* actor = RE::TESForm::GetFormByID<RE::Actor>(w.id);
				auto  table = w.table.get();
				if (w.offered || !table || !arrived(w) || !TableFree(table.get()) ||
					(player->data.location - table->data.location).Length() > 1500.0f) {
					continue;
				}
				w.offered = true;
				if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) > 0.6f) {
					continue;  // not everyone asks
				}
				_offerID = w.id;
				_offerTable = w.table;
				_offerUntil = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(s.offerWait));
				_lastOffer = now;
				log::info("{} ({:08X}) asks you for a game", actor->GetDisplayFullName(), actor->GetFormID());
				Notify(std::format("{} wants a game of pool. {} at the table to play", actor->GetDisplayFullName(),
					Settings::Key(_activateVk.load())));
				break;
			}
		}

		// two who've got to a table with no game on play each other, fNpcGameDelay seconds after the second gets
		// there (iNpcGameChance: the chance they do, 100 = always; a miss is tried again a minute later), while
		// fewer than kMaxTables games are going
		if (s.npcGames && !_pick && RunningGames() < kMaxTables) {
			for (const auto& handle : _watchTables) {
				auto table = handle.get();
				std::vector<RE::Actor*> there;
				if (table && TableFree(table.get())) {
					for (const auto& w : _watchers) {
						if (w.table == handle && arrived(w) && _offerID != w.id) {
							there.push_back(RE::TESForm::GetFormByID<RE::Actor>(w.id));
						}
					}
				}
				auto pair = std::ranges::find_if(_pairSince, [&](const auto& p) { return p.first == handle; });
				if (there.size() < 2) {
					if (pair != _pairSince.end()) {
						_pairSince.erase(pair);
					}
					continue;
				}
				if (pair == _pairSince.end()) {
					_pairSince.emplace_back(handle, now);
					log::info("two at a table: {} and {}; a game in {:.0f} s?", there[0]->GetDisplayFullName(), there[1]->GetDisplayFullName(), s.npcGameDelay);
					continue;
				}
				if (now - pair->second < std::chrono::duration<float>(s.npcGameDelay)) {
					continue;
				}
				if (std::uniform_real_distribution<float>(0.0f, 100.0f)(rng) >= s.npcGameChance) {
					pair->second = now + std::chrono::seconds(60) - std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(s.npcGameDelay));
					log::info("they didn't feel like a game (iNpcGameChance {:.0f}); again in a minute", s.npcGameChance);
					continue;
				}
				_pairSince.erase(pair);
				std::shuffle(there.begin(), there.end(), rng);
				StartNpcGameAt(there[0], there[1], table.get());
				return;
			}
		}

		// one waiting at a free table on their own: after kPartnerAfter seconds someone comes over to play them
		if (s.npcGames && !_pick && RunningGames() < kMaxTables) {
			for (const auto& handle : _watchTables) {
				auto       table = handle.get();
				const auto atTable = std::ranges::count_if(_watchers, [&](const Watcher& w) { return w.table == handle; });
				const auto waiting = std::ranges::count_if(_watchers, [&](const Watcher& w) { return w.table == handle && arrived(w) && _offerID != w.id; });
				auto       alone = std::ranges::find_if(_aloneSince, [&](const auto& p) { return p.first == handle; });
				if (!table || !TableFree(table.get()) || atTable != 1 || waiting != 1) {
					if (alone != _aloneSince.end()) {
						_aloneSince.erase(alone);
					}
					continue;
				}
				if (alone == _aloneSince.end()) {
					_aloneSince.emplace_back(handle, now);
					continue;
				}
				if (now - alone->second < std::chrono::duration<float>(kPartnerAfter)) {
					continue;
				}
				_aloneSince.erase(alone);
				for (auto& w : _watchers) {
					if (w.table == handle) {
						w.left = std::max(w.left, 90.0f);  // they wait for the one coming over
					}
				}
				SendWatcher(handle, "comes over to play the one waiting");
			}
		}

		if (_watchTables.empty() || std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) > dt / std::max(5.0f, s.wanderEvery)) {
			return;
		}
		// a table with room for one more (iWanderMax each)
		std::vector<RE::ObjectRefHandle> room;
		for (const auto& handle : _watchTables) {
			const auto n = std::ranges::count_if(_watchers, [&](const Watcher& w) { return w.table == handle; });
			if (handle.get() && n < s.wanderMax) {
				room.push_back(handle);
			}
		}
		if (room.empty()) {
			return;
		}
		const auto tableHandle = room[std::uniform_int_distribution<std::size_t>(0, room.size() - 1)(rng)];
		auto       table = tableHandle.get();

		SendWatcher(tableHandle, "wanders over");
	}

	bool Game::SendWatcher(const RE::ObjectRefHandle& a_table, const char* a_why)
	{
		const auto& s = Settings::Get();
		auto*       player = RE::PlayerCharacter::GetSingleton();
		auto        table = a_table.get();
		const auto& tableHandle = a_table;
		static std::mt19937 rng{ std::random_device{}() };
		if (!table || !player) {
			return false;
		}
		// someone: a grown-up NPC near the table who isn't fighting, following you, playing or already watching
		static auto* npcType = RE::TESForm::GetFormByID<RE::BGSKeyword>(0x00013794);    // ActorTypeNPC
		static auto* companions = RE::TESForm::GetFormByID<RE::TESFaction>(0x00023C01);  // CurrentCompanionFaction
		std::vector<RE::Actor*> candidates;
		util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(table->data.location, s.wanderRange, [&](RE::TESObjectREFR* a_ref) {
				auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
				if (!actor || actor == player || actor->IsDisabled() || actor->IsDead(false) || !actor->Get3D() ||
					actor->IsChild() || actor->IsInCombat() || (npcType && !actor->HasKeywordHelper(npcType, nullptr)) ||
					(companions && actor->IsInFaction(companions)) || IsPlaying(actor) ||
					std::ranges::any_of(_watchers, [&](const Watcher& w) { return w.id == actor->GetFormID(); }) ||
					std::ranges::any_of(candidates, [&](RE::Actor* c) { return c == actor; })) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				candidates.push_back(actor);
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		if (candidates.empty()) {
			return false;
		}
		auto* who = candidates[std::uniform_int_distribution<std::size_t>(0, candidates.size() - 1)(rng)];
		const float stay = s.wanderStay * std::uniform_real_distribution<float>(0.8f, 1.3f)(rng);
		auto spot = WatchSpot(table.get(), who->data.location);
		auto spotRef = spot.get();
		if (!spotRef) {
			log::info("no spot to watch from round this table (navmesh?)");
			return false;
		}
		Watcher w{ who->GetFormID(), stay };
		w.spot = spot;
		w.table = tableHandle;
		_watchers.push_back(w);
		log::info("{} ({:08X}) {} to the table, spot {}, for {:.0f} s ({} could)", who->GetDisplayFullName(), who->GetFormID(), a_why,
			NavmeshAt(spotRef->data.location), stay, candidates.size());
		Anim::AddWatcher(who, spotRef.get());
		return true;
	}
}
