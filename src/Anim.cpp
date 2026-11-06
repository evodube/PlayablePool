#include "Anim.h"

#include <unordered_set>

#include "Settings.h"
#include "Util.h"

// The base class's destructor is declared in CommonLibF4 but not defined there; our functor needs one to link.
RE::BSScript::IStackCallbackFunctor::~IStackCallbackFunctor() = default;

namespace fp::Anim
{
	namespace
	{
		bool g_warned{ false };

		// Logs what a Papyrus call returned (it runs on the script thread, later): SetRestrained says whether it
		// took, and PathToReference only returns once the walk is over, true if they got there.
		class LogResult final : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			explicit LogResult(std::string a_what) :
				_what(std::move(a_what))
			{}

			void CallQueued() override {}
			void CallCanceled() override { log::info("papyrus {}: canceled", _what); }
			void StartMultiDispatch() override {}
			void EndMultiDispatch() override {}
			void operator()(RE::BSScript::Variable a_result) override
			{
				if (a_result.is<bool>()) {
					log::info("papyrus {} -> {}", _what, RE::BSScript::get<bool>(a_result));
				} else if (a_result.is<RE::BSFixedString>()) {
					log::info("papyrus {} -> {}", _what, RE::BSScript::get<RE::BSFixedString>(a_result).c_str());
				} else {
					log::info("papyrus {} -> done", _what);
				}
			}

		private:
			std::string _what;
		};

		// The last few walks' results, written on the script thread: id and result together in one word
		constexpr std::size_t                                     kPaths = 16;
		std::array<std::atomic<std::uint64_t>, kPaths>            g_paths{};
		std::atomic<std::uint32_t>                                g_pathId{ 0 };

		class PathDone final : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			PathDone(std::string a_what, std::uint32_t a_id) :
				_what(std::move(a_what)),
				_id(a_id)
			{}

			void CallQueued() override {}
			void CallCanceled() override
			{
				log::info("papyrus {} (walk {}): canceled", _what, _id);
				Store(false);
			}
			void StartMultiDispatch() override {}
			void EndMultiDispatch() override {}
			void operator()(RE::BSScript::Variable a_result) override
			{
				const bool ok = a_result.is<bool>() && RE::BSScript::get<bool>(a_result);
				log::info("papyrus {} (walk {}) -> {}", _what, _id, ok);
				Store(ok);
			}

		private:
			void Store(bool a_ok) { g_paths[_id % kPaths].store((std::uint64_t{ _id } << 1) | (a_ok ? 1u : 0u)); }

