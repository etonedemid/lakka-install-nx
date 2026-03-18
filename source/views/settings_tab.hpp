#pragma once

#include <borealis.hpp>
#include "../util/config.hpp"

// SettingsTab — contains toggles for app preferences.
class SettingsTab : public brls::List
{
  public:
    SettingsTab();

  private:
    brls::ToggleListItem* m_toggleAutoUpdate = nullptr;
};
