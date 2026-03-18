#pragma once

#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>

namespace net {

// Progress callback: (downloaded_bytes, total_bytes)
using ProgressCallback = std::function<void(size_t, size_t)>;

// Initialize networking (call once at startup)
bool init();

// Shutdown networking
void exit();

// Perform an HTTP GET and return the body as a string
std::string httpGet(const std::string& url);

// Download a file to disk with optional progress callback
bool downloadFile(const std::string& url,
                  const std::string& outputPath,
                  ProgressCallback progressCb = nullptr);

// Async download task - runs in a background thread
class DownloadTask {
public:
    DownloadTask() = default;
    ~DownloadTask();

    // Start downloading url to outputPath
    void start(const std::string& url, const std::string& outputPath);

    // Cancel the in-progress download
    void cancel();

    // Query state (thread-safe via atomics)
    bool isRunning()   const { return m_running.load(); }
    bool isComplete()  const { return m_complete.load(); }
    bool hasError()    const { return m_error.load(); }
    bool isCancelled() const { return m_cancelled.load(); }

    float    getProgress()   const { return m_progress.load(); }
    size_t   getDownloaded() const { return m_downloaded.load(); }
    size_t   getTotal()      const { return m_total.load(); }

    std::string getErrorMessage() const;

private:
    void run(const std::string& url, const std::string& outputPath);

    std::thread        m_thread;
    std::atomic<bool>  m_running{false};
    std::atomic<bool>  m_complete{false};
    std::atomic<bool>  m_error{false};
    std::atomic<bool>  m_cancelled{false};
    std::atomic<float> m_progress{0.0f};
    std::atomic<size_t> m_downloaded{0};
    std::atomic<size_t> m_total{0};
    std::mutex         m_mutex;
    std::string        m_errorMessage;
};

} // namespace net
