// The HUD, drawn through F4SE Menu Framework when it's installed: the terminal-style scoreboard, turn tag, cue
// power meter, foul and game-over banners, ball in hand and the called pocket.
//
// The art (metal panels, balls, the two fonts with their glow) is pre-rendered PNGs in
// Data\F4SE\Plugins\fo4pool\hud (tools/hud/render_hud.py, positions in HudAtlas.h); this file only places it. It's
// laid out on a 1280x720 design screen and scaled to the real one: scale = screen height / 720 x [HUD] fScale.
// If the art can't be loaded, a plain text version is drawn instead.

#include "Hud.h"

#include "F4SEMenuFramework.h"
#include "Game.h"
#include "HudAtlas.h"
#include "MenuWatch.h"
#include "Settings.h"
#include "SettingsMenu.h"

#include <unordered_map>

namespace fp::Hud
{
	namespace
	{
		using namespace ImGuiMCP;
		namespace DL = ImGuiMCP::ImDrawListManager;

		// ------------------------------------------------------------ colours

		constexpr ImU32 Rgba(int r, int g, int b, float a = 1.0f)
		{
			return static_cast<ImU32>(r) | (static_cast<ImU32>(g) << 8) | (static_cast<ImU32>(b) << 16) |
			       (static_cast<ImU32>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f) << 24);
		}
		ImU32 Fade(ImU32 a_col, float a_alpha)
		{
			const float a = static_cast<float>(a_col >> 24) / 255.0f * std::clamp(a_alpha, 0.0f, 1.0f);
			return (a_col & 0x00FFFFFFu) | (static_cast<ImU32>(a * 255.0f + 0.5f) << 24);
		}

		struct Tint
		{
			ImU32 bright, text, dim, off, glow;
		};
		constexpr Tint kGreen{ Rgba(166, 255, 166), Rgba(134, 255, 134), Rgba(95, 174, 102), Rgba(79, 143, 86), Rgba(80, 255, 110, 0.55f) };
		constexpr Tint kRed{ Rgba(255, 116, 102), Rgba(255, 122, 110), Rgba(231, 164, 156), Rgba(160, 80, 70), Rgba(255, 60, 40, 0.6f) };

		// ------------------------------------------------------------ the art

		struct Tex
		{
			ImTextureID id{ nullptr };
			bool        tried{ false };
		};
		std::unordered_map<std::string, Tex> g_textures;
		bool                                 g_artMissing{ false };

		ImTextureID Texture(const char* a_file)
		{
			auto& t = g_textures[a_file];
			if (!t.tried) {
				t.tried = true;
				for (const char* dir : { "Data\\F4SE\\Plugins\\fo4pool\\hud\\", "F4SE\\Plugins\\fo4pool\\hud\\" }) {
					t.id = F4SEMenuFramework::LoadTexture(std::string(dir) + a_file);
					if (t.id) {
						break;
					}
				}
				if (!t.id) {
					g_artMissing = true;
					log::warn("HUD: could not load {}; the plain HUD is used", a_file);
				}
			}
			return t.id;
		}

		// the design screen mapped onto the real one
		struct View
		{
			ImDrawList* dl;
			ImVec2      origin;  // the design screen's top-left
			float       s;       // pixels per design unit
			float       w, h;    // the design screen's size in design units (w grows on wide screens)

			ImVec2 P(float a_x, float a_y) const { return { origin.x + a_x * s, origin.y + a_y * s }; }
		};

		// a panel image with its top-left (of the visible frame, not the shadow padding) at a_x, a_y
		void Panel(const View& a_v, const HudAtlas::Panel& a_p, float a_x, float a_y, float a_alpha = 1.0f, float a_size = 0.0f)
		{
			if (auto id = Texture(a_p.file)) {
				const float k = a_size > 0.0f ? a_size / (a_p.w - 2.0f * a_p.pad) : 1.0f;  // optional resize (balls)
				const float x = a_x - a_p.pad * k;
				const float y = a_y - a_p.pad * k;
				DL::AddImage(a_v.dl, id, a_v.P(x, y), a_v.P(x + a_p.w * k, y + a_p.h * k), ImVec2{ 0, 0 }, ImVec2{ 1, 1 },
					Rgba(255, 255, 255, a_alpha));
			}
		}

		const HudAtlas::Panel& BallPanel(int a_n)
		{
			static constexpr std::array<const HudAtlas::Panel*, 16> balls{ &HudAtlas::k_ball0, &HudAtlas::k_ball1, &HudAtlas::k_ball2,
				&HudAtlas::k_ball3, &HudAtlas::k_ball4, &HudAtlas::k_ball5, &HudAtlas::k_ball6, &HudAtlas::k_ball7, &HudAtlas::k_ball8,
				&HudAtlas::k_ball9, &HudAtlas::k_ball10, &HudAtlas::k_ball11, &HudAtlas::k_ball12, &HudAtlas::k_ball13, &HudAtlas::k_ball14,
				&HudAtlas::k_ball15 };
			return *balls[std::clamp(a_n, 0, 15)];
		}

		void Ball(const View& a_v, int a_n, float a_x, float a_y, float a_size, bool a_potted)
		{
			Panel(a_v, BallPanel(a_n), a_x, a_y, a_potted ? 0.25f : 1.0f, a_size);
		}

