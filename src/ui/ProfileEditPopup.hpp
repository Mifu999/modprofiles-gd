#pragma once

#include "../Manager.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>

#include <functional>
#include <map>
#include <string>
#include <vector>

/// Editor for a single profile: its name and which mods it wants enabled.
/// Shows every installed mod, plus the mods the profile mentions that are not
/// installed, so a shared profile does not silently lose entries when saved.
class ProfileEditPopup : public geode::Popup {
public:
    /// @param profile the profile to edit (or a fresh snapshot for a new one)
    /// @param isNew changes the title and the default name selection
    /// @param onSave called with the edited profile when Save is pressed
    static ProfileEditPopup* create(
        Profile const& profile,
        bool isNew,
        std::function<void(Profile const&)> onSave
    );

protected:
    /// One line in the list.
    struct Row {
        std::string id;
        std::string name;
        std::string version;
        bool installed = true;
    };

    bool init(Profile const& profile, bool isNew, std::function<void(Profile const&)> onSave);

    void buildRows();
    void refreshList();
    cocos2d::CCNode* createRowNode(Row const& row, float width);

    void setAll(bool enabled);
    void invertAll();
    void snapshotCurrent();
    void onSave();

    Profile m_profile;
    bool m_isNew = false;
    std::function<void(Profile const&)> m_onSave;

    /// Authoritative enabled state per mod id, keyed independently of the UI.
    std::map<std::string, bool> m_states;
    std::vector<Row> m_rows;
    std::string m_query;

    geode::ScrollLayer* m_list = nullptr;
    geode::TextInput* m_nameInput = nullptr;
    geode::TextInput* m_searchInput = nullptr;
};
