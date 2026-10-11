#include "Manager.hpp"
#include "Packs.hpp"
#include "ui/ProfilesPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/utils/cocos.hpp>

using namespace geode::prelude;

$on_mod(Loaded) {
    // Packs are unzipped into a scratch directory; last session's leftovers are
    // of no use and would only grow forever.
    std::error_code ec;
    std::filesystem::remove_all(packs::workDir() / "open", ec);

    Manager::get()->loadAll();
}

class $modify(ProfilesMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto menu = this->getChildByID("bottom-menu");
        if (!menu) {
            // Another mod rebuilt the main menu. Not a reason to break it.
            log::warn("bottom-menu is missing, skipping the Mod Profiles button");
            return true;
        }

        auto sprite = CircleButtonSprite::createWithSprite(
            "profiles.png"_spr,
            0.85f,
            CircleBaseColor::Green,
            CircleBaseSize::MediumAlt
        );
        if (!sprite) {
            log::error("Could not create the button sprite");
            return true;
        }

        auto button = CCMenuItemExt::createSpriteExtra(sprite, [](CCMenuItemSpriteExtra*) {
            if (auto popup = ProfilesPopup::create()) {
                popup->show();
            }
        });
        button->setID("profiles-button"_spr);

        menu->addChild(button);
        menu->updateLayout();

        return true;
    }
};
