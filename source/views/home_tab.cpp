#include "home_tab.hpp"
#include "../util/config.hpp"
#include "install_page.hpp"

extern config::Config g_config;

// ── Poll task helper ─────────────────────────────────────────────────
class HomeTabPollTask : public brls::RepeatingTask
{
  public:
    HomeTabPollTask(std::function<void()> cb)
        : brls::RepeatingTask(100000) // 100 ms
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

    // Install latest stable
    m_itemInstallLatest = new brls::ListItem("Install Latest Stable",
        "Download and install the latest stable Lakka build");
    m_itemInstallLatest->getClickEvent()->subscribe([this](brls::View* view) {
        this->installLatest();
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
    this->addView(m_itemInstallLatest);
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

    m_fetching.store(true);
    m_fetchDone.store(false);
    m_fetchError.store(false);

    if (m_fetchThread.joinable())
        m_fetchThread.detach();

    m_fetchThread = std::thread([this]() {
        try
        {
            auto versions = lakka::fetchStableVersions();
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
            m_fetchError.store(true);
        }
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

void HomeTab::installLatest()
{
    // If we already fetched the latest, go straight to install
    if (!m_latestUrl.empty())
    {
        lakka::Version ver;
        ver.version  = m_latestVersionStr;
        ver.filename = m_latestFilename;
        ver.url      = m_latestUrl;
        ver.isDev    = m_latestIsDev;

        InstallPage::pushInstallView(ver);
        return;
    }

    // Otherwise fetch first, then install
    if (m_fetching.load())
    {
        m_itemStatus->setValue("Already fetching...");
        return;
    }

    m_itemStatus->setValue("Fetching latest...");
    m_fetching.store(true);
    m_fetchDone.store(false);
    m_fetchError.store(false);

    if (m_fetchThread.joinable())
        m_fetchThread.detach();

    m_fetchThread = std::thread([this]() {
        try
        {
            auto versions = lakka::fetchStableVersions();
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
            m_fetchError.store(true);
        }
        m_fetching.store(false);
        m_fetchDone.store(true);
    });

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
            m_itemStatus->setValue("Error fetching versions.");
            return;
        }

        lakka::Version ver;
        ver.version  = m_latestVersionStr;
        ver.filename = m_latestFilename;
        ver.url      = m_latestUrl;
        ver.isDev    = m_latestIsDev;

        InstallPage::pushInstallView(ver);
    });
    m_pollTask->start();
}
