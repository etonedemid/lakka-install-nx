#pragma once

#include <borealis.hpp>
#include <string>

// HomeTab — displays current installation info and quick action buttons.
// This is added as a tab in the main TabFrame.
class HomeTab : public brls::List
{
  public:
    HomeTab();
    ~HomeTab();

    static void notifyInstallStateChanged(const std::string& status = "Ready");

  private:
    void refreshInstalledInfo(const std::string& status = "Ready");
    void confirmUninstall();
    void doUninstall();

    brls::ListItem* m_itemInstalledVersion = nullptr;
    brls::ListItem* m_itemStatus           = nullptr;
    brls::ListItem* m_itemUninstall        = nullptr;

    static HomeTab*       s_instance;
};
