// People watching a game react to it now and then, with the game's own idles (Fallout4.esm, the same ones the
// Combat Zone crowd and the settlers use): a clap or a cheer for a pot, a cheer for a win, a laugh at a scratch, a
// boo or a laugh when someone throws the game away, a shake of the head from an opponent who just lost.
//
// Only the settlers hanging round the table (the watchers) react, one at a time (two for a win), not too often
// ([Opponent] fReactionChance, at least 4 s between reactions, 20 s for the same person), and never someone playing.
// Someone sitting down just nods or shakes their head: the cheering and booing idles are for standing up.

#include "Anim.h"
#include "Game.h"
#include "Settings.h"

#include <random>

namespace fp
{
	namespace
	{
		// Fallout4.esm's idles (found in the game's own plugin: IDLE records)
		constexpr RE::TESFormID kClap = 0x00141F3C;         // IdleClapping
		constexpr RE::TESFormID kCheer = 0x0010BA3A;        // IdleCheeringStanding
		constexpr RE::TESFormID kCheerA = 0x0010FF8B;       // ActionCustomCheeringStandingA
		constexpr RE::TESFormID kCheerB = 0x001F04BB;       // ActionCustomCheeringStandingB
		constexpr RE::TESFormID kBoo = 0x0010BA3D;          // IdleBooingStanding
		constexpr RE::TESFormID kBooA = 0x001120DC;         // ActionCustomBooingStandingA
		constexpr RE::TESFormID kLaugh = 0x00118013;        // ActionCustomLaughingStandingA
		constexpr RE::TESFormID kNodYes = 0x00038C7B;       // HeadShakeYes
		constexpr RE::TESFormID kShakeNo = 0x00038C7A;      // HeadShakeNo

		enum class Moment
		{
			kPot,      // one of theirs went down
			kGreat,    // two or more at once
			kWin,      // the game's won
			kScratch,  // the cue ball went in
			kThrown,   // lost on a mistake (the 8 early, in the wrong pocket, on a foul...)
		};

		std::mt19937& Rng()
		{
			static std::mt19937 rng{ std::random_device{}() };
			return rng;
		}
		bool Roll(float a_chance) { return std::uniform_real_distribution<float>(0.0f, 1.0f)(Rng()) < a_chance; }
		template <class T>
		T Pick(std::initializer_list<T> a_list)
		{
			return *(a_list.begin() + std::uniform_int_distribution<std::size_t>(0, a_list.size() - 1)(Rng()));
		}

		struct Choice
		{
			RE::TESFormID id;
			const char*   name;
			bool          loops;  // a crowd idle: stopped after a few seconds, in case it would carry on
		};

		Choice ChooseIdle(Moment a_moment, bool a_sitting)
		{
			if (a_sitting) {
				// in a chair: just the head
				return a_moment == Moment::kScratch || a_moment == Moment::kThrown ? Choice{ kShakeNo, "HeadShakeNo", false } :
				                                                                    Choice{ kNodYes, "HeadShakeYes", false };
			}
			switch (a_moment) {
			case Moment::kPot:
				return Roll(0.6f) ? Choice{ kClap, "IdleClapping", false } : Choice{ kNodYes, "HeadShakeYes", false };
			case Moment::kGreat:
				return Roll(0.5f) ? Choice{ kClap, "IdleClapping", false } : Pick({ Choice{ kCheer, "IdleCheeringStanding", true }, Choice{ kCheerA, "CheeringStandingA", true } });
			case Moment::kWin:
				return Roll(0.35f) ? Choice{ kClap, "IdleClapping", false } :
				                     Pick({ Choice{ kCheer, "IdleCheeringStanding", true }, Choice{ kCheerA, "CheeringStandingA", true }, Choice{ kCheerB, "CheeringStandingB", true } });
			case Moment::kScratch:
				return Roll(0.6f) ? Choice{ kLaugh, "LaughingStandingA", true } : Choice{ kShakeNo, "HeadShakeNo", false };
			case Moment::kThrown:
			default:
				return Pick({ Choice{ kBoo, "IdleBooingStanding", true }, Choice{ kBooA, "BooingStandingA", true }, Choice{ kLaugh, "LaughingStandingA", true },
					Choice{ kShakeNo, "HeadShakeNo", false } });
			}
		}
	}

