#pragma once

#include <borealis.hpp>
#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include "../util/lakka_api.hpp"

// HomeTab — displays current installation info and quick action buttons.
// This is added as a tab in the main TabFrame.
class HomeTab : public brls::List
{
  public:
    HomeTab();
    ~HomeTab();

  private:
    void checkForUpdate();
    void installLatest();
    void confirmUninstall();
    void doUninstall();

    brls::ListItem* m_itemInstalledVersion = nullptr;
    brls::ListItem* m_itemCheckUpdate      = nullptr;
    brls::ListItem* m_itemInstallLatest    = nullptr;
    brls::ListItem* m_itemStatus           = nullptr;
    brls::ListItem* m_itemReboot           = nullptr;
    brls::ListItem* m_itemUninstall        = nullptr;

    // Async state
    std::thread           m_fetchThread;
    std::atomic<bool>     m_fetching{false};
    std::atomic<bool>     m_fetchDone{false};
    std::atomic<bool>     m_fetchError{false};
    std::vector<lakka::Version> m_fetchedVersions;
    std::string           m_latestVersionStr;
    std::string           m_latestUrl;
    std::string           m_latestFilename;
    bool                  m_latestIsDev{false};

    brls::RepeatingTask*  m_pollTask = nullptr;
};
