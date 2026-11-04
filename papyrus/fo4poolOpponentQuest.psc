Scriptname fo4poolOpponentQuest extends Quest
{fo4pool: the NPC you're playing pool against. While they're in the Opponent alias, its package
(fo4poolWalkToSpot: walk to their linked ref, keyword fo4poolLinkPoolSpot) outranks the sandbox packages
other mods give settlers. The F4SE plugin calls Join and Leave and sets the linked ref for each walk.}

ReferenceAlias Property Opponent Auto Const Mandatory

; puts them in the alias; true if the alias holds them afterwards
Bool Function Join(Actor akActor)
	if akActor == None
		return False
	endif
	if !IsRunning()
		Start()
		Int tries = 0
		while IsStarting() && tries < 50
			Utility.Wait(0.1)
			tries += 1
		endwhile
	endif
	Opponent.ForceRefTo(akActor)
	akActor.EvaluatePackage()
	return Opponent.GetActorReference() == akActor
EndFunction

; out of the alias: their own AI again
Function Leave()
	Actor current = Opponent.GetActorReference()
	Opponent.Clear()
	if current
		current.EvaluatePackage()
	endif
EndFunction
