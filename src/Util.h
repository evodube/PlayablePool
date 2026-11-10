#pragma once

namespace fp::util
{
	inline HWND GameWindow()
	{
		auto* data = RE::BSGraphics::GetRendererData();
		return data ? reinterpret_cast<HWND>(data->renderWindow[0].hwnd) : nullptr;
	}

	inline bool GameHasFocus()
	{
		const HWND window = GameWindow();
		return !window || ::GetForegroundWindow() == window;
	}

	inline RE::NiMatrix3 FromRows(const double a_m[3][3])
	{
		return RE::NiMatrix3(
			static_cast<float>(a_m[0][0]), static_cast<float>(a_m[0][1]), static_cast<float>(a_m[0][2]), 0.0f,
			static_cast<float>(a_m[1][0]), static_cast<float>(a_m[1][1]), static_cast<float>(a_m[1][2]), 0.0f,
			static_cast<float>(a_m[2][0]), static_cast<float>(a_m[2][1]), static_cast<float>(a_m[2][2]), 0.0f);
	}

	// Rotation whose rows are the three given axes (the game's convention: rows = the object's axes in the world).
	inline RE::NiMatrix3 FromAxes(const RE::NiPoint3& a_x, const RE::NiPoint3& a_y, const RE::NiPoint3& a_z)
	{
		return RE::NiMatrix3(
			a_x.x, a_x.y, a_x.z, 0.0f,
			a_y.x, a_y.y, a_y.z, 0.0f,
			a_z.x, a_z.y, a_z.z, 0.0f);
	}

	// A base object from a plugin by its form ID within that plugin (load order bits are ignored).
	// any form from a plugin, by its ID within that plugin (works for light plugins)
	inline RE::TESForm* ResolveAnyForm(std::string_view a_plugin, std::uint32_t a_rawID)
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		auto* file = dh ? dh->LookupModByName(a_plugin) : nullptr;
		if (!file || file->compileIndex == 0xFF) {
			return nullptr;
		}
		RE::TESFormID id;
		if (file->compileIndex == 0xFE) {  // light plugin
			id = (0xFEu << 24) | (static_cast<std::uint32_t>(file->smallFileCompileIndex) << 12) | (a_rawID & 0xFFF);
		} else {
			id = (static_cast<std::uint32_t>(file->compileIndex) << 24) | (a_rawID & 0xFFFFFF);
		}
		return RE::TESForm::GetFormByID(id);
	}

	inline RE::TESBoundObject* ResolveForm(std::string_view a_plugin, std::uint32_t a_rawID)
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		auto* file = dh ? dh->LookupModByName(a_plugin) : nullptr;
		if (!file || file->compileIndex == 0xFF) {
			return nullptr;
		}
		RE::TESFormID id;
		if (file->compileIndex == 0xFE) {  // light plugin
			id = (0xFEu << 24) | (static_cast<std::uint32_t>(file->smallFileCompileIndex) << 12) | (a_rawID & 0xFFF);
		} else {
			id = (static_cast<std::uint32_t>(file->compileIndex) << 24) | (a_rawID & 0xFFFFFF);
		}
		auto* form = RE::TESForm::GetFormByID(id);
		return form && form->IsBoundObject() ? static_cast<RE::TESBoundObject*>(form) : nullptr;
	}

	// Smallest difference between two angles in radians, in [-pi, pi].
	// at another game: held by Real Roulette (fo4roulette.esl's quest: playing roulette or poker, or on the way), so
	// not to be called over to the pool table
	inline bool AtAnotherGame(RE::Actor* a_actor)
	{
		auto* extra = a_actor ? a_actor->extraList.get() : nullptr;
		auto* aliases = extra ? extra->GetByType<RE::ExtraAliasInstanceArray>() : nullptr;
		if (!aliases) {
			return false;
		}
		for (const auto& a : aliases->aliasArray) {
			auto* file = a.quest ? a.quest->GetFile(0) : nullptr;
			if (file && _stricmp(std::string(file->GetFilename()).c_str(), "fo4roulette.esl") == 0) {
				return true;
			}
		}
		return false;
	}

	inline float AngleDiff(float a_a, float a_b)
	{
		constexpr float kPi = 3.14159265358979f;
		float d = std::fmod(a_a - a_b, 2.0f * kPi);
		if (d > kPi) {
			d -= 2.0f * kPi;
		} else if (d < -kPi) {
			d += 2.0f * kPi;
		}
		return d;
	}

	// every loaded cell: the player's, or the attached exterior grid
	template <class F>
	void ForEachLoadedCell(F&& a_fn)
	{
		std::vector<RE::TESObjectCELL*> seen;
		auto visit = [&](RE::TESObjectCELL* a_cell) {
			if (a_cell && std::ranges::find(seen, a_cell) == seen.end()) {
				seen.push_back(a_cell);
				a_fn(a_cell);
			}
		};
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			visit(player->GetParentCell());
		}
		auto* tes = RE::TES::GetSingleton();
		if (!tes) {
			return;
		}
		if (tes->interiorCell) {
			visit(tes->interiorCell);
			return;
		}
		if (auto* grid = tes->gridCells; grid && grid->gridCell) {
			const auto dim = std::min<std::uint32_t>(grid->dimension, 15);
			for (std::uint32_t i = 0; i < dim * dim; ++i) {
				auto* cell = grid->gridCell[i].cell;
				if (cell && cell->IsAttached()) {
					visit(cell);
				}
			}
		}
	}


	inline RE::NiPoint3 Normalized(const RE::NiPoint3& a_v)
	{
		const float l = std::sqrt(a_v.x * a_v.x + a_v.y * a_v.y + a_v.z * a_v.z);
		return l > 1e-6f ? RE::NiPoint3{ a_v.x / l, a_v.y / l, a_v.z / l } : RE::NiPoint3{ 0.0f, 0.0f, 0.0f };
	}
}
