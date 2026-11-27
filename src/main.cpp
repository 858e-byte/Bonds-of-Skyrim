// Bonds of Skyrim - BondsOfSkyrim.dll
// Copyright (C) 2026 nozarashi001
//
// This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
// License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
// warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with this program (LICENSE.txt).
// If not, see <https://www.gnu.org/licenses/>.
//
// Built with CommonLibSSE-NG (GPL-3.0-or-later WITH Modding Exception AND GPL-3.0 Linking Exception).

// BondsOfSkyrim.dll : valeurs par PNJ de Bonds of Skyrim (delais, lassitude), gardees dans la sauvegarde (co-save SKSE).
//
// Papyrus (script AML_Native, fonctions globales)
//   AML_Native.IsLoaded()                      -> true (la DLL est la)
//   AML_Native.GetValue(acteur, cle, defaut)   -> la valeur, ou defaut
//   AML_Native.SetValue(acteur, cle, valeur)
//   AML_Native.ClearActor(acteur)              -> oublie tout sur ce PNJ
//   AML_Native.SetRoll(id, valeur) / GetRoll(id) -> globale d'BondsOfSkyrim.esp (id local), sans attendre une image
//   AML_Native.UIReady()                       -> la vue existe et PrismaUI repond
//   AML_Native.OpenUI(donnees, pause)          -> Show + Focus (jeu en pause ou non) + amlOpen(donnees)
//   AML_Native.CloseUI()                       -> Unfocus + Hide
//   ModEvent "AML_UIAction" ("act", numero)    <- amlAction("<numero>")  <- un sujet choisi (l'ecran se ferme)
//   ModEvent "AML_UIAction" ("close", 0)       <- amlClose("")           <- Echap
// Les jauges (amitie, romance) restent en rangs de faction : les conditions de dialogue les lisent.

#include "PrismaUI_API.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <vector>
#include <mutex>
#include <unordered_map>

namespace
{
	using Values = std::unordered_map<std::string, float>;

	// 0.14.9 : Papyrus peut changer l'ecriture d'un mot ("on" devient "On") selon les mods charges. Une cle s'ecrit donc toujours
	// en minuscules, sinon une valeur sauvegardee ne serait plus retrouvee si la liste de mods change.
	void Normalize(std::string& a_key)
	{
		std::transform(a_key.begin(), a_key.end(), a_key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	}

	std::mutex g_lock;
	std::unordered_map<RE::FormID, Values> g_data;

	constexpr std::uint32_t kUniqueId = 'AMLS';
	constexpr std::uint32_t kRecord = 'DATA';
	constexpr std::uint32_t kVersion = 1;

	// ---------------------------------------------------------------- le menu (PrismaUI)

	PRISMA_UI_API::IVPrismaUI1* g_prisma = nullptr;
	PrismaView g_view = 0;
	bool g_domReady = false;

	constexpr auto kViewPath = "AML_Interact/index.html";
	constexpr auto kEvent = "AML_UIAction";

	// Un ModEvent Papyrus doit partir du fil du jeu : la page appelle depuis le fil de PrismaUI.
	void SendModEvent(std::string a_arg, float a_num)
	{
		SKSE::GetTaskInterface()->AddTask([arg = std::move(a_arg), a_num]() {
			SKSE::ModCallbackEvent ev{ kEvent, RE::BSFixedString(arg.c_str()), a_num, nullptr };
			SKSE::GetModCallbackEventSource()->SendEvent(&ev);
		});
	}

	void CloseView()
	{
		if (!g_prisma || !g_view) {
			return;
		}
		if (g_prisma->HasFocus(g_view)) {
			g_prisma->Unfocus(g_view);
		}
		if (!g_prisma->IsHidden(g_view)) {
			g_prisma->Hide(g_view);
		}
	}

	// 0.14.62 : le texte tape dans la page arrive en UTF-8 ; le jeu ecrit ses noms en Latin-1. Les accents courants sont gardes ; le reste,
	// les caracteres de controle et "|" (le separateur du menu) sont retires.
	std::string ToGameText(const char* a_text)
	{
		std::string out;
		if (!a_text) {
			return out;
		}
		for (auto* p = reinterpret_cast<const unsigned char*>(a_text); *p && out.size() < 24; ++p) {
			if (*p < 0x80) {
				if (*p >= 0x20 && *p != '|') {
					out.push_back(static_cast<char>(*p));
				}
			} else if ((*p == 0xC2 || *p == 0xC3) && (p[1] & 0xC0) == 0x80) {
				out.push_back(static_cast<char>(*p == 0xC2 ? p[1] : p[1] + 0x40));
				++p;
			}
		}
		return out;
	}

	void CreateView()
	{
		g_prisma = static_cast<PRISMA_UI_API::IVPrismaUI1*>(PRISMA_UI_API::RequestPluginAPI(PRISMA_UI_API::InterfaceVersion::V1));
		if (!g_prisma) {
			logger::warn("PrismaUI introuvable : le menu d'interactions est desactive");
			return;
		}
		g_view = g_prisma->CreateView(kViewPath, [](PrismaView a_view) {
			g_domReady = true;
			logger::info("vue {} prete", a_view);
		});
		g_prisma->RegisterJSListener(g_view, "amlAction", [](const char* a_arg) {
			const float number = a_arg ? static_cast<float>(std::atoi(a_arg)) : -1.0f;
			logger::info("la page choisit : {}", number);
			SKSE::GetTaskInterface()->AddTask([]() { CloseView(); });
			SendModEvent("act", number);
		});
		// 0.14.62 : le nom de famille tape dans la page de la maison : ModEvent "AML_UIAction" ("name:<texte>", 3930)
		g_prisma->RegisterJSListener(g_view, "amlText", [](const char* a_arg) {
			auto text = ToGameText(a_arg);
			SKSE::GetTaskInterface()->AddTask([]() { CloseView(); });
			SendModEvent("name:" + text, 3930.0f);
		});
		g_prisma->RegisterJSListener(g_view, "amlClose", [](const char*) {
			SKSE::GetTaskInterface()->AddTask([]() { CloseView(); });
			SendModEvent("close", 0.0f);
		});
		g_prisma->Hide(g_view);
		logger::info("vue {} creee ({})", g_view, kViewPath);
	}

	bool UIReady(RE::StaticFunctionTag*)
	{
		return g_prisma && g_view && g_domReady && g_prisma->IsValid(g_view);
	}

	void OpenUI(RE::StaticFunctionTag*, std::string a_data, bool a_pause)
	{
		if (!UIReady(nullptr)) {
			return;
		}
		g_prisma->Show(g_view);
		g_prisma->Focus(g_view, a_pause);
		g_prisma->InteropCall(g_view, "amlOpen", a_data.c_str());
	}

	void CloseUI(RE::StaticFunctionTag*)
	{
		CloseView();
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
			CreateView();
		}
	}

