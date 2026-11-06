#include "sim/Rules.h"

#include <algorithm>

namespace pool
{
	void Rules::NewGame(int a_breaker, Mode a_mode)
	{
		*this = Rules{};
		_mode = a_mode;
		_shooter = IsSolo(a_mode) ? 0 : a_breaker % kPlayers;
	}

	int Rules::LowestBall(const Sim& a_sim) const
	{
		for (int i = 1; i <= 9; ++i) {
			if (a_sim.Balls()[i].state == BallState::kOnTable) {
				return i;
			}
		}
		return 0;
	}

	int Rules::Remaining(int a_player, const Sim& a_sim) const
	{
		if (IsNineBall(_mode)) {  // the 1-9 left, for everyone
			int n = 0;
			for (int i = 1; i <= 9; ++i) {
				n += a_sim.Balls()[i].state == BallState::kOnTable;
			}
			return n;
		}
		if (_mode == Mode::kSolo) {  // every ball but the 8
			int n = 0;
			for (const auto& b : a_sim.Balls()) {
				n += b.state == BallState::kOnTable && GroupOfBall(b.number) != Group::kNone;
			}
			return n;
		}
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
		if (IsNineBall(_mode)) {
			return false;
		}
		if (_mode == Mode::kSolo) {
			return Remaining(a_player, a_sim) == 0;
		}
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
		_lowestAtStart = LowestBall(a_sim);
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
		if (_mode == Mode::kSolo) {
			return EndSoloShot(a_sim, r);
		}
		if (IsNineBall(_mode)) {
			return EndNineShot(a_sim, r);
		}

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

	ShotResult Rules::EndNineShot(const Sim& a_sim, ShotResult r)
	{
		const bool solo = _mode == Mode::kSoloNine;
		const int  opponent = solo ? _shooter : (_shooter + 1) % kPlayers;
		const bool nineIn = std::ranges::find(_potted, 9) != _potted.end();

		if (_cueIn) {
			r.foul = Foul::kScratch;
		} else if (_firstContact < 0) {
			r.foul = Foul::kNoContact;
		} else if (_firstContact != _lowestAtStart) {
			r.foul = Foul::kWrongBallFirst;
		} else if (!_break && _potted.empty() && !_railAfterContact) {
			r.foul = Foul::kNoRail;
		}

		if (nineIn) {
			if (r.foul == Foul::kNone) {
				r.end = GameEnd::kWon;
				_over = true;
				_winner = _shooter;
				r.winner = _winner;
				return r;
			}
			r.respotNine = true;  // on a foul: back on the foot spot
		}
		for (int n : _potted) {
			r.ownPotted += n != 9 || r.foul == Foul::kNone;
		}
		r.turnContinues = solo || (r.foul == Foul::kNone && !_potted.empty());
		if (!r.turnContinues) {
			_shooter = opponent;
		}
		r.ballInHand = r.foul != Foul::kNone;
		_ballInHand = r.ballInHand;
		_break = false;
		(void)a_sim;
		return r;
	}

	ShotResult Rules::EndSoloShot(const Sim& a_sim, ShotResult r)
	{
		const bool eightIn = std::ranges::find(_potted, 8) != _potted.end();
		if (_cueIn) {
			r.foul = Foul::kScratch;
		}
		if (eightIn) {
			if (_break) {
				r.end = GameEnd::kEightOnBreak;
			} else if (!_onEightAtStart) {
				r.end = GameEnd::kEightEarly;
			} else if (_cueIn) {
				r.end = GameEnd::kScratchOnEight;
			} else if (_called >= 0 && _eightPocket != _called) {
				r.end = GameEnd::kEightWrongPocket;
			} else {
				r.end = GameEnd::kWon;
			}
		} else if (_cueIn && _onEightAtStart) {
			r.end = GameEnd::kScratchOnEight;
		}
		if (r.end != GameEnd::kNone) {
			_over = true;
			_winner = r.end == GameEnd::kWon ? 0 : 1;  // 1 = the table won
			r.winner = _winner;
			return r;
		}
		for (int n : _potted) {
			r.ownPotted += n != 8;
		}
		r.turnContinues = true;  // always your shot
		r.ballInHand = _cueIn;    // a scratch: place the cue ball and carry on
		_ballInHand = r.ballInHand;
		_break = false;
		(void)a_sim;
		return r;
	}
}
