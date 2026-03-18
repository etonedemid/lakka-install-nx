#include "home_tab.hpp"
#include "../util/config.hpp"
#include "install_page.hpp"

#include <borealis.hpp>
#include <switch.h>
#include <algorithm>
#include <cstdio>
#include <sys/stat.h>

extern config::Config g_config;

// ── Poll task helper ─────────────────────────────────────────────────
class HomeTabPollTask : public brls::RepeatingTask
{
  public:
    HomeTabPollTask(std::function<void()> cb)
        : brls::RepeatingTask(100) // 100 ms
        , m_cb(std::move(cb))
    {}

    void run(retro_time_t currentTime) override
    {
        brls::RepeatingTask::run(currentTime);
        if (m_cb) m_cb();
    }

  private:
    std::function<void()> m_cb;
};

// ── HomeTab ──────────────────────────────────────────────────────────
HomeTab::HomeTab()
{
    // Current installation info
    std::string ver = g_config.getInstalledVersion();
    std::string ch  = g_config.getInstalledChannel();

    m_itemInstalledVersion = new brls::ListItem("Installed Version");
    if (!ver.empty())
        m_itemInstalledVersion->setValue(ver + " (" + ch + ")");
    else
        m_itemInstalledVersion->setValue("Not installed");

    // Check for updates button
    m_itemCheckUpdate = new brls::ListItem("Check for Updates",
        "Fetch the latest version info from Lakka servers");
    m_itemCheckUpdate->getClickEvent()->subscribe([this](brls::View* view) {
        if (m_fetching.load())
            return;
        m_itemCheckUpdate->setValue("Checking...");
        this->checkForUpdate();
    });

    // Status item (used for messages)
    m_itemStatus = new brls::ListItem("Status");
    m_itemStatus->setValue("Ready");

    // Add to the list
    this->addView(new brls::Header("Current Installation"));
    this->addView(m_itemInstalledVersion);
    this->addView(new brls::ListItemGroupSpacing(true));
    this->addView(new brls::Header("Quick Actions"));
    this->addView(m_itemCheckUpdate);
    this->addView(new brls::ListItemGroupSpacing(true));

    // Reboot into Lakka — only useful if something is installed
    m_itemReboot = new brls::ListItem("Reboot into Lakka",
        "Save all data, then reboot the Switch. Your bootloader will load Lakka.");
    m_itemReboot->getClickEvent()->subscribe([this](brls::View*) {
        brls::Dialog* dlg = new brls::Dialog(
            "The Switch will reboot now.\nMake sure Lakka is set up in your bootloader (Hekate).");
        dlg->addButton("Reboot", [](brls::View*) {
            bpcRebootSystem();
        });
        dlg->addButton("Cancel", [](brls::View*) {});
        dlg->setCancelable(true);
        dlg->open();
    });

    // Uninstall Lakka
    m_itemUninstall = new brls::ListItem("Uninstall Lakka",
        "Remove all Lakka files from the SD card using the install manifest.");
    m_itemUninstall->getClickEvent()->subscribe([this](brls::View*) {
        this->confirmUninstall();
    });

    this->addView(new brls::Header("Manage Installation"));
    this->addView(m_itemReboot);
    this->addView(m_itemUninstall);
    this->addView(new brls::ListItemGroupSpacing(true));

    this->addView(m_itemStatus);
}

HomeTab::~HomeTab()
{
    if (m_pollTask)
    {
        m_pollTask->stop();   // marks for deletion by TaskManager
        m_pollTask = nullptr;
    }
    if (m_fetchThread.joinable())
        m_fetchThread.detach();
}