		// ------------------------------------------------------------ text

		const HudAtlas::Glyph* FindGlyph(const HudAtlas::Font& a_f, std::uint32_t a_code)
		{
			for (int i = 0; i < a_f.count; ++i) {
				if (a_f.glyphs[i].code == a_code) {
					return &a_f.glyphs[i];
				}
			}
			return a_code != '?' ? FindGlyph(a_f, '?') : nullptr;
		}

		std::vector<std::uint32_t> Decode(std::string_view a_text)
		{
			std::vector<std::uint32_t> out;
			for (std::size_t i = 0; i < a_text.size();) {
				const auto c = static_cast<unsigned char>(a_text[i]);
				std::uint32_t cp = c;
				int           extra = 0;
				if (c >= 0xF0) {
					cp = c & 0x07;
					extra = 3;
				} else if (c >= 0xE0) {
					cp = c & 0x0F;
					extra = 2;
				} else if (c >= 0xC0) {
					cp = c & 0x1F;
					extra = 1;
				}
				++i;
				for (int k = 0; k < extra && i < a_text.size(); ++k, ++i) {
					cp = (cp << 6) | (static_cast<unsigned char>(a_text[i]) & 0x3F);
				}
				out.push_back(cp);
			}
			return out;
		}

		// width in design units of a_text at a_size design units, with a_spacing between letters
		float TextWidth(const HudAtlas::Font& a_f, std::string_view a_text, float a_size, float a_spacing)
		{
			const float k = a_size / a_f.size;
			float       w = 0.0f;
			const auto  codes = Decode(a_text);
			for (std::size_t i = 0; i < codes.size(); ++i) {
				if (const auto* g = FindGlyph(a_f, codes[i])) {
					w += g->advance * k + (i + 1 < codes.size() ? a_spacing : 0.0f);
				}
			}
			return w;
		}

		enum class Align
		{
			kLeft,
			kCentre,
			kRight
		};

		// a_y is the top of the line (the font's ascent sits below it); a_glow 0 = none
		void Print(const View& a_v, const HudAtlas::Font& a_f, std::string_view a_text, float a_x, float a_y, float a_size, ImU32 a_col,
			ImU32 a_glow, float a_spacing = 0.0f, Align a_align = Align::kLeft)
		{
			auto sharp = Texture(a_f.file);
			auto glow = (a_glow >> 24) ? Texture(a_f.glowFile) : nullptr;
			if (!sharp) {
				return;
			}
			const float k = a_size / a_f.size;
			const float width = TextWidth(a_f, a_text, a_size, a_spacing);
			float       x = a_align == Align::kLeft ? a_x : a_align == Align::kCentre ? a_x - width * 0.5f : a_x - width;
			// the font's line box is ascent + descent tall; centre the caps a bit lower like CSS does
			const float y = a_y;
			for (const auto code : Decode(a_text)) {
				const auto* g = FindGlyph(a_f, code);
				if (!g) {
					continue;
				}
				if (code != ' ') {
					const ImVec2 p0 = a_v.P(x + g->xoff * k, y + g->yoff * k);
					const ImVec2 p1 = a_v.P(x + (g->xoff + g->w) * k, y + (g->yoff + g->h) * k);
					const ImVec2 uv0{ g->x / a_f.atlasW, g->y / a_f.atlasH };
					const ImVec2 uv1{ (g->x + g->w) / a_f.atlasW, (g->y + g->h) / a_f.atlasH };
					if (glow) {
						DL::AddImage(a_v.dl, glow, p0, p1, uv0, uv1, a_glow);
					}
					DL::AddImage(a_v.dl, sharp, p0, p1, uv0, uv1, a_col);
				}
				x += g->advance * k + a_spacing;
			}
		}

		const HudAtlas::Font& Title() { return HudAtlas::k_title; }
		const HudAtlas::Font& Mono() { return HudAtlas::k_mono; }

		std::string Upper(std::string a_s)
		{
			for (auto& c : a_s) {
				c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			}
			return a_s;
		}

		// ------------------------------------------------------------ shapes

		void Chamfer(const View& a_v, float a_x, float a_y, float a_w, float a_h, float a_c, ImU32 a_col)
		{
			const std::array<ImVec2, 8> pts{ a_v.P(a_x + a_c, a_y), a_v.P(a_x + a_w - a_c, a_y), a_v.P(a_x + a_w, a_y + a_c),
				a_v.P(a_x + a_w, a_y + a_h - a_c), a_v.P(a_x + a_w - a_c, a_y + a_h), a_v.P(a_x + a_c, a_y + a_h), a_v.P(a_x, a_y + a_h - a_c),
				a_v.P(a_x, a_y + a_c) };
			DL::AddConvexPolyFilled(a_v.dl, pts.data(), static_cast<int>(pts.size()), a_col);
		}

