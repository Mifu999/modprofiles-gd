#include "ProfileEditPopup.hpp"
#include "../Vault.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/cocos.hpp>

#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace {
    constexpr float POPUP_WIDTH  = 440.f;
    constexpr float POPUP_HEIGHT = 290.f;
    constexpr float ROW_HEIGHT   = 34.f;

    std::string toLower(std::string_view text) {
        std::string out(text);
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return out;
    }
}

ProfileEditPopup* ProfileEditPopup::create(
    Profile const& profile,
    bool isNew,
    std::function<void(Profile const&)> onSave
) {
    auto ret = new ProfileEditPopup();
    if (ret->init(profile, isNew, std::move(onSave))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ProfileEditPopup::init(
    Profile const& profile,
    bool isNew,
    std::function<void(Profile const&)> onSave
) {
    if (!Popup::init(POPUP_WIDTH, POPUP_HEIGHT)) return false;

    m_profile = profile;
    m_isNew   = isNew;
    m_onSave  = std::move(onSave);

    this->setID("profile-edit-popup"_spr);
    this->setTitle(isNew ? "Save current setup" : "Edit profile");

    for (auto const& entry : m_profile.mods) {
        m_states[entry.id] = entry.enabled;
    }
    this->buildRows();

    // --- name + snapshot -----------------------------------------------------
    m_nameInput = TextInput::create(210.f, "Profile name");
    m_nameInput->setMaxCharCount(48);
    m_nameInput->setString(m_profile.name.c_str());
    m_nameInput->setPosition({ 20.f + 105.f, POPUP_HEIGHT - 48.f });
    m_mainLayer->addChild(m_nameInput);

    auto snapshotSpr = ButtonSprite::create("Snapshot", "bigFont.fnt", "GJ_button_05.png", 0.5f);
    snapshotSpr->setScale(0.6f);
    auto snapshotBtn = CCMenuItemExt::createSpriteExtra(snapshotSpr, [this](CCMenuItemSpriteExtra*) {
        this->snapshotCurrent();
    });

    auto topMenu = CCMenu::create();
    topMenu->setPosition({ POPUP_WIDTH - 60.f, POPUP_HEIGHT - 48.f });
    topMenu->addChild(snapshotBtn);
    m_mainLayer->addChild(topMenu);

    // --- search + bulk toggles ----------------------------------------------
    m_searchInput = TextInput::create(150.f, "Search mods...");
    m_searchInput->setPosition({ 20.f + 75.f, POPUP_HEIGHT - 80.f });
    m_searchInput->setCallback([this](std::string const& query) {
        m_query = query;
        this->refreshList();
    });
    m_mainLayer->addChild(m_searchInput);

    auto allSpr = ButtonSprite::create("All", "bigFont.fnt", "GJ_button_01.png", 0.5f);
    allSpr->setScale(0.55f);
    auto allBtn = CCMenuItemExt::createSpriteExtra(allSpr, [this](CCMenuItemSpriteExtra*) {
        this->setAll(true);
    });

    auto noneSpr = ButtonSprite::create("None", "bigFont.fnt", "GJ_button_06.png", 0.5f);
    noneSpr->setScale(0.55f);
    auto noneBtn = CCMenuItemExt::createSpriteExtra(noneSpr, [this](CCMenuItemSpriteExtra*) {
        this->setAll(false);
    });

    auto invertSpr = ButtonSprite::create("Invert", "bigFont.fnt", "GJ_button_04.png", 0.5f);
    invertSpr->setScale(0.55f);
    auto invertBtn = CCMenuItemExt::createSpriteExtra(invertSpr, [this](CCMenuItemSpriteExtra*) {
        this->invertAll();
    });

    auto bulkMenu = CCMenu::create();
    bulkMenu->setPosition({ POPUP_WIDTH - 130.f, POPUP_HEIGHT - 80.f });
    bulkMenu->addChild(allBtn);
    allBtn->setPositionX(0.f);
    bulkMenu->addChild(noneBtn);
    noneBtn->setPositionX(42.f);
    bulkMenu->addChild(invertBtn);
    invertBtn->setPositionX(92.f);
    m_mainLayer->addChild(bulkMenu);

    // --- list ----------------------------------------------------------------
    auto listSize = CCSize{ POPUP_WIDTH - 40.f, POPUP_HEIGHT - 150.f };

    auto listBG = CCScale9Sprite::create("square02b_001.png");
    listBG->setContentSize(listSize);
    listBG->setAnchorPoint({ 0.5f, 0.5f });
    listBG->ignoreAnchorPointForPosition(false);
    listBG->setPosition({ POPUP_WIDTH / 2.f, 48.f + listSize.height / 2.f });
    listBG->setColor({ 0, 0, 0 });
    listBG->setOpacity(90);
    m_mainLayer->addChild(listBG);

    auto innerSize = CCSize{ listSize.width - 8.f, listSize.height - 8.f };

    m_list = ScrollLayer::create(innerSize);
    m_list->setAnchorPoint({ 0.f, 0.f });
    m_list->ignoreAnchorPointForPosition(true);
    m_list->setPosition({
        listBG->getPositionX() - innerSize.width / 2.f,
        listBG->getPositionY() - innerSize.height / 2.f
    });
    m_list->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout(3.f));
    m_mainLayer->addChild(m_list);

    // --- save ----------------------------------------------------------------
    auto saveSpr = ButtonSprite::create("Save", "bigFont.fnt", "GJ_button_01.png", 0.6f);
    auto saveBtn = CCMenuItemExt::createSpriteExtra(saveSpr, [this](CCMenuItemSpriteExtra*) {
        this->onSave();
    });
    auto saveMenu = CCMenu::createWithItem(saveBtn);
    saveMenu->setPosition({ POPUP_WIDTH / 2.f, 24.f });
    m_mainLayer->addChild(saveMenu);

    this->refreshList();

    return true;
}

void ProfileEditPopup::buildRows() {
    m_rows.clear();

    for (auto* mod : Manager::togglableMods()) {
        auto id = std::string(mod->getID().view());

        Row row;
        row.id        = id;
        row.name      = std::string(mod->getName().view());
        row.version   = mod->getVersion().toNonVString();
        row.installed = true;
        m_rows.push_back(std::move(row));

        // A mod installed after the profile was made starts off enabled only if
        // it is enabled right now - the profile should describe reality.
        if (!m_states.contains(id)) {
            m_states[id] = mod->isOrWillBeEnabled();
        }
    }

    auto known = [this](std::string const& id) {
        return std::any_of(m_rows.begin(), m_rows.end(), [&](Row const& row) {
            return row.id == id;
        });
    };

    // Mods parked in the vault: not loaded, but still yours. They have to be
    // listed, otherwise a profile saved now would drop them for good.
    for (auto const& [id, stored] : vault::scanVault()) {
        if (known(id)) continue;

        Row row;
        row.id        = id;
        row.name      = id;
        row.installed = false;
        row.parked    = true;
        m_rows.push_back(std::move(row));

        if (!m_states.contains(id)) {
            m_states[id] = false;
        }
    }

    // Entries the profile knows about that are neither installed nor parked.
    for (auto const& entry : m_profile.mods) {
        if (known(entry.id)) continue;

        Row row;
        row.id        = entry.id;
        row.name      = entry.name.empty() ? entry.id : entry.name;
        row.version   = entry.version;
        row.installed = false;
        row.parked    = false;
        m_rows.push_back(std::move(row));
    }
}

void ProfileEditPopup::refreshList() {
    m_list->m_contentLayer->removeAllChildren();

    auto width = m_list->getContentSize().width;
    auto query = toLower(m_query);

    int shown = 0;
    for (auto const& row : m_rows) {
        if (!query.empty()) {
            auto name = toLower(row.name);
            auto id   = toLower(row.id);
            if (name.find(query) == std::string::npos && id.find(query) == std::string::npos) {
                continue;
            }
        }
        m_list->m_contentLayer->addChild(this->createRowNode(row, width));
        ++shown;
    }

    if (shown == 0) {
        auto node = CCNode::create();
        node->setContentSize({ width, 50.f });

        auto label = CCLabelBMFont::create("Nothing matches that search", "chatFont.fnt");
        label->setScale(0.55f);
        label->setOpacity(150);
        label->setPosition({ width / 2.f, 25.f });
        node->addChild(label);

        m_list->m_contentLayer->addChild(node);
    }

    m_list->m_contentLayer->updateLayout();
    m_list->scrollToTop();
}

CCNode* ProfileEditPopup::createRowNode(Row const& row, float width) {
    auto node = CCNode::create();
    node->setContentSize({ width, ROW_HEIGHT });

    bool enabled = m_states.contains(row.id) ? m_states.at(row.id) : false;

    auto bg = CCScale9Sprite::create("square02b_001.png");
    bg->setContentSize({ width, ROW_HEIGHT });
    bg->setAnchorPoint({ 0.f, 0.f });
    bg->ignoreAnchorPointForPosition(false);
    bg->setPosition({ 0.f, 0.f });
    bg->setColor({ 0, 0, 0 });
    bg->setOpacity(50);
    node->addChild(bg);

    auto name = CCLabelBMFont::create(row.name.c_str(), "bigFont.fnt");
    name->setAnchorPoint({ 0.f, 0.5f });
    limitNodeWidth(name, 230.f, 0.45f, 0.2f);
    name->setPosition({ 10.f, ROW_HEIGHT / 2.f + 5.f });
    if (row.parked) {
        name->setColor({ 255, 200, 120 });
    }
    else if (!row.installed) {
        name->setColor({ 160, 160, 160 });
    }
    else if (!enabled) {
        name->setColor({ 255, 170, 170 });
    }
    node->addChild(name);

    std::string sub = row.version.empty() ? row.id : fmt::format("{}  v{}", row.id, row.version);
    if (row.parked)           sub = fmt::format("in the vault  -  {}", row.id);
    else if (!row.installed)  sub = fmt::format("not installed  -  {}", row.id);

    auto subLabel = CCLabelBMFont::create(sub.c_str(), "chatFont.fnt");
    subLabel->setAnchorPoint({ 0.f, 0.5f });
    limitNodeWidth(subLabel, 240.f, 0.38f, 0.2f);
    subLabel->setPosition({ 10.f, 9.f });
    subLabel->setOpacity(140);
    node->addChild(subLabel);

    auto menu = CCMenu::create();
    menu->setPosition({ width - 20.f, ROW_HEIGHT / 2.f });

    if (row.installed) {
        auto infoSpr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
        infoSpr->setScale(0.6f);
        auto id = row.id;
        auto infoBtn = CCMenuItemExt::createSpriteExtra(infoSpr, [id](CCMenuItemSpriteExtra*) {
            (void)geode::openInfoPopup(id);
        });
        infoBtn->setPositionX(-46.f);
        menu->addChild(infoBtn);
    }

    auto rowID = row.id;
    bool installed = row.installed;
    bool parked = row.parked;
    // Held by reference so the callback can recolour just this row. Rebuilding
    // the whole list from inside a button's own callback would free the button
    // while it is still running.
    Ref<CCLabelBMFont> nameRef = name;

    auto toggler = CCMenuItemExt::createTogglerWithStandardSprites(
        0.6f,
        [this, rowID, installed, parked, nameRef](CCMenuItemToggler* item) {
            if (Manager::isProtected(rowID)) {
                // Keep it on and say why, instead of silently undoing the tap.
                m_states[rowID] = true;
                item->toggle(true);
                Notification::create(
                    "Mod Profiles keeps itself enabled (see its settings)",
                    NotificationIcon::Info
                )->show();
                return;
            }

            bool next = !(m_states.contains(rowID) && m_states.at(rowID));
            m_states[rowID] = next;
            // Drive the visual from our own state so the two can never disagree.
            item->toggle(next);

            if (nameRef) {
                if (parked)          nameRef->setColor({ 255, 200, 120 });
                else if (!installed) nameRef->setColor({ 160, 160, 160 });
                else if (next)       nameRef->setColor({ 255, 255, 255 });
                else                 nameRef->setColor({ 255, 170, 170 });
            }
        }
    );
    toggler->toggle(enabled);
    toggler->setPositionX(0.f);
    menu->addChild(toggler);

    node->addChild(menu);

    return node;
}

void ProfileEditPopup::setAll(bool enabled) {
    for (auto const& row : m_rows) {
        if (Manager::isProtected(row.id)) {
            m_states[row.id] = true;
            continue;
        }
        m_states[row.id] = enabled;
    }
    this->refreshList();
}

void ProfileEditPopup::invertAll() {
    for (auto const& row : m_rows) {
        if (Manager::isProtected(row.id)) {
            m_states[row.id] = true;
            continue;
        }
        bool current = m_states.contains(row.id) && m_states.at(row.id);
        m_states[row.id] = !current;
    }
    this->refreshList();
}

void ProfileEditPopup::snapshotCurrent() {
    for (auto* mod : Manager::togglableMods()) {
        m_states[std::string(mod->getID().view())] = mod->isOrWillBeEnabled();
    }
    this->refreshList();

    Notification::create("Copied your current mod states", NotificationIcon::Success)->show();
}

void ProfileEditPopup::onSave() {
    auto name = std::string(m_nameInput->getString());
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    while (!name.empty() && name.back() == ' ') name.pop_back();

    if (name.empty()) {
        Notification::create("Give the profile a name first", NotificationIcon::Warning)->show();
        return;
    }

    Profile saved = m_profile;
    saved.name = name;
    if (saved.slug.empty()) {
        saved.slug = Manager::get()->uniqueSlug(Profile::slugify(name));
    }
    if (saved.created == 0) {
        saved.created = Profile::nowSeconds();
    }

    saved.mods.clear();
    for (auto const& row : m_rows) {
        ProfileEntry entry;
        entry.id      = row.id;
        entry.name    = row.name;
        entry.version = row.version;
        entry.enabled = m_states.contains(row.id) ? m_states.at(row.id) : false;

        // Keep whatever the original profile knew about where this mod comes
        // from, so an imported pack does not lose its remote URLs on edit.
        if (auto const* previous = m_profile.find(row.id)) {
            entry.source = previous->source;
            entry.url    = previous->url;
        }

        saved.mods.push_back(std::move(entry));
    }

    if (m_onSave) m_onSave(saved);

    this->onClose(nullptr);
}