	// ---------------------------------------------------------------- les fonctions Papyrus (script AML_Native)

	bool IsLoaded(RE::StaticFunctionTag*)
	{
		return true;
	}

	float GetValue(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string a_key, float a_default)
	{
		if (!a_actor) {
			return a_default;
		}
		Normalize(a_key);
		std::scoped_lock lock(g_lock);
		const auto it = g_data.find(a_actor->GetFormID());
		if (it == g_data.end()) {
			return a_default;
		}
		const auto jt = it->second.find(a_key);
		return jt == it->second.end() ? a_default : jt->second;
	}

	void SetValue(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string a_key, float a_value)
	{
		if (!a_actor) {
			return;
		}
		Normalize(a_key);
		std::scoped_lock lock(g_lock);
		g_data[a_actor->GetFormID()][a_key] = a_value;
	}

	void ClearActor(RE::StaticFunctionTag*, RE::Actor* a_actor)
	{
		if (!a_actor) {
			return;
		}
		std::scoped_lock lock(g_lock);
		g_data.erase(a_actor->GetFormID());
	}

	RE::TESGlobal* RollGlobal(std::int32_t a_localId)
	{
		const auto data = RE::TESDataHandler::GetSingleton();
		return data ? data->LookupForm<RE::TESGlobal>(static_cast<RE::FormID>(a_localId), "BondsOfSkyrim.esp") : nullptr;
	}

	void SetRoll(RE::StaticFunctionTag*, std::int32_t a_localId, float a_value)
	{
		if (const auto global = RollGlobal(a_localId)) {
			global->value = a_value;
		}
	}

	float GetRoll(RE::StaticFunctionTag*, std::int32_t a_localId)
	{
		const auto global = RollGlobal(a_localId);
		return global ? global->value : 0.0f;
	}

	// ---------------------------------------------------------------- 0.14.28 : les lits d'une maison (Estate 0.5)
	// Un lit = un meuble dont le modele a le drapeau "Can sleep" (MNAM, bit 31) ; ses places = ses reperes actifs (bits 0-23 et 28).
	// Regle verifiee sur Skyrim.esm hors du jeu (work\diag_beds.py) : 142 lits a une place, 9 lits doubles.

