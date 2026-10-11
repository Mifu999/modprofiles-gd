#include "ProfilesPopup.hpp"
#include "ProfileEditPopup.hpp"
#include "../Vault.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/loader/Dirs.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/general.hpp>

using namespace geode::prelude;

namespace {
    constexpr float POPUP_WIDTH  = 420.f;
    constexpr float POPUP_HEIGHT = 280.f;
    constexpr float CELL_HEIGHT  = 46.f;

    void notify(std::string const& text, NotificationIcon icon) {
        Notification::create(text, icon)->show();
    }

    /// Joins with a separator. Written out rather than using fmt::join, which
    /// lives in a header Geode does not pull in for us.
    std::string join(std::vector<std::string> const& parts, std::string_view separator) {
        std::string out;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i != 0) out += separator;
            out += parts[i];
        }
        return out;
    }

    /// Shows what happened and offers the restart that makes it take effect.
    void reportAndOfferRestart(ApplyReport const& report, std::string const& profileName) {
        std::string body = fmt::format("Applied <cy>{}</c>.", profileName);

        if (report.movedOut > 0 || report.movedIn > 0) {
            body += fmt::format(
                "\n<co>{}</c> mod file(s) moved to the vault, <cg>{}</c> brought back.",
                report.movedOut, report.movedIn
            );
        }
        if (report.changed > 0 || (report.movedOut == 0 && report.movedIn == 0)) {
            body += fmt::format("\n<cj>{}</c> mod(s) switched.", report.changed);
        }

        if (!report.missing.empty()) {
            body += fmt::format("\n<cl>{}</c> listed mod(s) are not installed.", report.missing.size());
        }
        if (!report.failed.empty()) {
            body += fmt::format("\n<cr>{}</c> mod(s) refused to switch:\n{}",
                report.failed.size(), join(report.failed, "\n"));
        }

        if (!report.needsRestart()) {
            body += "\n\nNothing had to change - your mods folder already matches this profile.";
            // FLAlertLayer takes a gd::string; go through c_str() so the
            // conversion is the unambiguous one from a C string.
            FLAlertLayer::create("Mod Profiles", body.c_str(), "OK")->show();
            return;
        }

        body += "\n\nGeometry Dash has to restart for this to take effect.";

        createQuickPopup(
            "Mod Profiles", body, "Later", "Restart",
            [](FLAlertLayer*, bool restart) {
                if (restart) {
                    utils::game::restart(true);
                }
            }
        );
    }

}

