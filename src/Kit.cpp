// The Pool Table Kit (fo4pool.esl, an Aid item): sold by Vadim at the Dugout Inn. Used from the Pip-Boy it unfolds into
// a full-size pool table in front of you, anywhere; a table set up that way can be packed back into a kit (hold F at it:
// the list's last entry). Which tables are kit tables, and whether the Dugout Inn's been stocked, go in the co-save.

#include "Game.h"

#include "Settings.h"
#include "Util.h"

namespace fp
{
	namespace
	{
		constexpr std::uint32_t kKitRawID = 0x837;           // fo4pool.esl fo4poolTableKit
		constexpr RE::TESFormID kTableBase = 0x0010907A;     // Fallout4.esm: SetDressing\PoolTable\PoolTable01.nif
		constexpr RE::TESFormID kDugoutVendorBox = 0x001338B6;   // Fallout4.esm VendorDCDugoutInn (the container)
		constexpr RE::TESFormID kDugoutVendorChest = 0x001338B7; // ...and its reference (ServicesDmndDugoutInn's VENC)
		constexpr std::uint32_t kKitRecordType = 0x5054424C;      // 'PTBL'
		constexpr std::uint32_t kKitRecordVersion = 1;
		constexpr float         kPlaceAhead = 150.0f;        // the table's middle, this far in front of you
		constexpr float         kTooClose = 260.0f;          // not on top of another pool table

		std::mutex                        g_kitMutex;
		std::unordered_set<RE::TESFormID> g_kitTables;  // tables set up from a kit (created refs: FF......)
		bool                              g_kitStocked{ false };  // the Dugout Inn got its first kit in this save

		RE::TESBoundObject* KitForm()
		{
			return util::ResolveForm("fo4pool.esl", kKitRawID);
		}

