#include "CrowdVoices.h"

#include <thread>

namespace fp::CrowdVoices
{
	namespace
	{
		// the lines (INFO form IDs in Fallout4.esm); each has one response, recorded as <id>_1.fuz per voice
		constexpr std::array<std::uint32_t, 20> kCheers{ 0x0010C795, 0x0010C796, 0x0010C797, 0x0010C798, 0x0010C799, 0x0010C79A, 0x0010C79B,
			0x0010C79C, 0x0010C79D, 0x0010C79E, 0x0010C7A0, 0x0010C7A1, 0x0010C7A2, 0x0010C7A3, 0x0010C7A4, 0x0010C7A5, 0x0010C7A6, 0x0010C7A7,
			0x0010C7A8, 0x0010C7A9 };  // CZ_Crowd_Approval_Sounds
		constexpr std::array<std::uint32_t, 13> kBoos{ 0x0010C779, 0x0010C77A, 0x0010C77B, 0x0010C77C, 0x0010C77D, 0x0010C77E, 0x0010C77F,
			0x0010C780, 0x0010C781, 0x0010C782, 0x0010C783, 0x0010C784, 0x0010C785 };  // CZ_Crowd_Disapproval_Sounds
		constexpr std::array<std::uint32_t, 10> kLaughs{ 0x0010C76D, 0x0010C76E, 0x0010C76F, 0x0010C770, 0x0010C771, 0x0010C772, 0x0010C773,
			0x0010C774, 0x0010C776, 0x0010C777 };  // CZ_Crowd_Laughter

		constexpr std::array<const char*, 2> kVoices{ "malerough", "femalerough" };

		std::atomic<bool>                                    g_started{ false };
		std::atomic<bool>                                    g_ready{ false };
		std::array<std::array<std::vector<Clip>, 3>, 2>      g_clips;  // [female][kind]
		std::uint32_t                                        g_rng{ 0x2545F491u };

