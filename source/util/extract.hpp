#pragma once

#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>

namespace extract {

// Progress callback: (current_file_index, total_files, current_filename)
using ProgressCallback = std::function<void(size_t, size_t, const std::string&)>;

// Extract a 7z archive to destDir.
// Returns true on success.
bool extract7z(const std::string& archivePath,
               const std::string& destDir,
               ProgressCallback progressCb = nullptr);

// Async extraction task
class ExtractTask {
public:
    ExtractTask() = default;
    ~ExtractTask();

    void start(const std::string& archivePath, const std::string& destDir);
    void cancel();

    bool isRunning()   const { return m_running.load(); }
    bool isComplete()  const { return m_complete.load(); }
    bool hasError()    const { return m_error.load(); }
    bool isCancelled() const { return m_cancelled.load(); }

    float  getProgress() const { return m_progress.load(); }
    size_t getCurrent()  const { return m_current.load(); }
    size_t getTotal()    const { return m_total.load(); }

    // All file paths written during extraction (available after completion).
    // Paths are relative (as stored in the archive, e.g. "lakka/SYSTEM").
    std::vector<std::string> getExtractedPaths() const
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_extractedPaths;
    }

    std::string getCurrentFile() const;
    std::string getErrorMessage() const;

private:
    void run(const std::string& archivePath, const std::string& destDir);

    std::thread         m_thread;
    std::atomic<bool>   m_running{false};
    std::atomic<bool>   m_complete{false};
    std::atomic<bool>   m_error{false};
    std::atomic<bool>   m_cancelled{false};
    std::atomic<float>  m_progress{0.0f};
    std::atomic<size_t> m_current{0};
    std::atomic<size_t> m_total{0};
    mutable std::mutex  m_mutex;
    std::string         m_currentFile;
    std::string         m_errorMessage;
    std::vector<std::string> m_extractedPaths;
};

} // namespace extract
