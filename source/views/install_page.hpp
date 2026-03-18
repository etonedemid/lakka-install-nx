#pragma once

#include <borealis.hpp>
#include <atomic>
#include <string>
#include "../util/net.hpp"
#include "../util/extract.hpp"
#include "../util/lakka_api.hpp"

// InstallPage — full-screen staged view that downloads and extracts Lakka.
class InstallPage : public brls::View
{
  public:
    InstallPage(brls::StagedAppletFrame* frame, const lakka::Version& version);
    ~InstallPage();

    void draw(NVGcontext* vg, int x, int y,
              unsigned width, unsigned height,
              brls::Style* style, brls::FrameContext* ctx) override;

    void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override;

    brls::View* getDefaultFocus() override;

    void willAppear(bool resetState = false) override;
    void willDisappear(bool resetState = false) override;

    static void pushInstallView(const lakka::Version& version);

  private:
    enum class State {
        CONNECTING,   // download thread started but Content-Length not yet known
        DOWNLOADING,
        EXTRACTING,
        DONE,
        ERROR
    };

    void startDownload();
    void startExtract();
    void updateSpeed(size_t currentBytes);

    brls::StagedAppletFrame* m_frame;
    lakka::Version           m_version;
    State                    m_state{State::CONNECTING};
    std::string              m_downloadPath;

    net::DownloadTask        m_downloadTask;
    extract::ExtractTask     m_extractTask;

    bool                     m_downloadStarted{false};

    // --- UI widgets ---
    brls::Label*             m_stageLabel  = nullptr;  // "Step 1/2  ·  Downloading"
    brls::Label*             m_titleLabel  = nullptr;  // version + state title
    brls::Label*             m_statsLabel  = nullptr;  // "72%   342 MB / 478 MB   7.3 MB/s"
    brls::Label*             m_detailLabel = nullptr;  // file being extracted / ETA
    brls::Button*            m_doneButton  = nullptr;

    // --- progress bar state (updated by poll task, read in draw) ---
    float                    m_progressPct{0.0f};   // 0 – 100
    size_t                   m_dlBytes{0};
    size_t                   m_dlTotal{0};

    // --- speed / ETA ---
    retro_time_t             m_speedLastTime{0};
    size_t                   m_speedLastBytes{0};
    float                    m_speedMBs{0.0f};
    int                      m_etaSec{-1};

    // --- indeterminate animation ---
    float                    m_animPhase{0.0f};
    retro_time_t             m_lastFrameTime{0};
    int                      m_progressBarY{0};  // set in layout(), read in draw()

    // --- extraction stuck detection ---
    size_t                   m_extractLastIdx{(size_t)-1};
    retro_time_t             m_extractStuckSince{0};
    bool                     m_extractStuck{false};
    retro_time_t             m_extractStartTime{0};

    brls::RepeatingTask*     m_pollTask = nullptr;
};
