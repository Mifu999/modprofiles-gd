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

/// Result of applying a profile, so the UI can report what actually happened.
struct ApplyReport {
    /// Mods whose state was changed.
    int changed = 0;
    /// Mods listed by the profile that are not installed.
    std::vector<std::string> missing;
    /// Mods that refused to toggle, with the reason Geode gave.
    std::vector<std::string> failed;

    bool needsRestart() const { return changed > 0; }
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

    /// Applies the enable/disable states of a profile to the installed mods.
    /// Nothing is downloaded, installed or deleted here - this only toggles.
    /// Honours the `auto-backup` and `allow-self-disable` settings.
    /// @param disableUnlisted also switch off every installed mod the profile
    /// does not mention, for an exact match instead of a merge
    ApplyReport apply(Profile const& profile, bool disableUnlisted = false);

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
