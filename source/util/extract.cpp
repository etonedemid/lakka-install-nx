#include "extract.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <string>
#include <vector>

// ── LZMA SDK headers (placed in lib/lzma/) ────────────────────────────────────
extern "C" {
#include "7z.h"
#include "7zAlloc.h"
#include "7zBuf.h"
#include "7zCrc.h"
#include "7zFile.h"
#include "7zTypes.h"
}

namespace extract {

// ── helpers ────────────────────────────────────────────────────────────────────

static const size_t kInputBufSize = (1 << 18); // 256 KiB look-ahead

// Convert a UTF-16LE buffer (from the LZMA SDK) to a UTF-8 std::string.
static std::string utf16ToUtf8(const UInt16* src, size_t len)
{
    std::string out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        UInt16 c = src[i];
        if (c == 0) break;
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

// Recursively create directories (like mkdir -p).
static void mkdirs(const std::string& path)
{
    std::string current;
    for (size_t i = 0; i < path.size(); ++i) {
        current += path[i];
        if (path[i] == '/' || i == path.size() - 1) {
            mkdir(current.c_str(), 0755);
        }
    }
}

// Ensure parent directories exist for a given file path.
static void ensureParentDir(const std::string& filePath)
{
    auto pos = filePath.rfind('/');
    if (pos != std::string::npos) {
        mkdirs(filePath.substr(0, pos));
    }
}

// ── synchronous extraction ─────────────────────────────────────────────────────

bool extract7z(const std::string& archivePath,
               const std::string& destDir,
               ProgressCallback progressCb)
{
    CFileInStream archiveStream;
    CLookToRead2 lookStream;
    CSzArEx db;

    ISzAlloc allocImp     = { SzAlloc, SzFree };
    ISzAlloc allocTempImp = { SzAllocTemp, SzFreeTemp };

    if (InFile_Open(&archiveStream.file, archivePath.c_str())) {
        return false;
    }

    FileInStream_CreateVTable(&archiveStream);
    LookToRead2_CreateVTable(&lookStream, False);

    lookStream.buf = static_cast<Byte*>(ISzAlloc_Alloc(&allocImp, kInputBufSize));
    if (!lookStream.buf) {
        File_Close(&archiveStream.file);
        return false;
    }
    lookStream.bufSize    = kInputBufSize;
    lookStream.realStream = &archiveStream.vt;
    LookToRead2_Init(&lookStream);

    CrcGenerateTable();
    SzArEx_Init(&db);

    SRes res = SzArEx_Open(&db, &lookStream.vt, &allocImp, &allocTempImp);
    if (res != SZ_OK) {
        ISzAlloc_Free(&allocImp, lookStream.buf);
        File_Close(&archiveStream.file);
        return false;
    }

    UInt32 blockIndex    = 0xFFFFFFFF;
    Byte*  outBuffer     = nullptr;
    size_t outBufferSize = 0;

    bool success = true;

    for (UInt32 i = 0; i < db.NumFiles; ++i) {
        size_t offset          = 0;
        size_t outSizeProcessed = 0;
        unsigned isDir = SzArEx_IsDir(&db, i);

        // Get the file name
        size_t nameLen = SzArEx_GetFileNameUtf16(&db, i, nullptr);
        std::vector<UInt16> nameBuf(nameLen);
        SzArEx_GetFileNameUtf16(&db, i, nameBuf.data());

        std::string name = utf16ToUtf8(nameBuf.data(), nameLen);
        std::string fullPath = destDir;
        if (!fullPath.empty() && fullPath.back() != '/')
            fullPath += '/';
        fullPath += name;

        if (progressCb) {
            progressCb(i, db.NumFiles, name);
        }

        if (isDir) {
            mkdirs(fullPath);
            continue;
        }

        ensureParentDir(fullPath);

        res = SzArEx_Extract(&db, &lookStream.vt, i, &blockIndex,
                             &outBuffer, &outBufferSize,
                             &offset, &outSizeProcessed,
                             &allocImp, &allocTempImp);
        if (res != SZ_OK) {
            success = false;
            break;
        }

        FILE* outFile = fopen(fullPath.c_str(), "wb");
        if (!outFile) {
            success = false;
            break;
        }

        if (outSizeProcessed > 0) {
            fwrite(outBuffer + offset, 1, outSizeProcessed, outFile);
        }
        fclose(outFile);
    }

    ISzAlloc_Free(&allocImp, outBuffer);
    SzArEx_Free(&db, &allocImp);
    ISzAlloc_Free(&allocImp, lookStream.buf);
    File_Close(&archiveStream.file);

    return success;
}

// ── ExtractTask ────────────────────────────────────────────────────────────────

ExtractTask::~ExtractTask()
{
    cancel();
    if (m_thread.joinable())
        m_thread.join();
}

void ExtractTask::start(const std::string& archivePath,
                        const std::string& destDir)
{
    if (m_running.load())
        return;

    m_running   = true;
    m_complete  = false;
    m_error     = false;
    m_cancelled = false;
    m_progress  = 0.0f;
    m_current   = 0;
    m_total     = 0;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_currentFile.clear();
        m_errorMessage.clear();
        m_extractedPaths.clear();
    }

    if (m_thread.joinable())
        m_thread.join();

    m_thread = std::thread(&ExtractTask::run, this, archivePath, destDir);
}

void ExtractTask::cancel()
{
    m_cancelled.store(true);
}

std::string ExtractTask::getCurrentFile() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_currentFile;
}

std::string ExtractTask::getErrorMessage() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_errorMessage;
}

void ExtractTask::run(const std::string& archivePath,
                      const std::string& destDir)
{
    auto cb = [this](size_t cur, size_t total, const std::string& name) {
        m_current.store(cur);
        m_total.store(total);
        if (total > 0)
            m_progress.store(static_cast<float>(cur) / static_cast<float>(total));
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_currentFile = name;
            if (!name.empty())
                m_extractedPaths.push_back(name);
        }
    };

    bool ok = extract7z(archivePath, destDir, cb);

    if (!ok && !m_cancelled.load()) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_errorMessage = "Extraction failed";
        m_error = true;
    }

    m_progress = 1.0f;
    m_running  = false;
    m_complete = true;
}

} // namespace extract
