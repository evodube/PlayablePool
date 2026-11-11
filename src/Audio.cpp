// Pool sounds, played with our own XAudio2 engine (Windows' xaudio2_9) so they need no records in the plugin.
//
// Every sound is a mono 16-bit WAV. Each one is panned and made quieter with distance from the camera by hand,
// which is all a table's worth of clicks needs. Several takes per bank, picked at random with a small pitch
// change, keep repeated hits from sounding the same.

#include "Audio.h"

#include "CrowdVoices.h"
#include "Settings.h"

#include <xaudio2.h>

namespace fp::Audio
{
	namespace
	{
		constexpr std::size_t kVoices = 24;
		constexpr std::uint32_t kRate = 44100;

		struct Clip
		{
			std::vector<std::uint8_t> data;
		};

		struct Voice
		{
			IXAudio2SourceVoice*                  voice{ nullptr };
			std::chrono::steady_clock::time_point started{};
			std::vector<std::uint8_t>             cut;  // a shortened, faded copy while it plays
		};

		std::mutex                                                g_lock;
		bool                                                      g_tried{ false };
		IXAudio2*                                                 g_engine{ nullptr };
		IXAudio2MasteringVoice*                                   g_master{ nullptr };
		std::uint32_t                                             g_channels{ 2 };
		std::array<std::vector<Clip>, static_cast<std::size_t>(Bank::kCount)> g_banks;
		std::array<Voice, kVoices>                                g_voices;
		std::atomic<float>                                        g_yaw{ 0.0f };
		std::atomic<bool>                                         g_haveYaw{ false };
		std::uint32_t                                             g_rng{ 0x9E3779B9u };
		// the crowd voices (CrowdVoices.h): each its own format, so a voice of their own per sound, let go once done
		std::vector<IXAudio2SourceVoice*>                         g_crowd;

		float Random01()
		{
			g_rng ^= g_rng << 13;
			g_rng ^= g_rng >> 17;
			g_rng ^= g_rng << 5;
			return static_cast<float>(g_rng & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
		}

		std::filesystem::path SoundFolder()
		{
			wchar_t exe[MAX_PATH]{};
			::GetModuleFileNameW(nullptr, exe, MAX_PATH);
			return std::filesystem::path(exe).parent_path() / L"Data" / L"Sound" / L"fo4pool";
		}

		// mono 16-bit PCM at 44.1 kHz only (what the mod ships)
		bool LoadWav(const std::filesystem::path& a_path, Clip& a_out)
		{
			std::ifstream file(a_path, std::ios::binary);
			if (!file) {
				return false;
			}
			std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			auto u32 = [&](std::size_t o) { return o + 4 <= bytes.size() ? *reinterpret_cast<const std::uint32_t*>(&bytes[o]) : 0u; };
			auto u16 = [&](std::size_t o) { return o + 2 <= bytes.size() ? *reinterpret_cast<const std::uint16_t*>(&bytes[o]) : std::uint16_t{ 0 }; };
			if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
				return false;
			}
			bool        formatOK = false;
			std::size_t o = 12;
			while (o + 8 <= bytes.size()) {
				const std::uint32_t size = u32(o + 4);
				const std::size_t   body = o + 8;
				if (std::memcmp(&bytes[o], "fmt ", 4) == 0) {
					formatOK = u16(body) == 1 && u16(body + 2) == 1 && u32(body + 4) == kRate && u16(body + 14) == 16;
				} else if (std::memcmp(&bytes[o], "data", 4) == 0 && formatOK) {
					const std::size_t n = std::min<std::size_t>(size, bytes.size() - body);
					a_out.data.assign(bytes.begin() + body, bytes.begin() + body + n);
					return !a_out.data.empty();
				}
				o = body + size + (size & 1);
			}
			return false;
		}

		const wchar_t* BankName(Bank a_bank)
		{
			switch (a_bank) {
			case Bank::kCue:
				return L"cue";
			case Bank::kHit:
				return L"hit";
			case Bank::kCushion:
				return L"cushion";
			case Bank::kPocket:
				return L"pocket";
			case Bank::kReturn:
				return L"return";
			case Bank::kTurn:
				return L"turn";
			case Bank::kStart:
				return L"start";
			default:
				return L"";
			}
		}

