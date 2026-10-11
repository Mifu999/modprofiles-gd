#include "Vault.hpp"

#include <Geode/loader/Dirs.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/utils/file.hpp>

#include <deque>

using namespace geode::prelude;

namespace {
    /// Moves a file, falling back to copy + delete when rename refuses (which
    /// happens across volumes, and on Windows when something still holds it).
    geode::Result<> moveFile(std::filesystem::path const& from, std::filesystem::path const& to) {
        std::error_code ec;
        std::filesystem::rename(from, to, ec);
        if (!ec) return geode::Ok();

        std::error_code copyEc;
        std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, copyEc);
        if (copyEc) {
            return geode::Err("{}", copyEc.message());
        }

        std::error_code removeEc;
        std::filesystem::remove(from, removeEc);
        if (removeEc) {
            // The copy is in place, so the switch still works; the leftover is
            // worth saying out loud though.
            log::warn("Copied '{}' but could not remove the original: {}",
                from.filename().string(), removeEc.message());
        }

        return geode::Ok();
    }

    /// The loader itself is not a file in the mods folder, and this mod must
    /// keep its own file there or the vault becomes unreachable from the UI.
    bool mustStayInMods(std::string const& id) {
        return id == "geode.loader" || id == GEODE_MOD_ID;
    }
}

std::filesystem::path vault::dir() {
    auto path = dirs::getGeodeDir() / "mod-profiles-vault";
    if (auto res = file::createDirectoryAll(path); !res) {
        log::error("Could not create the vault directory: {}", res.unwrapErr());
    }
    return path;
}

std::filesystem::path vault::indexFile() {
    return vault::dir() / "index.json";
}

std::map<std::string, std::string> vault::readIndex() {
    std::map<std::string, std::string> index;

    auto path = vault::indexFile();
    if (!std::filesystem::exists(path)) return index;

    auto json = file::readJson(path);
    if (!json) {
        log::error("Could not read the vault index: {}", json.unwrapErr());
        return index;
    }

    auto value = json.unwrap();
    if (!value.isObject()) return index;

    for (auto const& item : value) {
        auto key = item.getKey();
        if (!key.has_value() || key->empty()) continue;

        auto name = item.asString().unwrapOrDefault();
        if (name.empty()) continue;

        index[*key] = name;
    }

    return index;
}

void vault::writeIndex(std::map<std::string, std::string> const& index) {
    auto value = matjson::Value::object();
    for (auto const& [id, name] : index) {
        value.set(id, name);
    }

    if (auto res = file::writeString(vault::indexFile(), value.dump()); !res) {
        log::error("Could not write the vault index: {}", res.unwrapErr());
    }
}

std::map<std::string, std::filesystem::path> vault::scanModsDir() {
    std::map<std::string, std::filesystem::path> result;

    // Package paths are authoritative: they say which file holds which mod,
    // whatever the file happens to be called.
    std::map<std::string, std::string> byFileName;
    for (auto* mod : Loader::get()->getAllMods()) {
        if (mod->isInternal()) continue;
        auto package = mod->getPackagePath();
        if (package.empty()) continue;
        byFileName[package.filename().string()] = std::string(mod->getID().view());
    }

    auto entries = file::readDirectory(dirs::getModsDir());
    if (!entries) {
        log::error("Could not read the mods directory: {}", entries.unwrapErr());
        return result;
    }

    for (auto const& path : entries.unwrap()) {
        if (path.extension() != ".geode") continue;

        auto name = path.filename().string();
        auto found = byFileName.find(name);
        // Mods that failed to load have no entry, so fall back to the file name,
        // which is how Geode names downloaded packages.
        auto id = found != byFileName.end() ? found->second : path.stem().string();

        result[id] = path;
    }

    return result;
}

std::map<std::string, std::filesystem::path> vault::scanVault() {
    std::map<std::string, std::filesystem::path> result;

    auto index = vault::readIndex();
    std::map<std::string, std::string> idByFileName;
    for (auto const& [id, name] : index) {
        idByFileName[name] = id;
    }

    auto entries = file::readDirectory(vault::dir());
    if (!entries) {
        log::error("Could not read the vault: {}", entries.unwrapErr());
        return result;
    }

    for (auto const& path : entries.unwrap()) {
        if (path.extension() != ".geode") continue;

        auto name = path.filename().string();
        auto found = idByFileName.find(name);
        auto id = found != idByFileName.end() ? found->second : path.stem().string();

        result[id] = path;
    }

    return result;
}