		struct EquipSink final : RE::BSTEventSink<RE::TESEquipEvent>
		{
			RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent& a_event, RE::BSTEventSource<RE::TESEquipEvent>*) override
			{
				auto* kit = KitForm();
				if (a_event.equipped && kit && a_event.baseObject == kit->GetFormID() && a_event.actor &&
					a_event.actor.get() == RE::PlayerCharacter::GetSingleton()) {
					// used from the Pip-Boy (it's gone from your inventory now): set the table up once you're back
					// in the world
					if (auto* tasks = F4SE::GetTaskInterface()) {
						tasks->AddTask([]() { Game::Get().PlaceKitTable(); });
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Game::SetUpKit()
	{
		auto* kit = KitForm();
		if (!kit) {
			log::warn("kit: fo4pool.esl's Pool Table Kit not found");
			return;
		}
		// the Dugout Inn's stock: one kit each time Vadim restocks (in memory, every launch: nothing in the esl
		// touches the vendor)
		if (auto* box = RE::TESForm::GetFormByID<RE::TESObjectCONT>(kDugoutVendorBox)) {
			box->AddObject(kit, 1, nullptr);
			log::info("kit: added to the Dugout Inn's stock ({:08X})", kDugoutVendorBox);
		} else {
			log::warn("kit: the Dugout Inn's vendor box {:08X} not found", kDugoutVendorBox);
		}
		static EquipSink sink;
		if (auto* source = RE::TESEquipEvent::GetEventSource()) {
			source->RegisterSink(&sink);
		}
	}

	void Game::StockKit()
	{
		// a save from before the kit existed: one in Vadim's stock now, not just after his next restock
		auto* kit = KitForm();
		auto* chest = RE::TESForm::GetFormByID<RE::TESObjectREFR>(kDugoutVendorChest);
		std::scoped_lock lock(g_kitMutex);
		if (g_kitStocked || !kit || !chest) {
			return;
		}
		g_kitStocked = true;
		if (chest->GetInventoryObjectCount(kit) == 0) {
			chest->AddObjectToContainer(kit, nullptr, 1, nullptr, RE::ITEM_REMOVE_REASON::kNone);
			log::info("kit: one in the Dugout Inn's stock");
		}
	}

	bool Game::IsKitTable(const RE::TESObjectREFR* a_table)
	{
		std::scoped_lock lock(g_kitMutex);
		return a_table && g_kitTables.contains(a_table->GetFormID());
	}

	void Game::PlaceKitTable()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* kit = KitForm();
		auto* base = RE::TESForm::GetFormByID(kTableBase);
		auto* cell = player ? player->GetParentCell() : nullptr;
		auto  giveBack = [&](const std::string& a_why) {
			if (player && kit) {
				player->AddObjectToContainer(kit, nullptr, 1, nullptr, RE::ITEM_REMOVE_REASON::kNone);
			}
			Notify("Pool: " + a_why + ". The kit's back in your inventory");
		};
		if (!player || !kit || !base || !base->IsBoundObject() || !cell) {
			giveBack("can't set up a table here");
			return;
		}
		// (no combat check: the player's IsInCombat reads true out of combat too)
		const float        h = player->data.angle.z;
		const RE::NiPoint3 fwd{ std::sin(h), std::cos(h), 0.0f };
		RE::NiPoint3       at = player->data.location + fwd * kPlaceAhead;
		at.z = player->data.location.z;
		// not on top of another table
		bool crowded = false;
		util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(at, kTooClose, [&](RE::TESObjectREFR* a_ref) {
				if (a_ref && !a_ref->IsDisabled() && IsPoolTable(a_ref)) {
					crowded = true;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		if (crowded) {
			giveBack("too close to another pool table");
			return;
		}
		RE::NEW_REFR_DATA data;
		data.location = at;
		// its long side towards you
		data.direction = { 0.0f, 0.0f, h + 1.5707963f };
		data.object = static_cast<RE::TESBoundObject*>(base);
		if (cell->IsInterior()) {
			data.interior = cell;
		} else {
			data.world = cell->worldSpace;
		}
		data.forcePersist = true;  // stays when the cell resets, until you pack it up
		data.initializeScripts = false;
		auto* dh = RE::TESDataHandler::GetSingleton();
		auto  table = dh ? dh->CreateReferenceAtLocation(data).get() : nullptr;
		if (!table) {
			giveBack("the table couldn't be set up");
			return;
		}
		{
			std::scoped_lock lock(g_kitMutex);
			g_kitTables.insert(table->GetFormID());
		}
		log::info("kit: table {:08X} set up at ({:.0f}, {:.0f}, {:.0f})", table->GetFormID(), at.x, at.y, at.z);
		Notify(std::format("Pool: table set up. Hold {} at it to pack it away again", Settings::Key(_activateVk.load())));
	}

	void Game::PackUpTable(RE::TESObjectREFR* a_table)
	{
		if (!a_table || !IsKitTable(a_table)) {
			return;
		}
		if (!TableFree(a_table)) {
			Notify("Pool: there's a game on at that table. Finish it (or forfeit) first");
			return;
		}
		// its balls off the table, and nobody hanging round it
		if (SlotOf(a_table) > 0) {
			FocusTable(a_table);
		}
		if (SlotOf(a_table) == 0) {
			ExitAim();
			Clear();
		}
		std::vector<RE::TESFormID> there;
		for (const auto& w : _watchers) {
			if (w.table.get().get() == a_table) {
				there.push_back(w.id);
			}
		}
		for (const auto id : there) {
			DropWatcher(id);
		}
		{
			std::scoped_lock lock(g_kitMutex);
			g_kitTables.erase(a_table->GetFormID());
		}
		log::info("kit: table {:08X} packed up", a_table->GetFormID());
		a_table->Disable();
		a_table->SetWantsDelete(true);
		if (auto* player = RE::PlayerCharacter::GetSingleton(); player && KitForm()) {
			player->AddObjectToContainer(KitForm(), nullptr, 1, nullptr, RE::ITEM_REMOVE_REASON::kNone);
		}
		Notify("Pool: table packed up. The kit's in your inventory");
	}

	void Game::SaveKit(const F4SE::SerializationInterface* a_intfc)
	{
		std::scoped_lock lock(g_kitMutex);
		if (!a_intfc->OpenRecord(kKitRecordType, kKitRecordVersion)) {
			return;
		}
		const std::uint8_t stocked = g_kitStocked ? 1 : 0;
		a_intfc->WriteRecordData(stocked);
		const auto count = static_cast<std::uint32_t>(g_kitTables.size());
		a_intfc->WriteRecordData(count);
		for (const auto id : g_kitTables) {
			a_intfc->WriteRecordData(id);
		}
	}

	void Game::LoadKit(const F4SE::SerializationInterface* a_intfc, std::uint32_t a_version)
	{
		if (a_version != kKitRecordVersion) {
			return;
		}
		std::scoped_lock lock(g_kitMutex);
		std::uint8_t  stocked = 0;
		std::uint32_t count = 0;
		a_intfc->ReadRecordData(stocked);
		a_intfc->ReadRecordData(count);
		g_kitStocked = stocked != 0;
		for (std::uint32_t i = 0; i < count; ++i) {
			RE::TESFormID id = 0;
			a_intfc->ReadRecordData(id);
			if (const auto now = a_intfc->ResolveFormID(id)) {
				g_kitTables.insert(*now);
			}
		}
		log::info("kit: {} table(s) set up from kits in this save", g_kitTables.size());
	}

	void Game::RevertKit()
	{
		std::scoped_lock lock(g_kitMutex);
		g_kitTables.clear();
		g_kitStocked = false;
	}

	bool Game::IsKitRecord(std::uint32_t a_type)
	{
		return a_type == kKitRecordType;
	}
}
