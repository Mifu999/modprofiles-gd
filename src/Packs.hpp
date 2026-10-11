#pragma once

#include "Profile.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/async.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

/// Reading and writing shareable profile packs.
///
/// A pack is a zip file named `<slug>.modprofile` containing:
///   profile.json        the profile itself (same format as a local profile)
///   mods/<id>.geode     optional, for mods that are not on the Geode index
namespace packs {
    inline constexpr char const* PACK_EXTENSION = "modprofile";

    /// Scratch directory under the Geode temp dir, wiped on every launch.
    std::filesystem::path workDir();

    /// Writes `profile` to `file`. Every id in `embedModIDs` that is installed
    /// has its .geode package copied into the pack, so the profile stays usable
    /// for mods that are not published on the index.
    geode::Result<> exportToFile(
        Profile const& profile,
        std::filesystem::path const& file,
        std::vector<std::string> const& embedModIDs
    );

    /// A pack that has been unzipped into `dir`.
    struct OpenedPack {
        Profile profile;
        std::filesystem::path dir;

        /// .geode files shipped inside the pack, by mod id.
        std::vector<std::pair<std::string, std::filesystem::path>> packedMods() const;
    };

    geode::Result<OpenedPack> openFile(std::filesystem::path const& file);

    /// Copies every .geode shipped in the pack into the mods directory.
    /// Existing files are overwritten: an explicitly packed mod is what the
    /// profile author intended to ship.
    /// @returns how many packages were installed
    geode::Result<int> installPackedMods(OpenedPack const& pack);

    /// Ids listed by the profile that are neither installed nor packed, and can
    /// therefore only be obtained by downloading them.
    std::vector<ProfileEntry> missingDownloadableMods(Profile const& profile, OpenedPack const* pack);
}

/// Downloads a list of mods one at a time, reporting progress on the main
/// thread. Lives as long as the download runs by holding a reference to itself,
/// so the caller can let its own pointer go.
class ModDownloader : public std::enable_shared_from_this<ModDownloader> {
public:
    struct Summary {
        int installed = 0;
        std::vector<std::string> failed;
    };

    /// @param entries mods to fetch
    /// @param onProgress called with (index, total, name) before each download
    /// @param onFinish called once, after the last download
    static void start(
        std::vector<ProfileEntry> entries,
        std::function<void(std::size_t, std::size_t, std::string)> onProgress,
        std::function<void(Summary)> onFinish
    );

    void cancel();

private:
    void step();
    void fetchIndexLink(ProfileEntry const& entry);
    void download(ProfileEntry const& entry, std::string const& url);
    void failCurrent(std::string reason);

    std::vector<ProfileEntry> m_entries;
    std::size_t m_index = 0;
    bool m_cancelled = false;
    Summary m_summary;
    std::function<void(std::size_t, std::size_t, std::string)> m_onProgress;
    std::function<void(Summary)> m_onFinish;
    std::shared_ptr<ModDownloader> m_self;
};
