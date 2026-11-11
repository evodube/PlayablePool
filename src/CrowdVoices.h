#pragma once

// The Combat Zone crowd's wordless cheers, boos and laughs (Fallout4.esm's CZ_Crowd_Approval_Sounds,
// CZ_Crowd_Disapproval_Sounds, CZ_Crowd_Laughter), read at runtime from the player's own game files (a loose file in
// Data\Sound\Voice\Fallout4.esm\..., else Fallout 4 - Voices.ba2). Nothing of Bethesda's is shipped with the mod.
//
// Only the MaleRough and FemaleRough recordings are used (no raider voices, no spoken lines): a woman watching gets
// FemaleRough's, a man MaleRough's, whatever their own voice is.
namespace fp::CrowdVoices
{
	enum class Kind
	{
		kCheer,
		kBoo,
		kLaugh,
		kCount
	};

	// One recording: the xWMA audio out of its .fuz (the lip-sync part is dropped), ready for XAudio2.
	struct Clip
	{
		std::vector<std::uint8_t>  format;  // WAVEFORMATEX (with its extra bytes), as in the file
		std::vector<std::uint8_t>  data;    // the xWMA packets
		std::vector<std::uint32_t> dpds;    // decoded bytes after each packet (XAUDIO2_BUFFER_WMA)
	};

	// Starts reading them on a thread of its own (once). Ready() turns true when it's done.
	void Load();
	bool Ready();

	// A recording of that kind for a man's or a woman's voice, picked at random (nullptr: none found).
	const Clip* Pick(Kind a_kind, bool a_female);
}
