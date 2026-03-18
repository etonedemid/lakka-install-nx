// Lakka Installer NX – entry point
//
// A Nintendo Switch homebrew application that downloads and installs Lakka
// (https://www.lakka.tv) using the Borealis UI toolkit.

#include <cstdlib>
#include <switch.h>
#include <borealis.hpp>

#include "util/net.hpp"
#include "util/config.hpp"
#include "views/main_view.hpp"

int main(int argc, char* argv[])
{
    // ── libnx service initialisation ───────────────────────────────────────
    // socketInitialize is handled by net::init() below.
    // nxlinkStdio() gives us printf output over nxlink for debugging.
#ifdef __SWITCH__
    nxlinkStdio();
#endif

    // ── Networking ─────────────────────────────────────────────────────────
    if (!net::init()) {
        brls::Logger::error("Failed to initialise networking");
        // Continue anyway – the user just won't be able to download.
    }

    // ── Load configuration ─────────────────────────────────────────────────
    config::Config cfg;
    cfg.load(); // silently fails if the file doesn't exist yet – defaults used

    // ── Borealis ───────────────────────────────────────────────────────────
    brls::Logger::setLogLevel(brls::LogLevel::DEBUG);

    if (!brls::Application::init("Lakka Installer NX")) {
        brls::Logger::error("Unable to initialise Borealis");
        net::exit();
        return EXIT_FAILURE;
    }

    // Create the main tabbed view and push it
    MainView* mainView = new MainView(cfg);
    brls::Application::pushView(mainView);

    // ── Main loop ──────────────────────────────────────────────────────────
    while (brls::Application::mainLoop()) {
        // All work happens inside Borealis callbacks and draw() overrides.
    }

    // ── Cleanup ────────────────────────────────────────────────────────────
    net::exit();

    return EXIT_SUCCESS;
}
