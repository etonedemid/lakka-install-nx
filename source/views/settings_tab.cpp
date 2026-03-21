#include "settings_tab.hpp"

SettingsTab::SettingsTab()
{
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