		bool Start()
		{
			const HRESULT co = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			(void)co;  // already initialised in another mode is fine for XAudio2 2.9
			if (FAILED(::XAudio2Create(&g_engine, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
				log::warn("sound: XAudio2 could not start; no pool sounds");
				g_engine = nullptr;
				return false;
			}
			if (FAILED(g_engine->CreateMasteringVoice(&g_master))) {
				log::warn("sound: no audio output device; no pool sounds");
				g_engine->Release();
				g_engine = nullptr;
				return false;
			}
			XAUDIO2_VOICE_DETAILS details{};
			g_master->GetVoiceDetails(&details);
			g_channels = std::max<std::uint32_t>(1, details.InputChannels);

			WAVEFORMATEX format{};
			format.wFormatTag = WAVE_FORMAT_PCM;
			format.nChannels = 1;
			format.nSamplesPerSec = kRate;
			format.wBitsPerSample = 16;
			format.nBlockAlign = 2;
			format.nAvgBytesPerSec = kRate * 2;
			for (auto& v : g_voices) {
				if (FAILED(g_engine->CreateSourceVoice(&v.voice, &format, 0, 2.0f))) {
					v.voice = nullptr;
				}
			}
			return true;
		}

		Voice* FreeVoice()
		{
			Voice* oldest = nullptr;
			for (auto& v : g_voices) {
				if (!v.voice) {
					continue;
				}
				XAUDIO2_VOICE_STATE state{};
				v.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
				if (state.BuffersQueued == 0) {
					return &v;
				}
				if (!oldest || v.started < oldest->started) {
					oldest = &v;
				}
			}
			if (oldest) {  // all busy: cut off the oldest
				oldest->voice->Stop();
				oldest->voice->FlushSourceBuffers();
			}
			return oldest;
		}
	}

	void Init()
	{
		std::scoped_lock lock(g_lock);
		if (g_tried) {
			return;
		}
		g_tried = true;
		if (!Settings::Get().sound) {
			log::info("sound: off in the INI");
			return;
		}
		const auto folder = SoundFolder();
		int        loaded = 0;
		for (std::size_t b = 0; b < g_banks.size(); ++b) {
			for (int i = 1; i <= 16; ++i) {
				Clip clip;
				if (!LoadWav(folder / std::format(L"{}_{}.wav", BankName(static_cast<Bank>(b)), i), clip)) {
					break;
				}
				g_banks[b].push_back(std::move(clip));
				++loaded;
			}
		}
		if (loaded == 0) {
			log::warn("sound: no sounds found in {}", folder.string());
			return;
		}
		if (Start()) {
			log::info("sound: {} sounds loaded, {} output channels", loaded, g_channels);
			if (Settings::Get().reactions && Settings::Get().reactionVoices) {
				CrowdVoices::Load();  // the watchers' cheers, boos and laughs, read from the game's own voice files
			}
		}
	}

	void SetListenerYaw(float a_yaw, bool a_valid)
	{
		g_yaw = a_yaw;
		g_haveYaw = a_valid;
	}

	void PlayUI(Bank a_bank, float a_gain)
	{
		// at the listener: full volume, centred
		RE::NiPoint3 ear{};
		if (auto* camera = RE::PlayerCamera::GetSingleton(); camera && camera->cameraRoot) {
			ear = camera->cameraRoot->world.translate;
		}
		Play(a_bank, ear, a_gain);
	}

	namespace
	{
		// How loud a_pos is from the camera, panned: the left and right gains (false: too far to hear). Under g_lock.
		bool Placement(const RE::NiPoint3& a_pos, float a_gain, float& a_left, float& a_right)
		{
			const auto& s = Settings::Get();
			// where the listener is and which way they face
			RE::NiPoint3 ear = a_pos;
			float        yaw = g_yaw;
			if (auto* camera = RE::PlayerCamera::GetSingleton(); camera && camera->cameraRoot) {
				ear = camera->cameraRoot->world.translate;
			}
			if (!g_haveYaw) {
				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					yaw = player->data.angle.z;
				}
			}

			const RE::NiPoint3 to = a_pos - ear;
			const float        distance = std::sqrt(to.x * to.x + to.y * to.y + to.z * to.z);
			if (distance >= s.soundMaxDistance) {
				return false;
			}
			float gain = a_gain * s.soundVolume;
			if (distance > s.soundFullDistance) {
				gain *= s.soundFullDistance / distance;
				gain *= 1.0f - (distance - s.soundFullDistance) / (s.soundMaxDistance - s.soundFullDistance);  // reaches 0 at the max
			}
			if (gain <= 0.001f) {
				return false;
			}

			// -1 left .. 1 right, kept off the extremes; equal-power pan
			float pan = 0.0f;
			if (distance > 1.0f) {
				const float right = (to.x * std::cos(yaw) - to.y * std::sin(yaw)) / distance;
				pan = std::clamp(right, -1.0f, 1.0f) * 0.7f;
			}
			const float angle = (pan + 1.0f) * 0.25f * 3.14159265f;
			a_left = gain * std::cos(angle);
			a_right = gain * std::sin(angle);
			return true;
		}
	}

