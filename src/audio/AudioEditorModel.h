#pragma once

#include "audio/AudioEffect.h"
#include "audio/AudioFile.h"

#include <QObject>
#include <QUndoStack>
#include <QUuid>
#include <QColor>
#include <QFileInfo>
#include <QJsonObject>
#include <memory>
#include <vector>

namespace quewi::audio {

// ── Fade curve ────────────────────────────────────────────────────────────────

struct FadeCurve {
    enum Type { Linear, EqualPower, SCurve };
    Type   type            = Linear;
    qint64 durationSamples = 0; // 0 = no fade
};

// ── Region ────────────────────────────────────────────────────────────────────

// A contiguous slice of a source file placed at a position on the timeline.
struct AudioRegion {
    QUuid   id;
    QString name;
    std::shared_ptr<AudioFile> sourceFile; // may be null for silence gaps
    qint64  timelinePosSamples = 0;  // left edge on timeline
    qint64  srcInSamples       = 0;  // offset into sourceFile where region starts
    qint64  srcOutSamples      = -1; // -1 = end of file
    float   gainDb             = 0.f;
    FadeCurve fadeIn;
    FadeCurve fadeOut;
    QColor  color              = QColor(60, 130, 200);

    qint64 durationSamples() const;
    qint64 timelineEndSamples() const;

    QJsonObject toJson() const;
    static AudioRegion fromJson(const QJsonObject &, std::shared_ptr<AudioFile> file);
};

// ── Track ─────────────────────────────────────────────────────────────────────

class AudioEditorTrack : public QObject {
    Q_OBJECT
public:
    explicit AudioEditorTrack(QObject *parent = nullptr);
    ~AudioEditorTrack() override;

    QUuid   id()      const { return m_id; }
    QString name()    const { return m_name; }
    float   volume()  const { return m_volume; }  // linear
    bool    isMuted() const { return m_muted; }
    bool    isSoloed()const { return m_soloed; }

    void setName  (const QString &n) { m_name   = n; emit changed(); }
    void setVolume(float v)          { m_volume  = v; emit changed(); }
    void setMuted (bool m)           { m_muted   = m; emit changed(); }
    void setSoloed(bool s)           { m_soloed  = s; emit changed(); }

    const std::vector<AudioRegion> &regions()  const { return m_regions; }
    std::vector<AudioRegion>       &regions()        { return m_regions; }

    // Effects chain — track owns effects via unique_ptr, parent = nullptr
    const std::vector<std::unique_ptr<AudioEffect>> &effects() const { return m_effects; }
    AudioEffect *addEffect(AudioEffect::Type t);
    void removeEffect(int index);
    void moveEffect(int from, int to);

    QJsonObject toJson() const;
    void fromJson(const QJsonObject &);

signals:
    void changed();

private:
    QUuid   m_id;
    QString m_name;
    float   m_volume = 1.f;
    bool    m_muted  = false;
    bool    m_soloed = false;
    std::vector<AudioRegion>                   m_regions;
    std::vector<std::unique_ptr<AudioEffect>>  m_effects;
};

// ── Model ─────────────────────────────────────────────────────────────────────

class AudioEditorModel : public QObject {
    Q_OBJECT
public:
    explicit AudioEditorModel(QObject *parent = nullptr);
    ~AudioEditorModel() override;

    // Initialise from a single audio file (opens as one track / one region).
    void initFromFile(const QString &path, int sampleRate = 48000);

    // Serialise / deserialise the whole model (for cue payload storage).
    QJsonObject toJson() const;
    void        fromJson(const QJsonObject &);

    int  sampleRate()       const { return m_sampleRate; }
    bool isDirty()          const { return m_dirty; }
    void markClean()              { m_dirty = false; }

    QUndoStack *undoStack()       { return &m_undoStack; }

    int trackCount()              const { return int(m_tracks.size()); }
    AudioEditorTrack *track(int i) const;
    AudioEditorTrack *addTrack(const QString &name = QString());
    void removeTrack(int index);

    // Returns duration of the longest track in frames.
    qint64 totalDurationSamples() const;

    // Returns {trackIndex, regionIndex} or {-1,-1}
    std::pair<int,int> findRegion(const QUuid &id) const;

signals:
    void tracksChanged();
    void regionMoved(QUuid regionId);
    void dirtyChanged(bool);

public slots:
    // Undoable mutations — push commands onto m_undoStack
    void moveRegion  (QUuid regionId, qint64 newTimelinePos);
    void trimRegion  (QUuid regionId, bool leftEdge, qint64 newSample);
    void splitRegion (QUuid regionId, qint64 splitAtTimeline);
    void removeRegion(QUuid regionId);
    void setRegionGain(QUuid regionId, float gainDb);

private:
    friend class MoveRegionCmd;
    friend class TrimRegionCmd;
    friend class SplitRegionCmd;
    friend class RemoveRegionCmd;
    friend class SetRegionGainCmd;

    int m_sampleRate = 48000;
    std::vector<std::unique_ptr<AudioEditorTrack>> m_tracks;
    QUndoStack m_undoStack;
    bool m_dirty = false;

    void setDirty();
};

} // namespace quewi::audio
