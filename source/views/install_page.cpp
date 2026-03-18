#include "install_page.hpp"
#include "../util/config.hpp"

#include <borealis.hpp>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>

extern config::Config g_config;

// ── Poll task helper ─────────────────────────────────────────────────
class InstallPollTask : public brls::RepeatingTask
{
  public:
    InstallPollTask(std::function<void()> cb)
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

// ── helpers ──────────────────────────────────────────────────────────
static std::string formatBytes(size_t bytes)
{
    const char* units[] = {"B", "KB", "MB", "GB"};
    int i = 0;
    double sz = static_cast<double>(bytes);
    while (sz >= 1024.0 && i < 3)
    {
        sz /= 1024.0;
        i++;
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << sz << " " << units[i];
    return oss.str();
}

// ── InstallPage ──────────────────────────────────────────────────────
InstallPage::InstallPage(brls::StagedAppletFrame* frame,
                         const lakka::Version& version)
    : m_frame(frame)
    , m_version(version)
{
    m_downloadPath = std::string(config::DOWNLOAD_DIR) + "/" + m_version.filename;

    // Title label
    m_titleLabel = new brls::Label(brls::LabelStyle::MEDIUM,
        "Downloading " + m_version.version + "...", false);
    m_titleLabel->setHorizontalAlign(NVG_ALIGN_CENTER);
    m_titleLabel->setParent(this);

    // Progress display
    m_progressDisplay = new brls::ProgressDisplay();
    m_progressDisplay->setParent(this);
    m_progressDisplay->setProgress(0, 100);

    // Status label (shows percentage and download size)
    m_statusLabel = new brls::Label(brls::LabelStyle::REGULAR,
        "0%", false);
    m_statusLabel->setHorizontalAlign(NVG_ALIGN_CENTER);
    m_statusLabel->setParent(this);

    // Detail label (shows current file during extraction)
    m_detailLabel = new brls::Label(brls::LabelStyle::SMALL,
        "", false);
    m_detailLabel->setHorizontalAlign(NVG_ALIGN_CENTER);
    m_detailLabel->setParent(this);

    // Done/back button (hidden initially)
    m_doneButton = new brls::Button(brls::ButtonStyle::PRIMARY);
    m_doneButton->setLabel("Back");
    m_doneButton->setParent(this);
    m_doneButton->hide([](){}, false);
    m_doneButton->getClickEvent()->subscribe([](brls::View* view) {
        brls::Application::popView();
    });
}

InstallPage::~InstallPage()
{
    if (m_pollTask)
    {
        m_pollTask->stop();
        m_pollTask = nullptr;
    }

    m_downloadTask.cancel();
    m_extractTask.cancel();

    delete m_titleLabel;
    delete m_progressDisplay;
    delete m_statusLabel;
    delete m_detailLabel;
    delete m_doneButton;
}

void InstallPage::draw(NVGcontext* vg, int x, int y,
                       unsigned width, unsigned height,
                       brls::Style* style, brls::FrameContext* ctx)
{
    m_titleLabel->frame(ctx);
    m_progressDisplay->frame(ctx);
    m_statusLabel->frame(ctx);
    m_detailLabel->frame(ctx);
    m_doneButton->frame(ctx);
}

void InstallPage::layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash)
{
    unsigned px = this->x;
    unsigned py = this->y;
    unsigned pw = this->width;
    unsigned ph = this->height;

    unsigned margin = 40;
    unsigned centerX = px + pw / 2;

    // Title at top center
    m_titleLabel->setBoundaries(px + margin, py + ph / 4, pw - margin * 2, 40);
    m_titleLabel->invalidate();

    // Progress display centered
    unsigned pdW = 300;
    unsigned pdH = 60;
    m_progressDisplay->setBoundaries(centerX - pdW / 2, py + ph / 4 + 60, pdW, pdH);
    m_progressDisplay->invalidate();

    // Status below progress
    m_statusLabel->setBoundaries(px + margin, py + ph / 4 + 140, pw - margin * 2, 30);
    m_statusLabel->invalidate();

    // Detail below status
    m_detailLabel->setBoundaries(px + margin, py + ph / 4 + 180, pw - margin * 2, 30);
    m_detailLabel->invalidate();

    // Done button centered below
    unsigned btnW = 200;
    unsigned btnH = 60;
    m_doneButton->setBoundaries(centerX - btnW / 2, py + ph / 4 + 240, btnW, btnH);
    m_doneButton->invalidate();
}

brls::View* InstallPage::getDefaultFocus()
{
    if (!m_doneButton->isHidden())
        return m_doneButton;
    return nullptr;
}

void InstallPage::willAppear(bool resetState)
{
    brls::Logger::debug("InstallPage::willAppear resetState={} downloadStarted={}",
        resetState, m_downloadStarted);

    // AppletFrame::setContentView calls willAppear() before the view has been
    // laid out (no bounds).  Application::pushView then calls willAppear(true)
    // a second time once the view is properly set up.  Only run the real setup
    // on the second call to avoid spawning duplicate download threads and
    // calling m_progressDisplay->willAppear twice (which can crash the spinner).
    if (!m_downloadStarted) {
        m_downloadStarted = true;  // first call: remember we were here
        brls::Logger::debug("InstallPage::willAppear: first call (pre-layout), skipping startDownload");
        return;
    }

    m_progressDisplay->willAppear(resetState);
    startDownload();
}

void InstallPage::willDisappear(bool resetState)
{
    m_progressDisplay->willDisappear(resetState);
    if (m_pollTask)
        m_pollTask->pause();
}

// ── static helper ────────────────────────────────────────────────────
void InstallPage::pushInstallView(const lakka::Version& version)
{
    brls::Logger::debug("InstallPage::pushInstallView ver={} url={}",
        version.version, version.url);

    if (version.url.empty()) {
        brls::Logger::error("InstallPage::pushInstallView: empty URL, aborting");
        return;
    }
    brls::StagedAppletFrame* staged = new brls::StagedAppletFrame();
    staged->setTitle("Installing Lakka " + version.version);

    staged->addStage(new InstallPage(staged, version));

    brls::Application::pushView(staged);
}

// ── download / extract logic ─────────────────────────────────────────
void InstallPage::startDownload()
{
    brls::Logger::debug("InstallPage::startDownload url={}", m_version.url);

    // Create the download directory if it doesn't exist (all parent dirs too).
    // mkdir() only creates a single level so we do it manually.
    std::string dirPath = config::DOWNLOAD_DIR;
    std::string cur;
    for (size_t i = 0; i < dirPath.size(); ++i) {
        cur += dirPath[i];
        if (dirPath[i] == '/' || i == dirPath.size() - 1)
            mkdir(cur.c_str(), 0755);
    }
    brls::Logger::debug("InstallPage::startDownload dir ensured: {}", config::DOWNLOAD_DIR);

    m_state = State::DOWNLOADING;
    m_titleLabel->setText("Downloading " + m_version.version + "...");
    m_statusLabel->setText("0%");
    m_detailLabel->setText(m_version.filename);

    m_downloadTask.start(m_version.url, m_downloadPath);

    // Start poll timer — stop any previous task first
    if (m_pollTask)
    {
        m_pollTask->stop();
        m_pollTask = nullptr;
    }

    m_pollTask = new InstallPollTask([this]() {
        switch (m_state)
        {
        case State::DOWNLOADING:
        {
            float progress = m_downloadTask.getProgress();
            int pct = static_cast<int>(progress * 100.0f);
            m_progressDisplay->setProgress(pct, 100);
            m_statusLabel->setText(std::to_string(pct) + "%");

            size_t dl = m_downloadTask.getDownloaded();
            size_t total = m_downloadTask.getTotal();
            if (total > 0)
                m_detailLabel->setText(formatBytes(dl) + " / " + formatBytes(total));

            if (m_downloadTask.isComplete() && !m_downloadTask.hasError())
            {
                brls::Logger::debug("InstallPage: download complete, starting extract");
                startExtract();
            }
            else if (m_downloadTask.hasError())
            {
                brls::Logger::error("InstallPage: download error: {}",
                    m_downloadTask.getErrorMessage());
                m_state = State::ERROR;
                m_titleLabel->setText("Download Failed");
                m_statusLabel->setText(m_downloadTask.getErrorMessage());
                m_detailLabel->setText("");
                m_doneButton->show([](){});
                if (m_pollTask) m_pollTask->pause();
            }
            else if (m_downloadTask.isCancelled())
            {
                m_state = State::ERROR;
                m_titleLabel->setText("Download Cancelled");
                m_statusLabel->setText("");
                m_detailLabel->setText("");
                m_doneButton->show([](){});
                if (m_pollTask) m_pollTask->pause();
            }
            break;
        }

        case State::EXTRACTING:
        {
            float progress = m_extractTask.getProgress();
            int pct = static_cast<int>(progress * 100.0f);
            m_progressDisplay->setProgress(pct, 100);
            m_statusLabel->setText(std::to_string(pct) + "%");

            size_t cur = m_extractTask.getCurrent();
            size_t total = m_extractTask.getTotal();
            if (total > 0)
                m_detailLabel->setText(
                    std::to_string(cur) + "/" + std::to_string(total) +
                    " — " + m_extractTask.getCurrentFile());
            else
                m_detailLabel->setText(m_extractTask.getCurrentFile());

            if (m_extractTask.isComplete() && !m_extractTask.hasError())
            {
                brls::Logger::debug("InstallPage: extract complete");
                m_state = State::DONE;
                m_progressDisplay->setProgress(100, 100);
                m_titleLabel->setText("Installation Complete!");
                m_statusLabel->setText(m_version.version + " installed successfully");
                m_detailLabel->setText("");
                m_doneButton->show([](){});
                if (m_pollTask) m_pollTask->pause();

                // Save config
                g_config.setInstalledVersion(m_version.version);
                g_config.setInstalledChannel(m_version.isDev ? "dev" : "stable");
                g_config.save();

                // Remove downloaded archive
                std::remove(m_downloadPath.c_str());
            }
            else if (m_extractTask.hasError())
            {
                brls::Logger::error("InstallPage: extract error: {}",
                    m_extractTask.getErrorMessage());
                m_state = State::ERROR;
                m_titleLabel->setText("Extraction Failed");
                m_statusLabel->setText(m_extractTask.getErrorMessage());
                m_detailLabel->setText("");
                m_doneButton->show([](){});
                if (m_pollTask) m_pollTask->pause();
            }
            break;
        }

        case State::DONE:
        case State::ERROR:
            break;
        }
    });
    m_pollTask->start();
}

void InstallPage::startExtract()
{
    m_state = State::EXTRACTING;
    m_titleLabel->setText("Extracting " + m_version.version + "...");
    m_statusLabel->setText("0%");
    m_detailLabel->setText("");
    m_progressDisplay->setProgress(0, 100);

    std::string installPath = g_config.getInstallPath();
    if (installPath.empty())
        installPath = config::INSTALL_DIR;

    m_extractTask.start(m_downloadPath, installPath);
}
