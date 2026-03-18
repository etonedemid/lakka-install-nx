#include "install_page.hpp"
#include "../util/config.hpp"

#include <cstdio>
#include <sys/stat.h>

// ── helpers ────────────────────────────────────────────────────────────────────

static void mkdirs(const std::string& path)
{
    std::string cur;
    for (size_t i = 0; i < path.size(); ++i) {
        cur += path[i];
        if (path[i] == '/' || i == path.size() - 1)
            mkdir(cur.c_str(), 0755);
    }
}

static std::string formatBytes(size_t bytes)
{
    if (bytes >= 1048576)
        return std::to_string(bytes / 1048576) + "." +
               std::to_string((bytes % 1048576) * 10 / 1048576) + " MB";
    if (bytes >= 1024)
        return std::to_string(bytes / 1024) + " KB";
    return std::to_string(bytes) + " B";
}

// ── InstallPage ────────────────────────────────────────────────────────────────

InstallPage::InstallPage(const lakka::Version& version, config::Config& cfg)
    : m_version(version)
    , m_cfg(cfg)
{
    mkdirs(config::DOWNLOAD_DIR);
    m_archivePath = std::string(config::DOWNLOAD_DIR) + "/" + version.filename;

    // Register B button to cancel / go back
    registerAction("Cancel", brls::Key::B, [this]() {
        if (m_state == State::DOWNLOADING) {
            m_dlTask.cancel();
        } else if (m_state == State::EXTRACTING) {
            m_exTask.cancel();
        }
        brls::Application::popView();
        return true;
    });

    // Register A button for "Done" once finished
    registerAction("OK", brls::Key::A, [this]() {
        if (m_state == State::DONE || m_state == State::ERROR) {
            brls::Application::popView();
            return true;
        }
        return false;
    });
}

InstallPage::~InstallPage()
{
    // Clean up downloaded archive if still present
    // (leave it if the user might want to retry)
}

void InstallPage::onFocusGained()
{
    View::onFocusGained();
    if (!m_started) {
        m_started = true;
        startDownload();
    }
}

// ── Download / Extract ─────────────────────────────────────────────────────────

void InstallPage::startDownload()
{
    m_state = State::DOWNLOADING;
    m_dlTask.start(m_version.url, m_archivePath);
}

void InstallPage::startExtraction()
{
    m_state = State::EXTRACTING;
    std::string destDir = m_cfg.getInstallPath();
    m_exTask.start(m_archivePath, destDir);
}

// ── draw() ─────────────────────────────────────────────────────────────────────

