#pragma once

#include "Profile.hpp"

#include <Geode/Geode.hpp>

#include <cstdint>
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
/// not inside it, so Geode never looks at what is parked there. It is sorted by
/// setup:
///
///     mod-profiles-vault/
///     ├── index.json          what is parked, where, and since when
///     ├── my-setup/           parked while "My setup" was the active profile
///     │   └── someone.mod.geode
///     └── unsorted/           parked with no profile active yet
///
/// A file is stored once, in the folder named after the profile it was last in
/// use under - not once per profile that lists it. Two profiles sharing 200
/// mods would otherwise mean 200 duplicated files on disk, drifting apart as
/// mods update.
///
/// Nothing is ever deleted: `restoreAll()` puts every parked file back.
namespace vault {
    /// Folder used when a mod is parked before any profile has been applied.
    inline constexpr char const* UNSORTED_FOLDER = "unsorted";

    /// `geode/mod-profiles-vault`, created on first use.
    std::filesystem::path dir();

    /// `<vault>/index.json`.
    std::filesystem::path indexFile();

    /// One parked mod file.
    struct StoredMod {
        std::string id;
        std::string fileName;
        /// Sub-folder of the vault. Empty means the vault root, which only
        /// happens for files parked by version 2.1.
        std::string folder;
        /// Unix seconds when it was parked, or 0 when unknown.
        std::int64_t parked = 0;
        /// Full path on disk.
        std::filesystem::path path;
    };

    /// What the index file claims is parked. Reads both the flat 2.1 layout and
    /// the current one.
    std::map<std::string, StoredMod> readIndex();
    void writeIndex(std::map<std::string, StoredMod> const& index);

    /// Every .geode file sitting in `geode/mods` right now, by mod id.
    /// Ids come from the loaded mods' package paths, falling back to the file
    /// name for anything that did not load this session.
    std::map<std::string, std::filesystem::path> scanModsDir();

    /// Every .geode file parked in the vault, by mod id. The files on disk win
    /// over the index, so moving things by hand cannot desynchronise anything.
    std::map<std::string, StoredMod> scanVault();

    /// Parked mods grouped by the folder they sit in, for display.
    std::map<std::string, std::vector<StoredMod>> byFolder();

    std::size_t storedCount();

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
    /// @param parkInto sub-folder for the files being taken out, normally the
    /// slug of the profile that is active right now
    MoveReport applyProfile(Profile const& profile, std::string const& parkInto);

    /// Moves everything parked back into `geode/mods`.
    geode::Result<int> restoreAll();
}