		ImU32 Hsl(float a_h, float a_s, float a_l, float a_a = 1.0f)
		{
			const float c = (1.0f - std::abs(2.0f * a_l - 1.0f)) * a_s;
			const float hp = std::fmod(a_h / 60.0f, 6.0f);
			const float x = c * (1.0f - std::abs(std::fmod(hp, 2.0f) - 1.0f));
			float r = 0, g = 0, b = 0;
			if (hp < 1) { r = c; g = x; }
			else if (hp < 2) { r = x; g = c; }
			else if (hp < 3) { g = c; b = x; }
			else if (hp < 4) { g = x; b = c; }
			else if (hp < 5) { r = x; b = c; }
			else { r = c; b = x; }
			const float m = a_l - c * 0.5f;
			return Rgba(static_cast<int>((r + m) * 255), static_cast<int>((g + m) * 255), static_cast<int>((b + m) * 255), a_a);
		}

		// ------------------------------------------------------------ the pieces

		std::string KeyLabel(int a_vk)
		{
			auto name = Upper(SettingsMenu::KeyName(a_vk));
			if (name.rfind("NUMPAD ", 0) == 0) {
				name = "NUM " + name.substr(7);
			}
			return name;
		}

		// a player's name in capitals (yours, an NPC's, "PLAYER 2"), cut to fit the panel
		std::string NameLabel(const Game::HudState& a_st, int a_player, std::size_t a_max)
		{
			std::string n = a_st.names[a_player].empty() ? std::format("PLAYER {}", a_player + 1) : a_st.names[a_player];
			if (n.size() > a_max) {
				n = n.substr(0, a_max - 1) + ".";
			}
			return n;
		}

		// 9-ball: the players (or SOLO 9-BALL) above and below one row of the nine balls, the one to hit first marked
		void NineBoard(const View& a_v, const Game::HudState& a_st, float x0, float y0)
		{
			const float cx = x0 + 29.0f;
			const float rightX = x0 + 380 - 29;
			auto nameRow = [&](int p, float row) {
				const bool  up = !a_st.over && a_st.shooter == p;
				std::string name = a_st.names[p].empty() ? std::format("PLAYER {}", p + 1) : a_st.names[p];
				const std::string right = a_st.stats[p];
				const float room = rightX - (right.empty() ? 0.0f : TextWidth(Mono(), right, 13, 0.0f) + 14.0f) - (cx + 18);
				float       size = 19.0f;
				if (const float w = TextWidth(Title(), name, 19.0f, 1.5f); w > room && room > 0.0f) {
					size = std::max(10.0f, 19.0f * room / w);
				}
				if (up) {
					DL::AddTriangleFilled(a_v.dl, a_v.P(cx, row + 6), a_v.P(cx + 10, row + 12), a_v.P(cx, row + 18), kGreen.text);
				}
				Print(a_v, Title(), name, cx + 18, row - 2 + (19.0f - size) * 0.5f, size, up ? kGreen.text : kGreen.off, up ? kGreen.glow : 0,
					1.5f * size / 19.0f);
				if (!right.empty()) {
					Print(a_v, Mono(), right, rightX, row + 1, 13, kGreen.dim, 0, 0.0f, Align::kRight);
				}
			};
			const float row0 = y0 + 25.0f;
			const float row1 = y0 + 25.0f + 92.0f;
			int         left = 0;
			for (int n = 1; n <= 9; ++n) {
				left += a_st.onTable[n];
			}
			if (a_st.solo) {
				DL::AddTriangleFilled(a_v.dl, a_v.P(cx, row0 + 6), a_v.P(cx + 10, row0 + 12), a_v.P(cx, row0 + 18), kGreen.text);
				Print(a_v, Title(), "SOLO 9-BALL", cx + 18, row0 - 2, 19, kGreen.text, kGreen.glow, 1.5f);
				Print(a_v, Mono(), std::format("{} LEFT", left), rightX, row0 + 1, 13, kGreen.dim, 0, 0.0f, Align::kRight);
			} else {
				nameRow(0, row0);
			}
			// the nine, smaller to fit; a mark under the one to hit first
			for (int i = 0; i < 9; ++i) {
				const int   n = i + 1;
				const float bx = cx + 18 + i * 34.0f;
				Ball(a_v, n, bx, row0 + 34, 30, !a_st.onTable[n]);
				if (n == a_st.lowest && !a_st.over) {
					DL::AddTriangleFilled(a_v.dl, a_v.P(bx + 15, row0 + 67), a_v.P(bx + 9, row0 + 75), a_v.P(bx + 21, row0 + 75), kGreen.text);
				}
			}
			if (a_st.solo) {
				Print(a_v, Mono(), a_st.lowest > 0 ? std::format("HIT THE {} FIRST · THE 9 WINS", a_st.lowest) : std::string("THE 9 WINS"), cx + 18, row1 + 4,
					13, kGreen.dim, 0, 1.0f);
			} else {
				nameRow(1, row1);
				Print(a_v, Mono(), a_st.lowest > 0 ? std::format("THE {} FIRST · THE 9 WINS", a_st.lowest) : std::string("THE 9 WINS"), cx + 18, row1 + 34,
					13, kGreen.dim, 0, 1.0f);
			}
		}

