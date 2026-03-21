#include "version_list_tab.hpp"
#include "install_page.hpp"

#include <borealis.hpp>

// ── Poll task helper ─────────────────────────────────────────────────
class VersionListPollTask : public brls::RepeatingTask
{
  public:
    VersionListPollTask(std::function<void()> cb)
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

// ── VersionListTab ───────────────────────────────────────────────────
VersionListTab::VersionListTab(const std::string& channel)
    : m_channel(channel)
{
    m_loadingItem = new brls::ListItem("Loading versions...");
    this->addView(m_loadingItem);
}

VersionListTab::~VersionListTab()
{
    m_stopRequested.store(true);

    if (m_pollTask)
    {
        m_pollTask->stop();
        m_pollTask = nullptr;
    }
    if (m_fetchThread.joinable())
        m_fetchThread.join();
}

void VersionListTab::willAppear(bool resetState)
{
    brls::Logger::debug("VersionListTab[{}]::willAppear loaded={} fetching={}",
        m_channel, m_loaded, m_fetching.load());
    brls::List::willAppear(resetState);

    if (!m_loaded && !m_fetching.load())
        fetchVersions();
}

void VersionListTab::fetchVersions()
{
    brls::Logger::debug("VersionListTab[{}]::fetchVersions starting thread", m_channel);
    m_fetching.store(true);
    m_fetchDone.store(false);
    m_fetchError.store(false);
    m_errorMsg.clear();

    if (m_loadingItem)
        m_loadingItem->setValue("Fetching...");

    if (m_fetchThread.joinable())
        m_fetchThread.detach();

    m_fetchThread = std::thread([this]() {
        brls::Logger::debug("VersionListTab[{}] fetch thread entered", m_channel);
        try
        {
            bool isDev = (m_channel == "nightly" || m_channel == "dev");
            std::vector<lakka::Version> versions;
            if (isDev)
                versions = lakka::fetchNightlyVersions();
            else
                versions = lakka::fetchStableVersions();

            if (m_stopRequested.load())
                return;

            brls::Logger::debug("VersionListTab[{}] fetch thread got {} versions",
                m_channel, versions.size());

            if (versions.empty())
            {
                m_errorMsg = "No versions found.";
                m_fetchError.store(true);
            }
            else
            {
                m_versions = std::move(versions);
            }
        }
        catch (const std::exception& e)
        {
            if (m_stopRequested.load()) return;
            brls::Logger::error("VersionListTab[{}] fetch thread exception: {}", m_channel, e.what());
            m_errorMsg = std::string("Error: ") + e.what();
            m_fetchError.store(true);
        }
        catch (...)
        {
            if (m_stopRequested.load()) return;
            brls::Logger::error("VersionListTab[{}] fetch thread unknown exception", m_channel);
            m_errorMsg = "Unknown error fetching versions.";
            m_fetchError.store(true);
        }
        if (m_stopRequested.load())
            return;
        brls::Logger::debug("VersionListTab[{}] fetch thread done, error={}",
            m_channel, m_fetchError.load());
        m_fetching.store(false);
        m_fetchDone.store(true);
    });

    // Start polling — stop any previous task first
    if (m_pollTask)
    {
        m_pollTask->stop();
        m_pollTask = nullptr;
    }

    m_pollTask = new VersionListPollTask([this]() {
        if (!m_fetchDone.load())
            return;

        brls::Logger::debug("VersionListTab[{}] poll fired, error={}",
            m_channel, m_fetchError.load());

        // Pause from within callback (cannot stop/delete self)
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
    brls::Logger::debug("VersionListTab[{}]::fetchVersions poll task started", m_channel);
}

void VersionListTab::populateList()
{
    brls::Logger::debug("VersionListTab[{}]::populateList {} versions", m_channel, m_versions.size());
    m_loaded = true;

    bool restoreFocusToList = false;
    for (brls::View* focused = brls::Application::getCurrentFocus(); focused != nullptr; focused = focused->getParent())
    {
        if (focused == this)
        {
            restoreFocusToList = true;
            break;
        }
    }

    // Clear focus before destroying the old views so Application::currentFocus
    // is not left as a dangling pointer after clear() deletes them.
    if (restoreFocusToList)
        brls::Application::giveFocus(nullptr);

    // Remove loading item.  After clear() the pointer is dangling — null it so
    // nothing can accidentally call methods on the freed object.
    m_loadingItem = nullptr;
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

    // Only restore focus to this list if it was focused before rebuilding it.
    if (restoreFocusToList)
        brls::Application::giveFocus(this->getDefaultFocus());
}
