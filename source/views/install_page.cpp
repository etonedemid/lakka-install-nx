#include "install_page.hpp"
#include "../util/config.hpp"

#include <borealis.hpp>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>
#include <vector>
#include <algorithm>
#include <cmath>

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
    while (sz >= 1024.0 && i < 3) { sz /= 1024.0; i++; }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << sz << " " << units[i];
    return oss.str();
}

static std::string formatEta(int seconds)
{
    if (seconds < 0) return "";
    int m = seconds / 60;
    int s = seconds % 60;
    std::ostringstream oss;
    if (m > 0) oss << m << "m ";
    oss << s << "s remaining";
    return oss.str();
}

// ── InstallPage ──────────────────────────────────────────────────────
InstallPage::InstallPage(brls::StagedAppletFrame* frame,
                         const lakka::Version& version)
    : m_frame(frame)
    , m_version(version)
{
    m_downloadPath = std::string(config::DOWNLOAD_DIR) + "/" + m_version.filename;

    // Stage badge  e.g. "Step 1/2  ·  Downloading"
    m_stageLabel = new brls::Label(brls::LabelStyle::DESCRIPTION,
        "Step 1/2  \u00b7  Connecting...", false);
    m_stageLabel->setHorizontalAlign(NVG_ALIGN_CENTER);
    m_stageLabel->setParent(this);

    // Main title label
    m_titleLabel = new brls::Label(brls::LabelStyle::MEDIUM,
        "Lakka " + m_version.version, false);
    m_titleLabel->setHorizontalAlign(NVG_ALIGN_CENTER);
    m_titleLabel->setParent(this);

    // Stats: percentage · size · speed
    m_statsLabel = new brls::Label(brls::LabelStyle::SMALL,
        "", false);
    m_statsLabel->setHorizontalAlign(NVG_ALIGN_CENTER);
    m_statsLabel->setParent(this);

    // Detail: current filename during extraction / ETA
    m_detailLabel = new brls::Label(brls::LabelStyle::DESCRIPTION,
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

    delete m_stageLabel;
    delete m_titleLabel;
    delete m_statsLabel;
    delete m_detailLabel;
    delete m_doneButton;
}

void InstallPage::draw(NVGcontext* vg, int x, int y,
                       unsigned width, unsigned height,
                       brls::Style* style, brls::FrameContext* ctx)
{
    // ── advance indeterminate animation ──────────────────────────────
    retro_time_t now = cpu_features_get_time_usec();
    if (m_lastFrameTime == 0)
        brls::Logger::debug("InstallPage::draw: first frame");
    if (m_lastFrameTime != 0) {
        float dt = static_cast<float>(now - m_lastFrameTime) / 1000000.0f;
        m_animPhase += dt * 0.65f;  // ~1 sweep every 1.5 s
        if (m_animPhase >= 1.0f) m_animPhase -= 1.0f;
    }
    m_lastFrameTime = now;

    // ── labels ───────────────────────────────────────────────────────
    m_stageLabel->frame(ctx);
    m_titleLabel->frame(ctx);

    // ── custom progress bar ──────────────────────────────────────────
    const int   barMargin = 120;
    const float barH      = 18.0f;
    const float barR      = barH / 2.0f;
    float barX = static_cast<float>(x + barMargin);
    float barY = static_cast<float>(m_progressBarY);
    float barW = static_cast<float>(width - barMargin * 2);

    // Track (background)
    nvgBeginPath(vg);
    nvgRoundedRect(vg, barX, barY, barW, barH, barR);
    nvgFillColor(vg, nvgRGBA(128, 128, 128, 55));
    nvgFill(vg);

    bool deterministic = (m_progressPct > 0.0f &&
                          (m_state == State::DOWNLOADING ||
                           m_state == State::EXTRACTING));

    if (deterministic) {
        // Deterministic fill with gradient
        float fillW = std::max(barR * 2, barW * (m_progressPct / 100.0f));
        NVGpaint paint = nvgLinearGradient(vg,
            barX, barY, barX + fillW, barY,
            a(ctx->theme->highlightColor1),
            a(ctx->theme->highlightColor2));
        nvgBeginPath(vg);
        nvgRoundedRect(vg, barX, barY, fillW, barH, barR);
        nvgFillPaint(vg, paint);
        nvgFill(vg);

        // When stuck on the same archive file, overlay a shimmer on the
        // unfilled area so the user knows the CPU is still grinding.
        if (m_extractStuck && m_state == State::EXTRACTING) {
            float filledX = barX + barW * (m_progressPct / 100.0f);
            float restW   = barX + barW - filledX;
            if (restW > barR) {
                float sweepFrac = 0.45f;
                float sweepW    = restW * sweepFrac;
                float offset    = (restW + sweepW) * m_animPhase - sweepW;
                float rectX1    = std::max(filledX, filledX + offset);
                float rectX2    = std::min(barX + barW, filledX + offset + sweepW);
                if (rectX2 > rectX1) {
                    NVGpaint sp = nvgLinearGradient(vg,
                        filledX + offset, barY,
                        filledX + offset + sweepW, barY,
                        nvgRGBAf(ctx->theme->highlightColor2.r,
                                 ctx->theme->highlightColor2.g,
                                 ctx->theme->highlightColor2.b, 0.0f),
                        nvgRGBAf(ctx->theme->highlightColor2.r,
                                 ctx->theme->highlightColor2.g,
                                 ctx->theme->highlightColor2.b, 0.6f));
                    nvgBeginPath(vg);
                    nvgRoundedRect(vg, rectX1, barY, rectX2 - rectX1, barH, barR);
                    nvgFillPaint(vg, sp);
                    nvgFill(vg);
                }
            }
        }
    } else if (m_state == State::CONNECTING ||
               (m_state == State::DOWNLOADING && m_dlTotal == 0)) {
        // Indeterminate: sweeping shimmer ~38% wide
        float sweepFrac = 0.38f;
        float sweepW    = barW * sweepFrac;
        float offset    = (barW + sweepW) * m_animPhase - sweepW;
        float rectX1    = std::max(barX, barX + offset);
        float rectX2    = std::min(barX + barW, barX + offset + sweepW);
        if (rectX2 > rectX1) {
            NVGpaint paint = nvgLinearGradient(vg,
                barX + offset, barY,
                barX + offset + sweepW, barY,
                nvgRGBAf(ctx->theme->highlightColor1.r,
                         ctx->theme->highlightColor1.g,
                         ctx->theme->highlightColor1.b, 0.0f),
                a(ctx->theme->highlightColor1));
            nvgBeginPath(vg);
            nvgRoundedRect(vg, rectX1, barY, rectX2 - rectX1, barH, barR);
            nvgFillPaint(vg, paint);
            nvgFill(vg);
        }
    } else if (m_state == State::EXTRACTING) {
        // Extraction just started (0%), show full shimmer until first progress
        float sweepFrac = 0.38f;
        float sweepW    = barW * sweepFrac;
        float offset    = (barW + sweepW) * m_animPhase - sweepW;
        float rectX1    = std::max(barX, barX + offset);
        float rectX2    = std::min(barX + barW, barX + offset + sweepW);
        if (rectX2 > rectX1) {
            NVGpaint paint = nvgLinearGradient(vg,
                barX + offset, barY,
                barX + offset + sweepW, barY,
                nvgRGBAf(ctx->theme->highlightColor1.r,
                         ctx->theme->highlightColor1.g,
                         ctx->theme->highlightColor1.b, 0.0f),
                a(ctx->theme->highlightColor1));
            nvgBeginPath(vg);
            nvgRoundedRect(vg, rectX1, barY, rectX2 - rectX1, barH, barR);
            nvgFillPaint(vg, paint);
            nvgFill(vg);
        }
    } else if (m_state == State::DONE) {
        // Full bar
        NVGpaint paint = nvgLinearGradient(vg, barX, barY, barX + barW, barY,
            a(ctx->theme->highlightColor1), a(ctx->theme->highlightColor2));
        nvgBeginPath(vg);
        nvgRoundedRect(vg, barX, barY, barW, barH, barR);
        nvgFillPaint(vg, paint);
        nvgFill(vg);
    } else if (m_state == State::ERROR) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, barX, barY, barW, barH, barR);
        nvgFillColor(vg, nvgRGBA(200, 60, 60, 180));
        nvgFill(vg);
    }

    // ── remaining labels ─────────────────────────────────────────────
    m_statsLabel->frame(ctx);
    m_detailLabel->frame(ctx);
    m_doneButton->frame(ctx);
}

