// The 8-ball game on top of the physics: judging shots (sim/Rules), ball in hand, calling the 8, and the state
// the HUD shows.

#include "Game.h"

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

	void Game::Say(const std::string& a_message)
	{
		_message = a_message;
		_messageTime = std::chrono::steady_clock::now();
		_log[0] = std::move(_log[1]);
		_log[1] = a_message;
		Notify("Pool: " + a_message);
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

		const std::string shooter = PlayerName(r.shooter);
		const std::string other = PlayerName((r.shooter + 1) % 2);
		std::string       line;
		switch (r.end) {
		case pool::GameEnd::kWon:
			line = std::format("{} sinks the 8 and wins!", shooter);
			break;
		case pool::GameEnd::kEightEarly:
			line = std::format("the 8 went down too early. {} wins", other);
			break;
		case pool::GameEnd::kEightOnFoul:
			line = std::format("the 8 went down on a foul. {} wins", other);
			break;
		case pool::GameEnd::kEightWrongPocket:
			line = std::format("the 8 went in the wrong pocket. {} wins", other);
			break;
		default:
			break;
		}
		if (r.end != pool::GameEnd::kNone) {
			const bool playerOneWon = r.winner == 0;
			const char* why = r.end == pool::GameEnd::kWon ? "8 BALL IN THE CALLED POCKET" : r.end == pool::GameEnd::kEightEarly ? "THE 8 WENT DOWN EARLY" :
			                  r.end == pool::GameEnd::kEightOnFoul ? "THE 8 WENT DOWN ON A FOUL" : "THE 8 WENT IN THE WRONG POCKET";
			if (!Settings::Get().callEight && r.end == pool::GameEnd::kWon) {
				why = "8 BALL DOWN";
			}
			Banner(playerOneWon ? 2 : 3, playerOneWon ? "YOU WIN" : "THEY WIN", why);
			Say(line + ". Numpad 7 racks again");
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

		if (!foul.empty()) {
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
		if (r.respotEight) {
			line = "the 8 on the break is spotted. " + line;
		}
		if (_rules.OnEight(_rules.Shooter(), *_sim)) {
			line += Settings::Get().callEight ? ". On the 8: aim, and Numpad 5 to change the called pocket" : ". On the 8";
		}
		Say(line);
		if (!r.turnContinues) {
			Audio::PlayUI(Audio::Bank::kTurn, 0.7f);
		}
		log::info("shot judged: foul {} continues {} groups {} potted {} -> {}", static_cast<int>(r.foul), r.turnContinues, r.groupsSet,
			r.pocketed.size(), line);

		if (r.ballInHand && _aiming) {
			StartPlacement();
		}
	}

	// ---------------------------------------------------------------- ball in hand

	void Game::StartPlacement()
	{
		if (_placing || !_sim) {
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
		Notify("Pool: ball in hand. Move the mouse to place the cue ball, left click to set it");
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
		Notify("Pool: cue ball set");
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
		HudState h;
		if (_sim) {
			h.game = true;
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
		_hud = std::move(h);
	}
}
