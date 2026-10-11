#include "Packs.hpp"

#include <Geode/loader/Dirs.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <optional>
#include <utility>

using namespace geode::prelude;

namespace {
    /// Mod metadata endpoint of the official Geode index.
    /// The response carries payload.versions[].download_link, which is what we
    /// follow instead of guessing a download URL.
    std::string indexModURL(std::string const& id) {
        return fmt::format("https://api.geode-sdk.org/v1/mods/{}", id);
    }

    /// Picks the download link of the newest accepted version.
    std::optional<std::string> pickDownloadLink(matjson::Value const& json) {
        if (!json.contains("payload")) return std::nullopt;
        auto const& payload = json["payload"];
        if (!payload.contains("versions")) return std::nullopt;

        auto versions = payload["versions"].asArray();
        if (!versions) return std::nullopt;

        for (auto const& version : versions.unwrap()) {
            auto status = version["status"].asString().unwrapOrDefault();
            if (!status.empty() && status != "accepted") continue;

            auto link = version["download_link"].asString().unwrapOrDefault();
            if (!link.empty()) return link;
        }
        return std::nullopt;
    }
}

std::filesystem::path packs::workDir() {
    auto dir = dirs::getTempDir() / GEODE_MOD_ID;
    if (auto res = file::createDirectoryAll(dir); !res) {
        log::error("Could not create the work directory: {}", res.unwrapErr());
    }
    return dir;
}

geode::Result<> packs::exportToFile(
    Profile const& profile,
    std::filesystem::path const& file,
    std::vector<std::string> const& embedModIDs
) {
    GEODE_UNWRAP_INTO(auto zip, file::Zip::create(file));

    // Record how every mod is expected to be obtained, so the importing side
    // knows what to do without having to guess.
    Profile stored = profile;
    for (auto& entry : stored.mods) {
        bool embedded = std::find(embedModIDs.begin(), embedModIDs.end(), entry.id) != embedModIDs.end();
        if (embedded) {
            entry.source = ModSource::Packed;
        }
        else if (entry.source == ModSource::Packed) {
            // It was packed by whoever made the profile we are re-exporting,
            // but we are not shipping the file, so it can only come from the index.
            entry.source = ModSource::Index;
        }
    }

    GEODE_UNWRAP(zip.add("profile.json", matjson::Value(stored).dump()));

    for (auto const& id : embedModIDs) {
        auto* mod = Loader::get()->getLoadedMod(id);
        if (!mod) {
            log::warn("Cannot embed '{}': not installed", id);
            continue;
        }

        auto package = mod->getPackagePath();
        if (!std::filesystem::exists(package)) {
            log::warn("Cannot embed '{}': package file is missing", id);
            continue;
        }

        // Read and re-add under a deterministic name rather than using
        // addFrom, which would keep whatever the file is called on disk.
        auto data = file::readBinary(package);
        if (!data) {
            log::warn("Cannot embed '{}': {}", id, data.unwrapErr());
            continue;
        }

        GEODE_UNWRAP(zip.add(fmt::format("mods/{}.geode", id), data.unwrap()));
    }

    return geode::Ok();
}

std::vector<std::pair<std::string, std::filesystem::path>> packs::OpenedPack::packedMods() const {
    std::vector<std::pair<std::string, std::filesystem::path>> result;

    auto modsDir = dir / "mods";
    if (!std::filesystem::exists(modsDir)) return result;

    auto entries = file::readDirectory(modsDir);
    if (!entries) {
        log::error("Could not read the packed mods: {}", entries.unwrapErr());
        return result;
    }

    for (auto const& path : entries.unwrap()) {
        if (path.extension() != ".geode") continue;
        result.emplace_back(path.stem().string(), path);
    }

    return result;
}

geode::Result<packs::OpenedPack> packs::openFile(std::filesystem::path const& file) {
    if (!std::filesystem::exists(file)) {
        return geode::Err("That file does not exist");
    }

    auto target = packs::workDir() / "open" / file.stem();

    std::error_code ec;
    std::filesystem::remove_all(target, ec);
    GEODE_UNWRAP(file::createDirectoryAll(target));

    GEODE_UNWRAP(file::Unzip::intoDir(file, target));

    auto profileFile = target / "profile.json";
    if (!std::filesystem::exists(profileFile)) {
        return geode::Err("This file is not a Mod Profiles pack (no profile.json inside)");
    }

    GEODE_UNWRAP_INTO(auto profile, file::readFromJson<Profile>(profileFile));

    return geode::Ok(packs::OpenedPack{ std::move(profile), std::move(target) });
}

