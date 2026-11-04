#include "sim/Rules.h"

#include <algorithm>

namespace pool
{
	void Rules::NewGame(int a_breaker)
	{
		*this = Rules{};
		_shooter = a_breaker % kPlayers;
	}

	int Rules::Remaining(int a_player, const Sim& a_sim) const
	{
		const Group g = _groups[a_player];
		if (g == Group::kNone) {
			return 7;
		}
		int n = 0;
		for (const auto& b : a_sim.Balls()) {
			n += b.state == BallState::kOnTable && GroupOfBall(b.number) == g;
		}
		return n;
	}

	bool Rules::OnEight(int a_player, const Sim& a_sim) const
	{
		return _groups[a_player] != Group::kNone && Remaining(a_player, a_sim) == 0;
	}

	void Rules::BeginShot(const Sim& a_sim, int a_calledPocket)
	{
		_inShot = true;
		_onEightAtStart = OnEight(_shooter, a_sim);
		_called = _onEightAtStart ? a_calledPocket : -1;
		_firstContact = -1;
		_railAfterContact = false;
		_cueIn = false;
		_potted.clear();
		_eightPocket = -1;
	}

	void Rules::OnEvents(const std::vector<Event>& a_events)
	{
		if (!_inShot) {
			return;
		}
		for (const auto& e : a_events) {
			switch (e.type) {
			case Event::Type::kBallBall:
				if (_firstContact < 0 && (e.a == 0 || e.b == 0)) {
					_firstContact = e.a == 0 ? e.b : e.a;
				}
				break;
			case Event::Type::kCushion:
				if (_firstContact >= 0) {
					_railAfterContact = true;
				}
				break;
			case Event::Type::kPocket:
				if (e.a == 0) {
					_cueIn = true;
				} else {
					_potted.push_back(e.a);
					if (e.a == 8) {
						_eightPocket = e.b;
					}
				}
				break;
			}
		}
	}

	ShotResult Rules::EndShot(const Sim& a_sim)
	{
		ShotResult r;
		r.shooter = _shooter;
		r.breakShot = _break;
		r.pocketed = _potted;
		if (!_inShot || _over) {
			return r;
		}
		_inShot = false;

		const int   opponent = (_shooter + 1) % kPlayers;
		const Group mine = _groups[_shooter];
		const bool  eightIn = std::ranges::find(_potted, 8) != _potted.end();

		// fouls
		if (_cueIn) {
			r.foul = Foul::kScratch;
		} else if (_firstContact < 0) {
			r.foul = Foul::kNoContact;
		} else if (!_break) {
			const int first = _firstContact;
			if (mine == Group::kNone) {
				if (first == 8) {
					r.foul = Foul::kWrongBallFirst;  // open table: anything but the 8
				}
			} else if (_onEightAtStart ? first != 8 : GroupOfBall(first) != mine) {
				r.foul = Foul::kWrongBallFirst;
			}
			if (r.foul == Foul::kNone && _potted.empty() && !_railAfterContact) {
				r.foul = Foul::kNoRail;
			}
		}

		// the 8
		if (eightIn) {
			if (_break) {
				r.respotEight = true;
			} else if (!_onEightAtStart) {
				r.end = GameEnd::kEightEarly;
			} else if (r.foul != Foul::kNone) {
				r.end = GameEnd::kEightOnFoul;
			} else if (_called >= 0 && _eightPocket != _called) {
				r.end = GameEnd::kEightWrongPocket;
			} else {
				r.end = GameEnd::kWon;
			}
		}
		if (r.end != GameEnd::kNone) {
			_over = true;
			_winner = r.end == GameEnd::kWon ? _shooter : opponent;
			r.winner = _winner;
			return r;
		}

		// groups: the first ball legally potted after the break decides
		if (!_break && mine == Group::kNone && r.foul == Foul::kNone) {
			for (int n : _potted) {
				const Group g = GroupOfBall(n);
				if (g != Group::kNone) {
					_groups[_shooter] = g;
					_groups[opponent] = g == Group::kSolids ? Group::kStripes : Group::kSolids;
					r.groupsSet = true;
					break;
				}
			}
		}

		// does the shooter carry on?
		const Group now = _groups[_shooter];
		for (int n : _potted) {
			if (n != 8 && (now == Group::kNone || GroupOfBall(n) == now)) {
				++r.ownPotted;  // on the break (table still open) any ball counts
			}
		}
		r.turnContinues = r.foul == Foul::kNone && r.ownPotted > 0;
		if (!r.turnContinues) {
			_shooter = opponent;
		}
		r.ballInHand = r.foul != Foul::kNone;
		_ballInHand = r.ballInHand;
		_break = false;
		(void)a_sim;
		return r;
	}
}
