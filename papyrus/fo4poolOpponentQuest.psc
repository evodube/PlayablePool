Scriptname fo4poolOpponentQuest extends Quest
{fo4pool: the NPC you're playing pool against. While they're in the Opponent alias, its package
(fo4poolWalkToSpot: walk to their linked ref, keyword fo4poolLinkPoolSpot) outranks the sandbox packages
other mods give settlers. The F4SE plugin calls Join, Walk, Arrived and Leave.
Actors, refs and keywords come in as form IDs, not objects: an NPC with other scripts on them can come through
from the plugin as an object Papyrus won't take as an Actor ("Type mismatch for argument 1").}

ReferenceAlias Property Opponent Auto Const Mandatory
ReferenceAlias Property Opponent2 Auto Const Mandatory  ; the second NPC when two NPCs play each other
RefCollectionAlias Property Watchers Auto Const Mandatory  ; settlers hanging round a table (fo4poolWatchTable)

; the alias for a seat: 0 Opponent (the NPC playing you, or the first of two NPCs), 1 Opponent2 (the second)
ReferenceAlias Function SlotAlias(Int aiSlot)
	if aiSlot == 1
		return Opponent2
	endif
	return Opponent
EndFunction

; puts them in the Opponent alias (seat 0)
String Function Join(Int aiActor)
	return JoinSlot(aiActor, 0)
EndFunction

; puts them in the seat's alias. Returns "in ..." if the alias holds them afterwards, otherwise "NOT in ..." with
; what it found, for fo4pool.log. If the first ForceRefTo doesn't take, the quest is restarted (whoever the other
; seat held goes back in) and it's tried again.
String Function JoinSlot(Int aiActor, Int aiSlot)
	Actor akActor = Game.GetForm(aiActor) as Actor
	if akActor == None
		return "NOT in: no actor with form ID " + aiActor
	endif
	ReferenceAlias seat = SlotAlias(aiSlot)
	ReferenceAlias other = SlotAlias(1 - aiSlot)
	Debug.Trace("fo4pool: Join " + akActor + " seat " + aiSlot + ", alias " + seat)
	String before = "running " + IsRunning() + ", alias held " + seat.GetReference()
	if !IsRunning()
		StartAndWait()
	endif
	; whoever it held before (an earlier opponent, kept by a save) out first
	Actor previous = seat.GetActorReference()
	if previous && previous != akActor
		seat.Clear()
		previous.EvaluatePackage()
	endif
	if other.GetActorReference() == akActor
		other.Clear()
	endif
	seat.ForceRefTo(akActor)
	if seat.GetActorReference() == akActor
		akActor.EvaluatePackage()
		return "in (" + before + ")"
	endif
	String first = "running " + IsRunning() + ", alias held " + seat.GetReference()
	; a fresh start of the quest (a save from before this alias existed), then again
	Actor keep = other.GetActorReference()
	Stop()
	Int tries = 0
	while IsStopping() && tries < 50
		Utility.Wait(0.1)
		tries += 1
	endwhile
	StartAndWait()
	if keep
		other.ForceRefTo(keep)
		keep.EvaluatePackage()
	endif
	seat.ForceRefTo(akActor)
	String second = "running " + IsRunning() + ", alias held " + seat.GetReference()
	if seat.GetActorReference() == akActor
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
	LeaveSlot(0)
EndFunction

Function LeaveSlot(Int aiSlot)
	ReferenceAlias seat = SlotAlias(aiSlot)
	Actor current = seat.GetActorReference()
	seat.Clear()
	if current
		current.EvaluatePackage()
	endif
EndFunction

; a settler wanders over to a table and hangs round it (the collection's sandbox, round their linked ref). "in" if
; the collection holds them afterwards. A save from before the collection existed has the quest running without it:
; then the quest is restarted (only with no opponent in it) and it's tried again.
String Function AddWatcher(Int aiActor, Int aiTable, Int aiLink)
	Actor akActor = Game.GetForm(aiActor) as Actor
	ObjectReference akTable = Game.GetForm(aiTable) as ObjectReference
	Keyword akLink = Game.GetForm(aiLink) as Keyword
	if akActor == None || akTable == None || akLink == None
		return "NOT in: actor " + akActor + ", table " + akTable + ", keyword " + akLink
	endif
	akActor.SetLinkedRef(akTable, akLink)
	Watchers.AddRef(akActor)
	if Watchers.Find(akActor) < 0 && Opponent.GetReference() == None && Opponent2.GetReference() == None
		Stop()
		Int tries = 0
		while IsStopping() && tries < 50
			Utility.Wait(0.1)
			tries += 1
		endwhile
		StartAndWait()
		Watchers.AddRef(akActor)
	endif
	akActor.EvaluatePackage()
	if Watchers.Find(akActor) < 0
		return "NOT in (collection " + Watchers + ", count " + Watchers.GetCount() + ")"
	endif
	return "in"
EndFunction

; back to their own business
Function RemoveWatcher(Int aiActor, Int aiLink)
	Actor akActor = Game.GetForm(aiActor) as Actor
	Keyword akLink = Game.GetForm(aiLink) as Keyword
	if akActor == None
		return
	endif
	Watchers.RemoveRef(akActor)
	if akLink
		akActor.SetLinkedRef(None, akLink)
	endif
	akActor.EvaluatePackage()
EndFunction

; everyone out of the collection (a loaded save: the plugin no longer knows who's in it)
Function ClearWatchers(Int aiLink)
	Keyword akLink = Game.GetForm(aiLink) as Keyword
	Int i = Watchers.GetCount()
	while i > 0
		i -= 1
		ObjectReference r = Watchers.GetAt(i)
		Actor a = r as Actor
		Watchers.RemoveRef(r)
		if a
			if akLink
				a.SetLinkedRef(None, akLink)
			endif
			a.EvaluatePackage()
		endif
	endwhile
EndFunction