void InstallPage::layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash)
{
    unsigned px = this->x;
    unsigned py = this->y;
    unsigned pw = this->width;
    unsigned ph = this->height;
    unsigned cx = px + pw / 2;

    // Centre a block of ~250 px vertically
    const int blockH    = 250;
    int       blockTop  = static_cast<int>(py) + (static_cast<int>(ph) - blockH) / 2;

    unsigned margin = 80;

    // Stage badge (small, muted)
    m_stageLabel->setBoundaries(px + margin, blockTop, pw - margin * 2, 24);
    m_stageLabel->invalidate();

    // Version title
    m_titleLabel->setBoundaries(px + margin, blockTop + 32, pw - margin * 2, 44);
    m_titleLabel->invalidate();

    // Progress bar Y (drawn in draw(), store for reference)
    m_progressBarY = blockTop + 96;

    // Stats row (percent · size · speed)
    m_statsLabel->setBoundaries(px + margin, blockTop + 128, pw - margin * 2, 26);
    m_statsLabel->invalidate();

    // Detail / ETA
    m_detailLabel->setBoundaries(px + margin, blockTop + 162, pw - margin * 2, 26);
    m_detailLabel->invalidate();

    // Done button
    unsigned btnW = 220;
    unsigned btnH = 50;
    m_doneButton->setBoundaries(cx - btnW / 2, blockTop + 200, btnW, btnH);
    m_doneButton->invalidate();
}

