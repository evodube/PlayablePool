#include "Anim.h"

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

		RE::BGSRefAlias* OwnAlias()
		{
			auto* quest = OwnQuest();
			if (!quest || quest->aliases.empty()) {
				return nullptr;
			}
			auto* alias = quest->aliases[0];
			return alias && alias->GetVMTypeID() == RE::BGSRefAlias::TYPE_ID ? static_cast<RE::BGSRefAlias*>(alias) : nullptr;
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

		// Calls a Papyrus method on a form or alias (a script object is bound to it first if it has none)
		template <class T, class... Args>
		bool CallOn(T* a_object, const char* a_script, const char* a_function, const std::string& a_what, Args... a_args)
		{
			auto* game = RE::GameVM::GetSingleton();
			auto  vm = game ? game->GetVM() : nullptr;
			if (!vm || !a_object) {
				return false;
			}
			RE::BSScript::Variable self;
			RE::BSScript::PackVariable(self, a_object);
			const auto object = self.is<RE::BSScript::Object>() ? RE::BSScript::get<RE::BSScript::Object>(self) : nullptr;
			if (!object) {
				log::warn("{}: no script object", a_what);
				return false;
			}
			const RE::BSFixedString script{ a_script };
			const RE::BSFixedString function{ a_function };
			const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{ new LogResult(a_what) };
			const bool ok = vm->DispatchMethodCall(
				object->GetHandle(), script, function,
				[&](RE::BSScrapArray<RE::BSScript::Variable>& a_out) {
					a_out = RE::BSScript::detail::PackVariables(a_args...);
					return true;
				},
				callback);
			log::info("{} {}", a_what, ok ? "sent" : "failed");
			return ok;
		}

		bool g_questStarted{ false };
		bool g_ownWarned{ false };

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

	void JoinOwnPackage(RE::Actor* a_actor)
	{
		if (!a_actor || !Settings::Get().opponentOwnPackage) {
			return;
		}
		auto* quest = OwnQuest();
		auto* alias = OwnAlias();
		if (!quest || !alias || !LinkKeyword()) {
			if (!g_ownWarned) {
				g_ownWarned = true;
				log::warn("fo4pool.esl has no opponent quest/package (an old one?): walks use PathToReference");
			}
			return;
		}
		if (!g_questStarted) {
			g_questStarted = true;  // start game enabled, but a save from before it was added may not have it running
			CallOn(quest, "Quest", "Start", std::string("quest fo4poolOpponent.Start"));
		}
		if (alias->GetActorReference() != a_actor) {
			CallOn(alias, "ReferenceAlias", "ForceRefTo", What(a_actor, "into the Opponent alias (ForceRefTo)"), static_cast<RE::TESObjectREFR*>(a_actor));
		}
	}

	bool InOwnPackage(RE::Actor* a_actor)
	{
		auto* alias = a_actor && Settings::Get().opponentOwnPackage ? OwnAlias() : nullptr;
		return alias && alias->GetActorReference() == a_actor;
	}

	void ReleaseOwnPackage(RE::Actor* a_actor)
	{
		auto* alias = a_actor ? OwnAlias() : nullptr;
		if (!alias || alias->GetActorReference() != a_actor) {
			return;
		}
		CallOn(alias, "ReferenceAlias", "Clear", What(a_actor, "out of the Opponent alias (Clear)"));
		if (auto* keyword = LinkKeyword()) {
			a_actor->SetLinkedRef(nullptr, keyword);
		}
		CallActor(a_actor, "EvaluatePackage", false);
	}

	std::uint32_t PathTo(RE::Actor* a_actor, RE::TESObjectREFR* a_target, float a_walkRun)
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
				a_actor->SetLinkedRef(a_target, keyword);
				log::info("{}: fo4poolWalkToSpot to {:08X} (walk {})", What(a_actor, "package"), a_target->GetFormID(), id);
				CallActor(a_actor, "EvaluatePackage", false);
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
