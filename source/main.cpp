#include <borealis.hpp>

#include "util/config.hpp"
#include "util/net.hpp"
#include "views/home_tab.hpp"
#include "views/version_list_tab.hpp"
#include "views/settings_tab.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

// Global configuration
config::Config g_config;

int main(int argc, char* argv[])
{
    // Init networking
    net::init();

    // Load configuration
    g_config.load();

    brls::Logger::setLogLevel(brls::LogLevel::DEBUG);
    i18n::loadTranslations();

    // Init borealis
    if (!brls::Application::init("Lakka Installer NX"))
    {
        brls::Logger::error("Unable to init borealis application");
        net::exit();
        return EXIT_FAILURE;
    }

    // Root TabFrame
    brls::TabFrame* rootFrame = new brls::TabFrame();
    rootFrame->setTitle("Lakka Installer NX");
    rootFrame->setIcon(BOREALIS_ASSET("icon/icon.jpg"));

    // Tabs
    rootFrame->addTab("Home", new HomeTab());
    rootFrame->addTab("Stable", new VersionListTab("stable"));
    rootFrame->addSeparator();
    rootFrame->addTab("Nightly", new VersionListTab("nightly"));
    rootFrame->addSeparator();
    rootFrame->addTab("Settings", new SettingsTab());

    // Push the root frame
    brls::Application::pushView(rootFrame);

    // Main loop
    while (brls::Application::mainLoop())
        ;

    // Cleanup
    net::exit();

    return EXIT_SUCCESS;
}
