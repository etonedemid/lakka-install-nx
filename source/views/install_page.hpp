#pragma once

#include <borealis.hpp>
#include "../util/config.hpp"
#include "../util/lakka_api.hpp"
#include "../util/net.hpp"
#include "../util/extract.hpp"

#include <atomic>
#include <string>

// InstallPage – full-screen view that handles downloading and extracting a
// Lakka release.  Pushed onto the borealis view stack; draws a custom
// progress bar and status text using NanoVG.
class InstallPage : public brls::View {
public:
    InstallPage(const lakka::Version& version, config::Config& cfg);
    ~InstallPage() override;

    void draw(NVGcontext* vg,
              int x, int y,
              unsigned width, unsigned height) override;

    brls::View* getDefaultFocus() override { return this; }

    void onFocusGained() override;

private:
    enum class State {
        DOWNLOADING,
        EXTRACTING,
        DONE,
        ERROR,
    };

    void startDownload();
    void startExtraction();

    // NanoVG drawing helpers
    void drawTitle(NVGcontext* vg, int cx, int y, unsigned w);
    void drawProgressBar(NVGcontext* vg, int x, int y, int w, int h, float pct);
    void drawCenteredText(NVGcontext* vg, int cx, int y,
                          const std::string& text, float fontSize,
                          NVGcolor color);

    lakka::Version      m_version;
    config::Config&     m_cfg;

    State               m_state{State::DOWNLOADING};

    net::DownloadTask   m_dlTask;
    extract::ExtractTask m_exTask;

    std::string         m_archivePath;
    bool                m_started{false};
};