		void Scoreboard(const View& a_v, const Game::HudState& a_st)
		{
			constexpr float x0 = 28.0f, y0 = 28.0f;
			Panel(a_v, HudAtlas::k_score, x0, y0);
			if (a_st.nine) {
				NineBoard(a_v, a_st, x0, y0);
				return;
			}
			const float cx = x0 + 29.0f;
			if (a_st.solo) {
				// Solo Play: the 14, solids on top, stripes below, and the 8 waiting at the end
				const int left = a_st.remaining[0];
				for (int p = 0; p < 2; ++p) {
					const float row = y0 + 25.0f + p * 92.0f;
					if (p == 0) {
						DL::AddTriangleFilled(a_v.dl, a_v.P(cx, row + 6), a_v.P(cx + 10, row + 12), a_v.P(cx, row + 18), kGreen.text);
						Print(a_v, Title(), "SOLO PLAY", cx + 18, row - 2, 19, kGreen.text, kGreen.glow, 1.5f);
						Print(a_v, Mono(), std::format("{} LEFT", left), x0 + 380 - 29, row + 1, 13, kGreen.dim, 0, 0.0f, Align::kRight);
					} else {
						const bool onEight = left == 0;
						Print(a_v, Title(), "THEN THE 8", cx + 18, row - 2, 19, onEight ? kGreen.text : kGreen.off, onEight ? kGreen.glow : 0, 1.5f);
						Ball(a_v, 8, x0 + 380 - 29 - 24, row - 2, 24, !a_st.onTable[8]);
					}
					for (int i = 0; i < 7; ++i) {
						const int n = (p == 0 ? 1 : 9) + i;
						Ball(a_v, n, cx + 18 + i * 43.0f, row + 33, 36, !a_st.onTable[n]);
					}
				}
				return;
			}
			for (int p = 0; p < 2; ++p) {
				const float row = y0 + 25.0f + p * 92.0f;
				const bool  up = !a_st.over && a_st.shooter == p;
				const char* group = a_st.group[p] == 1 ? "SOLIDS" : a_st.group[p] == 2 ? "STRIPES" : "OPEN";
				const std::string name = std::format("{} · {}", NameLabel(a_st, p, 14), group);
				if (up) {
					DL::AddTriangleFilled(a_v.dl, a_v.P(cx, row + 6), a_v.P(cx + 10, row + 12), a_v.P(cx, row + 18), kGreen.text);
				}
				// their record and skill (against an NPC, or two NPCs); otherwise how many they've left. The name is drawn
				// smaller when it would run into that
				const int         left = a_st.group[p] == 0 ? 7 : a_st.remaining[p];
				const std::string right = a_st.stats[p].empty() ? std::format("{} LEFT", left) : a_st.stats[p];
				const float       rightX = x0 + 380 - 29;
				const float       room = rightX - TextWidth(Mono(), right, 13, 0.0f) - 14.0f - (cx + 18);
				float             size = 19.0f;
				if (const float w = TextWidth(Title(), name, 19.0f, 1.5f); w > room && room > 0.0f) {
					size = std::max(10.0f, 19.0f * room / w);
				}
				Print(a_v, Title(), name, cx + 18, row - 2 + (19.0f - size) * 0.5f, size, up ? kGreen.text : kGreen.off, up ? kGreen.glow : 0,
					1.5f * size / 19.0f);
				Print(a_v, Mono(), right, rightX, row + 1, 13, kGreen.dim, 0, 0.0f, Align::kRight);
				// the group's balls; with the table open, the solids on top and the stripes below
				const int first = a_st.group[p] == 2 ? 9 : a_st.group[p] == 1 ? 1 : (p == 0 ? 1 : 9);
				for (int i = 0; i < 7; ++i) {
					const int n = first + i;
					Ball(a_v, n, cx + 18 + i * 43.0f, row + 33, 36, !a_st.onTable[n]);
				}
			}
		}

		[[maybe_unused]] void TurnTag(const View& a_v, const Game::HudState& a_st)
		{
			const float x = a_v.w - 28.0f - 300.0f, y = 28.0f;
			Panel(a_v, HudAtlas::k_tag, x, y);
			std::string title;
			std::string sub;
			if (a_st.over) {
				title = "GAME OVER";
				sub = KeyLabel(Settings::Get().keyRack) + " TO RACK AGAIN";
			} else if (a_st.solo) {
				title = "SOLO PLAY";
				sub = a_st.breakShot ? "BREAK" : a_st.ballInHand ? "BALL IN HAND" : a_st.onEight ? "SHOOTING THE 8" : std::format("{} TO GO", a_st.remaining[0]);
			} else {
				title = a_st.shooter == 0 && !a_st.npcGame ? std::string("YOUR SHOT") : NameLabel(a_st, a_st.shooter, 20) + "'S SHOT";
				const char* group = a_st.group[a_st.shooter] == 1 ? "SOLIDS" : a_st.group[a_st.shooter] == 2 ? "STRIPES" : "";
				if (a_st.breakShot) {
					sub = "BREAK";
				} else if (a_st.ballInHand) {
					sub = "BALL IN HAND";
				} else if (a_st.onEight) {
					sub = "SHOOTING THE 8";
				} else if (*group) {
					sub = std::format("{} · {} LEFT", group, a_st.remaining[a_st.shooter]);
				} else {
					sub = "OPEN TABLE";
				}
			}
			// a long name is drawn smaller, so it stays inside the tag: nothing wider than "HEATHER'S SHOT" at full size
			float size = 30.0f;
			const float kFits = TextWidth(Title(), "HEATHER'S SHOT", 30.0f, 3.0f);
			if (const float w = TextWidth(Title(), title, 30.0f, 3.0f); w > kFits) {
				size = std::max(14.0f, 30.0f * kFits / w);
			}
			Print(a_v, Title(), title, x + 150, y + 10 + (30.0f - size) * 0.5f, size, kGreen.bright, kGreen.glow, 3.0f * size / 30.0f, Align::kCentre);
			Print(a_v, Mono(), sub, x + 150, y + 44, 13, kGreen.dim, 0, 1.0f, Align::kCentre);
		}