geode::Result<int> packs::installPackedMods(OpenedPack const& pack) {
    int installed = 0;

    for (auto const& [id, path] : pack.packedMods()) {
        auto target = dirs::getModsDir() / fmt::format("{}.geode", id);

        std::error_code ec;
        std::filesystem::copy_file(
            path, target,
            std::filesystem::copy_options::overwrite_existing,
            ec
        );
        if (ec) {
            return geode::Err("Could not install '{}': {}", id, ec.message());
        }

        ++installed;
    }

    return geode::Ok(installed);
}

std::vector<ProfileEntry> packs::missingDownloadableMods(Profile const& profile, OpenedPack const* pack) {
    std::vector<std::string> packed;
    if (pack) {
        for (auto const& [id, path] : pack->packedMods()) {
            packed.push_back(id);
        }
    }

    std::vector<ProfileEntry> missing;
    for (auto const& entry : profile.mods) {
        if (!entry.enabled) continue;
        if (Loader::get()->getLoadedMod(entry.id)) continue;
        if (std::find(packed.begin(), packed.end(), entry.id) != packed.end()) continue;

        missing.push_back(entry);
    }

    return missing;
}

void ModDownloader::start(
    std::vector<ProfileEntry> entries,
    std::function<void(std::size_t, std::size_t, std::string)> onProgress,
    std::function<void(Summary)> onFinish
) {
    auto downloader = std::make_shared<ModDownloader>();
    downloader->m_entries   = std::move(entries);
    downloader->m_onProgress = std::move(onProgress);
    downloader->m_onFinish  = std::move(onFinish);
    // Keep itself alive until the queue is drained or cancelled.
    downloader->m_self = downloader;
    downloader->step();
}

void ModDownloader::cancel() {
    m_cancelled = true;
}

void ModDownloader::step() {
    if (m_cancelled || m_index >= m_entries.size()) {
        auto finish = std::move(m_onFinish);
        auto summary = m_summary;
        // Release the self-reference before handing control back.
        m_self.reset();
        if (finish && !m_cancelled) finish(summary);
        return;
    }

    auto const& entry = m_entries[m_index];
    if (m_onProgress) {
        m_onProgress(m_index + 1, m_entries.size(), entry.name.empty() ? entry.id : entry.name);
    }

    if (entry.source == ModSource::Remote && !entry.url.empty()) {
        this->download(entry, entry.url);
    }
    else {
        this->fetchIndexLink(entry);
    }
}

void ModDownloader::failCurrent(std::string reason) {
    auto const& entry = m_entries[m_index];
    m_summary.failed.push_back(fmt::format("{}: {}", entry.name.empty() ? entry.id : entry.name, reason));
    log::error("Download of '{}' failed: {}", entry.id, reason);
    ++m_index;
    this->step();
}

void ModDownloader::fetchIndexLink(ProfileEntry const& entry) {
    auto self = shared_from_this();
    auto id = entry.id;

    geode::async::spawn(
        web::WebRequest().get(indexModURL(id)),
        [self, id](web::WebResponse response) {
            if (self->m_cancelled) return;

            if (!response.ok()) {
                self->failCurrent(fmt::format("the index answered {}", response.code()));
                return;
            }

            auto json = response.json();
            if (!json) {
                self->failCurrent("the index sent something unreadable");
                return;
            }

            auto link = pickDownloadLink(json.unwrap());
            if (!link.has_value()) {
                self->failCurrent("not published on the index");
                return;
            }

            self->download(self->m_entries[self->m_index], *link);
        }
    );
}

void ModDownloader::download(ProfileEntry const& entry, std::string const& url) {
    auto self = shared_from_this();
    auto id = entry.id;

    geode::async::spawn(
        web::WebRequest().get(url),
        [self, id](web::WebResponse response) {
            if (self->m_cancelled) return;

            if (!response.ok()) {
                self->failCurrent(fmt::format("download failed with code {}", response.code()));
                return;
            }

            auto target = dirs::getModsDir() / fmt::format("{}.geode", id);
            if (auto res = response.into(target); !res) {
                self->failCurrent(res.unwrapErr());
                return;
            }

            ++self->m_summary.installed;
            ++self->m_index;
            self->step();
        }
    );
}
