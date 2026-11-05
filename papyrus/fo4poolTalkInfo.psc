Scriptname fo4poolTalkInfo extends TopicInfo Const
{fo4pool: the "Talk" choice: their own dialogue, as if fo4pool weren't asking.}

Event OnEnd(ObjectReference akSpeakerRef, bool abHasBeenSaid)
	(GetOwningQuest() as fo4poolTalkQuest).Answer(2)
EndEvent