		void Log(const View& a_v, const Game::HudState& a_st)
		{
			float y = 28.0f + 236.0f + 10.0f;
			for (std::size_t i = 0; i < a_st.log.size(); ++i) {
				if (a_st.log[i].empty()) {
					continue;
				}
				const bool latest = i + 1 == a_st.log.size();
				const bool foul = a_st.log[i].rfind("Foul", 0) == 0;
				const Tint& t = foul ? kRed : kGreen;
				std::string line = "> " + Upper(a_st.log[i]);
				if (line.size() > 46) {
					line = line.substr(0, 43) + "...";
				}
				Print(a_v, Mono(), line, 34, y, 14, latest ? t.text : t.dim, latest ? Fade(t.glow, 0.6f) : 0);
				if (latest && std::fmod(GetTime(), 1.0) < 0.55) {  // the blinking cursor
					const float w = TextWidth(Mono(), line, 14, 0.0f);
					DL::AddRectFilled(a_v.dl, a_v.P(38 + w, y + 3), a_v.P(47 + w, y + 18), t.text, 0.0f, 0);
				}
				y += 21.0f;
			}
		}

		void Meter(const View& a_v, const Game::HudState& a_st)
		{
			const float right = a_v.w - 34.0f;
			const float x = right - 132.0f + 10.0f;  // the frame inside the 132-wide column
			const float y = 150.0f + 24.0f;
			const float alpha = a_st.charging ? 1.0f : 0.55f;
			Print(a_v, Title(), "CUE POWER", right - 66.0f, 150.0f - 2.0f, 16, Fade(kGreen.text, alpha), Fade(kGreen.glow, alpha), 2.0f, Align::kCentre);
			Panel(a_v, HudAtlas::k_meter, x, y, alpha);

			// the tube: 16 segments from green to red
			constexpr int count = 16;
			const float   p = a_st.charging ? std::clamp(a_st.power, 0.0f, 1.0f) : 0.0f;
			const int     lit = static_cast<int>(std::round(p * count));
			const float   tx = x + 22 + 6, ty = y + 20 + 6, tw = 58 - 12, th = 352 - 12;
			const float   seg = (th - 3.0f * (count - 1)) / count;
			for (int i = 0; i < count; ++i) {
				const float hue = 120.0f - 118.0f * i / (count - 1);
				const float y1 = ty + th - (i + 1) * seg - i * 3.0f;
				const ImVec2 a = a_v.P(tx, y1), b = a_v.P(tx + tw, y1 + seg), m = a_v.P(tx + tw * 0.5f, y1 + seg);
				if (i < lit) {
					DL::AddRectFilled(a_v.dl, a_v.P(tx - 3, y1 - 2), a_v.P(tx + tw + 3, y1 + seg + 2), Hsl(hue, 1.0f, 0.55f, 0.28f * alpha), 3.0f * a_v.s, 0);
					DL::AddRectFilledMultiColor(a_v.dl, a, ImVec2{ m.x, b.y }, Hsl(hue, 0.9f, 0.40f, alpha), Hsl(hue, 0.95f, 0.58f, alpha),
						Hsl(hue, 0.95f, 0.58f, alpha), Hsl(hue, 0.9f, 0.40f, alpha));
					DL::AddRectFilledMultiColor(a_v.dl, ImVec2{ m.x, a.y }, b, Hsl(hue, 0.95f, 0.58f, alpha), Hsl(hue, 0.9f, 0.42f, alpha),
						Hsl(hue, 0.9f, 0.42f, alpha), Hsl(hue, 0.95f, 0.58f, alpha));
				} else {
					DL::AddRectFilled(a_v.dl, a, b, Hsl(hue, 0.6f, 0.4f, 0.13f * alpha), 2.0f * a_v.s, 0);
				}
			}
			Panel(a_v, HudAtlas::k_glass, x + 22, y + 20, alpha);
			// the level marker
			const float my = y + 20 + 6 + 340.0f * (1.0f - p);
			DL::AddTriangleFilled(a_v.dl, a_v.P(x + 84, my), a_v.P(x + 102, my - 11), a_v.P(x + 102, my + 11), Fade(kGreen.text, alpha));
			Print(a_v, Mono(), std::format("{}%", static_cast<int>(p * 100.0f + 0.5f)), right - 66.0f, y + 392 + 6, 26, Fade(kGreen.text, alpha),
				Fade(kGreen.glow, alpha), 0.0f, Align::kCentre);
		}