	void Game::React(const pool::ShotResult& a_result)
	{
		const auto& s = Settings::Get();
		if (!s.reactions || !s.anim || s.reactionChance <= 0.0f) {
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		if (now - _lastReaction < std::chrono::seconds(4)) {
			return;
		}
		auto  table = _table.get();
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!table || !player || (player->data.location - table->data.location).Length() > 2500.0f) {
			return;  // nobody about to see it
		}

		// what happened, and how likely a reaction is
		Moment moment;
		float  chance = s.reactionChance;
		if (a_result.end == pool::GameEnd::kWon) {
			moment = Moment::kWin;
			chance = std::max(chance, 0.85f);
		} else if (a_result.end != pool::GameEnd::kNone) {
			moment = Moment::kThrown;
			chance = std::max(chance, 0.6f);
		} else if (a_result.foul == pool::Foul::kScratch) {
			moment = Moment::kScratch;
			chance = std::min(1.0f, chance * 1.2f);
		} else if (a_result.foul == pool::Foul::kNone && a_result.ownPotted >= 2) {
			moment = Moment::kGreat;
			chance = std::min(1.0f, chance * 2.0f);
		} else if (a_result.foul == pool::Foul::kNone && a_result.ownPotted == 1) {
			moment = Moment::kPot;
		} else {
			return;  // misses and other fouls: nothing
		}

		bool reacted = false;

		// Your opponent, once the game's over: the loser shakes their head, a winner nods or laughs. Not now: they're
		// still in their pool pose (PoolWait or the strike), which would cover it or be stopped right over it when they're
		// let go. So it waits until they've been let go (TickReactions).
		if (a_result.end != pool::GameEnd::kNone && !_npcGame && a_result.winner >= 0 && Roll(0.6f)) {
			if (auto* npc = Opponent(); npc && !npc->IsDead(false)) {
				const Choice idle = a_result.winner == 0 ? Choice{ kShakeNo, "HeadShakeNo", false } :
				                    Roll(0.5f)           ? Choice{ kLaugh, "LaughingStandingA", true } :
				                                           Choice{ kNodYes, "HeadShakeYes", false };
				_reactLater.push_back({ npc->GetFormID(), idle.id, idle.name, idle.loops, now + std::chrono::milliseconds(1200), now + std::chrono::seconds(6) });
				reacted = true;
			}
		}

		if (Roll(chance)) {
			// who's watching this table, near enough, free to react
			std::vector<RE::Actor*> crowd;
			for (const auto& w : _watchers) {
				auto* actor = RE::TESForm::GetFormByID<RE::Actor>(w.id);
				if (!actor || w.table.get().get() != table.get() || actor->IsDead(false) || actor->IsDisabled() || !actor->Get3D() ||
					actor->IsInCombat() || IsPlaying(actor) || (actor->data.location - table->data.location).Length() > 700.0f) {
					continue;
				}
				if (auto it = _reactedAt.find(w.id); it != _reactedAt.end() && now - it->second < std::chrono::seconds(20)) {
					continue;
				}
				crowd.push_back(actor);
			}
			std::ranges::shuffle(crowd, Rng());
			const std::size_t many = moment == Moment::kWin ? 2 : 1;
			for (std::size_t i = 0; i < std::min(many, crowd.size()); ++i) {
				auto*        actor = crowd[i];
				const bool   sitting = actor->interactingState != RE::INTERACTING_STATE::kNotInteracting;
				const Choice idle = ChooseIdle(moment, sitting);
				if (Anim::PlayGame(actor, idle.id, idle.name)) {
					reacted = true;
					_reactedAt[actor->GetFormID()] = now;
					if (idle.loops) {
						_reactStops.emplace_back(actor->GetFormID(), now + std::chrono::milliseconds(3500));
					}
				}
			}
		}
		if (reacted) {
			_lastReaction = now;
		}
	}

	void Game::TickReactions()
	{
		const auto now = std::chrono::steady_clock::now();
		// the opponent's reaction to the end of the game, once they're out of their pool pose
		std::erase_if(_reactLater, [&](const ReactLater& a_r) {
			if (now < a_r.at) {
				return false;
			}
			auto* actor = RE::TESForm::GetFormByID<RE::Actor>(a_r.id);
			if (!actor || actor->IsDead(false) || now > a_r.giveUp) {
				return true;
			}
			const bool stillPosed = actor == Opponent() && (!_npcReleasedForOver || _npcWaitIdle);
			if (stillPosed || actor->interactingState != RE::INTERACTING_STATE::kNotInteracting) {
				return false;  // not yet
			}
			if (Anim::PlayGame(actor, a_r.idle, a_r.name) && a_r.loops) {
				_reactStops.emplace_back(a_r.id, now + std::chrono::milliseconds(3500));
			}
			return true;
		});
		// the crowd idles (cheering, booing, laughing) had their moment: back to hanging round
		std::erase_if(_reactStops, [&](const auto& a_stop) {
			if (now < a_stop.second) {
				return false;
			}
			auto* actor = RE::TESForm::GetFormByID<RE::Actor>(a_stop.first);
			if (actor && !IsPlaying(actor) && actor->interactingState == RE::INTERACTING_STATE::kNotInteracting) {
				Anim::Stop(actor);
			}
			return true;
		});
	}
}