ProfilesPopup* ProfilesPopup::create() {
    auto ret = new ProfilesPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ProfilesPopup::init() {
    if (!Popup::init(POPUP_WIDTH, POPUP_HEIGHT)) return false;

    this->setID("profiles-popup"_spr);
    this->setTitle("Mod Profiles");

    // --- list background -----------------------------------------------------
    auto listSize = CCSize{ POPUP_WIDTH - 40.f, POPUP_HEIGHT - 105.f };

    auto listBG = CCScale9Sprite::create("square02b_001.png");
    listBG->setContentSize(listSize);
    listBG->setAnchorPoint({ 0.5f, 0.5f });
    listBG->ignoreAnchorPointForPosition(false);
    listBG->setPosition({ POPUP_WIDTH / 2.f, POPUP_HEIGHT / 2.f - 7.5f });
    listBG->setColor({ 0, 0, 0 });
    listBG->setOpacity(90);
    m_mainLayer->addChild(listBG);

    // --- the list itself -----------------------------------------------------
    auto innerSize = CCSize{ listSize.width - 8.f, listSize.height - 8.f };

    m_list = ScrollLayer::create(innerSize);
    m_list->setAnchorPoint({ 0.f, 0.f });
    m_list->ignoreAnchorPointForPosition(true);
    m_list->setPosition({
        listBG->getPositionX() - innerSize.width / 2.f,
        listBG->getPositionY() - innerSize.height / 2.f
    });
    m_list->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout(4.f));
    m_mainLayer->addChild(m_list);

    // --- bottom actions ------------------------------------------------------
    auto newSpr = ButtonSprite::create("Save setup", "bigFont.fnt", "GJ_button_01.png", 0.6f);
    newSpr->setScale(0.6f);
    auto newBtn = CCMenuItemExt::createSpriteExtra(newSpr, [this](CCMenuItemSpriteExtra*) {
        this->onNewFromCurrent();
    });

    auto importSpr = ButtonSprite::create("Import", "bigFont.fnt", "GJ_button_05.png", 0.6f);
    importSpr->setScale(0.6f);
    auto importBtn = CCMenuItemExt::createSpriteExtra(importSpr, [this](CCMenuItemSpriteExtra*) {
        this->onImport();
    });

    auto vaultSpr = ButtonSprite::create("Vault", "bigFont.fnt", "GJ_button_04.png", 0.6f);
    vaultSpr->setScale(0.6f);
    auto vaultBtn = CCMenuItemExt::createSpriteExtra(vaultSpr, [this](CCMenuItemSpriteExtra*) {
        this->onVault();
    });

    auto bottomMenu = CCMenu::create();
    bottomMenu->setPosition({ POPUP_WIDTH / 2.f, 23.f });
    bottomMenu->addChild(newBtn);
    newBtn->setPositionX(-110.f);
    bottomMenu->addChild(importBtn);
    importBtn->setPositionX(-5.f);
    bottomMenu->addChild(vaultBtn);
    vaultBtn->setPositionX(85.f);
    m_mainLayer->addChild(bottomMenu);

    this->refresh();

    return true;
}

void ProfilesPopup::refresh() {
    m_list->m_contentLayer->removeAllChildren();

    auto width = m_list->getContentSize().width;
    auto const& profiles = Manager::get()->profiles();

    if (profiles.empty()) {
        m_list->m_contentLayer->addChild(this->createEmptyLabel(width));
    }
    else {
        for (auto const& profile : profiles) {
            m_list->m_contentLayer->addChild(this->createProfileCell(profile, width));
        }
    }

    m_list->m_contentLayer->updateLayout();
    m_list->scrollToTop();
}

CCNode* ProfilesPopup::createEmptyLabel(float width) {
    auto node = CCNode::create();
    node->setContentSize({ width, 70.f });

    // Two single-line labels rather than one with a line break: CCLabelBMFont's
    // setAlignment has no address on Windows in the 2.2081 bindings, so a
    // centred multi-line label is not portable.
    auto first = CCLabelBMFont::create("No profiles yet", "bigFont.fnt");
    first->setScale(0.5f);
    first->setOpacity(190);
    first->setPosition({ width / 2.f, 45.f });
    node->addChild(first);

    auto second = CCLabelBMFont::create("Use \"Save current setup\" to make one", "chatFont.fnt");
    second->setScale(0.55f);
    second->setOpacity(150);
    second->setPosition({ width / 2.f, 23.f });
    node->addChild(second);

    return node;
}