brls::View* InstallPage::getDefaultFocus()
{
    if (!m_doneButton->isHidden())
        return m_doneButton;
    return this;
}

void InstallPage::willAppear(bool resetState)
{
    brls::Logger::debug("InstallPage::willAppear resetState={} downloadStarted={}",
        resetState, m_downloadStarted);

    if (!m_downloadStarted) {
        m_downloadStarted = true;
        brls::Logger::debug("InstallPage::willAppear: first call (pre-layout), skipping startDownload");
        return;
    }

    // Override + → exit while a transfer is active
    this->registerAction("Back", brls::Key::PLUS, [this]() -> bool {
        if (m_state == State::CONNECTING ||
            m_state == State::DOWNLOADING ||
            m_state == State::EXTRACTING) {
            brls::Dialog* dlg = new brls::Dialog(
                "A transfer is in progress.\nCancel and go back?");
            dlg->addButton("Keep Going",      [](brls::View*) {});
            dlg->addButton("Cancel & Back",   [](brls::View*) {
                brls::Application::popView();
            });
            dlg->setCancelable(true);
            dlg->open();
        } else {
            brls::Application::popView();
        }
        return true;
    });

    startDownload();
}

void InstallPage::willDisappear(bool resetState)
{
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

// ── speed helper ─────────────────────────────────────────────────────
void InstallPage::updateSpeed(size_t currentBytes)
{
    retro_time_t now = cpu_features_get_time_usec();
    if (m_speedLastTime == 0) {
        m_speedLastTime  = now;
        m_speedLastBytes = currentBytes;
        return;
    }
    retro_time_t elapsed = now - m_speedLastTime;
    if (elapsed < 500000) // update at most every 500 ms
        return;

    size_t delta = currentBytes > m_speedLastBytes ?
                   currentBytes - m_speedLastBytes : 0;
    float newSpeed = static_cast<float>(delta) /
                     (static_cast<float>(elapsed) / 1000000.0f) /
                     (1024.0f * 1024.0f);

    // Exponential moving average  (alpha = 0.35)
    m_speedMBs = m_speedMBs * 0.65f + newSpeed * 0.35f;

    if (m_dlTotal > 0 && m_speedMBs > 0.01f) {
        size_t remaining = m_dlTotal > currentBytes ? m_dlTotal - currentBytes : 0;
        m_etaSec = static_cast<int>(
            static_cast<float>(remaining) / (m_speedMBs * 1024.0f * 1024.0f));
    } else {
        m_etaSec = -1;
    }

    m_speedLastTime  = now;
    m_speedLastBytes = currentBytes;
}

// ── download / extract logic ─────────────────────────────────────────
void InstallPage::startDownload()
{
    brls::Logger::debug("InstallPage::startDownload url={}", m_version.url);

    // Create the download directory if it doesn't exist
    std::string dirPath = config::DOWNLOAD_DIR;
    std::string cur;
    for (size_t i = 0; i < dirPath.size(); ++i) {
        cur += dirPath[i];
        if (dirPath[i] == '/' || i == dirPath.size() - 1)
            mkdir(cur.c_str(), 0755);
    }
    brls::Logger::debug("InstallPage::startDownload dir ensured: {}", config::DOWNLOAD_DIR);

    m_state = State::CONNECTING;
    m_speedLastTime  = 0;
    m_speedLastBytes = 0;
    m_speedMBs       = 0.0f;
    m_etaSec         = -1;

    m_stageLabel->setText("Step 1/2  \u00b7  Downloading");
    m_titleLabel->setText("Lakka " + m_version.version);
    m_statsLabel->setText("Connecting...");
    m_detailLabel->setText(m_version.filename);

    m_downloadTask.start(m_version.url, m_downloadPath);

    if (m_pollTask) {
        m_pollTask->stop();
        m_pollTask = nullptr;
    }

    m_pollTask = new InstallPollTask([this]() {
        switch (m_state)
        {
        case State::CONNECTING:
        case State::DOWNLOADING:
        {
            size_t dl    = m_downloadTask.getDownloaded();
            size_t total = m_downloadTask.getTotal();

            m_dlBytes = dl;
            m_dlTotal = total;

            if (total > 0) {
                m_state = State::DOWNLOADING;
                float progress = static_cast<float>(dl) / static_cast<float>(total);
                m_progressPct  = progress * 100.0f;
            }

            updateSpeed(dl);

            // Build stats line:  "72%    342.1 MB / 478.3 MB    7.3 MB/s"
            std::ostringstream stats;
            if (total > 0) {
                int pct = static_cast<int>(m_progressPct);
                stats << pct << "%";
                stats << "    " << formatBytes(dl) << " / " << formatBytes(total);
            } else {
                stats << formatBytes(dl) << " received";
            }
            if (m_speedMBs > 0.05f) {
                std::ostringstream sp;
                sp << std::fixed << std::setprecision(1) << m_speedMBs;
                stats << "    " << sp.str() << " MB/s";
            }
            m_statsLabel->setText(stats.str());

            // Detail line: ETA while size is known, filename while connecting
            if (total > 0 && m_etaSec >= 0)
                m_detailLabel->setText(formatEta(m_etaSec));
            else
                m_detailLabel->setText(m_version.filename);

            if (m_downloadTask.isComplete() && !m_downloadTask.hasError()) {
                brls::Logger::debug("InstallPage: download complete, starting extract");
                startExtract();
            } else if (m_downloadTask.hasError()) {
                brls::Logger::error("InstallPage: download error: {}",
                    m_downloadTask.getErrorMessage());
                m_state = State::ERROR;
                m_stageLabel->setText("Download Failed");
                m_titleLabel->setText("Lakka " + m_version.version);
                m_statsLabel->setText(m_downloadTask.getErrorMessage());
                m_detailLabel->setText("");
                m_doneButton->show([](){});
                if (m_pollTask) m_pollTask->pause();
            } else if (m_downloadTask.isCancelled()) {
                m_state = State::ERROR;
                m_stageLabel->setText("Cancelled");
                m_statsLabel->setText("");
                m_detailLabel->setText("");
                m_doneButton->show([](){});
                if (m_pollTask) m_pollTask->pause();
            }
            break;
        }

        case State::EXTRACTING:
        {
            float progress = m_extractTask.getProgress();
            m_progressPct  = progress * 100.0f;

            size_t cur   = m_extractTask.getCurrent();
            size_t total = m_extractTask.getTotal();

            // Stuck detection: has the file index moved since last poll?
            retro_time_t now = cpu_features_get_time_usec();
            if (cur != m_extractLastIdx) {
                m_extractLastIdx    = cur;
                m_extractStuckSince = now;
                m_extractStuck      = false;
            } else if (now - m_extractStuckSince > 1000000) { // >1 s
                m_extractStuck = true;
            }

            // Elapsed
            int elapsedSec = static_cast<int>(
                (now - m_extractStartTime) / 1000000);
            int em = elapsedSec / 60;
            int es = elapsedSec % 60;

            std::ostringstream stats;
            stats << static_cast<int>(m_progressPct) << "%";
            if (total > 0)
                stats << "    file " << cur << " / " << total;
            m_statsLabel->setText(stats.str());

            std::string fname = m_extractTask.getCurrentFile();
            auto slash = fname.rfind('/');
            if (slash != std::string::npos) fname = fname.substr(slash + 1);

            std::ostringstream detail;
            if (!fname.empty()) detail << fname;
            if (m_extractStuck) {
                // Large file - show a note and elapsed time
                detail.str(""); detail.clear();
                if (!fname.empty()) detail << fname << "  ";
                detail << "(";
                if (em > 0) detail << em << "m ";
                detail << es << "s elapsed)";
            }
            m_detailLabel->setText(detail.str());

            if (m_extractTask.isComplete() && !m_extractTask.hasError()) {
                brls::Logger::debug("InstallPage: extract complete");
                m_state = State::DONE;
                m_progressPct = 100.0f;
                m_stageLabel->setText("Complete");
                m_titleLabel->setText("Installation Complete!");
                m_statsLabel->setText(m_version.version + " installed successfully");
                m_detailLabel->setText("");
                m_doneButton->show([](){});
                if (m_pollTask) m_pollTask->pause();

                g_config.setInstalledVersion(m_version.version);
                g_config.setInstalledChannel(m_version.isDev ? "dev" : "stable");
                g_config.save();

                // Save manifest
                auto relPaths = m_extractTask.getExtractedPaths();
                if (!relPaths.empty()) {
                    std::vector<std::string> absPaths;
                    absPaths.reserve(relPaths.size());
                    std::string root = g_config.getInstallPath();
                    if (root.empty()) root = config::INSTALL_DIR;
                    if (!root.empty() && root.back() != '/') root += '/';
                    for (const auto& p : relPaths)
                        absPaths.push_back(root + p);
                    g_config.saveManifest(absPaths);
                }

                std::remove(m_downloadPath.c_str());
            } else if (m_extractTask.hasError()) {
                brls::Logger::error("InstallPage: extract error: {}",
                    m_extractTask.getErrorMessage());
                m_state = State::ERROR;
                m_stageLabel->setText("Extraction Failed");
                m_statsLabel->setText(m_extractTask.getErrorMessage());
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
    m_state       = State::EXTRACTING;
    m_progressPct = 0.0f;
    m_extractLastIdx    = (size_t)-1;
    m_extractStuckSince = 0;
    m_extractStuck      = false;
    m_extractStartTime  = cpu_features_get_time_usec();
    m_stageLabel->setText("Step 2/2  \u00b7  Extracting");
    m_titleLabel->setText("Lakka " + m_version.version);
    m_statsLabel->setText("0%");
    m_detailLabel->setText("");

    std::string installPath = g_config.getInstallPath();
    if (installPath.empty())
        installPath = config::INSTALL_DIR;

    m_extractTask.start(m_downloadPath, installPath);
}

