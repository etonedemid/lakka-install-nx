#include "net.hpp"

#include <borealis.hpp>
#include <curl/curl.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sys/socket.h>

// Global mutex: mbedtls (the Switch's TLS backend) is not safe to use from
// multiple threads simultaneously.  Serialising all curl operations prevents
// random crashes when two tabs fetch version lists at the same time.
static std::mutex g_curlMutex;

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
    size_t written = fwrite(contents, size, nmemb, ctx->fp);
    return written * size;
}

// Enlarge the TCP receive buffer so the kernel can absorb bursts without
// stalling the sender.  4 MiB is well within libnx limits and gives curl
// enough headroom to sustain ~50 MB/s on 802.11ac.
static int sockoptCb(void* /*clientp*/, curl_socket_t curlfd, curlsocktype /*purpose*/)
{
    int rcvbuf = 4 * 1024 * 1024;
    setsockopt(curlfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    return CURL_SOCKOPT_OK;
}

static int curlProgressCb(void* clientp,
                           curl_off_t dltotal, curl_off_t dlnow,
                           curl_off_t /*ultotal*/, curl_off_t /*ulnow*/)
{
    auto* ctx = static_cast<FileWriteCtx*>(clientp);

    if (ctx->task) {
        if (ctx->task->isCancelled())
            return 1; // abort transfer

        ctx->task->updateProgress(static_cast<size_t>(dlnow),
                                   static_cast<size_t>(dltotal));
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
    // Socket services are already initialized by borealis (switch_wrapper.c)
    // via userAppInit(). We only need to initialize libcurl.
    curl_global_init(CURL_GLOBAL_DEFAULT);
    return true;
}

void exit()
{
    curl_global_cleanup();
}

std::string httpGet(const std::string& url)
{
    std::string response;

    brls::Logger::debug("net::httpGet waiting for mutex: {}", url);
    std::lock_guard<std::mutex> lock(g_curlMutex);
    brls::Logger::debug("net::httpGet acquired mutex, starting request: {}", url);

    CURL* curl = curl_easy_init();
    if (!curl) {
        brls::Logger::error("net::httpGet curl_easy_init failed");
        return response;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "LakkaInstallerNX/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stringWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        brls::Logger::error("net::httpGet curl error {}: {}", (int)res, curl_easy_strerror(res));
        response.clear();
    } else {
        brls::Logger::debug("net::httpGet success, {} bytes received", response.size());
    }

    curl_easy_cleanup(curl);
    brls::Logger::debug("net::httpGet released mutex: {}", url);
    return response;
}

bool downloadFile(const std::string& url,
                  const std::string& outputPath,
                  ProgressCallback progressCb)
{
    FILE* fp = fopen(outputPath.c_str(), "wb");
    if (!fp)
        return false;

    setvbuf(fp, nullptr, _IOFBF, 1024 * 1024);

    FileWriteCtx ctx{fp, nullptr, progressCb};

    CURL* curl = curl_easy_init();
    if (!curl) {
        fclose(fp);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "LakkaInstallerNX/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fileWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgressCb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 512L * 1024L);
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, sockoptCb);
    curl_easy_setopt(curl, CURLOPT_MAX_RECV_SPEED_LARGE, (curl_off_t)(50L * 1024L * 1024L));
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

void DownloadTask::updateProgress(size_t downloaded, size_t total)
{
    m_downloaded.store(downloaded);
    m_total.store(total);
    if (total > 0)
        m_progress.store(static_cast<float>(downloaded) / static_cast<float>(total));
}

std::string DownloadTask::getErrorMessage() const
{
    std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(m_mutex));
    return m_errorMessage;
}

void DownloadTask::run(const std::string& url, const std::string& outputPath)
{
    brls::Logger::debug("DownloadTask::run started url={}", url);

    FILE* fp = fopen(outputPath.c_str(), "wb");
    if (!fp) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_errorMessage = "Failed to create output file";
        m_error    = true;
        m_running  = false;
        m_complete = true;
        return;
    }

    setvbuf(fp, nullptr, _IOFBF, 1024 * 1024);

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
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fileWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgressCb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 512L * 1024L);
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, sockoptCb);
    curl_easy_setopt(curl, CURLOPT_MAX_RECV_SPEED_LARGE, (curl_off_t)(50L * 1024L * 1024L));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1000L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);

    CURLcode res = curl_easy_perform(curl);
    brls::Logger::debug("DownloadTask::run curl_easy_perform returned {} ({})",
        (int)res, curl_easy_strerror(res));
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
