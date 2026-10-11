#include "Profile.hpp"
#include "Vault.hpp"

#include <algorithm>
#include <chrono>

using namespace geode::prelude;

std::string modSourceToString(ModSource source) {
    switch (source) {
        case ModSource::Index:  return "index";
        case ModSource::Packed: return "packed";
        case ModSource::Remote: return "remote";
        case ModSource::Unknown: break;
    }
    return "unknown";
}

ModSource modSourceFromString(std::string_view str) {
    if (str == "index")  return ModSource::Index;
    if (str == "packed") return ModSource::Packed;
    if (str == "remote") return ModSource::Remote;
    return ModSource::Unknown;
}

ProfileEntry const* Profile::find(std::string_view id) const {
    for (auto const& entry : mods) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

std::size_t Profile::enabledCount() const {
    return static_cast<std::size_t>(
        std::count_if(mods.begin(), mods.end(), [](ProfileEntry const& e) {
            return e.enabled;
        })
    );
}

void Profile::setEntry(ProfileEntry entry) {
    for (auto& existing : mods) {
        if (existing.id == entry.id) {
            existing = std::move(entry);
            return;
        }
    }
    mods.push_back(std::move(entry));
}

std::int64_t Profile::nowSeconds() {
    return static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

Profile Profile::fromCurrentState(std::string name) {
    Profile profile;
    profile.name = name;
    profile.slug = Profile::slugify(name);
    profile.created = Profile::nowSeconds();

    for (auto* mod : Loader::get()->getAllMods()) {
        // geode.loader itself is internal and cannot be toggled.
        if (mod->isInternal()) continue;

        ProfileEntry entry;
        entry.id      = std::string(mod->getID().view());
        entry.name    = std::string(mod->getName().view());
        entry.version = mod->getVersion().toNonVString();
        entry.enabled = mod->isOrWillBeEnabled();
        entry.source  = ModSource::Unknown;
        profile.mods.push_back(std::move(entry));
    }

    // Mods parked in the vault belong in the picture too. Without them, every
    // profile made after a switch would quietly forget the mods that are out of
    // the folder at that moment.
    for (auto const& [id, stored] : vault::scanVault()) {
        if (profile.find(id)) continue;

        ProfileEntry entry;
        entry.id      = id;
        entry.name    = id;
        entry.enabled = false;
        entry.source  = ModSource::Unknown;
        profile.mods.push_back(std::move(entry));
    }

    std::sort(profile.mods.begin(), profile.mods.end(), [](auto const& a, auto const& b) {
        return a.name < b.name;
    });

    return profile;
}

std::string Profile::slugify(std::string_view text) {
    std::string out;
    out.reserve(text.size());

    for (char c : text) {
        unsigned char uc = static_cast<unsigned char>(c);
        if ((uc >= 'a' && uc <= 'z') || (uc >= '0' && uc <= '9')) {
            out.push_back(static_cast<char>(c));
        }
        else if (uc >= 'A' && uc <= 'Z') {
            out.push_back(static_cast<char>(c - 'A' + 'a'));
        }
        else if (c == '-' || c == '_' || c == '.') {
            out.push_back(c);
        }
        else if (c == ' ') {
            // Collapse runs of separators into a single dash.
            if (!out.empty() && out.back() != '-') out.push_back('-');
        }
        // Everything else (including non-ASCII bytes) is dropped: the slug is a
        // file name on three different platforms.
    }

    while (!out.empty() && (out.back() == '-' || out.back() == '.')) {
        out.pop_back();
    }
    while (!out.empty() && (out.front() == '-' || out.front() == '.')) {
        out.erase(out.begin());
    }

    if (out.empty()) out = "profile";
    if (out.size() > 64) out.resize(64);

    return out;
}

geode::Result<Profile> matjson::Serialize<Profile>::fromJson(matjson::Value const& value) {
    if (!value.isObject()) {
        return geode::Err("Profile is not a JSON object");
    }

    auto format = static_cast<int>(value["format"].asInt().unwrapOr(PROFILE_FORMAT));
    if (format > PROFILE_FORMAT) {
        return geode::Err(
            "This profile was made by a newer version of Mod Profiles "
            "(format {}, this build understands {})",
            format, PROFILE_FORMAT
        );
    }

    Profile profile;
    profile.name        = value["name"].asString().unwrapOrDefault();
    profile.slug        = value["slug"].asString().unwrapOrDefault();
    profile.description = value["description"].asString().unwrapOrDefault();
    profile.creator     = value["creator"].asString().unwrapOrDefault();
    profile.version     = value["version"].asString().unwrapOr("1.0.0");
    profile.created     = static_cast<std::int64_t>(value["created"].asInt().unwrapOr(0));

    if (profile.name.empty()) {
        return geode::Err("Profile has no name");
    }
    if (profile.slug.empty()) {
        profile.slug = Profile::slugify(profile.name);
    }

    if (value.contains("mods") && value["mods"].isObject()) {
        for (auto const& item : value["mods"]) {
            auto key = item.getKey();
            if (!key.has_value() || key->empty()) continue;

            ProfileEntry entry;
            entry.id      = *key;
            entry.enabled = item["enabled"].asBool().unwrapOr(true);
            entry.source  = modSourceFromString(item["source"].asString().unwrapOrDefault());
            entry.url     = item["url"].asString().unwrapOrDefault();
            entry.version = item["version"].asString().unwrapOrDefault();
            entry.name    = item["name"].asString().unwrapOr(entry.id);

            profile.mods.push_back(std::move(entry));
        }
    }

    return geode::Ok(std::move(profile));
}

matjson::Value matjson::Serialize<Profile>::toJson(Profile const& profile) {
    auto mods = matjson::Value::object();
    for (auto const& entry : profile.mods) {
        auto obj = matjson::makeObject({
            { "enabled", entry.enabled },
            { "source",  modSourceToString(entry.source) },
            { "version", entry.version },
            { "name",    entry.name },
        });
        if (entry.source == ModSource::Remote && !entry.url.empty()) {
            obj.set("url", entry.url);
        }
        mods.set(entry.id, std::move(obj));
    }

    return matjson::makeObject({
        { "format",      matjson::Value(static_cast<std::intmax_t>(PROFILE_FORMAT)) },
        { "name",        profile.name },
        { "slug",        profile.slug },
        { "description", profile.description },
        { "creator",     profile.creator },
        { "version",     profile.version },
        { "created",     matjson::Value(static_cast<std::intmax_t>(profile.created)) },
        { "mods",        std::move(mods) },
    });
}
