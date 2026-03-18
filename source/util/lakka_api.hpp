#pragma once

#include <string>
#include <vector>

namespace lakka {

// Base URLs for Lakka builds
constexpr const char* STABLE_BASE_URL =
    "https://le.builds.lakka.tv/Switch.aarch64/";
constexpr const char* NIGHTLY_BASE_URL =
    "https://nightly.builds.lakka.tv/latest/Switch.aarch64/";

// Represents a single Lakka release available for download.
struct Version {
    std::string version;    // e.g. "6.1"
    std::string filename;   // e.g. "Lakka-Switch.aarch64-6.1.7z"
    std::string url;        // full download URL
    std::string date;       // date string from the server listing (may be empty)
    std::string size;       // human-readable size from the listing  (may be empty)
    bool        isDev;      // true = nightly / dev build
};

// Fetch the list of available versions from a Lakka builds page.
// `baseUrl` should be STABLE_BASE_URL or NIGHTLY_BASE_URL.
std::vector<Version> fetchVersionList(const std::string& baseUrl, bool isDev);

// Convenience wrappers
std::vector<Version> fetchStableVersions();
std::vector<Version> fetchNightlyVersions();

// Find the latest version in a list (assumes versions are sortable strings).
Version getLatest(const std::vector<Version>& versions);

// Build the full download URL for a filename.
std::string buildDownloadUrl(const std::string& baseUrl,
                             const std::string& filename);

} // namespace lakka