void HomeTab::checkForUpdate()
{
    if (m_fetching.load())
        return;

    brls::Logger::debug("HomeTab::checkForUpdate starting thread");
    m_fetching.store(true);
    m_fetchDone.store(false);
    m_fetchError.store(false);

    if (m_fetchThread.joinable())
        m_fetchThread.detach();

    m_fetchThread = std::thread([this]() {
        brls::Logger::debug("HomeTab fetch thread entered");
        try
        {
            auto versions = lakka::fetchStableVersions();
            brls::Logger::debug("HomeTab fetch thread got {} versions", versions.size());
            if (versions.empty())
            {
                m_fetchError.store(true);
            }
            else
            {
                m_fetchedVersions = versions;
                auto latest = lakka::getLatest(versions);
                m_latestVersionStr = latest.version;
                m_latestFilename   = latest.filename;
                m_latestUrl        = latest.url;
                m_latestIsDev      = latest.isDev;
            }
        }
        catch (...)
        {
            brls::Logger::error("HomeTab fetch thread unknown exception");
            m_fetchError.store(true);
        }
        brls::Logger::debug("HomeTab fetch thread done, error={}", m_fetchError.load());
        m_fetching.store(false);
        m_fetchDone.store(true);
    });

    // Start poll timer — stop any previous task first
    if (m_pollTask)
    {
        m_pollTask->stop();
        m_pollTask = nullptr;
    }

    m_pollTask = new HomeTabPollTask([this]() {
        if (!m_fetchDone.load())
            return;

        // Pause from within callback (cannot stop/delete self)
        if (m_pollTask)
            m_pollTask->pause();

        if (m_fetchError.load())
        {
            m_itemCheckUpdate->setValue("Error");
            m_itemStatus->setValue("Failed to fetch versions");
            return;
        }

        std::string installed = g_config.getInstalledVersion();
        if (installed.empty())
        {
            m_itemCheckUpdate->setValue("Latest: " + m_latestVersionStr);
            m_itemStatus->setValue("Not installed — use Install Latest");
        }
        else if (installed == m_latestVersionStr)
        {
            m_itemCheckUpdate->setValue("Up to date");
            m_itemStatus->setValue("Running latest: " + installed);
        }
        else
        {
            m_itemCheckUpdate->setValue("Update: " + m_latestVersionStr);
            m_itemStatus->setValue("Installed: " + installed +
                                   " → Available: " + m_latestVersionStr);
        }
    });
    m_pollTask->start();
}

void HomeTab::confirmUninstall()
{
    std::string ver = g_config.getInstalledVersion();
    if (ver.empty()) {
        m_itemStatus->setValue("No Lakka installation recorded.");
        return;
    }

    auto manifest = g_config.loadManifest();
    if (manifest.empty()) {
        // No manifest — installed before this feature was added, or
        // the config dir was wiped.  Let the user know.
        brls::Dialog* dlg = new brls::Dialog(
            "No install manifest found.\n"
            "Cannot automatically remove Lakka files.\n"
            "Please delete the 'lakka' folder from your SD card manually.");
        dlg->addButton("OK", [](brls::View*) {});
        dlg->setCancelable(true);
        dlg->open();
        return;
    }

    brls::Dialog* dlg = new brls::Dialog(
        "Remove Lakka " + ver + " from the SD card?\n" +
        std::to_string(manifest.size()) + " files will be deleted.");
    dlg->addButton("Uninstall", [this](brls::View*) {
        this->doUninstall();
    });
    dlg->addButton("Cancel", [](brls::View*) {});
    dlg->setCancelable(true);
    dlg->open();
}

void HomeTab::doUninstall()
{
    auto manifest = g_config.loadManifest();

    // Sort so that deeper (longer) paths come first — files before directories
    std::sort(manifest.begin(), manifest.end(),
        [](const std::string& a, const std::string& b) {
            return a.size() > b.size();
        });

    size_t removed = 0;
    for (const auto& path : manifest) {
        // Try as a file first; if that fails, try as a directory
        if (std::remove(path.c_str()) == 0 || rmdir(path.c_str()) == 0)
            ++removed;
    }

    // Delete the manifest itself
    std::remove(config::MANIFEST_FILE);

    // Clear the installed version from config
    g_config.setInstalledVersion("");
    g_config.setInstalledChannel("");
    g_config.save();

    // Refresh the installed version label
    m_itemInstalledVersion->setValue("Not installed");
    m_itemStatus->setValue("Removed " + std::to_string(removed) + " of " +
        std::to_string(manifest.size()) + " files.");
    brls::Logger::debug("HomeTab::doUninstall removed {}/{} entries", removed, manifest.size());
}