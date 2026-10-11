#pragma once

#include "Profile.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

/// File-level profile switching.
///
/// Toggling a mod off still leaves it installed, loaded and listed. For a real
/// setup switch, the .geode files themselves move: everything the profile does
/// not want is taken out of `geode/mods` and parked in the vault, and whatever
/// the profile wants is brought back from the vault into `geode/mods`.
///
/// The vault lives at `geode/mod-profiles-vault`, next to the mods folder but
/// not inside it, so Geode never looks at what is parked there. Nothing is ever
/// deleted - `restoreAll()` puts every parked file back.
namespace vault {
    /// `geode/mod-profiles-vault`, created on first use.
    std::filesystem::path dir();

    /// `<vault>/index.json`, mapping mod id to the file name it was parked as.
    std::filesystem::path indexFile();

    std::map<std::string, std::string> readIndex();
    void writeIndex(std::map<std::string, std::string> const& index);

    /// Every .geode file sitting in `geode/mods` right now, by mod id.
    /// Ids come from the loaded mods' package paths, falling back to the file
    /// name for anything that did not load this session.
    std::map<std::string, std::filesystem::path> scanModsDir();

    /// Every .geode file parked in the vault, by mod id.
    std::map<std::string, std::filesystem::path> scanVault();

    struct MoveReport {
        int movedOut = 0;
        int movedIn = 0;
        /// Wanted by the profile but present neither in mods nor in the vault.
        std::vector<std::string> missing;
        /// Files that could not be moved, with the reason.
        std::vector<std::string> failed;
    };

    /// Ids the profile wants, plus the dependencies of every loaded one of them,
    /// plus this mod itself. These are the mods that must end up in `geode/mods`.
    std::set<std::string> wantedIds(Profile const& profile);

    /// Moves files so that `geode/mods` holds exactly what the profile wants.
    MoveReport applyProfile(Profile const& profile);

    /// Moves everything parked back into `geode/mods`.
    geode::Result<int> restoreAll();

    std::size_t storedCount();
}
