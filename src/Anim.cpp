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
		bool CallActor(RE::Actor* a_actor, const char* a_function, Args... a_args)
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
			const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{ new LogResult(
				std::format("{} ({:08X}).{}", a_actor->GetDisplayFullName(), a_actor->GetFormID(), a_function)) };
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

	bool PathTo(RE::Actor* a_actor, RE::TESObjectREFR* a_target, float a_walkRun)
	{
		// Actor.PathToReference(ObjectReference aTarget, float afWalkRunFactor)
		return CallActor(a_actor, "PathToReference", a_target, a_walkRun);
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
