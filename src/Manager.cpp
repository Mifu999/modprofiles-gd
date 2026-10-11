#include "Manager.hpp"
#include "Vault.hpp"

#include <Geode/loader/Dirs.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/utils/file.hpp>

#include <algorithm>

using namespace geode::prelude;

Manager* Manager::get() {
    static Manager instance;
    return &instance;
}

std::filesystem::path Manager::profilesDir() const {
    auto dir = Mod::get()->getSaveDir() / "profiles";
    if (auto res = file::createDirectoryAll(dir); !res) {
        log::error("Could not create the profiles directory: {}", res.unwrapErr());
    }
    return dir;
}

bool Manager::isProtected(std::string_view modID) {
    if (modID != GEODE_MOD_ID) return false;
    // The user can explicitly allow profiles to switch this mod off.
    return !Mod::get()->getSettingValue<bool>("allow-self-disable");
}

std::vector<Mod*> Manager::togglableMods() {
    std::vector<Mod*> mods;
    for (auto* mod : Loader::get()->getAllMods()) {
        if (mod->isInternal()) continue;
        mods.push_back(mod);
    }
    std::sort(mods.begin(), mods.end(), [](Mod* a, Mod* b) {
        return a->getName().view() < b->getName().view();
    });
    return mods;
}

void Manager::loadAll() {
    m_profiles.clear();

    auto dir = this->profilesDir();
    auto entries = file::readDirectory(dir);
    if (!entries) {
        log::error("Could not read the profiles directory: {}", entries.unwrapErr());
        return;
    }

    for (auto const& path : entries.unwrap()) {
        if (path.extension() != ".json") continue;

        auto parsed = file::readFromJson<Profile>(path);
        if (!parsed) {
            log::error("Skipping profile '{}': {}", path.filename().string(), parsed.unwrapErr());
            continue;
        }

        auto profile = parsed.unwrap();
        // The file name is authoritative: it is what remove() and save() use.
        profile.slug = path.stem().string();
        m_profiles.push_back(std::move(profile));
    }

    std::sort(m_profiles.begin(), m_profiles.end(), [](Profile const& a, Profile const& b) {
        // Keep the automatic backup last, then sort by name.
        if ((a.slug == AUTO_BACKUP_SLUG) != (b.slug == AUTO_BACKUP_SLUG)) {
            return b.slug == AUTO_BACKUP_SLUG;
        }
        return a.name < b.name;
    });

    log::info("Loaded {} profile(s)", m_profiles.size());
}

Profile const* Manager::bySlug(std::string_view slug) const {
    for (auto const& profile : m_profiles) {
        if (profile.slug == slug) return &profile;
    }
    return nullptr;
}

std::string Manager::uniqueSlug(std::string_view slug, std::string_view ignoreSlug) const {
    std::string base = Profile::slugify(slug);
    std::string candidate = base;

    int suffix = 2;
    while (true) {
        bool taken = false;
        for (auto const& profile : m_profiles) {
            if (profile.slug == ignoreSlug) continue;
            if (profile.slug == candidate) {
                taken = true;
                break;
            }
        }
        if (!taken) return candidate;

        candidate = fmt::format("{}-{}", base, suffix);
        ++suffix;
    }
}

geode::Result<> Manager::save(Profile profile, bool allowRename) {
    if (profile.slug.empty()) {
        profile.slug = Profile::slugify(profile.name);
    }
    if (allowRename) {
        profile.slug = this->uniqueSlug(profile.slug, profile.slug);
    }

    auto path = this->profilesDir() / (profile.slug + ".json");
    GEODE_UNWRAP(file::writeToJson(path, profile));

    bool replaced = false;
    for (auto& existing : m_profiles) {
        if (existing.slug == profile.slug) {
            existing = profile;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        m_profiles.push_back(std::move(profile));
    }

    return geode::Ok();
}

geode::Result<> Manager::remove(std::string_view slug) {
    auto path = this->profilesDir() / (std::string(slug) + ".json");

    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec) {
        return geode::Err("Could not delete the profile file: {}", ec.message());
    }

    std::erase_if(m_profiles, [&](Profile const& p) { return p.slug == slug; });

    return geode::Ok();
}

