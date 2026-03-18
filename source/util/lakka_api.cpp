#include "lakka_api.hpp"
#include "net.hpp"

#include <algorithm>
#include <cstring>

namespace lakka {

// ── HTML parsing ───────────────────────────────────────────────────────────────
//
// The builds page is a standard Apache/nginx directory listing.  Each line that
// interests us looks roughly like:
//
//   <a href="Lakka-Switch.aarch64-6.1.7z">…</a>   2024-12-01 10:00  215M
//
// We extract the filename using simple string searches (no std::regex — it is
// far too heavy on the Switch ARM and can easily crash with a stack overflow).

// Helpers for lightweight date/size extraction from text after the </a> tag.
static bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Try to parse "YYYY-MM-DD HH:MM   SIZE" from |tail|.
static void parseMetadata(const std::string& tail, std::string& outDate,
                          std::string& outSize)
{
    // Look for a date like "2024-12-01"
    for (size_t i = 0; i + 9 < tail.size(); ++i) {
        if (isDigit(tail[i])   && isDigit(tail[i+1]) &&
            isDigit(tail[i+2]) && isDigit(tail[i+3]) &&
            tail[i+4] == '-' &&
            isDigit(tail[i+5]) && isDigit(tail[i+6]) &&
            tail[i+7] == '-' &&
            isDigit(tail[i+8]) && isDigit(tail[i+9]))
        {
            outDate = tail.substr(i, 10);

            // Skip past "YYYY-MM-DD HH:MM" then whitespace to get size
            size_t j = i + 10;
            // skip spaces
            while (j < tail.size() && (tail[j] == ' ' || tail[j] == '\t')) ++j;
            // skip HH:MM
            if (j + 4 < tail.size() && isDigit(tail[j]) && isDigit(tail[j+1])
                && tail[j+2] == ':' && isDigit(tail[j+3]) && isDigit(tail[j+4]))
            {
                j += 5;
            }
            // skip spaces
            while (j < tail.size() && (tail[j] == ' ' || tail[j] == '\t')) ++j;
            // read size token
            size_t sizeStart = j;
            while (j < tail.size() && tail[j] != ' ' && tail[j] != '\t' &&
                   tail[j] != '<' && tail[j] != '\n' && tail[j] != '\r')
                ++j;
            if (j > sizeStart)
                outSize = tail.substr(sizeStart, j - sizeStart);

            return;
        }
    }
}

static std::vector<Version> parseDirectoryListing(const std::string& html,
                                                   const std::string& baseUrl,
                                                   bool isDev)
{
    std::vector<Version> versions;

    const std::string needle = "href=\"Lakka-Switch.aarch64-";
    const std::string suffix = ".7z\"";

    size_t searchPos = 0;
    while (searchPos < html.size()) {
        // Case-insensitive search is not needed — the server uses consistent
        // casing.  A plain find is sufficient and extremely fast.
        size_t hrefPos = html.find(needle, searchPos);
        if (hrefPos == std::string::npos)
            break;

        // The filename starts right after href="
        size_t filenameStart = hrefPos + 6; // skip 'href="'
        size_t filenameEnd = html.find('"', filenameStart);
        if (filenameEnd == std::string::npos) {
            searchPos = hrefPos + needle.size();
            continue;
        }

        std::string filename = html.substr(filenameStart, filenameEnd - filenameStart);

        // Must end with .7z
        if (filename.size() < 4 ||
            filename.compare(filename.size() - 3, 3, ".7z") != 0)
        {
            searchPos = filenameEnd;
            continue;
        }

        // Extract version: between "Lakka-Switch.aarch64-" and ".7z"
        const std::string prefix = "Lakka-Switch.aarch64-";
        std::string ver = filename.substr(prefix.size(),
                                          filename.size() - prefix.size() - 3);

        Version v;
        v.filename = filename;
        v.version  = ver;
        v.url      = buildDownloadUrl(baseUrl, v.filename);
        v.isDev    = isDev;

        // Try to extract date and size from the text after the link
        if (filenameEnd + 1 < html.size()) {
            size_t tailStart = filenameEnd + 1;
            size_t tailLen   = std::min<size_t>(120, html.size() - tailStart);
            std::string tail = html.substr(tailStart, tailLen);
            parseMetadata(tail, v.date, v.size);
        }

        versions.push_back(std::move(v));
        searchPos = filenameEnd;
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
