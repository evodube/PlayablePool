// The 8-ball game on top of the physics: judging shots (sim/Rules), ball in hand, calling the 8, and the state
// the HUD shows.

#include "Game.h"

#include "Anim.h"
#include "Audio.h"
#include "Settings.h"

namespace fp
{
	namespace
	{
		const char* PocketName(int a_pocket)
		{
			static constexpr std::array<const char*, 6> names{ "head-left corner", "head-right corner", "foot-left corner",
				"foot-right corner", "left side", "right side" };
			return a_pocket >= 0 && a_pocket < 6 ? names[a_pocket] : "any pocket";
		}

		const char* GroupName(pool::Group a_group)
		{
			return a_group == pool::Group::kSolids ? "solids" : a_group == pool::Group::kStripes ? "stripes" : "open";
		}
	}

	void Game::StartGame()
	{
		const auto& s = Settings::Get();
		// Solo Play (iGameMode=1) unless someone's playing you; 8-ball or 9-ball as racked
		const bool solo = s.gameMode == 1 && !_opponent;
		const bool nine = _gameType == 1;
		const auto mode = solo ? (nine ? pool::Mode::kSoloNine : pool::Mode::kSolo) : (nine ? pool::Mode::kNineBall : pool::Mode::kEightBall);
		_bet = 0;
		if (_opponent && !_npcGame && Settings::Get().bets) {
			if (auto* them = Opponent()) {
				_bet = StakeFor(SkillOf(them->GetFormID()));  // for caps: what they play for, by their skill
			}
		}
		if (_opponent) {
			_autoAim = !_npcGame && _nextBreaker == 0;  // your break: straight into the shot camera
			ForBothSeats([&]() {
				if (auto* actor = Opponent(); actor && _npcStep != NpcStep::kNone) {
					Anim::Stop(actor);
				}
				_npcStep = NpcStep::kNone;
				_npcTimer = 0.0f;
				_npcReleasedForOver = false;
				if (!_npcWaitMarker) {
					WaitByTable();
				}
			});
		}
		_rules.NewGame(_nextBreaker, mode);
		if (!solo) {
			_nextBreaker = (_nextBreaker + 1) % pool::Rules::kPlayers;
		}
		_shotLive = false;
		_overTime = 0.0f;
		_manualPocket = _autoPocket = -1;
		if (!_bgTick) {
			_placing = false;
		}
		_log = {};
		_banner = 0;
		Say(mode == pool::Mode::kSolo     ? std::string("racked. Solo Play: all 14, then the 8") :
			mode == pool::Mode::kSoloNine ? std::string("racked. Solo 9-ball: always the lowest ball first; the 9 wins") :
			nine                          ? std::format("racked for 9-ball. {} breaks{}", PlayerName(_rules.Shooter()), _bet > 0 ? std::format(". {} caps on it", _bet) : "") :
			                                std::format("racked. {} breaks{}", PlayerName(_rules.Shooter()), _bet > 0 ? std::format(". {} caps on it", _bet) : ""));
		if (!_quietStart && !_bgTick && WithinEarshot()) {
			Audio::PlayUI(Audio::Bank::kStart, 0.8f);
		}
	}

	void Game::Say(const std::string& a_message)
	{
		_message = a_message;
		_messageTime = std::chrono::steady_clock::now();
		_log[0] = std::move(_log[1]);
		_log[1] = a_message;
		Tell("Pool: " + a_message);  // the HUD's log has it anyway
	}

	void Game::Banner(int a_kind, const std::string& a_title, const std::string& a_sub)
	{
		_banner = a_kind;
		_bannerTitle = a_title;
		_bannerSub = a_sub;
		_bannerTime = std::chrono::steady_clock::now();
	}

	int Game::CalledPocket() const
	{
		if (!_sim || !Settings::Get().callEight || !_rules.OnEight(_rules.Shooter(), *_sim)) {
			return -1;
		}
		return _manualPocket >= 0 ? _manualPocket : _autoPocket;
	}