		void Keys(const View& a_v, const Game::HudState& a_st)
		{
			const auto& set = Settings::Get();
			const std::string rack = KeyLabel(set.keyRack), clear = KeyLabel(set.keyClear), call = KeyLabel(a_st.activateVk),
							  forfeit = KeyLabel(set.keyForfeit);
			std::vector<std::pair<std::string, std::string>> keys;
			if (a_st.pad) {
				// the controller's buttons
				const bool squeeze = set.padPowerMode == 1;
				const char* power = squeeze ? "SQUEEZE FOR POWER · LET GO" : "HOLD · PULL R-STICK BACK";
				if (a_st.placing) {
					keys = { { "L-STICK", "MOVE CUE BALL" }, { "A", "SET IT DOWN" }, { "B", "LEAVE TABLE" } };
				} else if (a_st.over) {
					keys = { { "D-UP", "RACK AGAIN" }, { "D-DOWN", "CLEAR TABLE" }, { "B", "LEAVE TABLE" } };
				} else if (a_st.calling) {
					keys = { { "RT", power }, { "D-PAD", "CALL THE POCKET" }, { "Y", "OVERHEAD" }, { "B", "LEAVE TABLE" } };
				} else {
					keys = { { "RT", power }, { "Y", a_st.overhead ? "CUE BALL VIEW" : "OVERHEAD" }, { "B", "LEAVE TABLE" } };
				}
				if (a_st.noOverhead && !a_st.overhead) {
					std::erase_if(keys, [](const auto& k) { return k.first == "Y"; });
				}
				if (!a_st.over) {
					keys.push_back({ "HOLD BACK", "FORFEIT" });
				}
			} else if (a_st.placing) {
				keys = { { "MOUSE", "MOVE CUE BALL" }, { "LMB", "SET IT DOWN" }, { call, "LEAVE TABLE" } };
			} else if (a_st.over) {
				keys = { { rack, "RACK AGAIN" }, { clear, "CLEAR TABLE" }, { call, "LEAVE TABLE" } };
			} else if (a_st.calling) {
				keys = { { "LMB", "HOLD · PULL BACK FOR POWER" }, { "AIM 2S", "CALLS THE POCKET" }, { "RMB", "OVERHEAD" }, { call, "LEAVE TABLE" } };
			} else {
				keys = { { "LMB", "HOLD · PULL BACK FOR POWER" }, { "RMB", a_st.overhead ? "CUE BALL VIEW" : "OVERHEAD" }, { call, "LEAVE TABLE" } };
			}
			if (a_st.noOverhead && !a_st.overhead) {
				std::erase_if(keys, [](const auto& k) { return std::string_view(k.first) == "RMB"; });  // no top view against this one
			}
			if (!a_st.pad && !a_st.over && set.keyForfeit > 0) {
				keys.push_back({ forfeit, "FORFEIT" });
			}
			// measure, then centre the row
			float total = 0.0f;
			std::vector<float> capW;
			for (auto& [k, label] : keys) {
				capW.push_back(std::max(44.0f, TextWidth(Mono(), k, 14, 0.0f) + 18.0f));
				total += capW.back() + 10.0f + TextWidth(Title(), label, 15, 1.5f);
			}
			total += 26.0f * (keys.size() - 1);
			float       x = a_v.w * 0.5f - total * 0.5f;
			const float y = a_v.h - 18.0f - 43.0f;
			for (std::size_t i = 0; i < keys.size(); ++i) {
				Chamfer(a_v, x, y, capW[i], 30, 6, Rgba(42, 45, 43));
				Chamfer(a_v, x + 1, y + 1, capW[i] - 2, 28, 5, Rgba(78, 83, 79));
				DL::AddRectFilledMultiColor(a_v.dl, a_v.P(x + 6, y + 15), a_v.P(x + capW[i] - 6, y + 28), Rgba(58, 62, 59, 0.0f), Rgba(58, 62, 59, 0.0f),
					Rgba(42, 45, 43, 0.9f), Rgba(42, 45, 43, 0.9f));
				DL::AddLine(a_v.dl, a_v.P(x + 6, y + 1.5f), a_v.P(x + capW[i] - 6, y + 1.5f), Rgba(255, 255, 255, 0.25f), a_v.s);
				Print(a_v, Mono(), keys[i].first.c_str(), x + capW[i] * 0.5f, y + 5, 14, Rgba(233, 236, 231), 0, 0.0f, Align::kCentre);
				x += capW[i] + 10.0f;
				Print(a_v, Title(), keys[i].second, x, y + 4, 15, kGreen.text, Fade(kGreen.glow, 0.7f), 1.5f);
				x += TextWidth(Title(), keys[i].second, 15, 1.5f) + 26.0f;
			}
		}

		void BallInHand(const View& a_v)
		{
			const float x = 28.0f, y = a_v.h - 92.0f - 104.0f;
			Panel(a_v, HudAtlas::k_small, x, y);
			DL::AddCircleFilled(a_v.dl, a_v.P(x + 56, y + 52), 34 * a_v.s, Rgba(140, 255, 150, 0.12f), 48);
			Ball(a_v, 0, x + 26, y + 22, 60, false);
			DL::AddCircleFilled(a_v.dl, a_v.P(x + 56, y + 52), 4 * a_v.s, Rgba(26, 26, 26), 16);
			Print(a_v, Title(), "CUE BALL", x + 104, y + 22, 24, kGreen.bright, kGreen.glow, 2.0f);
			Print(a_v, Title(), "IN HAND", x + 104, y + 50, 24, kGreen.bright, kGreen.glow, 2.0f);
		}

