// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "secret.h"

#include "compat.h"

#include <map>
#include <mutex>

#include <wx/defs.h>
#include <wx/intl.h>
#include <wx/log.h>
#if wxUSE_SECRETSTORE
#include <wx/secretstore.h>
#endif

namespace secret {
#if wxUSE_SECRETSTORE
bool Available(std::string *why) {
	wxLogNull quiet;
	wxString error;
	bool const ok = wxSecretStore::GetDefault().IsOk(&error);
	if (!ok && why) *why = from_wx(error);
	return ok;
}

std::optional<std::string> Load(std::string const& service) {
	wxLogNull quiet;
	auto store = wxSecretStore::GetDefault();
	if (!store.IsOk()) return std::nullopt;
	wxString user;
	wxSecretValue value;
	if (!store.Load(to_wx(service), user, value)) return std::nullopt;
	wxString text = value.GetAsString();
	std::string out = from_wx(text);
	wxSecretValue::WipeString(text);
	return out;
}

bool Save(std::string const& service, std::string const& value) {
	wxLogNull quiet;
	auto store = wxSecretStore::GetDefault();
	if (!store.IsOk()) return false;
	return store.Save(to_wx(service), "Aegisub", wxSecretValue(to_wx(value)));
}

bool Forget(std::string const& service) {
	wxLogNull quiet;
	auto store = wxSecretStore::GetDefault();
	if (!store.IsOk()) return false;
	return store.Delete(to_wx(service));
}
#else
bool Available(std::string *why) {
	if (why) *why = from_wx(_("This build of Aegisub was made without a keychain."));
	return false;
}
std::optional<std::string> Load(std::string const&) { return std::nullopt; }
bool Save(std::string const&, std::string const&) { return false; }
bool Forget(std::string const&) { return false; }
#endif

namespace {
std::mutex held_mutex;
std::map<std::string, std::string>& HeldMap() {
	static std::map<std::string, std::string> held;
	return held;
}
}

void Hold(std::string const& service, std::string const& value) {
	std::lock_guard<std::mutex> lock(held_mutex);
	HeldMap()[service] = value;
}

std::optional<std::string> Held(std::string const& service) {
	std::lock_guard<std::mutex> lock(held_mutex);
	auto it = HeldMap().find(service);
	if (it == HeldMap().end()) return std::nullopt;
	return it->second;
}

void Drop(std::string const& service) {
	std::lock_guard<std::mutex> lock(held_mutex);
	HeldMap().erase(service);
}

}