CCNode* ProfilesPopup::createProfileCell(Profile const& profile, float width) {
    auto cell = CCNode::create();
    cell->setContentSize({ width, CELL_HEIGHT });

    auto bg = CCScale9Sprite::create("square02b_001.png");
    bg->setContentSize({ width, CELL_HEIGHT });
    bg->setAnchorPoint({ 0.f, 0.f });
    bg->ignoreAnchorPointForPosition(false);
    bg->setPosition({ 0.f, 0.f });
    bg->setColor({ 0, 0, 0 });
    bg->setOpacity(60);
    cell->addChild(bg);

    bool isBackup = profile.slug == AUTO_BACKUP_SLUG;

    auto name = CCLabelBMFont::create(profile.name.c_str(), "bigFont.fnt");
    name->setAnchorPoint({ 0.f, 0.5f });
    limitNodeWidth(name, 190.f, 0.5f, 0.2f);
    name->setPosition({ 10.f, CELL_HEIGHT - 16.f });
    if (isBackup) name->setColor({ 255, 200, 120 });
    cell->addChild(name);

    std::string info = fmt::format("{}/{} enabled", profile.enabledCount(), profile.mods.size());
    if (!profile.creator.empty()) {
        info += fmt::format(" - by {}", profile.creator);
    }

    auto infoLabel = CCLabelBMFont::create(info.c_str(), "chatFont.fnt");
    infoLabel->setAnchorPoint({ 0.f, 0.5f });
    limitNodeWidth(infoLabel, 190.f, 0.45f, 0.2f);
    infoLabel->setPosition({ 10.f, 13.f });
    infoLabel->setOpacity(160);
    cell->addChild(infoLabel);

    // Buttons, laid out right to left from the cell's right edge.
    auto menu = CCMenu::create();
    menu->setPosition({ width - 14.f, CELL_HEIGHT / 2.f });

    auto applySpr = ButtonSprite::create("Apply", "bigFont.fnt", "GJ_button_01.png", 0.5f);
    applySpr->setScale(0.62f);
    auto applyBtn = CCMenuItemExt::createSpriteExtra(applySpr, [this, profile](CCMenuItemSpriteExtra*) {
        this->onApply(profile);
    });
    applyBtn->setPositionX(-146.f);
    menu->addChild(applyBtn);

    auto editSpr = ButtonSprite::create("Edit", "bigFont.fnt", "GJ_button_05.png", 0.5f);
    editSpr->setScale(0.62f);
    auto editBtn = CCMenuItemExt::createSpriteExtra(editSpr, [this, profile](CCMenuItemSpriteExtra*) {
        this->onEdit(profile);
    });
    editBtn->setPositionX(-96.f);
    menu->addChild(editBtn);

    auto exportSpr = ButtonSprite::create("Share", "bigFont.fnt", "GJ_button_04.png", 0.5f);
    exportSpr->setScale(0.62f);
    auto exportBtn = CCMenuItemExt::createSpriteExtra(exportSpr, [this, profile](CCMenuItemSpriteExtra*) {
        this->onExport(profile);
    });
    exportBtn->setPositionX(-44.f);
    menu->addChild(exportBtn);

    auto trashSpr = CCSprite::createWithSpriteFrameName("GJ_trashBtn_001.png");
    trashSpr->setScale(0.62f);
    auto trashBtn = CCMenuItemExt::createSpriteExtra(trashSpr, [this, profile](CCMenuItemSpriteExtra*) {
        this->onDelete(profile);
    });
    trashBtn->setPositionX(-4.f);
    menu->addChild(trashBtn);

    cell->addChild(menu);

    return cell;
}

void ProfilesPopup::onNewFromCurrent() {
    // Snapshot now, name it in the editor - that way the user sees exactly what
    // is being saved before committing to it.
    auto profile = Profile::fromCurrentState("My setup");
    profile.slug = Manager::get()->uniqueSlug("my-setup");
    profile.creator = "";

    // Ref, not a raw this: the editor outlives this popup if the user closes
    // the list from under it.
    Ref<ProfilesPopup> self = this;
    ProfileEditPopup::create(profile, true, [self](Profile const& saved) {
        if (auto res = Manager::get()->save(saved, false); !res) {
            notify(fmt::format("Could not save: {}", res.unwrapErr()), NotificationIcon::Error);
            return;
        }
        notify(fmt::format("Saved '{}'", saved.name), NotificationIcon::Success);
        self->refresh();
    })->show();
}

void ProfilesPopup::onEdit(Profile const& profile) {
    Ref<ProfilesPopup> self = this;
    ProfileEditPopup::create(profile, false, [self](Profile const& saved) {
        if (auto res = Manager::get()->save(saved, false); !res) {
            notify(fmt::format("Could not save: {}", res.unwrapErr()), NotificationIcon::Error);
            return;
        }
        notify(fmt::format("Saved '{}'", saved.name), NotificationIcon::Success);
        self->refresh();
    })->show();
}