		void CalledPocket(const View& a_v, const Game::HudState& a_st)
		{
			const float x = 28.0f, y = a_v.h - 92.0f - 172.0f;
			Panel(a_v, HudAtlas::k_call, x, y);
			Ball(a_v, 8, x + 26, y + 38, 64, false);
			Print(a_v, Title(), "ON THE 8", x + 58, y + 110, 14, kGreen.bright, kGreen.glow, 2.0f, Align::kCentre);
			const float mx = x + 128, my = y + 26;
			Print(a_v, Mono(), "CALLED POCKET", mx, my - 2, 12, kGreen.dim, 0, 2.0f);
			// the little table: pockets 0..3 corners (head-left, head-right, foot-left, foot-right), 4..5 sides
			const float tx = mx + 8, ty = my + 22, tw = 190, th = 76;
			DL::AddRect(a_v.dl, a_v.P(tx, ty), a_v.P(tx + tw, ty + th), kGreen.off, 3.0f * a_v.s, 0, 2.0f * a_v.s);
			const std::array<ImVec2, 6> pockets{ ImVec2{ tx, ty + th }, ImVec2{ tx, ty }, ImVec2{ tx + tw, ty + th }, ImVec2{ tx + tw, ty },
				ImVec2{ tx + tw * 0.5f, ty + th }, ImVec2{ tx + tw * 0.5f, ty } };
			for (int p = 0; p < 6; ++p) {
				const ImVec2 c = a_v.P(pockets[p].x, pockets[p].y);
				if (p == a_st.calledPocket) {
					DL::AddCircleFilled(a_v.dl, c, 14 * a_v.s, Rgba(80, 255, 110, 0.25f), 24);
					DL::AddCircleFilled(a_v.dl, c, 8 * a_v.s, Rgba(109, 255, 122), 24);
				} else {
					DL::AddCircleFilled(a_v.dl, c, 6 * a_v.s, Rgba(10, 16, 12), 16);
					DL::AddCircle(a_v.dl, c, 6 * a_v.s, kGreen.off, 16, 2.0f * a_v.s);
				}
			}
			static constexpr std::array<const char*, 6> names{ "HEAD-LEFT CORNER", "HEAD-RIGHT CORNER", "FOOT-LEFT CORNER", "FOOT-RIGHT CORNER",
				"LEFT SIDE", "RIGHT SIDE" };
			Print(a_v, Title(), a_st.calledPocket >= 0 ? names[a_st.calledPocket] : "AIM AT A POCKET", mx, ty + th + 10, 15, kGreen.bright,
				kGreen.glow, 1.5f);
		}

		void CentreBanner(const View& a_v, const Game::HudState& a_st)
		{
			// fouls: blink three times, hold, then fade; game over stays up
			float alpha = 1.0f;
			if (a_st.banner == 1) {
				constexpr float kBlink = 0.18f, kHold = 2.6f, kFade = 0.5f;
				const float     t = a_st.bannerAge;
				if (t < kBlink * 6.0f) {
					alpha = static_cast<int>(t / kBlink) % 2 == 0 ? 1.0f : 0.08f;
				} else if (t > kBlink * 6.0f + kHold) {
					alpha = 1.0f - (t - kBlink * 6.0f - kHold) / kFade;
				}
				if (alpha <= 0.0f) {
					return;
				}
			}
			const bool  green = a_st.banner == 2;
			const Tint& t = green ? kGreen : kRed;
			const float x = a_v.w * 0.5f - 290.0f, y = 330.0f;
			Panel(a_v, green ? HudAtlas::k_banner_green : HudAtlas::k_banner_red, x, y, alpha);
			if (green) {
				Ball(a_v, 8, x + 290 - 42, y + 26, 84, false);
			}
			Print(a_v, Title(), a_st.bannerTitle, x + 290, y + (green ? 120 : 146), green ? 60 : 52, Fade(t.bright, alpha), Fade(t.glow, alpha),
				green ? 6.0f : 4.0f, Align::kCentre);
			Print(a_v, Mono(), a_st.bannerSub, x + 290, y + (green ? 204 : 216), 15, Fade(t.dim, alpha), 0, 3.0f, Align::kCentre);
		}

		// ------------------------------------------------------------ plain fallback

		constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
		                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

