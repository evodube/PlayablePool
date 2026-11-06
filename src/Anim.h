#pragma once

// Making actors do things: the pool idles (fo4pool.esl, [Anim] in the INI) and a couple of Papyrus Actor
// functions called from the plugin. Main thread only.
namespace fp::Anim
{
	bool PlayAim(RE::Actor* a_actor, int a_variant = 0);   // PoolAim (1: PoolAim2): lined up over the table, loops
	bool PlayStrike(RE::Actor* a_actor, int a_variant = 0);  // PoolStrike (1: PoolStrike2, after PoolAim2)  // PoolStrike: the cue reaches the ball on frame 35
	bool PlayWait(RE::Actor* a_actor, int a_variant = 0);  // PoolWait (1: PoolWait2): standing at the table, cue upright, loops
	void Stop(RE::Actor* a_actor);        // back to whatever they were doing

	// Papyrus Actor.PathToReference: walks (0) or runs (1) to a_target. Returns at once with an id for the walk (0 =
	// couldn't send it); the walk goes on.
	// a_again: the same walk sent again because it isn't getting going (the own package then resets their AI too).
	std::uint32_t PathTo(RE::Actor* a_actor, RE::TESObjectREFR* a_target, float a_walkRun, bool a_again = false);
	// On the spot: the own package's destination comes off, so it doesn't keep stepping and turning them.
	void Arrived(RE::Actor* a_actor);
	// How that walk ended: -1 not yet (or unknown), 0 it failed (no path, or it was cut short), 1 they got there.
	int PathResult(std::uint32_t a_id);

	// fo4pool.esl's own walk: quest fo4poolOpponent (priority 100) has an alias whose package (fo4poolWalkToSpot)
	// walks the actor to their linked ref (keyword fo4poolLinkPoolSpot) and keeps them there, facing it. Alias
	// packages go by quest priority, so it outranks settler sandboxes from other mods, where PathToReference gets
	// cut short. PathTo uses it for an actor in the alias ([Opponent] bOwnPackage); otherwise PathToReference.
	void JoinOwnPackage(RE::Actor* a_actor, int a_slot = 0);  // into seat a_slot's alias (ForceRefTo; takes a moment)
	void ReleaseOwnPackage(RE::Actor* a_actor);  // out of it, no linked ref: their own AI again
	bool InOwnPackage(RE::Actor* a_actor);       // the alias holds them
	// The Watchers collection (fo4poolWatchTable: sandbox round their linked ref, keyword fo4poolLinkPoolTable):
	// settlers hanging round a pool table
	void AddWatcher(RE::Actor* a_actor, RE::TESObjectREFR* a_table);
	void RemoveWatcher(RE::Actor* a_actor);
	void ClearWatchers();
	bool OwnPackageReady();  // fo4pool.esl has the quest and keywords
	bool RunningOwnPackage(RE::Actor* a_actor);  // ...and fo4poolWalkToSpot is the package they're on
	// Papyrus ObjectReference.MoveTo: puts a_actor exactly on a_target, facing the same way.
	bool MoveTo(RE::Actor* a_actor, RE::TESObjectREFR* a_target);
	// Papyrus Actor.SetRestrained: true keeps them where they are, so their sandbox package can't walk them off.
	// Saved with the game, so always undo it before a save.
	bool SetRestrained(RE::Actor* a_actor, bool a_restrained);
	// Papyrus Actor.SetLookAt / ClearLookAt: what they turn their head to
	bool LookAt(RE::Actor* a_actor, RE::TESObjectREFR* a_target);
	bool ClearLookAt(RE::Actor* a_actor);
	// Papyrus ObjectReference.SetPosition: a small correction of where they stand (lining the cue up)
	bool SetPosition(RE::Actor* a_actor, const RE::NiPoint3& a_pos);
	// Papyrus Actor.SetHeadTracking: off while they're lining up a shot
	bool SetHeadTracking(RE::Actor* a_actor, bool a_on);
}
