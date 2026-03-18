#pragma once

#include <string>
#include <map>
#include <vector>

namespace config {

// Default paths
constexpr const char* CONFIG_DIR  = "sdmc:/config/lakka-install-nx";
constexpr const char* CONFIG_FILE = "sdmc:/config/lakka-install-nx/config.ini";
constexpr const char* DOWNLOAD_DIR = "sdmc:/config/lakka-install-nx/download";
constexpr const char* INSTALL_DIR  = "sdmc:/";
constexpr const char* MANIFEST_FILE = "sdmc:/config/lakka-install-nx/manifest.txt";

// Application configuration stored as a simple INI-like file.
class Config {
public:
    Config();

    // Load / save from CONFIG_FILE.
    bool load();
    bool save() const;

    // Manifest: list of files/dirs written during the last install.
    // Stored as one path per line in MANIFEST_FILE.
    bool saveManifest(const std::vector<std::string>& paths) const;
    std::vector<std::string> loadManifest() const;

    // Getters
    std::string getInstalledVersion()   const;
    std::string getInstalledChannel()   const; // "stable" or "dev"
    bool        getShowDevVersions()    const;
    bool        getAutoCheckUpdates()   const;
    std::string getInstallPath()        const;

    // Setters
    void setInstalledVersion(const std::string& ver);
    void setInstalledChannel(const std::string& channel);
    void setShowDevVersions(bool show);
    void setAutoCheckUpdates(bool check);
    void setInstallPath(const std::string& path);

    // Generic key/value (section.key)
    std::string get(const std::string& key, const std::string& def = "") const;
    void        set(const std::string& key, const std::string& value);

private:
    std::map<std::string, std::string> m_data;
    bool parseFile(const std::string& path);
    bool writeFile(const std::string& path) const;
};

} // namespace config