	RE::TESFurniture* BedBase(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref || a_ref->IsDisabled() || a_ref->IsDeleted()) {
			return nullptr;
		}
		const auto base = a_ref->GetBaseObject();
		const auto furniture = base ? base->As<RE::TESFurniture>() : nullptr;
		if (!furniture || !furniture->furnFlags.all(RE::TESFurniture::ActiveMarker::kCanSleep)) {
			return nullptr;
		}
		return furniture;
	}

	std::int32_t SleepSpots(RE::StaticFunctionTag*, RE::TESObjectREFR* a_ref)
	{
		const auto furniture = BedBase(a_ref);
		if (!furniture) {
			return 0;
		}
		const auto bits = furniture->furnFlags.underlying() & 0x10FFFFFFu;
		return (std::max)(1, static_cast<std::int32_t>(std::popcount(bits)));
	}

	// les lits de la piece ou se tient cette personne (une cellule interieure seulement), 128 au plus
	std::vector<RE::TESObjectREFR*> BedsHere(RE::StaticFunctionTag*, RE::Actor* a_who)
	{
		std::vector<RE::TESObjectREFR*> beds;
		const auto cell = a_who ? a_who->GetParentCell() : nullptr;
		if (!cell || !cell->IsInteriorCell()) {
			return beds;
		}
		cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
			if (BedBase(a_ref)) {
				beds.push_back(a_ref);
			}
			return beds.size() < 128 ? RE::BSContainer::ForEachResult::kContinue : RE::BSContainer::ForEachResult::kStop;
		});
		logger::info("lits de {:08X} : {}", cell->GetFormID(), beds.size());
		return beds;
	}

	// ---------------------------------------------------------------- 0.14.32 : essai "Bless Home partout" (Estate 0.6)
	// Un lieu garde la liste de ses reperes types (LCSR : type, reference, cellule ; une reference d'interieur a la case 0x7FFF7FFF,
	// lu dans Breezehome et Riverwood Keep par work\diag_lcsr.py). La quete de planning de l'adoption remplit ses alias par ces types.
	// On ajoute nos reperes a cette liste ; elle n'est pas sauvegardee : Estate les redonne a chaque chargement.
	bool AddLocationRef(RE::StaticFunctionTag*, RE::BGSLocation* a_loc, RE::TESObjectREFR* a_ref, RE::BGSLocationRefType* a_type)
	{
		if (!a_loc || !a_ref || !a_type) {
			return false;
		}
		const auto cell = a_ref->GetParentCell();
		if (!cell) {
			return false;
		}
		const auto id = a_ref->GetFormID();
		for (auto& special : a_loc->specialRefs) {
			if (special.refData.refID == id) {
				special.type = a_type;
				return true;
			}
		}
		RE::SpecialRefData data{};
		data.type = a_type;
		data.refData.refID = id;
		if (cell->IsInteriorCell()) {
			data.refData.parentSpaceID = cell->GetFormID();
			data.refData.cellKey.raw = 0x7FFF7FFF;
		} else {
			const auto world = a_ref->GetWorldspace();
			data.refData.parentSpaceID = world ? world->GetFormID() : 0;
			const auto coords = cell->GetCoordinates();
			data.refData.cellKey.xy.first = coords ? static_cast<std::int16_t>(coords->cellY) : 0;
			data.refData.cellKey.xy.second = coords ? static_cast<std::int16_t>(coords->cellX) : 0;
		}
		a_loc->specialRefs.push_back(data);
		logger::info("lieu {:08X} : repere {:08X} type {:08X} ajoute", a_loc->GetFormID(), id, a_type->GetFormID());
		return true;
	}

	RE::BGSLocation* ParentLocation(RE::StaticFunctionTag*, RE::BGSLocation* a_loc)
	{
		return a_loc ? a_loc->parentLoc : nullptr;
	}

	// ---------------------------------------------------------------- 0.14.50 : le scan d'une maison (Estate 0.10)
	// Ce qu'une piece offre a un foyer, reconnu par les mots-cles et les drapeaux du meuble (regle lue dans Skyrim.esm et HearthFires.esm
	// par work\diag_furniture_kinds.py) : 1 cuisine (marmite, broche, four), 2 siege, 3 bureau ou comptoir, 4 etabli, 5 billot a bois,
	// 6 repere d'animation (balai...), 7 porte qui mene ailleurs. 0 = rien de cela. Jamais un lit.
	std::int32_t SpotKind(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref || a_ref->IsDisabled() || a_ref->IsDeleted()) {
			return 0;
		}
		const auto base = a_ref->GetBaseObject();
		if (!base) {
			return 0;
		}
		if (base->Is(RE::FormType::Door)) {
			const auto tele = a_ref->extraList.GetByType<RE::ExtraTeleport>();
			return tele && tele->teleportData && tele->teleportData->linkedDoor.get() ? 7 : 0;
		}
		if (base->Is(RE::FormType::IdleMarker)) {
			return 6;
		}
		const auto f = base->As<RE::TESFurniture>();
		if (!f || f->furnFlags.all(RE::TESFurniture::ActiveMarker::kCanSleep)) {
			return 0;
		}
		const auto has = [f](std::string_view a_word) { return f->HasKeywordString(a_word); };
		if (has("CraftingCookpot") || has("BYOHCraftingOven") || has("isCraftingOven")) {
			return 1;
		}
		if (has("FurnitureWoodChoppingBlock")) {
			return 5;
		}
		if (has("isWritingChair") || has("isWritingTable") || has("FurnitureCounterLeanMarker") || has("isHadvarWriteLedger")) {
			return 3;
		}
		if (has("BYOHCarpenterTable") || has("BYOHBuildingDrafting") || has("BYOHBuildingTrophyBase1") || has("BYOHBuildingTrophyBase2")) {
			return 0;  // les etablis de construction de Hearthfire
		}
		using Bench = RE::TESFurniture::WorkBenchData::BenchType;
		const auto bench = f->workBenchData.benchType.get();
		if (bench == Bench::kSmithingWeapon || bench == Bench::kSmithingArmor || bench == Bench::kEnchanting || bench == Bench::kAlchemy ||
			has("CraftingSmithingForge") || has("CraftingTanningRack") || has("CraftingSmelter")) {
			return 4;
		}
		if (bench != Bench::kNone) {
			return 0;
		}
		if (f->furnFlags.all(RE::TESFurniture::ActiveMarker::kCanSit) && !has("FurnitureSpecial") && !has("FurnitureForce3rdPerson")) {
			return 2;
		}
		return 0;
	}

	// ce que la piece (ou la case du dehors) ou se tient cette personne offre, pour une sorte donnee ; 64 au plus
	std::vector<RE::TESObjectREFR*> SpotsHere(RE::StaticFunctionTag*, RE::Actor* a_who, std::int32_t a_kind)
	{
		std::vector<RE::TESObjectREFR*> spots;
		const auto cell = a_who ? a_who->GetParentCell() : nullptr;
		if (!cell || a_kind < 1 || a_kind > 7) {
			return spots;
		}
		cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
			if (SpotKind(a_ref) == a_kind) {
				spots.push_back(a_ref);
			}
			return spots.size() < 64 ? RE::BSContainer::ForEachResult::kContinue : RE::BSContainer::ForEachResult::kStop;
		});
		logger::info("scan de {:08X}, sorte {} : {}", cell->GetFormID(), a_kind, spots.size());
		return spots;
	}

	RE::DoorTeleportData* TeleportOf(RE::TESObjectREFR* a_door)
	{
		if (!a_door) {
			return nullptr;
		}
		const auto tele = a_door->extraList.GetByType<RE::ExtraTeleport>();
		return tele ? tele->teleportData : nullptr;
	}

	// la porte de l'autre cote d'une porte qui mene ailleurs
	RE::TESObjectREFR* DoorOther(RE::StaticFunctionTag*, RE::TESObjectREFR* a_door)
	{
		const auto data = TeleportOf(a_door);
		return data ? data->linkedDoor.get().get() : nullptr;
	}

	// ou l'on arrive en passant cette porte : x, y, z, angle (degres) ; vide si ce n'est pas une porte qui mene ailleurs
	std::vector<float> DoorArrival(RE::StaticFunctionTag*, RE::TESObjectREFR* a_door)
	{
		std::vector<float> out;
		if (const auto data = TeleportOf(a_door)) {
			out.push_back(data->position.x);
			out.push_back(data->position.y);
			out.push_back(data->position.z);
			out.push_back(data->rotation.z * 57.29578f);
		}
		return out;
	}

	bool RegisterFunctions(RE::BSScript::IVirtualMachine* a_vm)
	{
		// true : appelables sans attendre une image. Le script en fait des dizaines par conversation ; ces valeurs ont leur verrou.
		a_vm->RegisterFunction("IsLoaded", "AML_Native", IsLoaded, true);
		a_vm->RegisterFunction("GetValue", "AML_Native", GetValue, true);
		a_vm->RegisterFunction("SetValue", "AML_Native", SetValue, true);
		a_vm->RegisterFunction("ClearActor", "AML_Native", ClearActor, true);
		a_vm->RegisterFunction("SetRoll", "AML_Native", SetRoll, true);
		a_vm->RegisterFunction("GetRoll", "AML_Native", GetRoll, true);
		a_vm->RegisterFunction("UIReady", "AML_Native", UIReady);
		a_vm->RegisterFunction("OpenUI", "AML_Native", OpenUI);
		a_vm->RegisterFunction("CloseUI", "AML_Native", CloseUI);
		a_vm->RegisterFunction("BedsHere", "AML_Native", BedsHere);
		a_vm->RegisterFunction("SleepSpots", "AML_Native", SleepSpots);
		a_vm->RegisterFunction("AddLocationRef", "AML_Native", AddLocationRef);
		a_vm->RegisterFunction("ParentLocation", "AML_Native", ParentLocation);
		a_vm->RegisterFunction("SpotsHere", "AML_Native", SpotsHere);
		a_vm->RegisterFunction("DoorOther", "AML_Native", DoorOther);
		a_vm->RegisterFunction("DoorArrival", "AML_Native", DoorArrival);
		return true;
	}

	// ---------------------------------------------------------------- sauvegarde
	// DATA v1 : nb PNJ (u32), puis par PNJ : FormID (u32), nb valeurs (u32), puis par valeur : longueur (u16), cle, float.

	void OnSave(SKSE::SerializationInterface* a_intfc)
	{
		std::scoped_lock lock(g_lock);
		if (!a_intfc->OpenRecord(kRecord, kVersion)) {
			logger::error("sauvegarde : impossible d'ouvrir l'enregistrement");
			return;
		}
		a_intfc->WriteRecordData(static_cast<std::uint32_t>(g_data.size()));
		for (const auto& [formId, values] : g_data) {
			a_intfc->WriteRecordData(formId);
			a_intfc->WriteRecordData(static_cast<std::uint32_t>(values.size()));
			for (const auto& [key, value] : values) {
				const auto len = static_cast<std::uint16_t>(key.size());
				a_intfc->WriteRecordData(len);
				a_intfc->WriteRecordData(key.data(), len);
				a_intfc->WriteRecordData(value);
			}
		}
		logger::info("sauvegarde : {} PNJ", g_data.size());
	}

	void OnLoad(SKSE::SerializationInterface* a_intfc)
	{
		std::scoped_lock lock(g_lock);
		g_data.clear();
		std::uint32_t type, version, length;
		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			if (type != kRecord || version != kVersion) {
				logger::warn("chargement : enregistrement inconnu ignore");
				continue;
			}
			std::uint32_t actors = 0;
			a_intfc->ReadRecordData(actors);
			for (std::uint32_t i = 0; i < actors; ++i) {
				RE::FormID oldId = 0, newId = 0;
				std::uint32_t count = 0;
				a_intfc->ReadRecordData(oldId);
				a_intfc->ReadRecordData(count);
				const bool keep = a_intfc->ResolveFormID(oldId, newId);  // faux si le plugin du PNJ a disparu
				for (std::uint32_t j = 0; j < count; ++j) {
					std::uint16_t len = 0;
					a_intfc->ReadRecordData(len);
					std::string key(len, '\0');
					a_intfc->ReadRecordData(key.data(), len);
					float value = 0.0f;
					a_intfc->ReadRecordData(value);
					if (keep) {
						Normalize(key);  // les sauvegardes d'avant 0.14.9 ont des cles comme "est.0.0.On"
						g_data[newId][std::move(key)] = value;
					}
				}
			}
		}
		logger::info("chargement : {} PNJ", g_data.size());
	}

	void OnRevert(SKSE::SerializationInterface*)
	{
		std::scoped_lock lock(g_lock);
		g_data.clear();
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	logger::info("BondsOfSkyrim charge");
	SKSE::GetPapyrusInterface()->Register(RegisterFunctions);
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	const auto ser = SKSE::GetSerializationInterface();
	ser->SetUniqueID(kUniqueId);
	ser->SetSaveCallback(OnSave);
	ser->SetLoadCallback(OnLoad);
	ser->SetRevertCallback(OnRevert);
	return true;
}
