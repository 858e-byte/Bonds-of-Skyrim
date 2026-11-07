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
#include <cctype>
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