void ProfilesPopup::onApply(Profile const& profile) {
    auto mode  = Manager::currentMode();
    bool exact = Mod::get()->getSettingValue<bool>("disable-unlisted");

    std::string body = fmt::format(
        "Switch your mods to <cy>{}</c>?\n<cj>{}</c> of its <cj>{}</c> mod(s) are wanted.",
        profile.name, profile.enabledCount(), profile.mods.size()
    );

    if (mode == ApplyMode::MoveFiles) {
        // Count the actual moves, so the confirmation says what will happen
        // rather than what might.
        auto wanted  = vault::wantedIds(profile);
        auto inMods  = vault::scanModsDir();
        auto inVault = vault::scanVault();

        int toPark = 0;
        for (auto const& [id, path] : inMods) {
            if (!wanted.contains(id) && id != GEODE_MOD_ID) ++toPark;
        }
        int toRestore = 0;
        for (auto const& id : wanted) {
            if (!inMods.contains(id) && inVault.contains(id)) ++toRestore;
        }

        body += fmt::format(
            "\n\n<co>{}</c> mod file(s) will leave <cy>geode/mods</c> for the vault, "
            "<cg>{}</c> will come back from it.",
            toPark, toRestore
        );
        body += "\nDependencies of the mods you keep stay as well.";
    }
    else if (exact) {
        body += "\nMods this profile does not list will be <cr>disabled</c>.";
    }

    if (Mod::get()->getSettingValue<bool>("auto-backup") && profile.slug != AUTO_BACKUP_SLUG) {
        body += "\n\nYour current setup is saved as <co>Auto backup</c> first.";
    }

    auto copy = profile;
    Ref<ProfilesPopup> self = this;
    createQuickPopup(
        "Apply profile", body, "Cancel", "Apply",
        [copy, exact, mode, self](FLAlertLayer*, bool confirmed) {
            if (!confirmed) return;

            auto report = Manager::get()->apply(copy, mode, exact);
            self->refresh();
            reportAndOfferRestart(report, copy.name);
        }
    );
}

void ProfilesPopup::onVault() {
    auto stored = vault::scanVault();
    auto path = vault::dir().string();

    if (stored.empty()) {
        auto body = fmt::format(
            "The vault is empty - every mod file is in <cy>geode/mods</c>.\n\n"
            "When you apply a profile, the mods it does not list are parked here, "
            "in a folder named after the setup they were in use under:\n<cj>{}</c>",
            path
        );
        FLAlertLayer::create("Vault", body.c_str(), "OK")->show();
        return;
    }

    // One line per sub-folder, so it is obvious which setup each pile came from.
    std::vector<std::string> lines;
    for (auto const& [folder, mods] : vault::byFolder()) {
        auto const* profile = Manager::get()->bySlug(folder);
        lines.push_back(fmt::format(
            "<cy>{}</c>: {} file(s)",
            profile ? profile->name : folder,
            mods.size()
        ));
    }

    Ref<ProfilesPopup> self = this;
    createQuickPopup(
        "Vault",
        fmt::format(
            "<cj>{}</c> mod file(s) are parked outside your mods folder:\n<cl>{}</c>\n\n{}\n\n"
            "Put all of them back into <cy>geode/mods</c>?",
            stored.size(), path, join(lines, "\n")
        ),
        "Cancel", "Restore all",
        [self](FLAlertLayer*, bool confirmed) {
            if (!confirmed) return;

            auto res = vault::restoreAll();
            if (!res) {
                notify(fmt::format("Restore failed: {}", res.unwrapErr()), NotificationIcon::Error);
                return;
            }

            auto restored = res.unwrap();
            // Everything is back in one folder, so no setup is "active" now.
            Manager::setActiveProfile("");
            self->refresh();

            createQuickPopup(
                "Vault",
                fmt::format(
                    "<cg>{}</c> mod file(s) moved back into your mods folder.\n\n"
                    "Geometry Dash has to restart to load them.",
                    restored
                ),
                "Later", "Restart",
                [](FLAlertLayer*, bool restart) {
                    if (restart) utils::game::restart(true);
                }
            );
        }
    );
}

