#include "home_tab.hpp"
#include "../util/config.hpp"
#include "install_page.hpp"

#include <borealis.hpp>
#include <algorithm>
#include <cstdio>
#include <sys/stat.h>

extern config::Config g_config;

HomeTab* HomeTab::s_instance = nullptr;

// ── HomeTab ──────────────────────────────────────────────────────────
HomeTab::HomeTab()
{
    s_instance = this;

    m_itemInstalledVersion = new brls::ListItem("Installed Version",
        "The currently installed Lakka version on this SD card.");

    // Status item (used for messages)
    m_itemStatus = new brls::ListItem("Status");
    m_itemStatus->setValue("Ready");

    // Uninstall Lakka
    m_itemUninstall = new brls::ListItem("Uninstall Lakka",
        "Remove all Lakka files");
    m_itemUninstall->getClickEvent()->subscribe([this](brls::View*) {
        this->confirmUninstall();
    });

    // Add to the list
    this->addView(new brls::Header("Current Installation"));
    this->addView(m_itemInstalledVersion);
    this->addView(new brls::ListItemGroupSpacing(true));
    this->addView(new brls::Header("Actions"));
    this->addView(m_itemUninstall);
    this->addView(new brls::ListItemGroupSpacing(true));
    this->addView(m_itemStatus);

    this->refreshInstalledInfo();
}

HomeTab::~HomeTab()
{
    if (s_instance == this)
        s_instance = nullptr;


}

void HomeTab::notifyInstallStateChanged(const std::string& status)
{
    if (s_instance)
        s_instance->refreshInstalledInfo(status);
}

void HomeTab::refreshInstalledInfo(const std::string& status)
{
    std::string ver = g_config.getInstalledVersion();
    std::string ch  = g_config.getInstalledChannel();

    if (!ver.empty())
    {
        m_itemInstalledVersion->setValue(ver + " (" + ch + ")");
        m_itemStatus->setValue(status);
    }
    else
    {
        m_itemInstalledVersion->setValue("Not installed");
        if (status == "Ready")
            m_itemStatus->setValue("Not installed");
        else
            m_itemStatus->setValue(status);
    }
}

void HomeTab::confirmUninstall()
{
    std::string ver = g_config.getInstalledVersion();
    if (ver.empty()) {
        m_itemStatus->setValue("No Lakka installation recorded.");
        return;
    }

    auto manifest = g_config.loadManifest();
    if (manifest.empty()) {
        // No manifest — installed before this feature was added, or
        // the config dir was wiped.  Let the user know.
        brls::Dialog* dlg = new brls::Dialog(
            "No install manifest found.\n"
            "Cannot automatically remove Lakka files.\n"
            "Please delete the 'lakka' folder from your SD card manually.");
        dlg->addButton("OK", [dlg](brls::View*) { dlg->close(); });
        dlg->setCancelable(true);
        dlg->open();
        return;
    }

    brls::Dialog* dlg = new brls::Dialog(
        "Remove Lakka " + ver + "?\n" +
        std::to_string(manifest.size()) + " files will be deleted.");
    dlg->addButton("Uninstall", [this, dlg](brls::View*) {
        dlg->close([this]() { this->doUninstall(); });
    });
    dlg->addButton("Cancel", [dlg](brls::View*) { dlg->close(); });
    dlg->setCancelable(true);
    dlg->open();
}

void HomeTab::doUninstall()
{
    auto manifest = g_config.loadManifest();

    // Sort so that deeper (longer) paths come first — files before directories
    std::sort(manifest.begin(), manifest.end(),
        [](const std::string& a, const std::string& b) {
            return a.size() > b.size();
        });

    size_t removed = 0;
    for (const auto& path : manifest) {
        // Try as a file first; if that fails, try as a directory
        if (std::remove(path.c_str()) == 0 || rmdir(path.c_str()) == 0)
            ++removed;
    }

    // Delete the manifest itself
    std::remove(config::MANIFEST_FILE);

    // Clear the installed version from config
    g_config.setInstalledVersion("");
    g_config.setInstalledChannel("");
    g_config.save();

    this->refreshInstalledInfo("Removed " + std::to_string(removed) + " of " +
        std::to_string(manifest.size()) + " files.");
    brls::Logger::debug("HomeTab::doUninstall removed {}/{} entries", removed, manifest.size());
}