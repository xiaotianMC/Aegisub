// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#pragma once

#include <optional>
#include <string>

/// Secrets kept in the system's keychain: Windows' Credential Manager,
/// macOS's Keychain, the Secret Service (GNOME Keyring, KWallet) on Linux,
/// through wxSecretStore. Not every build has one (the system wxWidgets
/// may be made without, Linux needs libsecret) and not every machine (no
/// keyring running): what such a machine is told is held for the run alone.
namespace secret {

/// Whether there is a keychain to use; @p why told when there is not
bool Available(std::string *why = nullptr);

/// What the keychain holds for @p service, if anything
std::optional<std::string> Load(std::string const& service);

/// Keep @p value for @p service in the keychain. @return False when it could not be
bool Save(std::string const& service, std::string const& value);

/// Take @p service's secret out of the keychain. @return Whether there was one
bool Forget(std::string const& service);

/// Keep @p value for @p service in memory, for this run alone
void Hold(std::string const& service, std::string const& value);
/// What is held for @p service this run
std::optional<std::string> Held(std::string const& service);
/// Let go of what is held for @p service
void Drop(std::string const& service);

}
