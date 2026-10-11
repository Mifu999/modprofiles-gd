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

    /// Folder name of a parked file, relative to the vault root.
    std::string folderOf(std::filesystem::path const& file, std::filesystem::path const& root) {
        auto parent = file.parent_path();
        if (parent == root) return "";
        return parent.filename().string();
    }

    /// Drops sub-folders that no longer hold anything. Removing an empty folder
    /// is tidying, not deleting someone's mods.
    void pruneEmptyFolders(std::filesystem::path const& root) {
        std::error_code ec;
        for (auto const& entry : std::filesystem::directory_iterator(root, ec)) {
            if (ec) return;
            if (!entry.is_directory()) continue;

            std::error_code emptyEc;
            if (std::filesystem::is_empty(entry.path(), emptyEc) && !emptyEc) {
                std::error_code removeEc;
                std::filesystem::remove(entry.path(), removeEc);
            }
        }
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

std::map<std::string, vault::StoredMod> vault::readIndex() {
    std::map<std::string, StoredMod> index;

    auto path = vault::indexFile();
    if (!std::filesystem::exists(path)) return index;

    auto json = file::readJson(path);
    if (!json) {
        log::error("Could not read the vault index: {}", json.unwrapErr());
        return index;
    }

    auto value = json.unwrap();
    if (!value.isObject()) return index;

    auto root = vault::dir();

    // Version 2.1 wrote a flat `{ "<id>": "<file name>" }` object with every
    // file in the vault root. Read it so an upgrade keeps finding its files.
    auto const& mods = value.contains("mods") && value["mods"].isObject() ? value["mods"] : value;

    for (auto const& item : mods) {
        auto key = item.getKey();
        if (!key.has_value() || key->empty()) continue;
        if (*key == "format" || *key == "mods") continue;

        StoredMod stored;
        stored.id = *key;

        if (item.isString()) {
            stored.fileName = item.asString().unwrapOrDefault();
        }
        else if (item.isObject()) {
            stored.fileName = item["file"].asString().unwrapOrDefault();
            stored.folder   = item["folder"].asString().unwrapOrDefault();
            stored.parked   = static_cast<std::int64_t>(item["parked"].asInt().unwrapOr(0));
        }

        if (stored.fileName.empty()) continue;

        stored.path = stored.folder.empty()
            ? root / stored.fileName
            : root / stored.folder / stored.fileName;

        index[stored.id] = std::move(stored);
    }

    return index;
}

void vault::writeIndex(std::map<std::string, StoredMod> const& index) {
    auto mods = matjson::Value::object();
    for (auto const& [id, stored] : index) {
        mods.set(id, matjson::makeObject({
            { "file",   stored.fileName },
            { "folder", stored.folder },
            { "parked", matjson::Value(static_cast<std::intmax_t>(stored.parked)) },
        }));
    }

    auto value = matjson::makeObject({
        { "format", matjson::Value(static_cast<std::intmax_t>(2)) },
        { "mods",   std::move(mods) },
    });

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

std::map<std::string, vault::StoredMod> vault::scanVault() {
    std::map<std::string, StoredMod> result;

    auto root = vault::dir();
    auto index = vault::readIndex();

    // File name to id, so a file that was moved to another folder by hand is
    // still recognised as the mod it is.
    std::map<std::string, StoredMod const*> byFileName;
    for (auto const& [id, stored] : index) {
        byFileName[stored.fileName] = &stored;
    }

    auto entries = file::readDirectory(root, true);
    if (!entries) {
        log::error("Could not read the vault: {}", entries.unwrapErr());
        return result;
    }

    for (auto const& path : entries.unwrap()) {
        if (path.extension() != ".geode") continue;

        StoredMod stored;
        stored.fileName = path.filename().string();
        stored.folder   = folderOf(path, root);
        stored.path     = path;

        auto found = byFileName.find(stored.fileName);
        if (found != byFileName.end()) {
            stored.id     = found->second->id;
            stored.parked = found->second->parked;
        }
        else {
            stored.id = path.stem().string();
        }

        result[stored.id] = std::move(stored);
    }

    return result;
}

std::map<std::string, std::vector<vault::StoredMod>> vault::byFolder() {
    std::map<std::string, std::vector<StoredMod>> grouped;
    for (auto const& [id, stored] : vault::scanVault()) {
        auto key = stored.folder.empty() ? std::string(UNSORTED_FOLDER) : stored.folder;
        grouped[key].push_back(stored);
    }
    return grouped;
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

vault::MoveReport vault::applyProfile(Profile const& profile, std::string const& parkInto) {
    MoveReport report;

    auto wanted  = vault::wantedIds(profile);
    auto inMods  = vault::scanModsDir();
    auto inVault = vault::scanVault();
    auto index   = vault::readIndex();

    auto root      = vault::dir();
    auto modsPath  = dirs::getModsDir();
    auto folder    = parkInto.empty() ? std::string(UNSORTED_FOLDER) : Profile::slugify(parkInto);
    auto parkPath  = root / folder;

    auto now = Profile::nowSeconds();

    // 1. Park everything the profile does not ask for.
    bool parkDirReady = false;
    for (auto const& [id, path] : inMods) {
        if (wanted.contains(id)) continue;
        if (mustStayInMods(id)) continue;

        if (!parkDirReady) {
            if (auto res = file::createDirectoryAll(parkPath); !res) {
                report.failed.push_back(fmt::format("vault folder: {}", res.unwrapErr()));
                log::error("Could not create '{}': {}", parkPath.string(), res.unwrapErr());
                break;
            }
            parkDirReady = true;
        }

        auto name = path.filename().string();
        auto target = parkPath / name;

        if (auto res = moveFile(path, target); !res) {
            report.failed.push_back(fmt::format("{}: {}", id, res.unwrapErr()));
            log::error("Could not park '{}': {}", id, res.unwrapErr());
            continue;
        }

        StoredMod stored;
        stored.id       = id;
        stored.fileName = name;
        stored.folder   = folder;
        stored.parked   = now;
        stored.path     = target;
        index[id] = std::move(stored);

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

        auto target = modsPath / found->second.fileName;

        if (auto res = moveFile(found->second.path, target); !res) {
            report.failed.push_back(fmt::format("{}: {}", id, res.unwrapErr()));
            log::error("Could not restore '{}': {}", id, res.unwrapErr());
            continue;
        }

        index.erase(id);
        ++report.movedIn;
    }

    vault::writeIndex(index);
    pruneEmptyFolders(root);

    log::info("Vault switch into '{}': {} parked, {} restored, {} missing",
        folder, report.movedOut, report.movedIn, report.missing.size());

    return report;
}

geode::Result<int> vault::restoreAll() {
    auto stored   = vault::scanVault();
    auto inMods   = vault::scanModsDir();
    auto modsPath = dirs::getModsDir();
    auto index    = vault::readIndex();

    int restored = 0;
    int skipped = 0;
    std::vector<std::string> failed;

    for (auto const& [id, parked] : stored) {
        // A copy of this mod is already installed - probably a newer one from
        // an import. Leave the parked file where it is rather than overwrite it.
        if (inMods.contains(id)) {
            ++skipped;
            continue;
        }

        auto target = modsPath / parked.fileName;
        if (auto res = moveFile(parked.path, target); !res) {
            failed.push_back(fmt::format("{}: {}", id, res.unwrapErr()));
            continue;
        }

        index.erase(id);
        ++restored;
    }

    vault::writeIndex(index);
    pruneEmptyFolders(vault::dir());

    if (skipped > 0) {
        log::info("{} parked file(s) left alone: already installed", skipped);
    }

    if (!failed.empty()) {
        return geode::Err("{} file(s) could not be moved back", failed.size());
    }

    return geode::Ok(restored);
}