		void DrawPlain(const Game::HudState& a_st, ImGuiViewport* a_viewport)
		{
			SetNextWindowPos(ImVec2{ a_viewport->Pos.x + 24.0f, a_viewport->Pos.y + 24.0f }, ImGuiCond_Always, ImVec2{ 0.0f, 0.0f });
			SetNextWindowBgAlpha(0.6f);
			if (Begin("##fo4pool_score", nullptr, kFlags)) {
				for (int p = 0; p < 2; ++p) {
					const char* g = a_st.group[p] == 1 ? "solids" : a_st.group[p] == 2 ? "stripes" : "open table";
					Text("%s %s   %s, %d left", !a_st.over && a_st.shooter == p ? ">" : " ", p == 0 ? "You" : (a_st.opponent.empty() ? "Player 2" : a_st.opponent.c_str()), g, a_st.remaining[p]);
				}
				if (!a_st.message.empty()) {
					Separator();
					TextUnformatted(a_st.message.c_str());
				}
				if (a_st.aiming && a_st.charging) {
					char label[16];
					std::snprintf(label, sizeof(label), "%d%%", static_cast<int>(a_st.power * 100.0f + 0.5f));
					ProgressBar(a_st.power, ImVec2{ 260.0f, 20.0f }, label);
				}
			}
			End();
		}

		// "F) PLAY POOL" under the crosshair, like the game's own activate prompt
		void Prompt(ImGuiViewport* a_viewport, const std::string& a_text)
		{
			View v;
			v.dl = GetBackgroundDrawList();
			if (!v.dl) {
				return;
			}
			v.s = std::max(0.1f, a_viewport->Size.y / 720.0f * Settings::Get().hudScale);
			v.origin = a_viewport->Pos;
			v.w = a_viewport->Size.x / v.s;
			v.h = a_viewport->Size.y / v.s;
			// a line below where the game puts its own "F) Talk", so both show
			Print(v, Title(), a_text, v.w * 0.5f + 40.0f, v.h * 0.5f + 34.0f, 20, kGreen.bright, kGreen.glow, 1.5f);
		}

		// F on a table with people by it: who to play, under the crosshair
		void PickList(ImGuiViewport* a_viewport, const Game::HudState& a_st)
		{
			View v;
			v.dl = GetBackgroundDrawList();
			if (!v.dl) {
				return;
			}
			v.s = std::max(0.1f, a_viewport->Size.y / 720.0f * Settings::Get().hudScale);
			v.origin = a_viewport->Pos;
			v.w = a_viewport->Size.x / v.s;
			v.h = a_viewport->Size.y / v.s;
			const float x = v.w * 0.5f + 40.0f;
			float       y = v.h * 0.5f + 10.0f;
			Print(v, Mono(), a_st.pickTitle.empty() ? std::string("WHO PLAYS?") : a_st.pickTitle, x, y, 13, kGreen.dim, 0, 2.0f);
			y += 22.0f;
			for (std::size_t i = 0; i < a_st.pick.size(); ++i) {
				const bool on = static_cast<int>(i) == a_st.pickSel;
				if (on) {
					DL::AddTriangleFilled(v.dl, v.P(x, y + 6), v.P(x + 10, y + 12), v.P(x, y + 18), kGreen.text);
				}
				Print(v, Title(), a_st.pick[i], x + 18, y - 2, 20, on ? kGreen.bright : kGreen.off, on ? kGreen.glow : 0, 1.5f);
				y += 24.0f;
			}
			Print(v, Mono(), a_st.pickHint, x, y + 4.0f, 12, kGreen.dim, 0, 1.0f);
		}

		// ------------------------------------------------------------

		void __stdcall Draw()
		{
			if (menus::Open(menus::kLooks)) {
				return;  // nothing of ours on screen in the face and body editor
			}
			const auto st = Game::Get().Hud();
			auto*      viewport = GetMainViewport();
			if (viewport && !st.prompt.empty() && !st.aiming && !g_artMissing) {
				Prompt(viewport, st.prompt);
			}
			if (viewport && !st.pick.empty() && !st.aiming && !g_artMissing) {
				PickList(viewport, st);
			}
			if (!viewport || !st.game || !(st.aiming || st.watching)) {
				return;  // only at the table: leaving aim mode puts the normal HUD back
			}
			if (g_artMissing) {
				DrawPlain(st, viewport);
				return;
			}
			View v;
			v.dl = GetBackgroundDrawList();
			v.s = std::max(0.1f, viewport->Size.y / 720.0f * Settings::Get().hudScale);
			v.origin = viewport->Pos;
			v.w = viewport->Size.x / v.s;
			v.h = viewport->Size.y / v.s;
			if (!v.dl) {
				return;
			}

			Scoreboard(v, st);
			Log(v, st);
			// (no turn tag: the scoreboard already marks whose shot it is)
			if (st.aiming && !st.placing && !st.over) {
				Meter(v, st);
			}
			if (st.ballInHand && !st.over) {
				BallInHand(v);
			} else if (st.aiming && st.calling && !st.over) {
				CalledPocket(v, st);
			}
			if (st.banner != 0) {
				CentreBanner(v, st);
			}
			if (st.aiming) {
				Keys(v, st);
			}
		}

		F4SEMenuFramework::Model::HudElement* g_element{ nullptr };
	}

	bool Showing()
	{
		return g_element && !g_artMissing;
	}

	void Install()
	{
		if (g_element) {
			return;
		}
		if (!F4SEMenuFramework::IsInstalled()) {
			log::warn("F4SE Menu Framework not found: no HUD on screen (the game still works, with notifications)");
			return;
		}
		g_element = F4SEMenuFramework::AddHudElement(Draw);
		log::info("HUD element registered");
		SettingsMenu::Install();
	}
}