void ProfilesPopup::onDelete(Profile const& profile) {
    auto slug = profile.slug;
    auto name = profile.name;
    Ref<ProfilesPopup> self = this;

    createQuickPopup(
        "Delete profile",
        fmt::format("Delete <cy>{}</c>?\nThis only removes the profile - your mods are left alone.", name),
        "Cancel", "Delete",
        [self, slug, name](FLAlertLayer*, bool confirmed) {
            if (!confirmed) return;

            if (auto res = Manager::get()->remove(slug); !res) {
                notify(fmt::format("Could not delete: {}", res.unwrapErr()), NotificationIcon::Error);
                return;
            }
            notify(fmt::format("Deleted '{}'", name), NotificationIcon::Success);
            self->refresh();
        }
    );
}

void ProfilesPopup::onExport(Profile const& profile) {
    auto copy = profile;

    // Two honest ways to share a profile, so ask which one is wanted.
    createQuickPopup(
        "Share profile",
        fmt::format(
            "How should <cy>{}</c> be shared?\n\n"
            "<cj>List only</c> is a tiny file - whoever imports it downloads the mods from the Geode index.\n\n"
            "<cg>With files</c> also packs the .geode files of the enabled mods, so unpublished or private mods travel too. Much bigger file.",
            profile.name
        ),
        "List only", "With files",
        [self = Ref<ProfilesPopup>(this), copy](FLAlertLayer*, bool withFiles) {
            self->pickExportTarget(copy, withFiles);
        }
    );
}

void ProfilesPopup::pickExportTarget(Profile const& profile, bool withFiles) {
    auto copy = profile;

    utils::file::FilePickOptions options;
    options.defaultPath = dirs::getGameDir() / fmt::format("{}.{}", profile.slug, packs::PACK_EXTENSION);
    options.filters.push_back({
        "Mod Profile",
        { fmt::format("*.{}", packs::PACK_EXTENSION) }
    });

    m_pickTask.spawn(
        "ModProfiles export pick",
        utils::file::pick(utils::file::PickMode::SaveFile, options),
        [copy, withFiles](utils::file::PickResult result) {
            if (!result) return;                       // dialog failed
            auto picked = result.unwrap();
            if (!picked.has_value()) return;           // user cancelled

            std::vector<std::string> embed;
            if (withFiles) {
                for (auto const& entry : copy.mods) {
                    if (!entry.enabled) continue;
                    // Loaded or parked, both resolve to a file; anything else
                    // is skipped with a warning while packing.
                    embed.push_back(entry.id);
                }
            }

            auto res = packs::exportToFile(copy, *picked, embed);
            if (!res) {
                notify(fmt::format("Export failed: {}", res.unwrapErr()), NotificationIcon::Error);
                return;
            }

            auto embedded = res.unwrap();
            notify(
                embedded > 0
                    ? fmt::format("Exported with {} mod file(s)", embedded)
                    : "Profile exported",
                NotificationIcon::Success
            );
        }
    );
}

