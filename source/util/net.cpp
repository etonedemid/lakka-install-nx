#include "net.hpp"

#include <switch.h>
#include <curl/curl.h>
#include <cstdio>
#include <cstring>

namespace net {

// ── helpers ────────────────────────────────────────────────────────────────────

static size_t stringWriteCb(void* contents, size_t size, size_t nmemb, void* userp)
{
    auto* str = static_cast<std::string*>(userp);
    size_t realSize = size * nmemb;
    str->append(static_cast<char*>(contents), realSize);
    return realSize;
}

struct FileWriteCtx {
    FILE*            fp;
    DownloadTask*    task;       // may be nullptr for synchronous calls
    ProgressCallback syncCb;    // used for synchronous downloadFile()
};

static size_t fileWriteCb(void* contents, size_t size, size_t nmemb, void* userp)
{
    auto* ctx = static_cast<FileWriteCtx*>(userp);
    return fwrite(contents, size, nmemb, ctx->fp);
}

static int curlProgressCb(void* clientp,
                           curl_off_t dltotal, curl_off_t dlnow,
                           curl_off_t /*ultotal*/, curl_off_t /*ulnow*/)
{
    auto* ctx = static_cast<FileWriteCtx*>(clientp);

    if (ctx->task) {
        if (ctx->task->isCancelled())
            return 1; // abort transfer

        ctx->task->m_downloaded.store(static_cast<size_t>(dlnow));
        ctx->task->m_total.store(static_cast<size_t>(dltotal));
        if (dltotal > 0)
            ctx->task->m_progress.store(
                static_cast<float>(dlnow) / static_cast<float>(dltotal));
    }

    if (ctx->syncCb && dltotal > 0) {
        ctx->syncCb(static_cast<size_t>(dlnow),
                     static_cast<size_t>(dltotal));
    }
    return 0;
}

// ── public API ─────────────────────────────────────────────────────────────────

bool init()
{
    Result rc = socketInitializeDefault();
    if (R_FAILED(rc))
        return false;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    return true;
}

void exit()
{
    curl_global_cleanup();
    socketExit();
}

std::string httpGet(const std::string& url)
{
    std::string response;

    CURL* curl = curl_easy_init();
    if (!curl)
        return response;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "LakkaInstallerNX/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stringWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        response.clear();
    }

    curl_easy_cleanup(curl);
    return response;
}

bool downloadFile(const std::string& url,
                  const std::string& outputPath,
                  ProgressCallback progressCb)
{
    FILE* fp = fopen(outputPath.c_str(), "wb");
    if (!fp)
        return false;

    FileWriteCtx ctx{fp, nullptr, progressCb};

    CURL* curl = curl_easy_init();
    if (!curl) {
        fclose(fp);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "LakkaInstallerNX/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fileWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgressCb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1000L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);

    CURLcode res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);
    fclose(fp);

    if (res != CURLE_OK) {
        remove(outputPath.c_str());
        return false;
    }
    return true;
}

// ── DownloadTask ───────────────────────────────────────────────────────────────

DownloadTask::~DownloadTask()
{
    cancel();
    if (m_thread.joinable())
        m_thread.join();
}

void DownloadTask::start(const std::string& url, const std::string& outputPath)
{
    if (m_running.load())
        return;

    m_running   = true;
    m_complete  = false;
    m_error     = false;
    m_cancelled = false;
    m_progress  = 0.0f;
    m_downloaded = 0;
    m_total     = 0;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_errorMessage.clear();
    }

    if (m_thread.joinable())
        m_thread.join();

    m_thread = std::thread(&DownloadTask::run, this, url, outputPath);
}

void DownloadTask::cancel()
{
    m_cancelled.store(true);
}

std::string DownloadTask::getErrorMessage() const
{
    std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(m_mutex));
    return m_errorMessage;
}

void DownloadTask::run(const std::string& url, const std::string& outputPath)
{
    FILE* fp = fopen(outputPath.c_str(), "wb");
    if (!fp) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_errorMessage = "Failed to create output file";
        m_error    = true;
        m_running  = false;
        m_complete = true;
        return;
    }

    FileWriteCtx ctx{fp, this, nullptr};

    CURL* curl = curl_easy_init();
    if (!curl) {
        fclose(fp);
        std::lock_guard<std::mutex> lk(m_mutex);
        m_errorMessage = "Failed to initialise libcurl";
        m_error    = true;
        m_running  = false;
        m_complete = true;
        return;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "LakkaInstallerNX/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fileWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgressCb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1000L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    fclose(fp);

    if (m_cancelled.load()) {
        remove(outputPath.c_str());
        m_running  = false;
        m_complete = true;
        return;
    }

    if (res != CURLE_OK) {
        remove(outputPath.c_str());
        std::lock_guard<std::mutex> lk(m_mutex);
        m_errorMessage = std::string("Download failed: ") + curl_easy_strerror(res);
        m_error = true;
    }

    m_progress = 1.0f;
    m_running  = false;
    m_complete = true;
}

} // namespace net
