#pragma once

#include <borealis.hpp>
#include <atomic>
#include <string>
#include "../util/net.hpp"
#include "../util/extract.hpp"
#include "../util/lakka_api.hpp"

// InstallPage — a full-screen staged view that downloads and extracts Lakka.
// Uses AppletFrame wrapping a custom view with ProgressDisplay.
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

    // Static helper to push the full install flow
    static void pushInstallView(const lakka::Version& version);

  private:
    enum class State {
        DOWNLOADING,
        EXTRACTING,
        DONE,
        ERROR
    };

    void startDownload();
    void startExtract();

    brls::StagedAppletFrame* m_frame;
    lakka::Version           m_version;
    State                    m_state{State::DOWNLOADING};
    std::string              m_downloadPath;

    net::DownloadTask        m_downloadTask;
    extract::ExtractTask     m_extractTask;

    bool                     m_downloadStarted{false}; // guard against double willAppear

    brls::ProgressDisplay*   m_progressDisplay = nullptr;
    brls::Label*             m_titleLabel      = nullptr;
    brls::Label*             m_statusLabel     = nullptr;
    brls::Label*             m_detailLabel     = nullptr;
    brls::Button*            m_doneButton      = nullptr;

    brls::RepeatingTask*     m_pollTask        = nullptr;
};
