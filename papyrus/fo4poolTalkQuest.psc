Scriptname fo4poolTalkQuest extends Quest
{fo4pool: "Challenge in Pool" on the dialogue wheel. The plugin puts the NPC by a pool table under your
crosshair in the Asker alias (SetAsker); talking to them then runs fo4poolTalk's silent greeting and
fo4poolAskScene. The choice's info script calls Answer, which sets fo4poolAsked for the plugin to read:
1 = challenge them, 2 = you picked "Talk" (they're let go and you talk to them as the game would).}

ReferenceAlias Property Asker Auto Const Mandatory
GlobalVariable Property fo4poolAsked Auto Const Mandatory

; aiActor 0 empties the alias. Returns what the alias holds afterwards, for fo4pool.log.
String Function SetAsker(Int aiActor)
	if aiActor == 0
		Asker.Clear()
		return "asker cleared"
	endif
	Actor akActor = Game.GetForm(aiActor) as Actor
	if akActor == None
		return "no actor with form ID " + aiActor
	endif
	if !IsRunning()
		Start()
		Int tries = 0
		while IsStarting() && tries < 50
			Utility.Wait(0.1)
			tries += 1
		endwhile
	endif
	Asker.ForceRefTo(akActor)
	return "asker " + Asker.GetReference() + " (running " + IsRunning() + ")"
EndFunction

Function Answer(Int aiChoice)
	ObjectReference who = Asker.GetReference()
	if aiChoice == 2
		; out of the alias first, so the greeting is theirs again, then talk to them
		Asker.Clear()
		fo4poolAsked.SetValue(2)
		Utility.Wait(0.3)
		if who
			who.Activate(Game.GetPlayer())
		endif
	else
		fo4poolAsked.SetValue(aiChoice)
	endif
EndFunction
