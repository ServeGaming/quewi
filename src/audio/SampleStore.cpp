#include "audio/SampleStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <atomic>
#include <cstdint>

#ifdef Q_OS_WIN
#  include <qt_windows.h>
#else
#  include <cerrno>
#  include <csignal>
#  include <sys/mman.h>
#  include <unistd.h>
#endif

namespace quewi::audio {

namespace {

// ~10 minutes of 44.1 kHz stereo float. Sound effects, stings and most songs
// stay in RAM; long beds, full albums and hour-long mixes go to disk.
std::atomic<qint64> s_diskThreshold{96ll * 1024 * 1024};

QString cacheRoot()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) base = QDir::tempPath() + QStringLiteral("/quewi");
    return base + QStringLiteral("/audio-cache");
}

QString processCacheDir()
{
    return cacheRoot() + QLatin1Char('/')
         + QString::number(QCoreApplication::applicationPid());
}

bool processAlive(qint64 pid)
{
#ifdef Q_OS_WIN
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!h) return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
#else
    return ::kill(pid_t(pid), 0) == 0 || errno == EPERM;
#endif
}

// Byte range for [first, first+count) samples, clamped to the mapping.
bool byteRange(const float *base, size_t capacity, size_t first, size_t count,
               void *&addr, size_t &bytes)
{
    if (!base || first >= capacity || count == 0) return false;
    count = std::min(count, capacity - first);
    addr  = const_cast<float *>(base + first);
    bytes = count * sizeof(float);
    return true;
}

} // namespace

qint64 SampleStore::diskThresholdBytes()        { return s_diskThreshold.load(); }
void   SampleStore::setDiskThresholdBytes(qint64 b) { s_diskThreshold.store(b); }

std::shared_ptr<SampleStore> SampleStore::makeRam()
{
    return std::shared_ptr<SampleStore>(new SampleStore());
}

std::shared_ptr<SampleStore> SampleStore::makeDisk()
{
    const QString dir = processCacheDir();
    if (!QDir().mkpath(dir)) return makeRam();
    auto store = std::shared_ptr<SampleStore>(new SampleStore());
    store->m_file = std::make_unique<QFile>(
        dir + QLatin1Char('/') + QUuid::createUuid().toString(QUuid::WithoutBraces)
            + QStringLiteral(".pcm"));
    if (!store->m_file->open(QIODevice::ReadWrite | QIODevice::Truncate)) return makeRam();
    store->m_disk = true;
    return store;
}

void SampleStore::sweepStaleCache()
{
    QDir root(cacheRoot());
    if (!root.exists()) return;
    const qint64 me = QCoreApplication::applicationPid();
    for (const QString &name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        const qint64 pid = name.toLongLong(&ok);
        if (!ok || pid == me || processAlive(pid)) continue;
        QDir(root.filePath(name)).removeRecursively();
    }
}

SampleStore::~SampleStore()
{
    if (m_disk && m_file) {
        if (m_data) m_file->unmap(reinterpret_cast<uchar *>(m_data));
        m_data = nullptr;
        m_file->close();
        m_file->remove();
    }
}

bool SampleStore::mapDisk(size_t samples)
{
    if (m_data) {                         // growing an existing mapping
        m_file->unmap(reinterpret_cast<uchar *>(m_data));
        m_data = nullptr;
    }
    const qint64 bytes = qint64(samples) * qint64(sizeof(float));
    if (bytes <= 0) { m_capacity = 0; return true; }
    if (!m_file->resize(bytes)) return false;
    uchar *p = m_file->map(0, bytes);
    if (!p) return false;
    m_data = reinterpret_cast<float *>(p);
    m_capacity = samples;
    return true;
}

bool SampleStore::reserve(size_t samples)
{
    if (samples <= m_capacity) return true;
    if (!m_disk) {
        try { m_vec.reserve(samples); } catch (const std::bad_alloc &) { return false; }
        m_data = m_vec.data();
        m_capacity = m_vec.capacity();
        return true;
    }
    return mapDisk(samples);
}

bool SampleStore::resize(size_t samples)
{
    if (samples > m_capacity) return false;
    if (!m_disk) {
        m_vec.resize(samples);            // within capacity: never relocates
        m_data = m_vec.data();
    }
    m_size = samples;
    return true;
}

void SampleStore::prefetch(size_t firstSample, size_t count) const
{
    if (!m_disk) return;
    void *addr = nullptr; size_t bytes = 0;
    if (!byteRange(m_data, m_capacity, firstSample, count, addr, bytes)) return;
#ifdef Q_OS_WIN
    WIN32_MEMORY_RANGE_ENTRY range{ addr, bytes };
    PrefetchVirtualMemory(GetCurrentProcess(), 1, &range, 0);
#else
    const uintptr_t page  = uintptr_t(sysconf(_SC_PAGESIZE));
    const uintptr_t start = uintptr_t(addr) & ~(page - 1);
    const uintptr_t end   = (uintptr_t(addr) + bytes + page - 1) & ~(page - 1);
    madvise(reinterpret_cast<void *>(start), end - start, MADV_WILLNEED);
#endif
}

void SampleStore::release(size_t firstSample, size_t count) const
{
    if (!m_disk) return;
    void *addr = nullptr; size_t bytes = 0;
    if (!byteRange(m_data, m_capacity, firstSample, count, addr, bytes)) return;
#ifdef Q_OS_WIN
    // On pages that aren't locked, VirtualUnlock removes them from the
    // working set (documented behaviour); they move to the standby list and
    // come back on the next touch. It "fails" with ERROR_NOT_LOCKED — expected.
    VirtualUnlock(addr, bytes);
#else
    // Only whole pages strictly inside the range, so a neighbour's still-hot
    // bytes aren't dropped. MADV_DONTNEED on a shared file mapping keeps the
    // data in the page cache/file — it just leaves our resident set.
    const uintptr_t page  = uintptr_t(sysconf(_SC_PAGESIZE));
    const uintptr_t start = (uintptr_t(addr) + page - 1) & ~(page - 1);
    const uintptr_t end   = (uintptr_t(addr) + bytes) & ~(page - 1);
    if (end > start) madvise(reinterpret_cast<void *>(start), end - start, MADV_DONTNEED);
#endif
}

qint64 SampleStore::residentBytes() const
{
    return m_disk ? 0 : qint64(m_capacity) * qint64(sizeof(float));
}

} // namespace quewi::audio