void InstallPage::draw(NVGcontext* vg,
                       int x, int y,
                       unsigned width, unsigned height)
{
    // Background
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, nvgRGBA(45, 45, 45, 255));
    nvgFill(vg);

    int cx = x + width / 2;
    int barX = x + 80;
    int barW = width - 160;
    int barH = 28;

    // Title
    drawTitle(vg, cx, y + 80, width);

    switch (m_state) {
    // ── Downloading ────────────────────────────────────────────────────────
    case State::DOWNLOADING: {
        float pct = m_dlTask.getProgress();
        size_t dl = m_dlTask.getDownloaded();
        size_t tot = m_dlTask.getTotal();

        drawCenteredText(vg, cx, y + 180,
                         "Downloading...",
                         24.0f, nvgRGBA(220, 220, 220, 255));

        drawProgressBar(vg, barX, y + 220, barW, barH, pct);

        std::string pctText = std::to_string(static_cast<int>(pct * 100)) + "%";
        drawCenteredText(vg, cx, y + 270,
                         pctText,
                         20.0f, nvgRGBA(180, 180, 180, 255));

        if (tot > 0) {
            std::string sizeText = formatBytes(dl) + " / " + formatBytes(tot);
            drawCenteredText(vg, cx, y + 300,
                             sizeText,
                             18.0f, nvgRGBA(150, 150, 150, 255));
        }

        // State transitions
        if (m_dlTask.isComplete()) {
            if (m_dlTask.hasError()) {
                m_state = State::ERROR;
            } else {
                startExtraction();
            }
        }
        break;
    }
    // ── Extracting ─────────────────────────────────────────────────────────
    case State::EXTRACTING: {
        float pct = m_exTask.getProgress();
        size_t cur = m_exTask.getCurrent();
        size_t tot = m_exTask.getTotal();

        drawCenteredText(vg, cx, y + 180,
                         "Extracting to SD card...",
                         24.0f, nvgRGBA(220, 220, 220, 255));

        drawProgressBar(vg, barX, y + 220, barW, barH, pct);

        std::string countText = std::to_string(cur) + " / " +
                                std::to_string(tot) + " files";
        drawCenteredText(vg, cx, y + 270,
                         countText,
                         20.0f, nvgRGBA(180, 180, 180, 255));

        std::string curFile = m_exTask.getCurrentFile();
        if (!curFile.empty()) {
            // Truncate long paths
            if (curFile.size() > 60)
                curFile = "..." + curFile.substr(curFile.size() - 57);
            drawCenteredText(vg, cx, y + 300,
                             curFile,
                             16.0f, nvgRGBA(130, 130, 130, 255));
        }

        if (m_exTask.isComplete()) {
            if (m_exTask.hasError()) {
                m_state = State::ERROR;
            } else {
                m_state = State::DONE;

                // Update config
                m_cfg.setInstalledVersion(m_version.version);
                m_cfg.setInstalledChannel(m_version.isDev ? "dev" : "stable");
                m_cfg.save();

                // Clean up archive
                remove(m_archivePath.c_str());
            }
        }
        break;
    }
    // ── Done ───────────────────────────────────────────────────────────────
    case State::DONE: {
        drawCenteredText(vg, cx, y + 180,
                         "Installation Complete!",
                         28.0f, nvgRGBA(80, 220, 80, 255));

        drawCenteredText(vg, cx, y + 240,
                         "Lakka " + m_version.version +
                         " has been installed successfully.",
                         20.0f, nvgRGBA(200, 200, 200, 255));

        drawCenteredText(vg, cx, y + 300,
                         "Reboot your Switch to boot into Lakka.",
                         18.0f, nvgRGBA(150, 150, 150, 255));

        drawCenteredText(vg, cx, y + 360,
                         "Press A to return",
                         18.0f, nvgRGBA(120, 120, 120, 255));
        break;
    }
    // ── Error ──────────────────────────────────────────────────────────────
    case State::ERROR: {
        drawCenteredText(vg, cx, y + 180,
                         "Installation Failed",
                         28.0f, nvgRGBA(220, 60, 60, 255));

        std::string errMsg;
        if (m_dlTask.hasError())
            errMsg = m_dlTask.getErrorMessage();
        else if (m_exTask.hasError())
            errMsg = m_exTask.getErrorMessage();
        else
            errMsg = "Unknown error";

        drawCenteredText(vg, cx, y + 240,
                         errMsg,
                         20.0f, nvgRGBA(200, 200, 200, 255));

        drawCenteredText(vg, cx, y + 320,
                         "Press B to go back",
                         18.0f, nvgRGBA(120, 120, 120, 255));
        break;
    }
    }
}

// ── NanoVG drawing helpers ─────────────────────────────────────────────────────

void InstallPage::drawTitle(NVGcontext* vg, int cx, int y, unsigned /*w*/)
{
    std::string title = "Installing Lakka " + m_version.version;
    if (m_version.isDev)
        title += " (nightly)";

    nvgFontSize(vg, 30.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
    nvgText(vg, cx, y, title.c_str(), nullptr);
}

void InstallPage::drawProgressBar(NVGcontext* vg,
                                  int x, int y,
                                  int w, int h,
                                  float pct)
{
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 1.0f) pct = 1.0f;

    float r = h * 0.5f;

    // Background track
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, r);
    nvgFillColor(vg, nvgRGBA(70, 70, 70, 255));
    nvgFill(vg);

    // Filled portion
    float fillW = w * pct;
    if (fillW > 0) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, fillW, h, r);
        nvgFillColor(vg, nvgRGBA(0, 200, 83, 255));
        nvgFill(vg);
    }

    // Border
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, r);
    nvgStrokeColor(vg, nvgRGBA(120, 120, 120, 200));
    nvgStrokeWidth(vg, 1.0f);
    nvgStroke(vg);
}

void InstallPage::drawCenteredText(NVGcontext* vg,
                                   int cx, int y,
                                   const std::string& text,
                                   float fontSize,
                                   NVGcolor color)
{
    nvgFontSize(vg, fontSize);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    nvgFillColor(vg, color);
    nvgText(vg, cx, y, text.c_str(), nullptr);
}