			std::string   _what;
			std::uint32_t _id;
		};

		// fo4pool.esl: the opponent's own walk (make_esl.py)
		constexpr std::uint32_t kLinkKeywordID = 0x814;
		constexpr std::uint32_t kPackageID = 0x815;
		constexpr std::uint32_t kTableKeywordID = 0x817;
		constexpr std::uint32_t kQuestID = 0x816;

		RE::BGSKeyword* LinkKeyword()
		{
			auto* form = util::ResolveAnyForm(Settings::Get().animPlugin, kLinkKeywordID);
			return form ? form->As<RE::BGSKeyword>() : nullptr;
		}

		RE::TESQuest* OwnQuest()
		{
			auto* form = util::ResolveAnyForm(Settings::Get().animPlugin, kQuestID);
			return form ? form->As<RE::TESQuest>() : nullptr;
		}

		RE::TESIdleForm* Idle(std::uint32_t a_rawID)
		{
			const auto& s = Settings::Get();
			auto*       form = a_rawID ? util::ResolveAnyForm(s.animPlugin, a_rawID) : nullptr;
			auto*       idle = form ? form->As<RE::TESIdleForm>() : nullptr;
			if (!idle && !g_warned) {
				g_warned = true;
				log::warn("pool idle {}|{:X} not found (is the new fo4pool.esl installed?)", s.animPlugin, a_rawID);
			}
			return idle;
		}

		bool Play(RE::Actor* a_actor, std::uint32_t a_rawID, const char* a_name)
		{
			if (!Settings::Get().anim || !a_actor || !a_actor->currentProcess) {
				return false;
			}
			auto* idle = Idle(a_rawID);
			if (!idle) {
				return false;
			}
			const bool ok = a_actor->currentProcess->PlayIdle(*a_actor, idle, nullptr);
			log::info("{} ({:08X}): play idle {}: {}", a_actor->GetDisplayFullName(), a_actor->GetFormID(), a_name, ok ? "started" : "refused");
			return ok;
		}

		// Calls a Papyrus function on an actor's script object; the call runs on the script thread, later.
		template <class... Args>
		bool CallActorWith(RE::Actor* a_actor, const char* a_function, const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>& a_callback, Args... a_args)
		{
			auto* game = RE::GameVM::GetSingleton();
			auto  vm = game ? game->GetVM() : nullptr;
			if (!vm || !a_actor) {
				return false;
			}
			auto&      handles = vm->GetObjectHandlePolicy();
			const auto handle = handles.GetHandleForObject(RE::BSScript::kActor, a_actor);
			if (handle == handles.EmptyHandle()) {
				log::warn("{}: no script handle for {:08X}", a_function, a_actor->GetFormID());
				return false;
			}
			const RE::BSFixedString script{ "Actor" };
			const RE::BSFixedString function{ a_function };
			const auto& callback = a_callback;
			const bool ok = vm->DispatchMethodCall(
				handle, script, function,
				[&](RE::BSScrapArray<RE::BSScript::Variable>& a_out) {
					a_out = RE::BSScript::detail::PackVariables(a_args...);
					return true;
				},
				callback);
			log::info("{} ({:08X}): {} {}", a_actor->GetDisplayFullName(), a_actor->GetFormID(), a_function, ok ? "sent" : "failed");
			return ok;
		}

		bool                       g_ownWarned{ false };
		// who the Players collection holds, as the quest script says (any number: up to four tables of two)
		std::mutex                        g_joinedMutex;
		std::unordered_set<std::uint32_t> g_joined;
		bool Joined(std::uint32_t a_id)
		{
			std::scoped_lock lock(g_joinedMutex);
			return g_joined.contains(a_id);
		}
		std::atomic<bool>          g_joining{ false };  // a Join is on its way (one at a time: it may restart the quest)
		std::atomic<bool>          g_diagnose{ false };  // a Join didn't take: log what the quest looks like
		bool                       g_diagnosed{ false };

		// What fo4poolOpponent looks like from here: running, its alias, its script object and the Opponent property
		void DiagnoseQuest()
		{
			auto* quest = OwnQuest();
			if (!quest) {
				log::warn("quest check: fo4poolOpponent not found");
				return;
			}
			std::string alias = "no aliases";
			if (!quest->aliases.empty() && quest->aliases[0]) {
				auto* a = quest->aliases[0];
				auto* ref = a->GetVMTypeID() == RE::BGSRefAlias::TYPE_ID ? static_cast<RE::BGSRefAlias*>(a)->GetReference() : nullptr;
				alias = std::format("alias 0 '{}' type {} flags {:X} fill type {} holds {}", a->aliasName.c_str(), static_cast<int>(a->GetVMTypeID()),
					static_cast<std::uint32_t>(a->flags.get()), a->fillType, ref ? std::format("{:08X}", ref->GetFormID()) : std::string("nobody"));
			}
			std::string script = "no VM";
			if (auto* game = RE::GameVM::GetSingleton(); game && game->GetVM()) {
				auto        vm = game->GetVM();
				auto&       handles = vm->GetObjectHandlePolicy();
				const auto  handle = handles.GetHandleForObject(RE::BSScript::GetVMTypeID<RE::TESQuest>(), quest);
				RE::BSTSmartPointer<RE::BSScript::Object> object;
				const bool  found = vm->FindBoundObject(handle, "fo4poolOpponentQuest", false, object, false) && object;
				std::string prop = "-";
				if (found) {
					if (auto* v = object->GetProperty(RE::BSFixedString{ "Opponent" })) {
						prop = v->is<RE::BSScript::Object>() ? (RE::BSScript::get<RE::BSScript::Object>(*v) ? std::format("an object (handle {:X})", RE::BSScript::get<RE::BSScript::Object>(*v)->GetHandle()) : std::string("None")) :
						       v->is<std::nullptr_t>()            ? std::string("None") :
						                                            std::string("not an object");
					} else {
						prop = "no such property";
					}
				}
				script = std::format("quest handle {:X}, fo4poolOpponentQuest bound {}, Opponent property {}", handle, found ? "yes" : "NO", prop);
			}
			log::warn("quest check: fo4poolOpponent active {} flags {:X} priority {} | {} | {}", quest->GetActive(), quest->data.flags, quest->data.priority, alias, script);
		}

		// fo4poolOpponentQuest.Join returns whether the alias holds them now (sets g_joined)
		class JoinDone final : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			JoinDone(std::string a_what, std::uint32_t a_formID) :
				_what(std::move(a_what)),
				_formID(a_formID)
			{}

			void CallQueued() override {}
			void CallCanceled() override
			{
				log::warn("papyrus {}: canceled (is fo4poolOpponentQuest.pex installed?)", _what);
				g_joining = false;
			}
			void StartMultiDispatch() override {}
			void EndMultiDispatch() override {}
			void operator()(RE::BSScript::Variable a_result) override
			{
				// "in ..." / "NOT in ..." with what the script saw (an old script returns a bool)
				std::string text;
				bool        held = false;
				if (a_result.is<RE::BSFixedString>()) {
					text = RE::BSScript::get<RE::BSFixedString>(a_result).c_str();
					held = text.starts_with("in");
				} else if (a_result.is<bool>()) {
					held = RE::BSScript::get<bool>(a_result);
					text = held ? "in the alias" : "NOT in the alias";
				} else {
					text = a_result.is<std::nullptr_t>() ? "None (the call failed before Join ran?)" : "something else";
				}
				if (text.empty()) {
					text = "\"\" (Join didn't run to the end: an empty string is what a failed call returns)";
				}
				if (!held) {
					g_diagnose = true;  // the plugin looks at the quest on the next walk (main thread)
				}
				log::info("papyrus {} -> {}", _what, text);
				g_joining = false;
				if (held) {
					std::scoped_lock lock(g_joinedMutex);
					g_joined.insert(_formID);
				}
			}

		private:
			std::string   _what;
			std::uint32_t _formID;
		};

		// a form as the script's Int argument (it does Game.GetForm): objects for NPCs with other scripts on them
		// came through as the wrong type
		std::int32_t FormArg(const RE::TESForm* a_form)
		{
			return a_form ? static_cast<std::int32_t>(a_form->GetFormID()) : 0;
		}

		// Calls a function of the quest's own script (fo4poolOpponentQuest, attached in fo4pool.esl): the script does
		// the alias work in Papyrus, where aliases are at home
		template <class... Args>
		bool CallQuest(const char* a_function, const std::string& a_what, const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>& a_callback,
			Args... a_args)
		{
			auto* game = RE::GameVM::GetSingleton();
			auto  vm = game ? game->GetVM() : nullptr;
			auto* quest = OwnQuest();
			if (!vm || !quest) {
				return false;
			}
			auto&      handles = vm->GetObjectHandlePolicy();
			const auto handle = handles.GetHandleForObject(RE::BSScript::GetVMTypeID<RE::TESQuest>(), quest);
			if (handle == handles.EmptyHandle()) {
				log::warn("{}: no script handle for the quest", a_what);
				return false;
			}
			const RE::BSFixedString script{ "fo4poolOpponentQuest" };
			const RE::BSFixedString function{ a_function };
			const bool              ok = vm->DispatchMethodCall(
                handle, script, function,
                [&](RE::BSScrapArray<RE::BSScript::Variable>& a_out) {
                    a_out = RE::BSScript::detail::PackVariables(a_args...);
                    return true;
                },
                a_callback);
			log::info("{} {}", a_what, ok ? "sent" : "failed (is fo4poolOpponentQuest.pex installed?)");
			return ok;
		}

		std::string What(RE::Actor* a_actor, const char* a_function)
		{
			return a_actor ? std::format("{} ({:08X}).{}", a_actor->GetDisplayFullName(), a_actor->GetFormID(), a_function) : std::string(a_function);
		}

		template <class... Args>
		bool CallActor(RE::Actor* a_actor, const char* a_function, Args... a_args)
		{
			const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{ new LogResult(What(a_actor, a_function)) };
			return CallActorWith(a_actor, a_function, callback, a_args...);
		}
	}

	bool PlayAim(RE::Actor* a_actor)
	{
		return Play(a_actor, Settings::Get().animAimFormID, "PoolAim");
	}

	bool PlayStrike(RE::Actor* a_actor)
	{
		return Play(a_actor, Settings::Get().animStrikeFormID, "PoolStrike");
	}

	bool PlayWait(RE::Actor* a_actor)
	{
		return Play(a_actor, Settings::Get().animWaitFormID, "PoolWait");
	}

	void Stop(RE::Actor* a_actor)
	{
		if (a_actor && a_actor->currentProcess) {
			a_actor->currentProcess->StopCurrentIdle(a_actor, false, true);
		}
	}

	void JoinOwnPackage(RE::Actor* a_actor, [[maybe_unused]] int a_slot)
	{
		if (g_diagnose.exchange(false) && !g_diagnosed) {
			g_diagnosed = true;
			DiagnoseQuest();
		}
		if (!a_actor || !Settings::Get().opponentOwnPackage || Joined(a_actor->GetFormID())) {
			return;
		}
		if (!OwnQuest() || !LinkKeyword()) {
			if (!g_ownWarned) {
				g_ownWarned = true;
				log::warn("fo4pool.esl has no opponent quest/package (an old one?): walks use PathToReference");
			}
			return;
		}
		// fo4poolOpponentQuest.Join: starts the quest if it isn't running, ForceRefTo, EvaluatePackage; says whether
		// the alias holds them
		if (g_joining.exchange(true)) {
			return;  // the last one hasn't answered yet
		}
		const std::string what = What(a_actor, "fo4poolOpponentQuest.JoinPlayer");
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> done{ new JoinDone(what, a_actor->GetFormID()) };
		if (!CallQuest("JoinPlayer", what, done, FormArg(a_actor))) {
			g_joining = false;
		}
	}

	void AddWatcher(RE::Actor* a_actor, RE::TESObjectREFR* a_table)
	{
		auto* keyword = util::ResolveAnyForm(Settings::Get().animPlugin, kTableKeywordID);
		if (!a_actor || !a_table || !keyword) {
			return;
		}
		const std::string what = What(a_actor, "fo4poolOpponentQuest.AddWatcher");
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> logged{ new LogResult(what) };
		CallQuest("AddWatcher", what, logged, FormArg(a_actor), FormArg(a_table), FormArg(keyword));
	}

	void RemoveWatcher(RE::Actor* a_actor)
	{
		auto* keyword = util::ResolveAnyForm(Settings::Get().animPlugin, kTableKeywordID);
		if (!a_actor) {
			return;
		}
		const std::string what = What(a_actor, "fo4poolOpponentQuest.RemoveWatcher");
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> logged{ new LogResult(what) };
		CallQuest("RemoveWatcher", what, logged, FormArg(a_actor), FormArg(keyword));
	}

	bool OwnPackageReady()
	{
		return OwnQuest() && LinkKeyword() && Settings::Get().opponentOwnPackage;
	}

	void ClearWatchers()
	{
		auto*             keyword = util::ResolveAnyForm(Settings::Get().animPlugin, kTableKeywordID);
		const std::string what = "fo4poolOpponentQuest.ClearWatchers";
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> logged{ new LogResult(what) };
		CallQuest("ClearWatchers", what, logged, FormArg(keyword));
	}

	bool InOwnPackage(RE::Actor* a_actor)
	{
		return a_actor && Settings::Get().opponentOwnPackage && Joined(a_actor->GetFormID());
	}

	bool RunningOwnPackage(RE::Actor* a_actor)
	{
		auto* proc = a_actor ? a_actor->currentProcess : nullptr;
		auto* pkg = proc ? proc->currentPackage.package : nullptr;
		return pkg && pkg == util::ResolveAnyForm(Settings::Get().animPlugin, kPackageID);
	}

	void ReleaseOwnPackage(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return;
		}
		{
			std::scoped_lock lock(g_joinedMutex);
			if (!g_joined.erase(a_actor->GetFormID())) {
				return;
			}
		}
		if (auto* keyword = LinkKeyword()) {
			a_actor->SetLinkedRef(nullptr, keyword);
		}
		const std::string what = What(a_actor, "fo4poolOpponentQuest.LeavePlayer");
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> logged{ new LogResult(what) };
		CallQuest("LeavePlayer", what, logged, FormArg(a_actor));  // out of the collection, EvaluatePackage: their own AI again
	}

	void Arrived(RE::Actor* a_actor)
	{
		auto* keyword = LinkKeyword();
		if (!InOwnPackage(a_actor) || !keyword) {
			return;
		}
		const std::string what = What(a_actor, "fo4poolOpponentQuest.Arrived");
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> logged{ new LogResult(what) };
		CallQuest("Arrived", what, logged, FormArg(a_actor), FormArg(keyword));
	}

	std::uint32_t PathTo(RE::Actor* a_actor, RE::TESObjectREFR* a_target, float a_walkRun, bool a_again)
	{
		std::uint32_t id = ++g_pathId;
		if (id == 0) {
			id = ++g_pathId;
		}
		g_paths[id % kPaths].store(0);
		// in the Opponent alias: point the package at the target (their linked ref) and have them look at their
		// packages again; it walks them there and keeps them there. No result comes back: the plugin watches the
		// distance
		if (InOwnPackage(a_actor)) {
			if (auto* keyword = LinkKeyword(); keyword && a_target) {
				// in the quest script: linked ref off, look at packages, linked ref on the target, look again (and
				// reset their AI if this walk is being sent again), so the Travel starts over for the new spot
				const std::string what = std::format("{} to {:08X} (walk {}{})", What(a_actor, "fo4poolOpponentQuest.Walk"), a_target->GetFormID(), id,
					a_again ? ", AI reset" : "");
				const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> logged{ new LogResult(what) };
				CallQuest("Walk", what, logged, FormArg(a_actor), FormArg(a_target), FormArg(keyword), a_again);
				return id;
			}
		}
		// Actor.PathToReference(ObjectReference aTarget, float afWalkRunFactor)
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{ new PathDone(What(a_actor, "PathToReference"), id) };
		return CallActorWith(a_actor, "PathToReference", callback, a_target, a_walkRun) ? id : 0;
	}

	int PathResult(std::uint32_t a_id)
	{
		if (a_id == 0) {
			return -1;
		}
		const std::uint64_t v = g_paths[a_id % kPaths].load();
		return (v >> 1) == a_id ? static_cast<int>(v & 1) : -1;
	}

	bool MoveTo(RE::Actor* a_actor, RE::TESObjectREFR* a_target)
	{
		// ObjectReference.MoveTo(ObjectReference akTarget, float afXOffset, float afYOffset, float afZOffset,
		// bool abMatchRotation)
		return CallActor(a_actor, "MoveTo", a_target, 0.0f, 0.0f, 0.0f, true);
	}

	bool SetPosition(RE::Actor* a_actor, const RE::NiPoint3& a_pos)
	{
		return CallActor(a_actor, "SetPosition", a_pos.x, a_pos.y, a_pos.z);
	}

	bool SetRestrained(RE::Actor* a_actor, bool a_restrained)
	{
		return CallActor(a_actor, "SetRestrained", a_restrained);
	}

	bool LookAt(RE::Actor* a_actor, RE::TESObjectREFR* a_target)
	{
		// Actor.SetLookAt(ObjectReference akTarget, bool abPathingLookAt)
		return CallActor(a_actor, "SetLookAt", a_target, false);
	}

	bool ClearLookAt(RE::Actor* a_actor)
	{
		return CallActor(a_actor, "ClearLookAt");
	}

	bool SetHeadTracking(RE::Actor* a_actor, bool a_on)
	{
		return CallActor(a_actor, "SetHeadTracking", a_on);
	}
}
