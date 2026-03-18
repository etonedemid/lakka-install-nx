#include "config.hpp"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <fstream>
#include <sstream>

namespace config {

// ── helpers ────────────────────────────────────────────────────────────────────

static void mkdirs(const std::string& path)
{
    std::string cur;
    for (size_t i = 0; i < path.size(); ++i) {
        cur += path[i];
        if (path[i] == '/' || i == path.size() - 1)
            mkdir(cur.c_str(), 0755);
    }
}

static std::string trim(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// ── Config ─────────────────────────────────────────────────────────────────────

Config::Config()
{
    // Defaults
    m_data["lakka.installed_version"] = "";
    m_data["lakka.installed_channel"] = "";
    m_data["settings.show_dev_versions"]  = "false";
    m_data["settings.auto_check_updates"] = "true";
    m_data["settings.install_path"]       = INSTALL_DIR;
}

bool Config::load()
{
    return parseFile(CONFIG_FILE);
}

bool Config::save() const
{
    mkdirs(CONFIG_DIR);
    return writeFile(CONFIG_FILE);
}

// ── Getters ────────────────────────────────────────────────────────────────────

std::string Config::getInstalledVersion() const
{
    return get("lakka.installed_version");
}

std::string Config::getInstalledChannel() const
{
    return get("lakka.installed_channel");
}

bool Config::getShowDevVersions() const
{
    return get("settings.show_dev_versions") == "true";
}

bool Config::getAutoCheckUpdates() const
{
    return get("settings.auto_check_updates", "true") == "true";
}

std::string Config::getInstallPath() const
{
    std::string p = get("settings.install_path", INSTALL_DIR);
    return p.empty() ? INSTALL_DIR : p;
}

// ── Setters ────────────────────────────────────────────────────────────────────

void Config::setInstalledVersion(const std::string& ver)
{
    set("lakka.installed_version", ver);
}

void Config::setInstalledChannel(const std::string& channel)
{
    set("lakka.installed_channel", channel);
}

void Config::setShowDevVersions(bool show)
{
    set("settings.show_dev_versions", show ? "true" : "false");
}

void Config::setAutoCheckUpdates(bool check)
{
    set("settings.auto_check_updates", check ? "true" : "false");
}

void Config::setInstallPath(const std::string& path)
{
    set("settings.install_path", path);
}

// ── Generic ────────────────────────────────────────────────────────────────────

std::string Config::get(const std::string& key, const std::string& def) const
{
    auto it = m_data.find(key);
    if (it != m_data.end())
        return it->second;
    return def;
}

void Config::set(const std::string& key, const std::string& value)
{
    m_data[key] = value;
}

// ── INI parsing ────────────────────────────────────────────────────────────────

bool Config::parseFile(const std::string& path)
{
    std::ifstream ifs(path);
    if (!ifs.is_open())
        return false;

    std::string section;
    std::string line;

    while (std::getline(ifs, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;

        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            section = trim(section);
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        std::string key   = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));

        if (!section.empty())
            key = section + "." + key;

        m_data[key] = value;
    }

    return true;
}

bool Config::writeFile(const std::string& path) const
{
    std::ofstream ofs(path);
    if (!ofs.is_open())
        return false;

    // Group by section
    std::map<std::string, std::map<std::string, std::string>> sections;
    for (auto& kv : m_data) {
        auto dot = kv.first.find('.');
        if (dot != std::string::npos) {
            std::string sec = kv.first.substr(0, dot);
            std::string key = kv.first.substr(dot + 1);
            sections[sec][key] = kv.second;
        } else {
            sections[""][kv.first] = kv.second;
        }
    }

    for (auto& sec : sections) {
        if (!sec.first.empty())
            ofs << "[" << sec.first << "]\n";
        for (auto& kv : sec.second)
            ofs << kv.first << "=" << kv.second << "\n";
        ofs << "\n";
    }

    return true;
}

} // namespace config
