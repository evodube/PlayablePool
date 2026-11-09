#include "Game.h"
#include "MenuWatch.h"

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
		constexpr float kOverSeconds = 3.0f;  // after a game ends: the result shows this long, then the table clears

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
		PollPad();
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
		if ((window && ::GetForegroundWindow() != window) || _building.load()) {
			_activateWasDown = false;  // (a press that started in build mode isn't ours on the way out)
			_activateHeld = true;
			return;
		}
		const auto&              s = Settings::Get();
		const std::array<int, 10> keys{ s.keyRack, s.keyShoot, s.keyClear, s.keyCalibrate, s.keyCallPocket, s.keyChallenge, s.keyPlay, s.keyForfeit,
			s.keyStopAll, s.keyTables };
		for (std::size_t i = 0; i < keys.size(); ++i) {
			if (keys[i] <= 0) {
				continue;
			}
			const bool down = (::GetAsyncKeyState(keys[i]) & 0x8000) != 0;
			if (down && !_keyWasDown[i]) {
				_padActive = false;  // the keyboard: prompts go back to keys
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
						case 7:
							if (_kitPlacing) {
								CancelKit(true);  // setting a kit table up: X puts it away again
							} else if (!_picking) {
								Forfeit();
							}
							break;
						case 8: StopAllGames(); break;
						case 9:
							if (!_aiming && !_picking && !_kitPlacing) {
								ToggleTables();
							}
							break;
						case 6:
							// the NPC by a table under the crosshair (the prompt's), or nothing
							if (auto actor = _lookActor.get()) {
								Challenge(actor->As<RE::Actor>(), _lookActorTable.get().get());
							}
							break;
						default: Challenge(); break;
						}
					});
				}
			}
			_keyWasDown[i] = down;
		}
		// up/down through the who-to-play list while it's up; left/right: 8-ball or 9-ball (at a free table, or the list)
		const std::array<int, 4> arrows{ VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT };
		for (std::size_t i = 0; i < arrows.size(); ++i) {
			const bool down = (::GetAsyncKeyState(arrows[i]) & 0x8000) != 0;
			const bool live = i < 2 ? _picking.load() : (_picking.load() || _canSwitchType.load() || _kitPlacing.load());
			if (down && !_arrowWasDown[i] && live) {
				if (auto* tasks = F4SE::GetTaskInterface()) {
					tasks->AddTask([this, i]() {
						if (i >= 2 && _kitPlacing) {
							TurnKit(i == 2 ? -1 : 1);  // turning the kit table about
						} else if (i < 2) {
							MovePick(i == 0 ? -1 : 1);
						} else {
							SwitchType();
						}
					});
				}
			}
			_arrowWasDown[i] = down;
		}
		// the game's activate key, on a pool table (OnActivate checks it's one): a tap (on letting go) or a hold
		if (const int vk = _activateVk.load(); vk > 0) {
			const bool down = (::GetAsyncKeyState(vk) & 0x8000) != 0;
			const auto now = std::chrono::steady_clock::now();
			if (down && !_activateWasDown) {
				_activateDownAt = now;
				_activateHeld = false;
			}
			auto send = [this](bool a_hold) {
				if (auto* tasks = F4SE::GetTaskInterface()) {
					tasks->AddTask([this, a_hold]() { OnActivate(a_hold); });
				}
			};
			if (down && !_activateHeld && now - _activateDownAt >= std::chrono::milliseconds(kHoldMs)) {
				_activateHeld = true;
				send(true);
			} else if (!down && _activateWasDown && !_activateHeld) {
				send(false);
			}
			_activateWasDown = down;
		}
	}

	namespace
	{
		float Distance(const RE::NiPoint3& a, const RE::NiPoint3& b) { return (a - b).Length(); }
	}

	bool Game::ActorInView(const RE::NiPoint3& a_origin, const RE::NiPoint3& a_dir, float a_upTo) const
	{
		// someone under the crosshair before a_upTo: the activate key is theirs (talk), not the table's
		bool found = false;
		ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(a_origin, a_upTo, [&](RE::TESObjectREFR* a_ref) {
				if (!a_ref || !a_ref->IsActor() || a_ref == RE::PlayerCharacter::GetSingleton() || a_ref->IsDisabled()) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const RE::NiPoint3 at = a_ref->data.location + RE::NiPoint3{ 0.0f, 0.0f, 80.0f };
				const auto         oc = at - a_origin;
				const float        t = oc.Dot(a_dir);
				if (t > 0.0f && t < a_upTo && oc.SqrLength() - t * t < 40.0f * 40.0f) {
					found = true;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		return found;
	}

	void Game::UpdateLook()
	{
		// a few times a second: the activate key's binding, the pool table under the crosshair, the prompt
		const auto now = std::chrono::steady_clock::now();
		if (now - _lookTime < std::chrono::milliseconds(150)) {
			return;
		}
		_lookTime = now;
		if (auto* map = RE::ControlMap::GetSingleton()) {
			// Fallout 4 keeps keyboard bindings as Windows virtual-key codes (F = 0x46), not scan codes: reading 0x46
			// as a scan code gave Scroll Lock
			const auto vk = map->GetMappedKey("Activate", RE::INPUT_DEVICE::kKeyboard);
			if (vk != RE::ControlMap::kInvalid && vk > 0 && vk < 0xFF) {
				_activateVk = static_cast<int>(vk);
			}
		}
		std::string prompt;
		_lookTable = {};
		bool        canSwitch = false;
		const auto& s = Settings::Get();
		auto*       player = RE::PlayerCharacter::GetSingleton();
		auto*       camera = RE::PlayerCamera::GetSingleton();
		if (s.activateTable && !_aiming && !IsGamePaused() && player && camera && camera->cameraRoot) {
			auto* table = PickTable();
			if (table && IsPoolTable(table) && Distance(table->data.location, player->data.location) < s.activateDistance) {
				const auto         origin = camera->cameraRoot->GetWorldTranslate();
				const float        pitch = player->data.angle.x;
				const float        yaw = player->data.angle.z;
				const RE::NiPoint3 dir{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) };
				if (!ActorInView(origin, dir, Distance(table->data.location, origin))) {
					_lookTable = table->GetHandle();
					const bool        pad = _padActive.load();
					const std::string key = pad ? std::string("A") : Settings::Key(_activateVk.load());
					const bool        here = _sim && _table.get().get() == table;
					const int         slot = SlotOf(table);
					const TableSlot*  away = slot > 0 ? &_others[static_cast<std::size_t>(slot - 1)] : nullptr;
					if (away && away->_npcGame && !away->_rules.Over()) {
						prompt = std::format("{}) WATCH {}", key, SlotNames(*away));
					} else if (away && away->_sim && !away->_rules.Over()) {
						prompt = key + ") BACK TO THIS GAME";
					} else if (here && _npcGame && !_rules.Over()) {
						std::string a = PlayerName(0), b = PlayerName(1);
						for (auto* n : { &a, &b }) {
							std::ranges::transform(*n, n->begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
						}
						prompt = _spectating ? key + ") STOP WATCHING" : std::format("{}) WATCH {} VS {}", key, a, b);
					} else if (!here || _rules.Over()) {
						if (auto* asking = Offerer(table)) {
							std::string name = asking->GetDisplayFullName();
							std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
							prompt = std::format("{}) PLAY {} · {}", key, name, WantType() == 1 ? "9-BALL" : "8-BALL");
						} else {
							// (the controller's D-pad is the game's favourites here: 8- or 9-ball is picked in the held list)
							const char* game = WantType() == 1 ? "9-BALL" : "8-BALL";
							prompt = std::format("{}  {}) SOLO  ·  HOLD {}) {}", pad ? std::string(game) : std::format("< {} >", game), key, key,
								IsKitTable(table) ? "2 PLAYER / PACK UP" : "2 PLAYER");
						}
						canSwitch = true;  // left/right: the other game
					} else if (_opponent && OpponentsTurn()) {
						prompt.clear();  // their shot: nothing to do
					} else {
						prompt = key + ") TAKE YOUR SHOT";
					}
				}
			}
		}
		// not on a table: an NPC by a pool table, close enough to talk to; the play key ([Keys] iPlay, off by default)
		// challenges them. No prompt
		_lookActor = {};
		_lookActorTable = {};
		if (prompt.empty() && s.keyPlay > 0 && !_aiming && !IsGamePaused() && player) {
			auto* actor = PickActor(true);
			if (actor && actor != Opponent() && (actor->data.location - player->data.location).Length() < s.playDistance) {
				static auto* npcType = RE::TESForm::GetFormByID<RE::BGSKeyword>(0x00013794);  // ActorTypeNPC
				auto*        table = PoolTableNear(actor->data.location, s.playNearTable);
				if (table && !actor->IsChild() && !actor->IsInCombat() && !actor->GetHostileToActor(player) &&
					(!npcType || actor->HasKeywordHelper(npcType, nullptr))) {
					_lookActor = actor->GetHandle();
					_lookActorTable = table->GetHandle();
				}
			}
		}
		// the who-to-play list: gone if you walk off, or leave it be a while
		std::vector<std::string> pick;
		int                      pickSel = 0;
		if (_pick) {
			auto table = _pick->table.get();
			const bool away = !table || !player || IsGamePaused() || _aiming ||
			                  Distance(table->data.location, player->data.location) > s.activateDistance + 150.0f ||
			                  now - _pick->touched > std::chrono::seconds(20);
			if (away) {
				ClosePick();
			} else {
				pick = _pick->names;
				pickSel = _pick->sel;
				prompt.clear();
			}
		}
		if (_kitPlacing) {
			prompt = KitPrompt();  // setting a kit table up: what F / the arrows / X do
			canSwitch = false;
			pick.clear();
		}
		_canSwitchType = canSwitch;
		std::scoped_lock lock(_hudMutex);
		_hud.prompt = std::move(prompt);
		_hud.pick = std::move(pick);
		_hud.pickSel = pickSel;
		if (_hud.pick.empty()) {
			_hud.pickHint.clear();
		} else {
			const auto mine = RecordOf(0x14);
			_hud.pickHint = _padActive ? std::format("D-PAD UP/DOWN PICK · LEFT/RIGHT GAME · A PLAY · B CLOSE · YOU {}-{}", mine.wins, mine.losses) :
			                             std::format("UP/DOWN PICK · LEFT/RIGHT GAME · {} PLAY · YOU {}-{}", Settings::Key(_activateVk.load()), mine.wins,
				mine.losses);
			_hud.pickTitle = WantType() == 1 ? "WHO PLAYS? · 9-BALL" : "WHO PLAYS? · 8-BALL";
		}
	}

	void Game::QuietNpcs(bool a_quiet)
	{
		if (a_quiet == _npcsQuiet) {
			return;
		}
		_npcsQuiet = a_quiet;
		// the game's own settings for greetings and comments: turned off while you play, back as they were after
		static std::unordered_map<std::string, float> was;
		auto* settings = RE::GameSettingCollection::GetSingleton();
		if (!settings) {
			return;
		}
		static constexpr std::pair<const char*, float> kQuiet[] = {
			{ "fAIMinGreetingDistance", 0.0f },         // how close they have to be to say hello: never
			{ "fAIGreetingTimer", 100000.0f },          // ...and how long between hellos
			{ "fAIHelloTimer", 100000.0f },
			{ "fCommentOnPlayerActionsTimer", 100000.0f },  // remarks on what you're doing
		};
		for (const auto& [name, quiet] : kQuiet) {
			auto* setting = settings->GetSetting(name);
			if (!setting) {
				continue;
			}
			if (a_quiet) {
				was[name] = setting->GetFloat();
				setting->SetFloat(quiet);
			} else if (auto it = was.find(name); it != was.end()) {
				setting->SetFloat(it->second);
			}
		}
		log::info("NPC greetings {}", a_quiet ? "off while you play" : "back on");
	}

	void Game::OnActivate(bool a_hold)
	{
		if (IsGamePaused()) {
			return;
		}
		if (_kitPlacing) {
			if (!a_hold) {
				ConfirmKit();  // setting a kit table up: F puts it there
			}
			return;
		}
		if (_aiming) {
			if (!a_hold) {
				ExitAim();  // F at the shot camera: get up from the table (on the 8, aiming at a pocket 2 s calls it)
				Tell("Pool: stopped aiming");
			}
			return;
		}
		auto table = _lookTable.get();
		if (_pick && a_hold) {
			return;
		}
		if (_pick) {
			// the list is up: F on its table plays whoever's picked; anywhere else just closes it
			const Pick pick = *_pick;
			ClosePick();
			if (table && table == pick.table.get()) {
				auto who = pick.who[pick.sel].get();
				auto* actor = who ? who->As<RE::Actor>() : nullptr;
				if (who && who.get() == table.get()) {
					PackUpTable(table.get());  // the list's last entry at a kit table
				} else if (actor && !actor->IsDead(false) && !actor->IsDisabled()) {
					log::info("picked {} ({:08X}) to play", actor->GetDisplayFullName(), actor->GetFormID());
					Challenge(actor, table.get());
				} else {
					if (_opponent && SlotOf(table.get()) == 0) {
						EndChallenge("");
					}
					Rack(table.get());  // Solo Play
				}
			}
			_lookTime = {};
			return;
		}
		if (!table) {
			return;  // not looking at a pool table: the key does what the game does with it
		}
		if (SlotOf(table.get()) > 0 && !FocusTable(table.get())) {
			return;  // a game going on at another table: that table's the loaded one now (yours, or theirs to watch)
		}
		const bool here = _sim && _table.get().get() == table.get();
		if (here && _npcGame && !_rules.Over()) {
			// two NPCs at it: F watches their game (the HUD comes up; walk about as you like), F again stops
			if (!a_hold) {
				_spectating = !_spectating;
				Tell(_spectating ? std::format("Pool: watching {} and {}", PlayerName(0), PlayerName(1)) : std::string("Pool: stopped watching"));
			}
			_lookTime = {};
			return;
		}
		if (a_hold) {
			// held: two players. A list of who's by the table to pick from
			auto people = PlayersNear(table.get());
			const bool kit = IsKitTable(table.get());
			if (people.empty() && !kit) {
				Notify(std::format("Pool: nobody's by the table to play. {} plays Solo", Settings::Key(_activateVk.load())));
			} else {
				Pick pick;
				pick.table = table->GetHandle();
				for (auto* a : people) {
					std::string line = PickLine(a);
					if (line.empty()) {
						continue;
					}
					pick.who.push_back(a->GetHandle());
					pick.names.push_back(std::move(line));
				}
				if (kit) {
					pick.who.push_back(table->GetHandle());  // a kit table: last, pack it back into its kit
					pick.names.push_back("PACK UP THE TABLE");
				}
				if (!pick.names.empty()) {
					pick.touched = std::chrono::steady_clock::now();
					_pick = std::move(pick);
					_picking = true;
				}
			}
		} else if (!here || _rules.Over()) {
			if (auto* asking = Offerer(table.get())) {
				log::info("{} ({:08X}) asked for a game: playing them", asking->GetDisplayFullName(), asking->GetFormID());
				_offerID = 0;
				Challenge(asking, table.get());
			} else {
				if (_opponent && SlotOf(table.get()) == 0) {
					EndChallenge("");
				}
				Rack(table.get());  // tapped: Solo Play
			}
		} else if (!(_opponent && OpponentsTurn())) {
			ToggleAim();
		}
		_lookTime = {};  // the prompt follows straight away
	}

	int Game::WantType() const
	{
		const int t = _wantType.load();
		return t < 0 ? Settings::Get().gameType : t;
	}

	void Game::SwitchType()
	{
		_wantType = WantType() == 1 ? 0 : 1;
		log::info("game switched to {}", _wantType.load() == 1 ? "9-ball" : "8-ball");
		_lookTime = {};  // the prompt shows it straight away
	}

	void Game::MovePick(int a_by)
	{
		if (!_pick || _pick->names.empty()) {
			return;
		}
		const int n = static_cast<int>(_pick->names.size());
		_pick->sel = ((_pick->sel + a_by) % n + n) % n;
		_pick->touched = std::chrono::steady_clock::now();
		_lookTime = {};
	}

	void Game::ClosePick()
	{
		_pick.reset();
		_picking = false;
		_lookTime = {};
	}

	RE::Actor* Game::Offerer(RE::TESObjectREFR* a_table) const
	{
		if (!_offerID || !a_table || _offerTable.get().get() != a_table || std::chrono::steady_clock::now() > _offerUntil) {
			return nullptr;
		}
		auto* actor = RE::TESForm::GetFormByID<RE::Actor>(_offerID);
		if (!actor || actor->IsDead(false) || actor->IsDisabled() || actor->IsInCombat() ||
			(actor->data.location - a_table->data.location).Length() > Settings::Get().playNearTable) {
			return nullptr;
		}
		return actor;
	}

	std::vector<RE::Actor*> Game::PlayersNear(RE::TESObjectREFR* a_table) const
	{
		// grown-up NPCs (settlers, companions) by the table who aren't fighting or hostile; nearest first
		std::vector<std::pair<float, RE::Actor*>> found;
		auto*        player = RE::PlayerCharacter::GetSingleton();
		static auto* npcType = RE::TESForm::GetFormByID<RE::BGSKeyword>(0x00013794);  // ActorTypeNPC
		const float  within = Settings::Get().playNearTable;
		util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(a_table->data.location, within, [&](RE::TESObjectREFR* a_ref) {
				auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
				if (!actor || actor == player || actor->IsDisabled() || actor->IsDead(false) || !actor->Get3D() || actor->IsChild() ||
					actor->IsInCombat() || (player && actor->GetHostileToActor(player)) ||
					(npcType && !actor->HasKeywordHelper(npcType, nullptr)) ||
					std::ranges::any_of(found, [&](const auto& f) { return f.second == actor; })) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				found.emplace_back((actor->data.location - a_table->data.location).Length(), actor);
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		std::ranges::sort(found, {}, &std::pair<float, RE::Actor*>::first);
		std::vector<RE::Actor*> out;
		for (std::size_t i = 0; i < found.size() && i < 7; ++i) {
			out.push_back(found[i].second);
		}
		return out;
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
		return menus::AnyBlocking();  // (from the menu events: asking the UI holds the menus, see MenuWatch.h)
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

	// ---------------------------------------------------------------- win/loss records (in the co-save)

	namespace
	{
		std::unordered_map<RE::TESFormID, Game::Record> g_records;
		std::mutex                                     g_recordsMutex;
		constexpr std::uint32_t                        kRecordsType = 0x52434453;  // 'RCDS'
		constexpr std::uint32_t                        kRecordsVersion = 2;  // 2: and each NPC's skill
		constexpr RE::TESFormID                        kPlayerID = 0x14;
	}

	Game::Record Game::RecordOf(RE::TESFormID a_id) const
	{
		std::scoped_lock lock(g_recordsMutex);
		const auto       it = g_records.find(a_id);
		return it == g_records.end() ? Record{} : it->second;
	}

	float Game::StartingSkill(RE::TESFormID a_id)
	{
		// everyone starts somewhere different: a beginner or an amateur, the same for the same NPC
		const std::uint32_t h = (a_id * 2654435761u) >> 16;
		return 10.0f + static_cast<float>(h % 31);
	}

	float Game::SkillOf(RE::TESFormID a_id) const
	{
		const auto r = RecordOf(a_id);
		return r.skill >= 0.0f ? r.skill : StartingSkill(a_id);
	}

	const char* Game::SkillName(float a_skill)
	{
		return a_skill <= 25.5f ? "BEGINNER" : a_skill <= 50.5f ? "AMATEUR" : a_skill <= 74.5f ? "SEMI-PRO" : "PRO";
	}

	int Game::StakeFor(float a_skill)
	{
		return a_skill <= 25.5f ? 25 : a_skill <= 50.5f ? 50 : a_skill <= 74.5f ? 150 : 300;
	}

	int Game::StyleOf(RE::TESFormID a_id)
	{
		// fixed per NPC (the same every game, every save): from their form ID
		std::uint32_t h = a_id * 2654435761u;
		h ^= h >> 15;
		return static_cast<int>(h % 3);
	}

	const char* Game::StyleName(int a_style)
	{
		return a_style == 0 ? "SAFE" : a_style == 2 ? "AGGRESSIVE" : "BALANCED";
	}

	bool Game::BetsWithYou(RE::TESFormID a_id)
	{
		// some only ever play for fun: about a third of them, fixed per NPC
		std::uint32_t h = a_id * 0x9E3779B1u + 0x7F4A7C15u;
		h ^= h >> 13;
		h *= 0x85EBCA6Bu;
		h ^= h >> 16;
		return h % 100 >= 35;
	}

	int Game::StakeOf(RE::Actor* a_actor) const
	{
		// caps on a game against them: none if betting's off or they only play for fun
		if (!a_actor || !Settings::Get().bets || !BetsWithYou(a_actor->GetFormID())) {
			return 0;
		}
		return StakeFor(SkillOf(a_actor->GetFormID()));
	}

	std::string Game::PickLine(RE::Actor* a_actor) const
	{
		std::string name = a_actor ? a_actor->GetDisplayFullName() : "";
		if (name.empty()) {
			return {};
		}
		std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
		const auto  r = RecordOf(a_actor->GetFormID());
		const float skill = SkillOf(a_actor->GetFormID());
		const int   stake = StakeOf(a_actor);
		return std::format("{}   {}-{} · {} · {}", name, r.wins, r.losses, SkillName(skill),
			stake > 0 ? std::format("{} CAP BET", stake) : std::string("FOR FUN"));
	}

	int Game::PlayerCaps()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* caps = RE::TESForm::GetFormByID(0x0000000F);  // Caps001
		return player && caps && caps->IsBoundObject() ? static_cast<int>(player->GetInventoryObjectCount(static_cast<RE::TESBoundObject*>(caps))) : 0;
	}

	void Game::SettleBet(int a_winner)
	{
		const int bet = _bet;
		_bet = 0;
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* caps = RE::TESForm::GetFormByID(0x0000000F);
		if (bet <= 0 || _npcGame || !_opponent || !player || !caps || !caps->IsBoundObject() || a_winner < 0) {
			return;
		}
		auto* capsObject = static_cast<RE::TESBoundObject*>(caps);
		if (a_winner == 0) {
			player->AddObjectToContainer(capsObject, nullptr, bet, nullptr, RE::ITEM_REMOVE_REASON::kNone);
			RE::UIUtils::PlayMenuSound("ITMBottlecapsBarterReceive");
			Notify(std::format("Pool: you win {} caps from {}", bet, _opponentName));
		} else {
			const int pay = std::min(bet, PlayerCaps());
			if (pay > 0) {
				RE::TESObjectREFR::RemoveItemData data(capsObject, pay);
				player->RemoveItem(data);
			}
			RE::UIUtils::PlayMenuSound("ITMBottlecapsBarterGive");
			Notify(std::format("Pool: you lose {} caps to {}", pay, _opponentName));
		}
		log::info("bet: {} caps {} {}", bet, a_winner == 0 ? "won from" : "lost to", _opponentName);
	}

	void Game::CountResult(int a_winner)
	{
		SettleBet(a_winner);  // (you against an NPC, for caps)
		if (a_winner < 0 || a_winner > 1 || (!_opponent && !_npcGame)) {
			return;  // two at one keyboard, or Solo Play: no record
		}
		std::array<RE::TESFormID, 2> ids{};
		for (int p = 0; p < 2; ++p) {
			auto* actor = SeatActor(p);
			ids[p] = actor ? actor->GetFormID() : (!_npcGame && p == 0 ? kPlayerID : 0);
		}
		std::scoped_lock lock(g_recordsMutex);
		for (int p = 0; p < 2; ++p) {
			if (!ids[p]) {
				continue;
			}
			auto& r = g_records[ids[p]];
			(p == a_winner ? r.wins : r.losses) += 1;
			if (ids[p] != kPlayerID) {
				// every game makes them better, a win more so; slower the better they are
				if (r.skill < 0.0f) {
					r.skill = StartingSkill(ids[p]);
				}
				const float gain = (p == a_winner ? 4.0f : 2.0f) * (1.0f - r.skill / 125.0f);
				r.skill = std::min(100.0f, r.skill + gain);
			}
			log::info("record: {} ({:08X}) {}-{}, skill {:.0f}", PlayerName(p), ids[p], r.wins, r.losses, r.skill);
		}
	}

	void Game::SaveRecords(const F4SE::SerializationInterface* a_intfc)
	{
		std::scoped_lock lock(g_recordsMutex);
		if (!a_intfc->OpenRecord(kRecordsType, kRecordsVersion)) {
			return;
		}
		const auto count = static_cast<std::uint32_t>(g_records.size());
		a_intfc->WriteRecordData(count);
		for (const auto& [id, r] : g_records) {
			a_intfc->WriteRecordData(id);
			a_intfc->WriteRecordData(r.wins);
			a_intfc->WriteRecordData(r.losses);
			a_intfc->WriteRecordData(r.skill);
		}
		SaveKit(a_intfc);  // and the tables set up from kits
	}

	void Game::LoadRecords(const F4SE::SerializationInterface* a_intfc)
	{
		std::scoped_lock lock(g_recordsMutex);
		g_records.clear();
		RevertKit();
		std::uint32_t type = 0, version = 0, length = 0;
		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			if (IsKitRecord(type)) {
				LoadKit(a_intfc, version);
				continue;
			}
			if (type != kRecordsType || (version != 1 && version != kRecordsVersion)) {
				continue;
			}
			std::uint32_t count = 0;
			a_intfc->ReadRecordData(count);
			for (std::uint32_t i = 0; i < count; ++i) {
				RE::TESFormID id = 0;
				Record        r;
				a_intfc->ReadRecordData(id);
				a_intfc->ReadRecordData(r.wins);
				a_intfc->ReadRecordData(r.losses);
				if (version >= 2) {
					a_intfc->ReadRecordData(r.skill);
				}
				if (const auto now = a_intfc->ResolveFormID(id)) {
					g_records[*now] = r;  // (an NPC from a mod that's gone: dropped)
				}
			}
		}
		log::info("records: {} loaded from the save", g_records.size());
	}

	void Game::RevertRecords(const F4SE::SerializationInterface*)
	{
		std::scoped_lock lock(g_recordsMutex);
		g_records.clear();
		RevertKit();
	}

	// ---------------------------------------------------------------- loading screens

	namespace
	{
		// (CommonLibF4 only declares it) the game's own layout: the menu's name, and whether it's opening
		struct MenuOpenCloseEventData
		{
			RE::BSFixedString menuName;
			bool              opening;
		};

		struct LoadingSink final : RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				const auto& e = reinterpret_cast<const MenuOpenCloseEventData&>(a_event);
				if (e.opening && e.menuName.c_str() && std::string_view(e.menuName.c_str()) == "LoadingMenu") {
					Game::Get().OnLoadingStarts();
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Game::ListenForLoading()
	{
		static LoadingSink sink;
		static bool        registered = false;
		if (auto* ui = RE::UI::GetSingleton(); ui && !registered) {
			ui->RegisterSink<RE::MenuOpenCloseEvent>(&sink);
			registered = true;
			log::info("listening for loading screens (games end on fast travel)");
		}
	}

	void Game::OnLoadingStarts()
	{
		if (_kitPlacing) {
			CancelKit(true);  // travelling while setting a kit up: the kit back
		}
		if (!_sim && !_opponent && _others.empty()) {
			return;
		}
		log::info("loading screen: every game ends, its table cleared");
		EndGamesAway(true);
	}

	void Game::EndThisGameAway(const char* a_why)
	{
		// the loaded table's game, because you're not there for it. Two NPCs: one of them wins (it counts)
		if (_npcGame && _sim && !_rules.Over()) {
			const int winner = std::uniform_int_distribution<int>(0, 1)(_npcRng);
			log::info("{}: {} and {}'s game ends; {} wins", a_why, PlayerName(0), PlayerName(1), PlayerName(winner));
			CountResult(winner);
		} else if (_sim || _opponent) {
			log::info("{}: the game at {:08X} ends", a_why, _table ? _table.get()->GetFormID() : 0u);
		}
		if (_opponent || _npcGame) {
			EndChallenge("");
		}
		Clear();
	}

	void Game::EndGamesAway(bool a_travelled)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		constexpr float kAwayDistance = 8750.0f;  // 125 m
		auto away = [&]() {
			auto table = _table.get();
			if (!table) {
				return false;
			}
			auto* tableCell = table->GetParentCell();
			auto* playerCell = player->GetParentCell();
			const bool otherWorld = tableCell && playerCell && !tableCell->IsInterior() && !playerCell->IsInterior() &&
			                        tableCell->worldSpace != playerCell->worldSpace;
			if (a_travelled || otherWorld || (tableCell && playerCell && tableCell->IsInterior() != playerCell->IsInterior()) ||
				(table->GetParentCell() && table->GetParentCell()->IsInterior() && table->GetParentCell() != player->GetParentCell())) {
				return true;
			}
			return Distance(table->data.location, player->data.location) > kAwayDistance;
		};
		const char* why = a_travelled ? "you travelled" : "you're 125 m away";
		if ((_sim || _opponent) && away()) {
			ExitAim();
			EndThisGameAway(why);
			_watching = false;
			UpdateHud();
		}
		std::scoped_lock lock(_tableMutex);
		_bgTick = true;
		for (std::size_t i = 0; i < _others.size(); ++i) {
			SwapWith(i);
			if ((_sim || _opponent) && away()) {
				EndThisGameAway(why);
			}
			SwapWith(i);
		}
		_bgTick = false;
		std::erase_if(_others, [&](const TableSlot& a_slot) { return !Busy(a_slot); });
	}

	void Game::StopAllGames()
	{
		ExitAim();
		ClosePick();
		std::size_t n = 0;
		auto stop = [&]() {
			if (_sim || _opponent || _npcGame) {
				++n;
				if (_opponent || _npcGame) {
					EndChallenge("");
				}
				Clear();
			}
		};
		stop();
		{
			std::scoped_lock lock(_tableMutex);
			_bgTick = true;
			for (std::size_t i = 0; i < _others.size(); ++i) {
				SwapWith(i);
				stop();
				SwapWith(i);
			}
			_bgTick = false;
			_others.clear();
		}
		while (!_watchers.empty()) {
			DropWatcher(_watchers.back().id);
		}
		_offerID = 0;
		_pairSince.clear();
		_aloneSince.clear();
		_watching = false;
		UpdateHud();
		log::info("stop all: {} game(s) stopped, watchers sent home", n);
		Notify(std::format("Pool: {} stopped", n == 1 ? std::string("1 game") : std::format("{} games", n)));
	}

	void Game::Forget()
	{
		_kitPlacing = false;  // loading a save: the preview went with the old game
		_kitPreview = {};
		{
			std::scoped_lock lock(_tableMutex);
			_others.clear();  // the other tables' games went with the old game too
		}
		_pairSince.clear();
		_aloneSince.clear();
		_navCache.clear();
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
		if (_seatIdx != 1) {
			static_cast<NpcSlot&>(*this) = _seats[1];
			_seatIdx = 1;
		}
		_seats = {};
		_npcGame = false;
		_spectating = false;
		_clearAfterTick = false;
		_opponent = {};
		_npcMarker = {};
		_npcApproach = {};
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
		_watchers.clear();
		_watchersStale = true;
		_watchTables.clear();
		_offerID = 0;
		ClosePick();
	}

	void Game::ClearForSave()
	{
		if (_kitPlacing) {
			CancelKit(true);  // a save mid-set-up: the kit back in your inventory, no half-made table saved
		}
		{
			// the other tables first: every NPC let go, every ball taken off
			std::scoped_lock lock(_tableMutex);
			_bgTick = true;
			for (std::size_t i = 0; i < _others.size(); ++i) {
				SwapWith(i);
				if (_npcGame) {
					EndNpcGame("");
				}
				ReleaseOpponent();
				if (_sim || !_markers.empty()) {
					Clear();
				}
				SwapWith(i);
			}
			_bgTick = false;
			_others.clear();
		}
		if (_npcGame) {
			EndNpcGame("");  // two NPCs' game doesn't survive a save: let them both go
		}
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
		if (IsKitPreview(a_ref)) {
			return false;  // the one following your crosshair while you set a kit up: not a table yet
		}
		auto path = ModelPath(a_ref);
		std::ranges::transform(path, path.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
		return path.find("pooltable") != std::string::npos && path.find("rack") == std::string::npos && !TakenOver(a_ref);
	}

	bool Game::TakenOver(const RE::TESObjectREFR* a_table)
	{
		// Another mod's game using a pool table as its prop (Barstool Games' blackjack: its card table is a scripted
		// activator laid on the felt): that table is theirs, so no prompt, no rack, no NPCs walking up to play on it.
		// Found by an activator standing on the table's top, or a table from Barstool Games itself. Worked out once a
		// table (per where it stands: a table moved in the workshop is looked at again).
		static std::mutex                              lock;
		static std::unordered_map<std::uint64_t, bool> known;
		const auto&                                    at = a_table->data.location;
		const std::uint64_t key = (static_cast<std::uint64_t>(a_table->GetFormID()) << 32) ^
		                          (static_cast<std::uint64_t>(static_cast<std::int64_t>(at.x / 16.0f)) * 73856093u) ^
		                          (static_cast<std::uint64_t>(static_cast<std::int64_t>(at.y / 16.0f)) * 19349663u);
		{
			std::scoped_lock guard(lock);
			if (auto it = known.find(key); it != known.end()) {
				return it->second;
			}
		}
		bool        taken = false;
		std::string why;
		auto*       base = a_table->GetBaseObject();
		if (auto* file = base ? base->GetFile(0) : nullptr) {
			const std::string name(file->GetFilename());
			if (_stricmp(name.c_str(), "Games.esm") == 0 || _stricmp(name.c_str(), "BarstoolGames.esm") == 0) {
				taken = true;
				why = std::format("a Barstool Games table ({})", name);
			}
		}
		if (auto* cell = !taken ? a_table->GetParentCell() : nullptr) {
			cell->ForEachReferenceInRange(at, 140.0f, [&](RE::TESObjectREFR* a_ref) {
				auto* refBase = a_ref && a_ref != a_table && !a_ref->IsDisabled() ? a_ref->GetBaseObject() : nullptr;
				if (!refBase || !refBase->As<RE::TESObjectACTI>() || Get().IsOurs(a_ref)) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const auto& p = a_ref->data.location;
				const float dz = p.z - at.z;
				const float flat = std::hypot(p.x - at.x, p.y - at.y);
				if (flat < 110.0f && dz > 20.0f && dz < 140.0f) {  // on (or just over) the table top, not beside it
					taken = true;
					auto* file = refBase->GetFile(0);
					why = std::format("an activator on it ({:08X} from {})", refBase->GetFormID(), file ? std::string(file->GetFilename()) : std::string("?"));
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}
		if (taken) {
			log::info("table {:08X}: {}: another mod's game, left alone", a_table->GetFormID(), why);
		}
		std::scoped_lock guard(lock);
		known[key] = taken;
		return taken;
	}

	void Game::ToggleTables()
	{
		// Playable Pool's hands off every table (no prompt, F does the game's own thing), or back on. Kept in the INI.
		auto& s = Settings::Get();
		s.activateTable = !s.activateTable;
		::WritePrivateProfileStringW(L"Keys", L"bActivateTable", s.activateTable ? L"1" : L"0", L"Data\\F4SE\\Plugins\\fo4pool.ini");
		{
			std::scoped_lock lock(_hudMutex);
			_hud.prompt.clear();
		}
		_lookTable = {};
		const std::string key = s.keyTables > 0 ? Settings::Key(s.keyTables) : std::string("the settings menu");
		Notify(s.activateTable ? std::string("Pool: pool tables on") :
		                         std::format("Pool: pool tables off: no prompts, F does what the game does ({} turns them back on)", key));
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
		return FrameOf(_table.get().get(), a_out);
	}

	bool Game::FrameOf(RE::TESObjectREFR* a_table, Frame& a_out) const
	{
		auto* root = a_table ? a_table->Get3D() : nullptr;
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
		_gameType = RackType();
		const auto seed = static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
		if (_gameType == 1) {
			_sim->RackNine(seed);
		} else {
			_sim->Rack(seed);
		}
		_dropTime.fill(0.0f);
		_scratch = false;
		_haveLast = false;
		_modelRadius = 0.0f;
		log::info("re-racked on the same table ({})", _gameType == 1 ? "9-ball" : "8-ball");
		StartGame();
		return true;
	}

	void Game::Rack(RE::TESObjectREFR* a_table)
	{
		if (!_rackingNpcGame) {
			// the table you're racking becomes the loaded one (two NPCs playing elsewhere carry on in the background)
			auto* want = a_table ? a_table : _aiming ? nullptr : PickTable();
			if (want && !FocusTable(want)) {
				return;
			}
			if (_npcGame) {
				EndNpcGame("");  // racking where two NPCs are playing: their game's over
				Clear();
			}
		}
		auto& s = Settings::Get();
		s.Load();

		// Same table (or no other one in view, or in the shot camera, where there's no crosshair): just start again.
		auto* picked = a_table ? a_table : _aiming ? nullptr : PickTable();
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
		_gameType = RackType();
		if (const auto seed = static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()); _gameType == 1) {
			_sim->RackNine(seed);
		} else {
			_sim->Rack(seed);
		}
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
		SetTableCollision(true);
		ForBothSeats([&]() {
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
			if (auto marker = _npcApproach.get()) {
				marker->Disable();
				marker->SetWantsDelete(true);
			}
			_npcApproach = {};
			if (auto marker = _npcWaitMarker.get()) {
				marker->Disable();
				marker->SetWantsDelete(true);
			}
			_npcWaitMarker = {};
		});
		if (!_bgTick) {
			RemoveAimObjects();  // your cue and guide (only ever at the loaded table)
		}
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
		if (!_bgTick) {
			_placing = false;
		}
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
		// build mode (the workshop menu): everything of ours stands still, so nothing (no NPC walk, no Papyrus call,
		// no ball moved or spawned) happens while you build
		const bool building = menus::Open(menus::kWorkshop);
		if (building != _building.load()) {
			_building = building;
			log::info("build mode {}: pool {}", building ? "open" : "closed", building ? "paused" : "carries on");
			if (building) {
				std::scoped_lock lock(_hudMutex);
				_hud.prompt.clear();
				_hud.pick.clear();
			}
		}
		UpdatePadBlock();
		if (building) {
			return;
		}
		UpdateLook();
		// build mode: hands off the world. Every game (balls, NPCs, watchers) holds still until you leave it, so
		// nothing of ours moves 3D or sends AI calls while the workshop is placing things
		if (building) {
			if (!_inWorkshop) {
				_inWorkshop = true;
				log::info("build mode: pool paused");
			}
			return;
		}
		if (_inWorkshop) {
			_inWorkshop = false;
			_haveLast = false;  // no catching up on the time spent building
			_watchScan = 0.0f;  // and the tables are looked for again (some may have been placed or moved)
			log::info("build mode closed: pool carries on");
		}
		// fast travel (or any jump across the map): every game ends; and those you've walked 125 m from
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			const bool jumped = _haveLastPlayerPos && Distance(player->data.location, _lastPlayerPos) > 3000.0f;
			_lastPlayerPos = player->data.location;
			_haveLastPlayerPos = true;
			EndGamesAway(jumped);
			// watching: stopped once you're 10 m from their table
			if (_spectating) {
				auto table = _table.get();
				if (!table || Distance(table->data.location, player->data.location) > 700.0f) {
					_spectating = false;
					_watching = false;
					Tell("Pool: stopped watching");
				}
			}
		}
		if (_kitPlacing) {
			TickKitPlacement();
		}
		// your game on (Solo, or against someone): nobody comes up to say hello or comment while you play
		QuietNpcs(_aiming || (_sim && !_npcGame && !_rules.Over()));
		TickWatchers();
		TickTable();
		TickOthers();
	}

	// ---------------------------------------------------------------- several tables

	void Game::SwapWith(std::size_t a_other)
	{
		std::swap(static_cast<TableSlot&>(*this), _others[a_other]);
	}

	int Game::SlotOf(const RE::TESObjectREFR* a_table) const
	{
		if (!a_table) {
			return -1;
		}
		if (_table.get().get() == a_table) {
			return 0;
		}
		for (std::size_t i = 0; i < _others.size(); ++i) {
			if (_others[i]._table.get().get() == a_table) {
				return static_cast<int>(i) + 1;
			}
		}
		return -1;
	}

	bool Game::Busy(const TableSlot& a_slot) const
	{
		return a_slot._sim || a_slot._opponent || a_slot._npcGame;
	}

	std::size_t Game::RunningGames() const
	{
		std::size_t n = Busy(*this) ? 1 : 0;
		for (const auto& o : _others) {
			n += Busy(o) ? 1 : 0;
		}
		return n;
	}

	bool Game::IsPlaying(const RE::Actor* a_actor) const
	{
		if (!a_actor) {
			return false;
		}
		auto in = [&](const TableSlot& a_slot) {
			if (a_slot._opponent.get().get() == a_actor) {
				return true;
			}
			return a_slot._npcGame && std::ranges::any_of(a_slot._seats, [&](const NpcSlot& a_seat) { return a_seat._opponent.get().get() == a_actor; });
		};
		return in(*this) || std::ranges::any_of(_others, in);
	}

	std::string Game::SlotNames(const TableSlot& a_slot) const
	{
		std::array<std::string, 2> n;
		for (int p = 0; p < 2; ++p) {
			const NpcSlot& seat = p == a_slot._seatIdx ? static_cast<const NpcSlot&>(a_slot) : a_slot._seats[p];
			n[p] = seat._opponentName;
			std::ranges::transform(n[p], n[p].begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
		}
		return std::format("{} VS {}", n[0], n[1]);
	}

	bool Game::FocusTable(RE::TESObjectREFR* a_table)
	{
		const int slot = SlotOf(a_table);
		if (slot == 0) {
			return true;
		}
		ExitAim();
		std::scoped_lock lock(_tableMutex);
		_spectating = false;
		if (slot > 0) {
			// its game is in the background: swap it in; the one here (yours or theirs) carries on in the background
			SwapWith(static_cast<std::size_t>(slot - 1));
		} else if (Busy(*this)) {
			// a new table: the game here carries on in the background (yours waits for you to come back to it; an NPC's
			// turn in it goes on), if there's room
			if (_others.size() + 1 >= kMaxTables) {
				Notify(std::format("Pool: {} games are going already. Finish (or forfeit) one first", kMaxTables));
				return false;
			}
			_others.push_back(std::move(static_cast<TableSlot&>(*this)));
			static_cast<TableSlot&>(*this) = TableSlot{};
		}
		std::erase_if(_others, [&](const TableSlot& a_slot) { return !Busy(a_slot); });
		_watching = false;
		return true;
	}

	void Game::TickOthers()
	{
		if (_others.empty()) {
			return;
		}
		std::scoped_lock lock(_tableMutex);
		_bgTick = true;
		for (std::size_t i = 0; i < _others.size(); ++i) {
			SwapWith(i);
			TickTable();
			SwapWith(i);
		}
		_bgTick = false;
		std::erase_if(_others, [&](const TableSlot& a_slot) { return !Busy(a_slot); });
	}

	void Game::TickTable()
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
		if (_npcGame) {
			// two NPCs: the one waiting first, then the one who's up (left loaded, for the rest of the frame)
			const int up = _rules.Shooter();
			LoadSeat(1 - up);
			TickOpponent(frame, dt);
			if (_npcGame) {
				LoadSeat(up);
				TickOpponent(frame, dt);
			}
		} else {
			TickOpponent(frame, dt);
		}
		// the table's collision is off only while the NPC who's up is on their spot shooting
		if (_tableNoCollide && !(_opponent && (_npcStep == NpcStep::kAiming || _npcStep == NpcStep::kStriking || _npcStep == NpcStep::kFollow))) {
			SetTableCollision(true);
		}
		if (_clearAfterTick) {
			_clearAfterTick = false;
			ExitAim();
			Clear();
			if (!_bgTick) {
				_watching = false;
			}
			UpdateHud();
			return;
		}

		// the game's over: the result stays up for a moment, then the normal camera comes back and the table clears
		if (_rules.Over() && !_shotLive && _sim->AtRest()) {
			_overTime += dt;
			if (_overTime >= kOverSeconds) {
				_overTime = 0.0f;
				log::info("game over: back to the game camera, table cleared");
				if (_opponent) {
					// the match is over: let them go (before the balls and their spot disappear), and the next rack is
					// your own game (iGameMode, Solo Play by default) until you challenge someone again
					EndChallenge(std::format("{} is done playing", _opponentName));
				}
				ExitAim();
				Clear();
				if (!_bgTick) {
					_watching = false;
				}
				UpdateHud();  // nothing racked: the HUD goes
				return;       // no balls left to place
			}
		} else {
			_overTime = 0.0f;
		}

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
		if (_bgTick) {
			return;  // a table in the background: nothing of yours to update
		}
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
			auto* prop = netimmerse_cast<RE::BSLightingShaderProperty*>(a_geom->properties[1].get());
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
		const bool solo = pool::IsSolo(_rules.GameMode());
		const int  who = _opponent && !_npcGame ? 0 : _rules.Shooter();
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
