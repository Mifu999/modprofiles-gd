#pragma once

#include "../Manager.hpp"
#include "../Packs.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/file.hpp>

/// The main screen: the list of stored profiles and everything you can do with
/// one (apply, edit, export, delete), plus creating and importing.
class ProfilesPopup : public geode::Popup {
public:
    static ProfilesPopup* create();

    /// Rebuilds the list from the Manager. Safe to call at any time.
    void refresh();

protected:
    bool init();

    cocos2d::CCNode* createProfileCell(Profile const& profile, float width);
    cocos2d::CCNode* createEmptyLabel(float width);

    void onNewFromCurrent();
    void onImport();
    void onApply(Profile const& profile);
    void onEdit(Profile const& profile);
    void onExport(Profile const& profile);
    void pickExportTarget(Profile const& profile, bool withFiles);
    void onDelete(Profile const& profile);

    geode::ScrollLayer* m_list = nullptr;

    // Cancelled automatically when the popup is destroyed, so a file dialog
    // left open cannot call back into a dead node.
    geode::async::TaskHolder<geode::utils::file::PickResult> m_pickTask;
};
