Scriptname fo4poolOpponentQuest extends Quest
{fo4pool: the NPC you're playing pool against. While they're in the Opponent alias, its package
(fo4poolWalkToSpot: walk to their linked ref, keyword fo4poolLinkPoolSpot) outranks the sandbox packages
other mods give settlers. The F4SE plugin calls Join, Walk, Arrived and Leave.
Actors, refs and keywords come in as form IDs, not objects: an NPC with other scripts on them can come through
from the plugin as an object Papyrus won't take as an Actor ("Type mismatch for argument 1").}

ReferenceAlias Property Opponent Auto Const Mandatory

; puts them in the alias. Returns "in ..." if the alias holds them afterwards, otherwise "NOT in ..." with what
; it found, for fo4pool.log. If the first ForceRefTo doesn't take, the quest is restarted and it's tried again.
String Function Join(Int aiActor)
	Actor akActor = Game.GetForm(aiActor) as Actor
	if akActor == None
		return "NOT in: no actor with form ID " + aiActor
	endif
	Debug.Trace("fo4pool: Join " + akActor + ", Opponent property " + Opponent)
	String before = "running " + IsRunning() + ", alias held " + Opponent.GetReference()
	if !IsRunning()
		StartAndWait()
	endif
	; whoever it held before (an earlier opponent, kept by a save) out first
	Actor previous = Opponent.GetActorReference()
	if previous && previous != akActor
		Opponent.Clear()
		previous.EvaluatePackage()
	endif
	Opponent.ForceRefTo(akActor)
	if Opponent.GetActorReference() == akActor
		akActor.EvaluatePackage()
		return "in (" + before + ")"
	endif
	String first = "running " + IsRunning() + ", alias held " + Opponent.GetReference()
	; a fresh start of the quest, then again
	Stop()
	Int tries = 0
	while IsStopping() && tries < 50
		Utility.Wait(0.1)
		tries += 1
	endwhile
	StartAndWait()
	Opponent.ForceRefTo(akActor)
	String second = "running " + IsRunning() + ", alias held " + Opponent.GetReference()
	if Opponent.GetActorReference() == akActor
		akActor.EvaluatePackage()
		return "in, after restarting the quest (before: " + before + "; first ForceRefTo: " + first + ")"
	endif
	return "NOT in (before: " + before + "; first ForceRefTo: " + first + "; after a restart: " + second + "; dead " + akActor.IsDead() + ", disabled " + akActor.IsDisabled() + ")"
EndFunction

Function StartAndWait()
	Start()
	Int tries = 0
	while IsStarting() && tries < 50
		Utility.Wait(0.1)
		tries += 1
	endwhile
EndFunction

; a walk with the alias package: the package goes to their linked ref. It's taken off first and the package
; looked at again, so a Travel that already finished (at the last spot) starts over for the new one instead of
; staying done where it was. abReset: reset their AI too (a walk that isn't getting going)
Function Walk(Int aiActor, Int aiTarget, Int aiLink, Bool abReset)
	Actor akActor = Game.GetForm(aiActor) as Actor
	ObjectReference akTarget = Game.GetForm(aiTarget) as ObjectReference
	Keyword akLink = Game.GetForm(aiLink) as Keyword
	if akActor == None || akTarget == None || akLink == None
		Debug.Trace("fo4pool: Walk: actor " + akActor + ", target " + akTarget + ", keyword " + akLink)
		return
	endif
	akActor.SetLinkedRef(None, akLink)
	akActor.EvaluatePackage()
	akActor.SetLinkedRef(akTarget, akLink)
	akActor.EvaluatePackage(abReset)
EndFunction

; there: no destination, so the Travel package doesn't keep stepping and turning them on the spot
Function Arrived(Int aiActor, Int aiLink)
	Actor akActor = Game.GetForm(aiActor) as Actor
	Keyword akLink = Game.GetForm(aiLink) as Keyword
	if akActor && akLink
		akActor.SetLinkedRef(None, akLink)
		akActor.EvaluatePackage()
	endif
EndFunction

; out of the alias: their own AI again
Function Leave()
	Actor current = Opponent.GetActorReference()
	Opponent.Clear()
	if current
		current.EvaluatePackage()
	endif
EndFunction
