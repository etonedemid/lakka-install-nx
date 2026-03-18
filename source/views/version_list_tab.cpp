#include "version_list_tab.hpp"
#include "install_page.hpp"

// ── Poll task helper ─────────────────────────────────────────────────
class VersionListPollTask : public brls::RepeatingTask
{
  public:
    VersionListPollTask(std::function<void()> cb)
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

// ── VersionListTab ───────────────────────────────────────────────────
VersionListTab::VersionListTab(const std::string& channel)
    : m_channel(channel)
{
    m_loadingItem = new brls::ListItem("Loading versions...");
    this->addView(m_loadingItem);
}

VersionListTab::~VersionListTab()
{
    if (m_pollTask)
        m_pollTask->pause();
    if (m_fetchThread.joinable())
        m_fetchThread.detach();
}

void VersionListTab::willAppear(bool resetState)
{
    brls::List::willAppear(resetState);

    if (!m_loaded && !m_fetching.load())
        fetchVersions();
}

void VersionListTab::fetchVersions()
{
    m_fetching.store(true);
    m_fetchDone.store(false);
    m_fetchError.store(false);
    m_errorMsg.clear();

    if (m_loadingItem)
        m_loadingItem->setValue("Fetching...");

    if (m_fetchThread.joinable())
        m_fetchThread.detach();

    m_fetchThread = std::thread([this]() {
        try
        {
            bool isDev = (m_channel == "nightly" || m_channel == "dev");
            if (isDev)
                m_versions = lakka::fetchNightlyVersions();
            else
                m_versions = lakka::fetchStableVersions();

            if (m_versions.empty())
            {
                m_errorMsg = "No versions found.";
                m_fetchError.store(true);
            }
        }
        catch (const std::exception& e)
        {
            m_errorMsg = std::string("Error: ") + e.what();
            m_fetchError.store(true);
        }
        catch (...)
        {
            m_errorMsg = "Unknown error fetching versions.";
            m_fetchError.store(true);
        }
        m_fetching.store(false);
        m_fetchDone.store(true);
    });

    // Start polling
    if (m_pollTask)
        m_pollTask->pause();

    m_pollTask = new VersionListPollTask([this]() {
        if (!m_fetchDone.load())
            return;

        if (m_pollTask)
            m_pollTask->pause();

        if (m_fetchError.load())
        {
            if (m_loadingItem)
                m_loadingItem->setValue(m_errorMsg.empty() ? "Error" : m_errorMsg);
            return;
        }

        populateList();
    });
    m_pollTask->start();
}

void VersionListTab::populateList()
{
    m_loaded = true;

    // Remove loading item
    this->clear();

    this->addView(new brls::Header(
        m_channel == "nightly" ? "Nightly Builds" : "Stable Releases"));

    for (const auto& ver : m_versions)
    {
        std::string label = ver.version;
        std::string sublabel = ver.size.empty() ? "" : ver.size;
        std::string desc = ver.date.empty() ? "" : ver.date;

        brls::ListItem* item = new brls::ListItem(label, desc, sublabel);
        item->setValue("Install");

        // Capture version by value
        lakka::Version capturedVer = ver;
        item->getClickEvent()->subscribe([capturedVer](brls::View* view) {
            InstallPage::pushInstallView(capturedVer);
        });

        this->addView(item);
    }
}
