#pragma once

#include <borealis.hpp>
#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include "../util/lakka_api.hpp"

// VersionListTab — fetches and shows a list of available Lakka versions.
// Constructed with a channel ("stable" or "nightly").
class VersionListTab : public brls::List
{
  public:
    VersionListTab(const std::string& channel);
    ~VersionListTab();

    void willAppear(bool resetState = false) override;

  private:
    void fetchVersions();
    void populateList();

    std::string m_channel;
    bool        m_loaded = false;

    // Async state
    std::thread           m_fetchThread;
    std::atomic<bool>     m_fetching{false};
    std::atomic<bool>     m_fetchDone{false};
    std::atomic<bool>     m_fetchError{false};
    std::vector<lakka::Version> m_versions;
    std::string           m_errorMsg;

    brls::RepeatingTask*  m_pollTask = nullptr;

    // UI
    brls::ListItem* m_loadingItem = nullptr;
};
