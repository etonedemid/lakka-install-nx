#pragma once

#include <borealis.hpp>
#include "../util/config.hpp"
#include "../util/lakka_api.hpp"

// Forward declaration
class InstallPage;

// MainView – the root TabFrame shown on launch.
//
// Tabs:
//   Home       – installed-version status and quick-update button
//   Stable     – list of stable releases
//   Nightly    – list of dev / nightly builds
//   Settings   – preferences (show dev builds, etc.)
class MainView : public brls::TabFrame {
public:
    MainView(config::Config& cfg);

private:
    config::Config& m_cfg;

    // Build each tab
    brls::List* createHomeTab();
    brls::List* createVersionTab(bool isDev);
    brls::List* createSettingsTab();

    // Populate a version list (runs a background fetch then updates the list)
    void populateVersionList(brls::List* list, bool isDev);

    // Trigger install for a given version
    void startInstall(const lakka::Version& version);
};
