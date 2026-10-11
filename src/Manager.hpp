#pragma once

#include "Profile.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Slug reserved for the snapshot taken right before a profile is applied.
inline constexpr char const* AUTO_BACKUP_SLUG = "auto-backup";

/// How a profile is put into effect.
enum class ApplyMode {
    /// Only flip Geode's enabled/disabled switches. Every mod stays installed
    /// and listed.
    Toggle,
    /// Move the .geode files themselves: anything the profile does not want
    /// leaves `geode/mods` for the vault, anything it wants comes back.
    MoveFiles,
};

/// Result of applying a profile, so the UI can report what actually happened.
struct ApplyReport {
    /// Mods whose enabled state was changed.
    int changed = 0;
    /// Mod files taken out of `geode/mods` and parked in the vault.
    int movedOut = 0;
    /// Mod files brought back from the vault into `geode/mods`.
    int movedIn = 0;
    /// Mods listed by the profile that are neither installed nor parked.
    std::vector<std::string> missing;
    /// Mods that refused to toggle or whose file could not be moved.
    std::vector<std::string> failed;

    bool needsRestart() const { return changed > 0 || movedOut > 0 || movedIn > 0; }
};

/// Owns the on-disk profile collection. Profiles live as one JSON file each in
/// `<mod save dir>/profiles/`, so they can be edited and shared by hand.
class Manager {
public:
    static Manager* get();

    /// Reads every profile file from disk, replacing the in-memory list.
    /// Unreadable files are logged and skipped rather than aborting the load.
    void loadAll();

    std::vector<Profile> const& profiles() const { return m_profiles; }

    Profile const* bySlug(std::string_view slug) const;

    /// Writes the profile to disk and inserts or replaces it in the list.
    /// The slug is made unique first when `allowRename` is set.
    geode::Result<> save(Profile profile, bool allowRename = true);

    geode::Result<> remove(std::string_view slug);

    /// Puts a profile into effect. Nothing is ever deleted: Toggle only flips
    /// switches, MoveFiles parks unwanted mod files in the vault where they can
    /// be brought back at any time.
    /// Honours the `auto-backup` and `allow-self-disable` settings.
    /// @param mode whether to flip switches or move the files themselves
    /// @param disableUnlisted in Toggle mode, also switch off every installed
    /// mod the profile does not mention. Ignored in MoveFiles mode, where an
    /// unlisted mod leaves the folder anyway.
    ApplyReport apply(Profile const& profile, ApplyMode mode, bool disableUnlisted = false);

    /// The mode the user picked in the settings.
    static ApplyMode currentMode();

    /// Slug of the profile applied last, or empty if none was. Mod files parked
    /// by the next switch go into the vault folder named after it, so the vault
    /// reads as "what each of your setups holds while it is not in use".
    static std::string activeProfile();
    static void setActiveProfile(std::string slug);

    /// Directory holding the profile JSON files. Created on first use.
    std::filesystem::path profilesDir() const;

    /// Every mod a profile may legitimately touch: installed, not internal.
    static std::vector<geode::Mod*> togglableMods();

    /// True when this mod must stay enabled regardless of what a profile says.
    static bool isProtected(std::string_view modID);

    /// Turns `slug` into one that no other stored profile uses.
    std::string uniqueSlug(std::string_view slug, std::string_view ignoreSlug = "") const;

private:
    std::vector<Profile> m_profiles;
};