void ProfilesPopup::onImport() {
    utils::file::FilePickOptions options;
    options.filters.push_back({
        "Mod Profile",
        { fmt::format("*.{}", packs::PACK_EXTENSION), "*.zip" }
    });

    m_pickTask.spawn(
        "ModProfiles import pick",
        utils::file::pick(utils::file::PickMode::OpenFile, options),
        [](utils::file::PickResult result) {
            if (!result) return;
            auto picked = result.unwrap();
            if (!picked.has_value()) return;

            auto opened = packs::openFile(*picked);
            if (!opened) {
                notify(fmt::format("Could not read it: {}", opened.unwrapErr()), NotificationIcon::Error);
                return;
            }

            auto pack = std::make_shared<packs::OpenedPack>(std::move(opened.unwrap()));
            auto missing = packs::missingDownloadableMods(pack->profile, pack.get());
            auto packed = pack->packedMods();

            std::string body = fmt::format(
                "<cy>{}</c>{}\n<cj>{}</c> mod(s) listed, <cj>{}</c> to enable.",
                pack->profile.name,
                pack->profile.creator.empty() ? "" : fmt::format(" by {}", pack->profile.creator),
                pack->profile.mods.size(),
                pack->profile.enabledCount()
            );
            if (!pack->profile.description.empty()) {
                body += fmt::format("\n{}", pack->profile.description);
            }
            if (!packed.empty()) {
                body += fmt::format("\n<cg>{}</c> mod(s) ship inside this file.", packed.size());
            }
            if (!missing.empty()) {
                bool download = Mod::get()->getSettingValue<bool>("download-missing");
                body += fmt::format(
                    "\n<cl>{}</c> mod(s) are not installed - they will {}.",
                    missing.size(),
                    download ? "be downloaded" : "be skipped"
                );
            }
            body += "\n\nNothing is ever deleted: mods you already have stay installed.";

            createQuickPopup(
                "Import profile", body, "Cancel", "Install",
                [pack, missing](FLAlertLayer*, bool confirmed) {
                    if (!confirmed) return;

                    auto installed = packs::installPackedMods(*pack);
                    if (!installed) {
                        notify(fmt::format("Install failed: {}", installed.unwrapErr()), NotificationIcon::Error);
                        return;
                    }

                    auto profile = pack->profile;
                    profile.slug = Manager::get()->uniqueSlug(profile.slug);
                    if (auto res = Manager::get()->save(profile, false); !res) {
                        log::error("Could not store the imported profile: {}", res.unwrapErr());
                    }

                    bool download = Mod::get()->getSettingValue<bool>("download-missing");
                    if (!download || missing.empty()) {
                        auto report = Manager::get()->apply(
                            profile,
                            Manager::currentMode(),
                            Mod::get()->getSettingValue<bool>("disable-unlisted")
                        );
                        reportAndOfferRestart(report, profile.name);
                        return;
                    }

                    notify(fmt::format("Downloading {} mod(s)...", missing.size()), NotificationIcon::Loading);

                    ModDownloader::start(
                        missing,
                        [](std::size_t index, std::size_t total, std::string name) {
                            log::info("Downloading {}/{}: {}", index, total, name);
                        },
                        [profile](ModDownloader::Summary summary) {
                            if (!summary.failed.empty()) {
                                notify(
                                    fmt::format("{} download(s) failed", summary.failed.size()),
                                    NotificationIcon::Warning
                                );
                                log::error("Failed downloads: {}", join(summary.failed, " | "));
                            }

                            auto report = Manager::get()->apply(
                                profile,
                                Manager::currentMode(),
                                Mod::get()->getSettingValue<bool>("disable-unlisted")
                            );

                            std::string body = fmt::format(
                                "Imported <cy>{}</c>.\n<cg>{}</c> mod(s) downloaded, <cj>{}</c> switched.",
                                profile.name, summary.installed, report.changed
                            );
                            if (!summary.failed.empty()) {
                                body += fmt::format(
                                    "\n<cr>{}</c> could not be downloaded:\n{}",
                                    summary.failed.size(), join(summary.failed, "\n")
                                );
                            }
                            body += "\n\nGeometry Dash has to restart to load them.";

                            createQuickPopup(
                                "Mod Profiles", body, "Later", "Restart",
                                [](FLAlertLayer*, bool restart) {
                                    if (restart) utils::game::restart(true);
                                }
                            );
                        }
                    );
                }
            );
        }
    );
}
