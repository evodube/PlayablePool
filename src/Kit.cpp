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
		constexpr float         kPlaceNear = 140.0f;         // the table's middle: where you look on the ground, this far...
		constexpr float         kPlaceFar = 450.0f;          // ...to this far from you
		constexpr float         kTurnStep = 0.2617994f;      // an arrow press turns it 15 degrees
		constexpr float         kTooClose = 260.0f;          // not on top of another pool table

		std::mutex                        g_kitMutex;
		std::unordered_set<RE::TESFormID> g_kitTables;  // tables set up from a kit (created refs: FF......)
		bool                              g_kitStocked{ false };  // the Dugout Inn got its first kit in this save
		std::atomic<RE::TESFormID>        g_previewID{ 0 };       // the table following the crosshair while you set one up

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
						tasks->AddTask([]() { Game::Get().BeginKitPlacement(); });
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

	bool Game::IsKitPreview(const RE::TESObjectREFR* a_ref)
	{
		const auto id = g_previewID.load();
		return a_ref && id != 0 && a_ref->GetFormID() == id;
	}

	void Game::BeginKitPlacement()
	{
		if (_kitPlacing) {
			// (a second kit used while one's being set up: that one back)
			if (auto* player = RE::PlayerCharacter::GetSingleton(); player && KitForm()) {
				player->AddObjectToContainer(KitForm(), nullptr, 1, nullptr, RE::ITEM_REMOVE_REASON::kNone);
			}
			return;
		}
		ExitAim();
		ClosePick();
		_kitPlacing = true;
		_kitTurn = 0.0f;
		_kitReady = false;
		_kitSign = 0;
		_kitOk = false;
		// out of the Pip-Boy and into the world: the table follows where you look until F sets it down
		if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
			queue->AddMessage(RE::BSFixedString("PipboyMenu"), RE::UI_MESSAGE_TYPE::kHide);
		}
		_lookTime = {};
		log::info("kit: setting a table up");
	}

	std::string Game::KitPrompt() const
	{
		const std::string key = Settings::Key(_activateVk.load());
		const std::string cancel = Settings::Key(Settings::Get().keyForfeit);
		return _kitOk ? std::format("{}) SET UP THE TABLE  ·  LEFT/RIGHT TURN  ·  {}) CANCEL", key, cancel)
		              : std::format("TOO CLOSE TO ANOTHER TABLE  ·  {}) CANCEL", cancel);
	}

	void Game::TickKitPlacement()
	{
		if (IsGamePaused()) {
			return;  // (still in the Pip-Boy)
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* camera = RE::PlayerCamera::GetSingleton();
		auto* base = RE::TESForm::GetFormByID(kTableBase);
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!player || !camera || !camera->cameraRoot || !base || !base->IsBoundObject() || !cell) {
			return;
		}
		// where the crosshair meets the ground you stand on, between kPlaceNear and kPlaceFar away
		const auto         origin = camera->cameraRoot->GetWorldTranslate();
		const float        pitch = player->data.angle.x;
		const float        yaw = player->data.angle.z;
		const RE::NiPoint3 dir{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) };
		const RE::NiPoint3 flat{ std::sin(yaw), std::cos(yaw), 0.0f };
		float              reach = kPlaceFar;
		if (dir.z < -0.02f) {
			const float t = (origin.z - player->data.location.z) / -dir.z;
			reach = std::clamp(t * std::cos(pitch), kPlaceNear, kPlaceFar);
		}
		RE::NiPoint3 pos = player->data.location + flat * reach;
		pos.z = player->data.location.z;
		const float angle = yaw + 1.5707963f + _kitTurn;  // its long side towards you, turned as you like
		_kitPos = pos;
		_kitAngle = angle;

		auto preview = _kitPreview.get();
		if (!preview) {
			RE::NEW_REFR_DATA data;
			data.location = pos;
			data.direction = { 0.0f, 0.0f, angle };
			data.object = static_cast<RE::TESBoundObject*>(base);
			if (cell->IsInterior()) {
				data.interior = cell;
			} else {
				data.world = cell->worldSpace;
			}
			data.initializeScripts = false;
			auto* dh = RE::TESDataHandler::GetSingleton();
			_kitPreview = dh ? dh->CreateReferenceAtLocation(data) : RE::ObjectRefHandle{};
			if (auto made = _kitPreview.get()) {
				g_previewID = made->GetFormID();
				_kitMadeAngle = angle;
			}
			return;
		}
		auto* root = preview->Get3D();
		if (!root) {
			return;  // (its 3D on the way)
		}
		if (!_kitReady) {
			// nothing bumps into it while it follows you about; and which way round the 3D takes an angle
			RE::bhkWorld::EnableCollision(root, false, true, true);
			RE::NiMatrix3 plus, minus;
			plus.FromEulerAnglesXYZ(0.0f, 0.0f, _kitMadeAngle);
			minus.FromEulerAnglesXYZ(0.0f, 0.0f, -_kitMadeAngle);
			const auto& w = root->world.rotate;
			auto diff = [&](const RE::NiMatrix3& a_m) {
				float d = 0.0f;
				for (int r = 0; r < 3; ++r) {
					for (int c = 0; c < 3; ++c) {
						d += std::abs(a_m.entry[r][c] - w.entry[r][c]);
					}
				}
				return d;
			};
			_kitSign = diff(plus) <= diff(minus) ? 1 : -1;
			_kitReady = true;
		}
		RE::NiMatrix3 rot;
		rot.FromEulerAnglesXYZ(0.0f, 0.0f, _kitSign * angle);
		PlaceNode(preview.get(), pos, rot, 1.0f, false);

		// clear of the other pool tables
		bool crowded = false;
		util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(pos, kTooClose, [&](RE::TESObjectREFR* a_ref) {
				if (a_ref && a_ref != preview.get() && !a_ref->IsDisabled() && IsPoolTable(a_ref)) {
					crowded = true;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		if (_kitOk == crowded) {
			_kitOk = !crowded;
			_lookTime = {};  // the prompt says so at once
		}
	}

	void Game::TurnKit(int a_dir)
	{
		if (_kitPlacing) {
			_kitTurn += static_cast<float>(a_dir) * kTurnStep;
		}
	}

	void Game::CancelKit(bool a_giveBack)
	{
		if (!_kitPlacing) {
			return;
		}
		_kitPlacing = false;
		g_previewID = 0;
		if (auto preview = _kitPreview.get()) {
			preview->Disable();
			preview->SetWantsDelete(true);
		}
		_kitPreview = {};
		_lookTime = {};
		if (a_giveBack) {
			if (auto* player = RE::PlayerCharacter::GetSingleton(); player && KitForm()) {
				player->AddObjectToContainer(KitForm(), nullptr, 1, nullptr, RE::ITEM_REMOVE_REASON::kNone);
			}
			Notify("Pool: put the kit away");
		}
		log::info("kit: setting up cancelled");
	}

	void Game::ConfirmKit()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* base = RE::TESForm::GetFormByID(kTableBase);
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!_kitPlacing || !_kitPreview.get() || !player || !base || !base->IsBoundObject() || !cell) {
			return;
		}
		if (!_kitOk) {
			Notify("Pool: too close to another pool table");
			return;
		}
		const RE::NiPoint3 pos = _kitPos;
		const float        angle = _kitAngle;
		CancelKit(false);  // the preview goes; the real one in its place
		RE::NEW_REFR_DATA data;
		data.location = pos;
		data.direction = { 0.0f, 0.0f, angle };
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
			if (KitForm()) {
				player->AddObjectToContainer(KitForm(), nullptr, 1, nullptr, RE::ITEM_REMOVE_REASON::kNone);
			}
			Notify("Pool: the table couldn't be set up. The kit's back in your inventory");
			return;
		}
		{
			std::scoped_lock lock(g_kitMutex);
			g_kitTables.insert(table->GetFormID());
		}
		log::info("kit: table {:08X} set up at ({:.0f}, {:.0f}, {:.0f})", table->GetFormID(), pos.x, pos.y, pos.z);
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