std::size_t vault::storedCount() {
    return vault::scanVault().size();
}

std::set<std::string> vault::wantedIds(Profile const& profile) {
    std::set<std::string> wanted;
    std::deque<std::string> queue;

    for (auto const& entry : profile.mods) {
        if (!entry.enabled) continue;
        if (wanted.insert(entry.id).second) {
            queue.push_back(entry.id);
        }
    }

    // This mod's own file always stays, so the vault can still be reached.
    wanted.insert(GEODE_MOD_ID);

    // Pull in dependencies, so keeping a mod cannot silently break it. Only
    // loaded mods expose their metadata; a dependency that is itself parked is
    // still reached, because whatever depends on it is loaded right now.
    while (!queue.empty()) {
        auto id = queue.front();
        queue.pop_front();

        auto* mod = Loader::get()->getLoadedMod(id);
        if (!mod) continue;

        for (auto const& dependency : mod->getMetadata().getDependencies()) {
            auto depID = dependency.getID();
            if (wanted.insert(depID).second) {
                queue.push_back(depID);
            }
        }
    }

    return wanted;
}

vault::MoveReport vault::applyProfile(Profile const& profile) {
    MoveReport report;

    auto wanted  = vault::wantedIds(profile);
    auto inMods  = vault::scanModsDir();
    auto inVault = vault::scanVault();
    auto index   = vault::readIndex();
    auto vaultPath = vault::dir();
    auto modsPath  = dirs::getModsDir();

    // 1. Park everything the profile does not ask for.
    for (auto const& [id, path] : inMods) {
        if (wanted.contains(id)) continue;
        if (mustStayInMods(id)) continue;

        auto name = path.filename().string();
        auto target = vaultPath / name;

        if (auto res = moveFile(path, target); !res) {
            report.failed.push_back(fmt::format("{}: {}", id, res.unwrapErr()));
            log::error("Could not park '{}': {}", id, res.unwrapErr());
            continue;
        }

        index[id] = name;
        ++report.movedOut;
    }

    // 2. Bring back everything it does ask for.
    for (auto const& id : wanted) {
        if (inMods.contains(id)) continue;

        auto found = inVault.find(id);
        if (found == inVault.end()) {
            if (id != GEODE_MOD_ID) {
                auto const* entry = profile.find(id);
                report.missing.push_back(entry && !entry->name.empty() ? entry->name : id);
            }
            continue;
        }

        auto name = found->second.filename().string();
        auto target = modsPath / name;

        if (auto res = moveFile(found->second, target); !res) {
            report.failed.push_back(fmt::format("{}: {}", id, res.unwrapErr()));
            log::error("Could not restore '{}': {}", id, res.unwrapErr());
            continue;
        }

        index.erase(id);
        ++report.movedIn;
    }

    vault::writeIndex(index);

    log::info("Vault switch: {} parked, {} restored, {} missing",
        report.movedOut, report.movedIn, report.missing.size());

    return report;
}

geode::Result<int> vault::restoreAll() {
    auto stored = vault::scanVault();
    auto inMods = vault::scanModsDir();
    auto modsPath = dirs::getModsDir();
    auto index = vault::readIndex();

    int restored = 0;
    int skipped = 0;
    std::vector<std::string> failed;

    for (auto const& [id, path] : stored) {
        // A copy of this mod is already installed - probably a newer one from
        // an import. Leave the parked file where it is rather than overwrite it.
        if (inMods.contains(id)) {
            ++skipped;
            continue;
        }

        auto target = modsPath / path.filename();
        if (auto res = moveFile(path, target); !res) {
            failed.push_back(fmt::format("{}: {}", id, res.unwrapErr()));
            continue;
        }

        index.erase(id);
        ++restored;
    }

    vault::writeIndex(index);

    if (skipped > 0) {
        log::info("{} parked file(s) left alone: already installed", skipped);
    }

    if (!failed.empty()) {
        return geode::Err("{} file(s) could not be moved back", failed.size());
    }

    return geode::Ok(restored);
}