	void Game::CyclePocket()
	{
		if (!_sim || !_aiming || !_rules.OnEight(_rules.Shooter(), *_sim)) {
			return;
		}
		if (!Settings::Get().callEight) {
			Say("calling is off (bCallEight=0): the 8 can go in any pocket");
			return;
		}
		const int from = _manualPocket >= 0 ? _manualPocket : (_autoPocket >= 0 ? _autoPocket : 5);
		_manualPocket = (from + 1) % pool::Sim::kPockets;
		Say(std::format("calling the 8 in the {} (marked)", PocketName(_manualPocket)));
	}

	bool Game::WithinEarshot() const
	{
		// the turn and rack sounds aren't placed in the world: heard only at the table (or aiming at it)
		if (_aiming) {
			return true;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto  table = _table.get();
		return player && table && (player->data.location - table->data.location).Length() < Settings::Get().soundMaxDistance;
	}

	void Game::Forfeit()
	{
		if (!_sim || _rules.Over() || _npcGame) {
			return;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto  table = _table.get();
		if (!_aiming && (!player || !table || (player->data.location - table->data.location).Length() > 800.0f)) {
			return;  // only at the table you're playing at
		}
		if (_shotLive || !_sim->AtRest()) {
			Say("wait for the balls to stop to forfeit");
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		const auto key = Settings::Key(Settings::Get().keyForfeit);
		if (now - _forfeitAsked > std::chrono::seconds(3)) {
			_forfeitAsked = now;
			Say(std::format("press {} again to forfeit", key));
			return;
		}
		_forfeitAsked = {};
		const bool solo = pool::IsSolo(_rules.GameMode());
		ExitAim();
		_rules.Forfeit(0);
		_manualPocket = _autoPocket = -1;
		// the table clears at once (no result screen): the outcome goes in a notice
		if (solo || !_opponent) {
			log::info("game over: forfeit (Solo Play, no record)");
			Clear();
			Notify("Pool: you gave up");
		} else {
			CountResult(1);  // a loss for you, a win for them
			const auto  r = RecordOf(0x14);
			const std::string them = _opponentName;
			log::info("game over: forfeit to {} (a loss for you, a win for them)", them);
			EndChallenge("");
			Clear();
			Notify(std::format("Pool: you forfeit. {} wins (you're {}-{})", them, r.wins, r.losses));
		}
		_watching = false;
		UpdateHud();  // nothing racked: the HUD goes
	}

	void Game::TickPocketCall()
	{
		if (!_sim || !_aiming || !Settings::Get().callEight || !_rules.OnEight(_rules.Shooter(), *_sim) || _shotLive || _placing) {
			_dwellPocket = -1;
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		if (_lookPocket != _dwellPocket) {
			_dwellPocket = _lookPocket;
			_dwellSince = now;
			return;
		}
		if (_dwellPocket >= 0 && _dwellPocket != _manualPocket && now - _dwellSince >= std::chrono::seconds(2)) {
			_manualPocket = _dwellPocket;
			Say(std::format("calling the 8 in the {} (marked)", PocketName(_manualPocket)));
		}
	}

	void Game::CallLookedAtPocket()
	{
		if (!_sim || !_aiming || !_rules.OnEight(_rules.Shooter(), *_sim) || !Settings::Get().callEight) {
			return;
		}
		if (_lookPocket < 0) {
			Say("aim at a pocket (the 8 going into it), then F to call it");
			return;
		}
		_manualPocket = _lookPocket;
		_dwellPocket = _lookPocket;
		Say(std::format("calling the 8 in the {} (marked)", PocketName(_manualPocket)));
	}

	void Game::JudgeShot()
	{
		_shotLive = false;
		const auto r = _rules.EndShot(*_sim);
		_manualPocket = -1;
		_autoPocket = -1;

		if (r.respotEight) {
			_sim->RespotOnFoot(8);
			_dropTime[8] = 0.0f;
		}
		if (r.respotNine) {
			_sim->RespotOnFoot(9);
			_dropTime[9] = 0.0f;
		}
		const bool nine = pool::IsNineBall(_rules.GameMode());

		const bool        solo = _rules.GameMode() == pool::Mode::kSolo;
		const std::string shooter = PlayerName(r.shooter);
		const std::string other = PlayerName((r.shooter + 1) % 2);
		const std::string rack = Settings::Key(Settings::Get().keyRack);
		std::string       line;
		const char*       why = "";
		switch (r.end) {
		case pool::GameEnd::kWon:
			if (nine) {
				why = r.breakShot ? "THE 9 ON THE BREAK" : "9 BALL DOWN";
				line = solo ? "you sink the 9. You win!" : std::format("{} sinks the 9 and wins!", shooter);
				break;
			}
			why = Settings::Get().callEight ? "8 BALL IN THE CALLED POCKET" : "8 BALL DOWN";
			line = solo ? "you sink the 8. You win!" : std::format("{} sinks the 8 and wins!", shooter);
			break;
		case pool::GameEnd::kEightEarly:
			why = "THE 8 WENT DOWN EARLY";
			line = solo ? "the 8 went down too early. You lose" : std::format("the 8 went down too early. {} wins", other);
			break;
		case pool::GameEnd::kEightOnFoul:
			why = "THE 8 WENT DOWN ON A FOUL";
			line = std::format("the 8 went down on a foul. {} wins", other);
			break;
		case pool::GameEnd::kEightWrongPocket:
			why = "THE 8 WENT IN THE WRONG POCKET";
			line = solo ? "the 8 went in the wrong pocket. You lose" : std::format("the 8 went in the wrong pocket. {} wins", other);
			break;
		case pool::GameEnd::kEightOnBreak:
			why = "THE 8 WENT IN ON THE BREAK";
			line = solo ? "the 8 went in on the break. You lose" : std::format("the 8 went in on the break. {} wins", other);
			break;
		case pool::GameEnd::kScratchOnEight:
			why = "SCRATCHED ON THE 8";
			line = solo ? "scratched on the 8. You lose" : std::format("{} scratched on the 8. {} wins", shooter, other);
			break;
		default:
			break;
		}
		if (r.end != pool::GameEnd::kNone) {
			const bool won = r.winner == 0 && !_npcGame;
			std::string theyWin = (_opponent || _npcGame) && r.winner >= 0 ? PlayerName(r.winner) + " WINS" : std::string("THEY WIN");
			std::ranges::transform(theyWin, theyWin.begin(), [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
			Banner(won || _npcGame ? 2 : 3, won ? "YOU WIN" : (solo ? "YOU LOSE" : theyWin), why);
			if (!solo) {
				CountResult(r.winner);
			}
			Say(std::format("{}. {} racks again", line, rack));
			log::info("game over: {}", line);
			return;
		}

		std::string foul;
		switch (r.foul) {
		case pool::Foul::kScratch:
			foul = "scratch";
			break;
		case pool::Foul::kNoContact:
			foul = "the cue ball hit nothing";
			break;
		case pool::Foul::kWrongBallFirst:
			foul = "wrong ball hit first";
			break;
		case pool::Foul::kNoRail:
			foul = "nothing hit a cushion";
			break;
		default:
			break;
		}

		if (solo) {
			if (r.foul == pool::Foul::kScratch) {
				Banner(1, "SCRATCH", "BALL IN HAND · CARRY ON");
				line = "scratch: ball in hand";
			} else if (!foul.empty()) {
				Banner(1, "FOUL", "BALL IN HAND · CARRY ON");
				line = std::format("foul: {}. Ball in hand", foul);
			} else if (r.ownPotted > 0) {
				line = std::format("{} to go", _rules.Remaining(0, *_sim));
			} else {
				line = "shoot again";
			}
		} else if (!foul.empty()) {
			line = std::format("Foul: {}. {} has ball in hand", foul, PlayerName(_rules.Shooter()));
			static constexpr std::array<const char*, 5> subs{ "", "CUE BALL POTTED · BALL IN HAND", "NOTHING HIT · BALL IN HAND",
				"WRONG BALL FIRST · BALL IN HAND", "NO CUSHION AFTER CONTACT · BALL IN HAND" };
			Banner(1, r.foul == pool::Foul::kScratch ? "SCRATCH / FOUL" : "FOUL", subs[static_cast<int>(r.foul)]);
		} else if (r.groupsSet) {
			line = std::format("{} is {}, {} is {}. {} again", shooter, GroupName(_rules.GroupOf(r.shooter)), other,
				GroupName(_rules.GroupOf((r.shooter + 1) % 2)), shooter);
		} else if (r.turnContinues) {
			line = std::format("{} again", shooter);
		} else {
			line = std::format("{}'s shot", PlayerName(_rules.Shooter()));
		}
		if (r.respotNine) {
			line = "the 9 went in on a foul and is spotted. " + line;
		}
		if (nine && !_rules.Over() && _rules.LowestBall(*_sim) > 0) {
			line += std::format(". The {} first", _rules.LowestBall(*_sim));
		}
		if (r.respotEight) {
			line = "the 8 on the break is spotted. " + line;
		}
		if (_rules.OnEight(_rules.Shooter(), *_sim)) {
			line += Settings::Get().callEight ? std::string(". On the 8: aim at a pocket for 2 s to call it")
			                                  : std::string(". On the 8");
		}
		Say(line);
		if (!r.turnContinues && !_bgTick && WithinEarshot()) {
			Audio::PlayUI(Audio::Bank::kTurn, 0.7f);
		}
		log::info("shot judged: foul {} continues {} groups {} potted {} -> {}", static_cast<int>(r.foul), r.turnContinues, r.groupsSet,
			r.pocketed.size(), line);

		if (OpponentsTurn() && _aiming) {
			ExitAim();  // their shot: the table's theirs until the balls stop
		}
		if (_opponent && !_npcGame && r.shooter == 1 && !_rules.Over() && _rules.Shooter() == 0) {
			_autoAim = true;  // the opponent's tick puts you in the shot camera once they've finished their stroke
		}
		if (r.ballInHand && _aiming) {
			StartPlacement();
		}
	}

	// ---------------------------------------------------------------- ball in hand

	void Game::StartPlacement()
	{
		if (_placing || !_sim || _bgTick) {
			return;
		}
		_overheadBeforePlacing = _overhead;
		_overhead = true;  // place from above, where moving the mouse maps simply onto the felt
		_charging = false;
		{
			std::scoped_lock lock(_camMutex);
			_placeDX = _placeDY = 0.0;
		}
		_placing = true;
		Tell("Pool: ball in hand. Move the mouse to place the cue ball, left click to set it");
	}

	void Game::ConfirmPlacement()
	{
		if (!_placing) {
			return;
		}
		_placing = false;
		_rules.PlacedCueBall();
		if (!_overheadBeforePlacing && _overhead) {
			ToggleOverhead();  // back down to the cue ball, at the usual shot pitch
		}
		Tell("Pool: cue ball set");
	}

	void Game::TickPlacement(const Frame& a_frame)
	{
		double dx;
		double dy;
		float  yaw;
		{
			std::scoped_lock lock(_camMutex);
			dx = _placeDX;
			dy = _placeDY;
			_placeDX = _placeDY = 0.0;
			yaw = _camYaw;
		}
		auto& cue = _sim->Balls()[0];
		if (cue.state != pool::BallState::kOnTable) {
			_sim->RespotCue();
		}
		if (dx == 0.0 && dy == 0.0) {
			return;
		}
		// mouse right = screen right, mouse forward (negative counts) = screen up = the way the camera faces
		const double k = Settings::Get().placeSpeed / std::max(0.01f, a_frame.scale);
		const RE::NiPoint3 right{ std::cos(yaw), -std::sin(yaw), 0.0f };
		const RE::NiPoint3 forward{ std::sin(yaw), std::cos(yaw), 0.0f };
		const RE::NiPoint3 world = right * static_cast<float>(dx * k) - forward * static_cast<float>(dy * k);
		const RE::NiPoint3 table = a_frame.rot * world;
		const pool::Vec2   from = _sim->Balls()[0].pos;
		// slide along the edges and around other balls instead of stopping dead
		for (const pool::Vec2 step : { pool::Vec2{ table.x, table.y }, pool::Vec2{ table.x, 0.0 }, pool::Vec2{ 0.0, table.y } }) {
			if (_sim->CanPlace(0, from + step)) {
				_sim->Place(0, from + step);
				break;
			}
		}
	}

	// ---------------------------------------------------------------- HUD

	void Game::UpdateHud()
	{
		if (_bgTick) {
			return;  // the HUD is the loaded table's
		}
		HudState h;
		h.activateVk = _activateVk.load();
		h.npcGame = _npcGame;
		for (int p = 0; p < 2; ++p) {
			if (_opponent || _npcGame) {
				auto*               actor = SeatActor(p);
				const RE::TESFormID id = actor ? actor->GetFormID() : (p == 0 ? 0x14 : 0);
				if (id) {
					const auto r = RecordOf(id);
					h.stats[p] = actor ? std::format("{}-{} · {} · {}", r.wins, r.losses, SkillName(SkillOf(id)), StyleName(StyleOf(id))) :
					             _bet > 0       ? std::format("{}-{} · {} CAPS ON IT", r.wins, r.losses, _bet) :
					                              std::format("{}-{}", r.wins, r.losses);
				}
			}
			h.names[p] = PlayerName(p);
			std::ranges::transform(h.names[p], h.names[p].begin(), [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
		}
		if (_sim) {
			h.game = true;
			h.solo = pool::IsSolo(_rules.GameMode());
			h.nine = pool::IsNineBall(_rules.GameMode());
			h.lowest = h.nine ? _rules.LowestBall(*_sim) : 0;
			h.opponent = _opponent ? _opponentName : std::string();
			h.shooter = _rules.Shooter();
			for (int p = 0; p < 2; ++p) {
				const auto g = _rules.GroupOf(p);
				h.group[p] = g == pool::Group::kSolids ? 1 : g == pool::Group::kStripes ? 2 : 0;
				h.remaining[p] = _rules.Remaining(p, *_sim);
			}
			for (int i = 0; i < pool::Sim::kBalls; ++i) {
				h.onTable[i] = _sim->Balls()[i].state == pool::BallState::kOnTable;
			}
			h.breakShot = _rules.IsBreak();
			h.onEight = _rules.OnEight(h.shooter, *_sim);
			h.calling = h.onEight && Settings::Get().callEight;
			h.ballInHand = _rules.BallInHand();
			h.placing = _placing;
			h.over = _rules.Over();
			h.winner = _rules.Winner();
			h.message = _message;
			h.log = _log;
			h.calledPocket = CalledPocket();
			h.banner = _banner;
			h.bannerTitle = _bannerTitle;
			h.bannerSub = _bannerSub;
			h.bannerAge = std::chrono::duration<float>(std::chrono::steady_clock::now() - _bannerTime).count();
			h.messageAge = std::chrono::duration<float>(std::chrono::steady_clock::now() - _messageTime).count();
		}
		std::scoped_lock lock(_hudMutex);
		h.prompt = std::move(_hud.prompt);  // UpdateLook's
		h.pick = std::move(_hud.pick);
		h.pickSel = _hud.pickSel;
		h.pickHint = std::move(_hud.pickHint);
		h.pickTitle = std::move(_hud.pickTitle);
		_hud = std::move(h);
	}
}
