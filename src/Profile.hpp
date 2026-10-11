#pragma once

#include <Geode/Geode.hpp>
#include <matjson.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/// On-disk format version written into every profile.
/// Anything with a greater version is refused instead of being misread.
inline constexpr int PROFILE_FORMAT = 1;

/// Where a mod listed in a profile can come from if it is not installed.
enum class ModSource {
    /// Not determined (profile saved without checking the index).
    Unknown,
    /// Published on the Geode index under the same id.
    Index,
    /// The .geode file travels inside the profile pack, as mods/<id>.geode.
    Packed,
    /// Downloadable from an explicit URL.
    Remote,
};

std::string modSourceToString(ModSource source);
ModSource modSourceFromString(std::string_view str);

/// One mod as recorded in a profile.
struct ProfileEntry {
    std::string id;
    /// Whether this profile wants the mod enabled.
    bool enabled = true;
    ModSource source = ModSource::Unknown;
    /// Only meaningful for ModSource::Remote.
    std::string url;
    /// Version seen when the profile was saved. Informational only - applying a
    /// profile never downgrades or upgrades an installed mod.
    std::string version;
    /// Name seen when the profile was saved, so a mod that is not installed can
    /// still be listed with a readable name instead of a bare id.
    std::string name;
};

/// A named set of mod states, plus the metadata needed to share it.
struct Profile {
    std::string name;
    /// Filename-safe unique key. Also the name of the file in the profiles dir.
    std::string slug;
    std::string description;
    std::string creator;
    std::string version = "1.0.0";
    /// Unix seconds. 0 when unknown.
    std::int64_t created = 0;
    std::vector<ProfileEntry> mods;

    ProfileEntry const* find(std::string_view id) const;
    std::size_t enabledCount() const;

    /// Replaces the entry for `id`, or appends one if it is not listed yet.
    void setEntry(ProfileEntry entry);

    /// Snapshot of every currently installed user mod and its enable state.
    /// `Mod::isOrWillBeEnabled()` is used, so a toggle that is queued for the
    /// next launch is captured the way the user left it.
    static Profile fromCurrentState(std::string name);

    /// Lowercase, ASCII-only, filename-safe version of `text`.
    /// Returns "profile" when nothing usable is left.
    static std::string slugify(std::string_view text);

    /// Current time in Unix seconds.
    static std::int64_t nowSeconds();
};

template <>
struct matjson::Serialize<Profile> {
    static geode::Result<Profile> fromJson(matjson::Value const& value);
    static matjson::Value toJson(Profile const& profile);
};
