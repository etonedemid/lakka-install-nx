#include "lakka_api.hpp"
#include "net.hpp"

#include <algorithm>
#include <cstring>
#include <regex>

namespace lakka {

// ── HTML parsing ───────────────────────────────────────────────────────────────
//
// The builds page is a standard Apache/nginx directory listing.  Each line that
// interests us looks roughly like:
//
//   <a href="Lakka-Switch.aarch64-6.1.7z">…</a>   2024-12-01 10:00  215M
//
// We extract the filename with a simple regex scan and optionally pick up the
// date and size that follow the closing </a> tag.

static std::vector<Version> parseDirectoryListing(const std::string& html,
                                                   const std::string& baseUrl,
                                                   bool isDev)
{
    std::vector<Version> versions;

    // Match: href="(Lakka-Switch.aarch64-SOMETHING.7z)"
    // Capture group 1 = full filename
    std::regex linkRe(
        R"(href="(Lakka-Switch\.aarch64-([^"]+)\.7z)")",
        std::regex::icase);

    // Iterate over all matches
    auto begin = std::sregex_iterator(html.begin(), html.end(), linkRe);
    auto end   = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        Version v;
        v.filename = (*it)[1].str();              // full filename with .7z
        v.version  = (*it)[2].str();              // version part (e.g. "6.1")
        v.url      = buildDownloadUrl(baseUrl, v.filename);
        v.isDev    = isDev;

        // Try to extract date and size from the text after the link.
        // Pattern: </a>  YYYY-MM-DD HH:MM   SIZE
        size_t pos = static_cast<size_t>(it->position() + it->length());
        if (pos < html.size()) {
            // Look for a date pattern nearby
            std::string tail = html.substr(pos, 80);

            std::regex metaRe(R"((\d{4}-\d{2}-\d{2})\s+\d{2}:\d{2}\s+(\S+))");
            std::smatch metaMatch;
            if (std::regex_search(tail, metaMatch, metaRe)) {
                v.date = metaMatch[1].str();
                v.size = metaMatch[2].str();
            }
        }

        versions.push_back(std::move(v));
    }

    return versions;
}

// ── public API ─────────────────────────────────────────────────────────────────

std::vector<Version> fetchVersionList(const std::string& baseUrl, bool isDev)
{
    std::string html = net::httpGet(baseUrl);
    if (html.empty())
        return {};

    auto versions = parseDirectoryListing(html, baseUrl, isDev);

    // Sort by version string descending (newest first).
    // A simple reverse-lexicographic sort works for dotted numeric versions
    // when the major component has similar digit counts.
    std::sort(versions.begin(), versions.end(),
              [](const Version& a, const Version& b) {
                  return a.version > b.version;
              });

    return versions;
}

std::vector<Version> fetchStableVersions()
{
    return fetchVersionList(STABLE_BASE_URL, false);
}

std::vector<Version> fetchNightlyVersions()
{
    return fetchVersionList(NIGHTLY_BASE_URL, true);
}

Version getLatest(const std::vector<Version>& versions)
{
    if (versions.empty())
        return {};

    // Versions are already sorted newest-first by fetchVersionList.
    return versions.front();
}

std::string buildDownloadUrl(const std::string& baseUrl,
                             const std::string& filename)
{
    std::string url = baseUrl;
    if (!url.empty() && url.back() != '/')
        url += '/';
    url += filename;
    return url;
}

} // namespace lakka
