#pragma once

// Making actors do things: the pool idles (fo4pool.esl, [Anim] in the INI) and a couple of Papyrus Actor
// functions called from the plugin. Main thread only.
namespace fp::Anim
{
	bool PlayAim(RE::Actor* a_actor);     // PoolAim: lined up over the table, loops
	bool PlayStrike(RE::Actor* a_actor);  // PoolStrike: the cue reaches the ball on frame 35
	void Stop(RE::Actor* a_actor);        // back to whatever they were doing

	// Papyrus Actor.PathToReference: walks (0) or runs (1) to a_target. Returns at once; the walk goes on.
	bool PathTo(RE::Actor* a_actor, RE::TESObjectREFR* a_target, float a_walkRun);
	// Papyrus ObjectReference.MoveTo: puts a_actor exactly on a_target, facing the same way.
	bool MoveTo(RE::Actor* a_actor, RE::TESObjectREFR* a_target);
}
