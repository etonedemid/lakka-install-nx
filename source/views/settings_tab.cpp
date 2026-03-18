#include "settings_tab.hpp"

extern config::Config g_config;

SettingsTab::SettingsTab()
{
    this->addView(new brls::Header("Preferences"));

    // Show nightly / dev builds toggle
    m_toggleDev = new brls::ToggleListItem(
        "Show Nightly Builds",
        g_config.getShowDevVersions(),
        "When enabled, the Nightly tab will be visible");
    m_toggleDev->getClickEvent()->subscribe([this](brls::View* view) {
        bool val = m_toggleDev->getToggleState();
        g_config.setShowDevVersions(val);
        g_config.save();
    });

    // Auto-check updates toggle
    m_toggleAutoUpdate = new brls::ToggleListItem(
        "Auto-Check Updates on Launch",
        g_config.getAutoCheckUpdates(),
        "Automatically check for new Lakka versions on startup");
    m_toggleAutoUpdate->getClickEvent()->subscribe([this](brls::View* view) {
        bool val = m_toggleAutoUpdate->getToggleState();
        g_config.setAutoCheckUpdates(val);
        g_config.save();
    });

    this->addView(m_toggleDev);
    this->addView(m_toggleAutoUpdate);

    this->addView(new brls::ListItemGroupSpacing(true));
    this->addView(new brls::Header("About"));

    brls::ListItem* aboutItem = new brls::ListItem("Lakka Installer NX");
    aboutItem->setValue("v1.0.0");
    this->addView(aboutItem);

    brls::ListItem* descItem = new brls::ListItem(
        "Description",
        "Downloads and installs Lakka (https://www.lakka.tv) on your Nintendo Switch. "
        "Built with borealis (github.com/natinusala/borealis).");
    this->addView(descItem);
}
