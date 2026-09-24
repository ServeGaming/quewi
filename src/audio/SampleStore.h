#pragma once

#include <QtGlobal>

#include <cstddef>
#include <memory>
#include <vector>

class QFile;

namespace quewi::audio {

// Backing memory for decoded PCM (interleaved float32).
//
// Two kinds, one interface:
//
//   RAM  — an ordinary heap buffer. Short files: instant, editable, cheap.
//   Disk — a memory-mapped temp file in the cache folder. Long files (a
//          3-hour music bed decodes to ~4 GB of float) would otherwise hold
//          all of that in RAM for the life of the show. Mapped, the OS pages
//          it: quewi drops decoded regions from its working set as decode
//          proceeds, and during playback prefetches ahead of the playhead and
//          releases what's well behind it (see AudioEngine's housekeeping),
//          so a long file costs tens of MB of RAM instead of gigabytes. The
//          pages stay in the OS file cache while there's memory to spare, so
//          touching them again is normally just a soft fault.
//
// Semantics mirror std::vector just enough for AudioFile's decode loop and
// the readers: reserve() once, resize() within capacity (the data pointer is
// stable, which is what lets snapshots share a store while decode appends),
// and grow past capacity by copying into a fresh store (COW), never in place.
class SampleStore {
public:
    static std::shared_ptr<SampleStore> makeRam();
    // Falls back to RAM if the cache file can't be created or mapped.
    static std::shared_ptr<SampleStore> makeDisk();

    // Decoded-byte size above which AudioFile uses a disk store.
    static qint64 diskThresholdBytes();
    static void   setDiskThresholdBytes(qint64 bytes);   // tests / tuning

    // Delete cache files left behind by quewi processes that are no longer
    // running (a crash can't clean up after itself). Cheap; call at startup.
    static void sweepStaleCache();

    ~SampleStore();
    SampleStore(const SampleStore &) = delete;
    SampleStore &operator=(const SampleStore &) = delete;

    bool onDisk() const { return m_disk; }

    // Grow capacity (only ever called before the store is shared, or on a
    // fresh COW store). Returns false if the memory couldn't be obtained.
    bool   reserve(size_t samples);
    // Within capacity only — never relocates.
    bool   resize(size_t samples);
    size_t size() const     { return m_size; }
    size_t capacity() const { return m_capacity; }
    bool   empty() const    { return m_size == 0; }

    float       *data()       { return m_data; }
    const float *data() const { return m_data; }
    const float &operator[](size_t i) const { return m_data[i]; }
    const float *begin() const { return m_data; }
    const float *end() const   { return m_data + m_size; }

    // Paging hints for a disk store (no-ops for RAM). Ranges are in samples.
    // prefetch: start reading these pages in now, off the audio thread.
    // release:  drop them from quewi's working set (they stay cached by the
    //           OS until something else needs the memory).
    void prefetch(size_t firstSample, size_t count) const;
    void release(size_t firstSample, size_t count) const;

    // Resident-RAM cost for the memory readout: the whole buffer for RAM,
    // ~nothing for disk (it's paged, and trimmed as it's used).
    qint64 residentBytes() const;

private:
    SampleStore() = default;

    bool mapDisk(size_t samples);

    bool                   m_disk = false;
    std::vector<float>     m_vec;          // RAM
    std::unique_ptr<QFile> m_file;         // Disk
    float                 *m_data = nullptr;
    size_t                 m_size = 0;
    size_t                 m_capacity = 0;
};

} // namespace quewi::audio
