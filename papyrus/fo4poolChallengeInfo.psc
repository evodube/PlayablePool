Scriptname fo4poolChallengeInfo extends TopicInfo Const
{fo4pool: the "Challenge in Pool" choice. When it's done the plugin starts a game with the NPC.}

Event OnEnd(ObjectReference akSpeakerRef, bool abHasBeenSaid)
	(GetOwningQuest() as fo4poolTalkQuest).Answer(1)
EndEvent