ApplyMode Manager::currentMode() {
    return Mod::get()->getSettingValue<bool>("move-mod-files")
        ? ApplyMode::MoveFiles
        : ApplyMode::Toggle;
}

std::string Manager::activeProfile() {
    return Mod::get()->getSavedValue<std::string>("active-profile", "");
}

void Manager::setActiveProfile(std::string slug) {
    Mod::get()->setSavedValue<std::string>("active-profile", std::move(slug));
}

ApplyReport Manager::apply(Profile const& profile, ApplyMode mode, bool disableUnlisted) {
    ApplyReport report;

    // Snapshot first: applying a profile is the one destructive-feeling action
    // here, and this is what makes it reversible.
    if (Mod::get()->getSettingValue<bool>("auto-backup") && profile.slug != AUTO_BACKUP_SLUG) {
        auto backup = Profile::fromCurrentState("Auto backup");
        backup.slug = AUTO_BACKUP_SLUG;
        backup.description = fmt::format("State of your mods before '{}' was applied", profile.name);
        backup.creator = "Mod Profiles";
        if (auto res = this->save(std::move(backup), false); !res) {
            log::error("Could not write the automatic backup: {}", res.unwrapErr());
        }
    }

    if (mode == ApplyMode::MoveFiles) {
        // Files leaving the folder are parked under the name of the setup they
        // were in use under, which is the profile applied last.
        auto previous = Manager::activeProfile();
        auto moves = vault::applyProfile(profile, previous);
        report.movedOut = moves.movedOut;
        report.movedIn  = moves.movedIn;
        report.missing  = std::move(moves.missing);
        report.failed   = std::move(moves.failed);

        // Files are in place; make sure nothing that stayed is switched off.
        // Mods whose file just left are not disabled on top of that: they are
        // gone after the restart anyway, and disabling a dependency of a mod
        // that is still here would only produce noise.
        for (auto const& entry : profile.mods) {
            if (!entry.enabled) continue;

            auto* mod = Loader::get()->getLoadedMod(entry.id);
            if (!mod || mod->isInternal()) continue;
            if (mod->isOrWillBeEnabled()) continue;

            if (auto res = mod->enable(); !res) {
                auto error = res.unwrapErr();
                report.failed.push_back(fmt::format("{}: {}", mod->getName(), error));
                log::error("Could not enable {}: {}", entry.id, error);
                continue;
            }
            ++report.changed;
        }

        Manager::setActiveProfile(profile.slug);
        return report;
    }

    for (auto const& entry : profile.mods) {
        auto* mod = Loader::get()->getLoadedMod(entry.id);
        if (!mod || mod->isInternal()) {
            if (!mod) report.missing.push_back(entry.name.empty() ? entry.id : entry.name);
            continue;
        }

        bool wanted = entry.enabled;
        if (Manager::isProtected(entry.id)) {
            wanted = true;
        }

        if (mod->isOrWillBeEnabled() == wanted) continue;

        auto res = wanted ? mod->enable() : mod->disable();
        if (!res) {
            auto error = res.unwrapErr();
            report.failed.push_back(fmt::format("{}: {}", mod->getName(), error));
            log::error("Could not toggle {}: {}", entry.id, error);
            continue;
        }

        ++report.changed;
    }

    if (disableUnlisted) {
        // Mods the profile says nothing about are left alone unless the caller
        // asked for an exact match of the profile.
        for (auto* mod : Manager::togglableMods()) {
            auto id = std::string(mod->getID().view());
            if (profile.find(id)) continue;
            if (Manager::isProtected(id)) continue;
            if (!mod->isOrWillBeEnabled()) continue;

            if (auto res = mod->disable(); !res) {
                report.failed.push_back(fmt::format("{}: {}", mod->getName(), res.unwrapErr()));
                continue;
            }
            ++report.changed;
        }
    }

    Manager::setActiveProfile(profile.slug);
    return report;
}
