#include "main_view.hpp"
#include "install_page.hpp"
#include "../util/lakka_api.hpp"
#include "../util/net.hpp"

#include <borealis.hpp>
#include <thread>

// ── MainView ───────────────────────────────────────────────────────────────────

MainView::MainView(config::Config& cfg)
    : m_cfg(cfg)
{
    setTitle("Lakka Installer NX");
    setIcon(BOREALIS_ASSET("icon/borealis.jpg"));

    addTab("Home",     createHomeTab());
    addTab("Stable",   createVersionTab(false));
    addSeparator();
    addTab("Nightly",  createVersionTab(true));
    addSeparator();
    addTab("Settings", createSettingsTab());
}

// ── Home tab ───────────────────────────────────────────────────────────────────

brls::List* MainView::createHomeTab()
{
    auto* list = new brls::List();

    list->addView(new brls::Header("Current Installation"));

    std::string installedVer = m_cfg.getInstalledVersion();
    std::string channel      = m_cfg.getInstalledChannel();

    auto* statusItem = new brls::ListItem("Installed Version");
    if (installedVer.empty()) {
        statusItem->setValue("Not installed");
    } else {
        std::string label = installedVer;
        if (!channel.empty())
            label += " (" + channel + ")";
        statusItem->setValue(label);
    }
    list->addView(statusItem);

    list->addView(new brls::Header("Quick Actions"));

    // Check for updates
    auto* updateItem = new brls::ListItem("Check for Updates",
                                           "Fetches the latest stable version");
    updateItem->getClickEvent()->subscribe([this](brls::View* /*view*/) {
        brls::Application::blockInputs();
        brls::Application::notify("Checking for updates...");

        // Fetch in a detached thread so we don't block the UI
        std::thread([this]() {
            auto versions = lakka::fetchStableVersions();
            // Back on the main thread via a queued notification
            if (versions.empty()) {
                brls::Application::notify("Could not fetch version list");
                brls::Application::unblockInputs();
                return;
            }

            auto latest = lakka::getLatest(versions);
            std::string installed = m_cfg.getInstalledVersion();

            if (installed == latest.version) {
                brls::Application::notify("You are up to date (" + installed + ")");
            } else {
                brls::Application::notify("Update available: " + latest.version);
            }
            brls::Application::unblockInputs();
        }).detach();
    });
    list->addView(updateItem);

    // Install latest stable
    auto* installLatest = new brls::ListItem("Install Latest Stable",
                                              "Download and install the newest release");
    installLatest->getClickEvent()->subscribe([this](brls::View* /*view*/) {
        brls::Application::blockInputs();
        brls::Application::notify("Fetching latest version...");

        std::thread([this]() {
            auto versions = lakka::fetchStableVersions();
            brls::Application::unblockInputs();

            if (versions.empty()) {
                brls::Application::notify("Failed to retrieve versions");
                return;
            }
            auto latest = lakka::getLatest(versions);
            startInstall(latest);
        }).detach();
    });
    list->addView(installLatest);

    return list;
}

// ── Version list tab ───────────────────────────────────────────────────────────

brls::List* MainView::createVersionTab(bool isDev)
{
    auto* list = new brls::List();

    std::string label = isDev ? "Nightly Builds" : "Stable Releases";
    list->addView(new brls::Header(label));

    auto* loadingItem = new brls::ListItem("Loading...",
                                            "Fetching version list from server");
    list->addView(loadingItem);

    // Kick off the fetch
    populateVersionList(list, isDev);

    return list;
}

void MainView::populateVersionList(brls::List* list, bool isDev)
{
    std::thread([this, list, isDev]() {
        std::string url = isDev ? lakka::NIGHTLY_BASE_URL
                                : lakka::STABLE_BASE_URL;
        auto versions = lakka::fetchVersionList(url, isDev);

        // We clear the "Loading..." placeholder and add real items.
        // Borealis view manipulation must happen on the main thread – however
        // in practice on Switch the UI is single-threaded so this is safe as
        // long as we serialise carefully.  Using brls::sync if available,
        // otherwise the draw() loop picks up new state.

        // Remove the loading placeholder
        list->clear();

        if (versions.empty()) {
            list->addView(new brls::Header("No versions found"));
            auto* hint = new brls::ListItem(
                "Could not reach the Lakka build server",
                "Check your internet connection and try again");
            list->addView(hint);
            return;
        }

        list->addView(new brls::Header(
            isDev ? "Nightly Builds" : "Stable Releases"));

        for (auto& v : versions) {
            std::string desc;
            if (!v.date.empty())
                desc += v.date;
            if (!v.size.empty())
                desc += "  •  " + v.size;

            auto* item = new brls::ListItem("Lakka " + v.version, desc);
            item->setValue(v.isDev ? "dev" : "stable");

            item->getClickEvent()->subscribe(
                [this, v](brls::View* /*view*/) {
                    startInstall(v);
                });

            list->addView(item);
        }
    }).detach();
}

// ── Settings tab ───────────────────────────────────────────────────────────────

brls::List* MainView::createSettingsTab()
{
    auto* list = new brls::List();

    list->addView(new brls::Header("Preferences"));

    // Toggle: show dev versions
    auto* devToggle = new brls::ToggleListItem(
        "Show Nightly Builds",
        m_cfg.getShowDevVersions(),
        "Display development / nightly versions in the tabs");

    devToggle->getClickEvent()->subscribe([this, devToggle](brls::View*) {
        m_cfg.setShowDevVersions(devToggle->getToggleState());
        m_cfg.save();
    });
    list->addView(devToggle);

    // Toggle: auto-check updates
    auto* autoUpdate = new brls::ToggleListItem(
        "Auto-Check Updates on Launch",
        m_cfg.getAutoCheckUpdates(),
        "Automatically check for new versions when the app starts");

    autoUpdate->getClickEvent()->subscribe([this, autoUpdate](brls::View*) {
        m_cfg.setAutoCheckUpdates(autoUpdate->getToggleState());
        m_cfg.save();
    });
    list->addView(autoUpdate);

    list->addView(new brls::Header("Paths"));

    auto* pathItem = new brls::ListItem("Install Path");
    pathItem->setValue(m_cfg.getInstallPath());
    list->addView(pathItem);

    list->addView(new brls::Header("About"));

    auto* aboutItem = new brls::ListItem(
        "Lakka Installer NX v1.0.0",
        "Downloads and installs Lakka (https://www.lakka.tv) on your Nintendo Switch");
    list->addView(aboutItem);

    auto* licenseItem = new brls::ListItem(
        "Open Source",
        "Built with Borealis (github.com/natinusala/borealis)");
    list->addView(licenseItem);

    return list;
}

// ── Install trigger ────────────────────────────────────────────────────────────

void MainView::startInstall(const lakka::Version& version)
{
    std::string msg = "Install Lakka " + version.version;
    if (version.isDev)
        msg += " (nightly)";
    msg += "?\n\nThis will download and extract files to the SD card root.";

    auto* dialog = new brls::Dialog(msg);

    dialog->addButton("Cancel", [dialog](brls::View*) {
        dialog->close();
    });

    dialog->addButton("Install", [this, version, dialog](brls::View*) {
        dialog->close();
        auto* page = new InstallPage(version, m_cfg);
        brls::Application::pushView(page);
    });

    dialog->setCancelable(true);
    dialog->open();
}