	void Play(Bank a_bank, const RE::NiPoint3& a_pos, float a_gain, float a_seconds)
	{
		const auto& s = Settings::Get();
		if (!s.sound || a_gain <= 0.001f) {
			return;
		}
		std::scoped_lock lock(g_lock);
		const auto& clips = g_banks[static_cast<std::size_t>(a_bank)];
		if (!g_engine || clips.empty()) {
			return;
		}
		float left = 0.0f, right = 0.0f;
		if (!Placement(a_pos, a_gain, left, right)) {
			return;
		}
		std::vector<float> matrix(g_channels, 0.0f);
		if (g_channels == 1) {
			matrix[0] = (left + right) * 0.7071f;
		} else {
			matrix[0] = left;   // front left
			matrix[1] = right;  // front right
		}

		Voice* v = FreeVoice();
		if (!v) {
			return;
		}
		const auto& clip = clips[static_cast<std::size_t>(Random01() * static_cast<float>(clips.size())) % clips.size()];
		const std::uint8_t* data = clip.data.data();
		std::size_t         bytes = clip.data.size();
		const std::size_t   wanted = a_seconds > 0.0f ? static_cast<std::size_t>(a_seconds * kRate) * 2 : bytes;
		if (wanted < bytes) {
			// the voice is idle (FreeVoice stopped it if it wasn't), so its copy can be replaced
			v->cut.assign(clip.data.begin(), clip.data.begin() + wanted);
			auto*             samples = reinterpret_cast<std::int16_t*>(v->cut.data());
			const std::size_t count = wanted / 2;
			const std::size_t fade = std::min<std::size_t>(count, kRate * 2 / 5);  // 0.4 s
			for (std::size_t i = 0; i < fade; ++i) {
				auto& sample = samples[count - fade + i];
				sample = static_cast<std::int16_t>(sample * (1.0f - static_cast<float>(i + 1) / static_cast<float>(fade)));
			}
			data = v->cut.data();
			bytes = v->cut.size();
		}
		XAUDIO2_BUFFER buffer{};
		buffer.Flags = XAUDIO2_END_OF_STREAM;
		buffer.AudioBytes = static_cast<UINT32>(bytes);
		buffer.pAudioData = data;
		v->voice->SetOutputMatrix(g_master, 1, g_channels, matrix.data());
		v->voice->SetVolume(1.0f);
		v->voice->SetFrequencyRatio(0.95f + 0.1f * Random01());
		if (SUCCEEDED(v->voice->SubmitSourceBuffer(&buffer))) {
			v->voice->Start();
			v->started = std::chrono::steady_clock::now();
		}
	}

	void PlayCrowd(const CrowdVoices::Clip& a_clip, const RE::NiPoint3& a_pos, float a_gain, float a_pitch)
	{
		const auto& s = Settings::Get();
		if (!s.sound || a_gain <= 0.001f) {
			return;
		}
		std::scoped_lock lock(g_lock);
		if (!g_engine) {
			return;
		}
		// the ones that are done let go
		std::erase_if(g_crowd, [](IXAudio2SourceVoice* a_voice) {
			XAUDIO2_VOICE_STATE state{};
			a_voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
			if (state.BuffersQueued == 0) {
				a_voice->DestroyVoice();
				return true;
			}
			return false;
		});
		if (g_crowd.size() >= 4) {
			return;  // a crowd's worth already
		}
		float left = 0.0f, right = 0.0f;
		if (!Placement(a_pos, a_gain, left, right)) {
			return;
		}
		const auto*          format = reinterpret_cast<const WAVEFORMATEX*>(a_clip.format.data());
		IXAudio2SourceVoice* voice = nullptr;
		if (FAILED(g_engine->CreateSourceVoice(&voice, format, 0, 2.0f)) || !voice) {
			static bool warned = false;
			if (!warned) {
				warned = true;
				log::warn("sound: a crowd voice wouldn't play (format {:04X}, {} Hz, {} channels)", format->wFormatTag, format->nSamplesPerSec, format->nChannels);
			}
			return;
		}
		const std::uint32_t in = std::max<std::uint32_t>(1, format->nChannels);
		std::vector<float>  matrix(in * g_channels, 0.0f);
		for (std::uint32_t c = 0; c < in; ++c) {  // each source channel to both sides, panned
			const float share = 1.0f / static_cast<float>(in);
			if (g_channels == 1) {
				matrix[c] = (left + right) * 0.7071f * share;
			} else {
				matrix[0 * in + c] = left * share;
				matrix[1 * in + c] = right * share;
			}
		}
		voice->SetOutputMatrix(g_master, in, g_channels, matrix.data());
		voice->SetFrequencyRatio(std::clamp(a_pitch, 0.5f, 2.0f));
		XAUDIO2_BUFFER buffer{};
		buffer.Flags = XAUDIO2_END_OF_STREAM;
		buffer.AudioBytes = static_cast<UINT32>(a_clip.data.size());
		buffer.pAudioData = a_clip.data.data();
		XAUDIO2_BUFFER_WMA wma{};
		wma.PacketCount = static_cast<UINT32>(a_clip.dpds.size());
		wma.pDecodedPacketCumulativeBytes = a_clip.dpds.data();
		if (FAILED(voice->SubmitSourceBuffer(&buffer, &wma)) || FAILED(voice->Start())) {
			voice->DestroyVoice();
			return;
		}
		g_crowd.push_back(voice);
	}
}