		std::string Lower(std::string a_s)
		{
			std::ranges::transform(a_s, a_s.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
			std::ranges::replace(a_s, '/', '\\');
			return a_s;
		}

		std::filesystem::path DataFolder()
		{
			wchar_t exe[MAX_PATH]{};
			::GetModuleFileNameW(nullptr, exe, MAX_PATH);
			return std::filesystem::path(exe).parent_path() / L"Data";
		}

		// a .fuz: "FUZE", version, the lip-sync's size, the lip-sync, then the audio (an xWMA RIFF)
		bool ParseFuz(const std::vector<std::uint8_t>& a_bytes, Clip& a_out)
		{
			auto u32 = [&](std::size_t o) { return o + 4 <= a_bytes.size() ? *reinterpret_cast<const std::uint32_t*>(&a_bytes[o]) : 0u; };
			if (a_bytes.size() < 12 || std::memcmp(a_bytes.data(), "FUZE", 4) != 0) {
				return false;
			}
			const std::size_t riff = 12 + static_cast<std::size_t>(u32(8));
			if (riff + 12 > a_bytes.size() || std::memcmp(&a_bytes[riff], "RIFF", 4) != 0 || std::memcmp(&a_bytes[riff + 8], "XWMA", 4) != 0) {
				return false;
			}
			std::size_t o = riff + 12;
			while (o + 8 <= a_bytes.size()) {
				const std::uint32_t size = u32(o + 4);
				const std::size_t   body = o + 8;
				if (body + size > a_bytes.size()) {
					break;
				}
				if (std::memcmp(&a_bytes[o], "fmt ", 4) == 0) {
					a_out.format.assign(a_bytes.begin() + body, a_bytes.begin() + body + size);
				} else if (std::memcmp(&a_bytes[o], "dpds", 4) == 0) {
					a_out.dpds.resize(size / 4);
					std::memcpy(a_out.dpds.data(), &a_bytes[body], a_out.dpds.size() * 4);
				} else if (std::memcmp(&a_bytes[o], "data", 4) == 0) {
					a_out.data.assign(a_bytes.begin() + body, a_bytes.begin() + body + size);
				}
				o = body + size + (size & 1);
			}
			if (a_out.format.size() < 16 || a_out.data.empty() || a_out.dpds.empty()) {
				return false;
			}
			a_out.format.resize(std::max<std::size_t>(a_out.format.size(), 18), 0);  // cbSize, if the file left it off
			return true;
		}

		// Fallout 4 - Voices*.ba2 (a general archive: versions 1, 7 and 8 share the layout) -> the wanted files' bytes
		void ReadArchive(const std::filesystem::path& a_path, std::unordered_map<std::string, std::vector<std::uint8_t>>& a_wanted)
		{
			std::ifstream file(a_path, std::ios::binary);
			if (!file) {
				return;
			}
			char          magic[4]{}, type[4]{};
			std::uint32_t version = 0, count = 0;
			std::uint64_t names = 0;
			file.read(magic, 4).read(reinterpret_cast<char*>(&version), 4).read(type, 4).read(reinterpret_cast<char*>(&count), 4).read(reinterpret_cast<char*>(&names), 8);
			if (!file || std::memcmp(magic, "BTDX", 4) != 0 || std::memcmp(type, "GNRL", 4) != 0 || (version != 1 && version != 7 && version != 8)) {
				log::warn("crowd voices: {} isn't an archive this reads (version {})", a_path.filename().string(), version);
				return;
			}
			struct Entry
			{
				std::uint32_t nameHash;
				char          ext[4];
				std::uint32_t dirHash;
				std::uint32_t flags;
				std::uint64_t offset;
				std::uint32_t packed;
				std::uint32_t size;
				std::uint32_t align;
			};
			static_assert(sizeof(Entry) == 36);
			std::vector<Entry> entries(count);
			file.read(reinterpret_cast<char*>(entries.data()), static_cast<std::streamsize>(count) * sizeof(Entry));
			file.seekg(static_cast<std::streamoff>(names));
			std::string name;
			for (std::uint32_t i = 0; i < count && file; ++i) {
				std::uint16_t length = 0;
				file.read(reinterpret_cast<char*>(&length), 2);
				name.resize(length);
				file.read(name.data(), length);
				auto it = a_wanted.find(Lower(name));
				if (it == a_wanted.end() || !it->second.empty()) {
					continue;
				}
				if (entries[i].packed != 0) {
					continue;  // compressed (voice files never are): left out
				}
				const auto here = file.tellg();
				it->second.resize(entries[i].size);
				file.seekg(static_cast<std::streamoff>(entries[i].offset));
				file.read(reinterpret_cast<char*>(it->second.data()), entries[i].size);
				if (!file) {
					it->second.clear();
					file.clear();
				}
				file.seekg(here);
			}
		}

		void LoadAll()
		{
			const auto data = DataFolder();
			// every file wanted, by its path in the game's folders
			std::unordered_map<std::string, std::vector<std::uint8_t>> wanted;
			auto                                                       path = [](const char* a_voice, std::uint32_t a_id) {
                return std::format("sound\\voice\\fallout4.esm\\{}\\{:08x}_1.fuz", a_voice, a_id);
			};
			for (const char* voice : kVoices) {
				for (auto id : kCheers) wanted[path(voice, id)];
				for (auto id : kBoos) wanted[path(voice, id)];
				for (auto id : kLaughs) wanted[path(voice, id)];
			}
			// loose files first (a voice mod's), then the archives
			for (auto& [p, bytes] : wanted) {
				std::ifstream loose(data / p, std::ios::binary);
				if (loose) {
					bytes.assign(std::istreambuf_iterator<char>(loose), std::istreambuf_iterator<char>());
				}
			}
			std::vector<std::filesystem::path> archives;
			std::error_code                    ec;
			for (const auto& e : std::filesystem::directory_iterator(data, ec)) {
				const auto n = Lower(e.path().filename().string());
				if (n.starts_with("fallout4 - voices") && n.ends_with(".ba2")) {
					archives.push_back(e.path());
				}
			}
			std::ranges::sort(archives);  // "Fallout4 - Voices.ba2" (English) before the other languages
			for (const auto& a : archives) {
				ReadArchive(a, wanted);
			}
			int found = 0, missing = 0;
			for (std::size_t female = 0; female < 2; ++female) {
				auto add = [&](Kind a_kind, auto const& a_ids) {
					for (auto id : a_ids) {
						Clip  clip;
						auto& bytes = wanted[path(kVoices[female], id)];
						if (!bytes.empty() && ParseFuz(bytes, clip)) {
							g_clips[female][static_cast<std::size_t>(a_kind)].push_back(std::move(clip));
							++found;
						} else {
							++missing;
						}
					}
				};
				add(Kind::kCheer, kCheers);
				add(Kind::kBoo, kBoos);
				add(Kind::kLaugh, kLaughs);
			}
			log::info("crowd voices: {} cheers, boos and laughs read from the game's voice files ({} not found){}", found, missing,
				archives.empty() ? " - no Fallout 4 - Voices archive in Data" : "");
			g_ready = true;
		}
	}

	void Load()
	{
		if (g_started.exchange(true)) {
			return;
		}
		std::thread([] {
			try {
				LoadAll();
			} catch (const std::exception& e) {
				log::warn("crowd voices: couldn't read them ({})", e.what());
				g_ready = true;
			}
		}).detach();
	}

	bool Ready()
	{
		return g_ready.load();
	}

	const Clip* Pick(Kind a_kind, bool a_female)
	{
		if (!g_ready) {
			return nullptr;
		}
		const auto& list = g_clips[a_female ? 1 : 0][static_cast<std::size_t>(a_kind)];
		if (list.empty()) {
			return nullptr;
		}
		g_rng ^= g_rng << 13;
		g_rng ^= g_rng >> 17;
		g_rng ^= g_rng << 5;
		return &list[g_rng % list.size()];
	}
}
