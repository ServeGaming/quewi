#pragma once

#include "audio/AudioFile.h"
#include "audio/AudioTrajectory.h"
#include "cues/Cue.h"

#include <QUuid>
#include <QJsonObject>
#include <memory>
#include <vector>

namespace quewi::audio {

class AudioEngine;
class AudioEffect;

// A cue that plays an audio file through the AudioEngine.
//
// Phase 5.5 covers:
//   - file path
//   - gain (dB)
//   - fade in / fade out (seconds)
//   - trim in / trim out (seconds; 0 = play to end)
//   - pan (-1.0 = full L … +1.0 = full R)
//   - loop
//
// Slices, per-output matrix levels, and effects-chain plugins live in
// follow-up commits and the planned full audio editor (Phase 9).
class AudioCue : public cues::Cue {
    Q_OBJECT
public:
    explicit AudioCue(QObject *parent = nullptr);
    ~AudioCue() override;

    QString typeKey()  const override { return QStringLiteral("audio"); }
    QString typeName() const override { return tr("Audio"); }

    QVariant field(const QString &key) const override;
    void     setField(const QString &key, const QVariant &value) override;

    QJsonObject toPayload() const override;
    void        fromPayload(const QJsonObject &payload) override;

    void prepare();

    QString filePath()       const { return m_filePath; }
    double  gainDb()         const { return m_gainDb; }
    double  fadeInSeconds()  const { return m_fadeInSeconds; }
    double  fadeOutSeconds() const { return m_fadeOutSeconds; }
    double  trimInSeconds()  const { return m_trimInSeconds; }
    double  trimOutSeconds() const { return m_trimOutSeconds; }
    double  pan()            const { return m_pan; }
    bool    loop()           const { return m_loop; }
    QByteArray outputDeviceId() const { return m_outputDeviceId; }

    // Per-output send levels in dB. Empty = passthrough on every
    // channel (legacy behaviour). When non-empty, the engine applies
    // these gains AFTER the stereo pan so the operator can route a
    // stereo cue to FOH at 0 dB and lobby at -12 dB independently.
    // Length is whatever the operator set in the inspector — the
    // mixer pads with 1.0 (0 dB) for any output channel beyond the
    // matrix length, and silently ignores object-audio cues
    // (channelGains owns the routing in that path).
    const QList<double> &outputGainsDb() const { return m_outputGainsDb; }
    void  setOutputGainsDb(const QList<double> &g) { m_outputGainsDb = g; emitChanged(); }

    // Object-audio. When objectAudioEnabled is true and a speakerPatchId
    // resolves to a SpeakerArray patch, the engine renders the cue as a
    // point source at (azimuthDeg, elevationDeg) with VBAP gains and the
    // legacy `pan` is ignored. spread (0..1) blends toward omni.
    bool       objectAudioEnabled() const { return m_objAudio; }
    QUuid      speakerPatchId()     const { return m_speakerPatchId; }
    double     objectAzimuthDeg()   const { return m_objAzimuth; }
    double     objectElevationDeg() const { return m_objElevation; }
    double     objectSpread()       const { return m_objSpread; }

    // Animated source position. When the trajectory has 2+ keyframes
    // and Object Audio is on, GoEngine ticks it at ~30 Hz and updates
    // the live VBAP gains on the playing voice.
    const AudioTrajectory& trajectory() const { return m_trajectory; }
    void  setTrajectory(AudioTrajectory t) { m_trajectory = std::move(t); emitChanged(); }

    // The editable multitrack session from the audio editor, stored as
    // opaque JSON (AudioEditorModel::toJson) so regions/tracks/gains/fades
    // survive reopening the editor and a show save/load. The cue still
    // *plays* its filePath; this only restores the editor's working state.
    const QJsonObject &editorModelJson() const { return m_editorModelJson; }
    void setEditorModelJson(const QJsonObject &j) { m_editorModelJson = j; emitChanged(); }

    // Build a fresh effects chain from the saved editor session (track 0's
    // rack in editorModelJson). GoEngine uses it to apply the cue's
    // EQ/comp/reverb/delay to the fired voice. Empty when the cue has no
    // editor session or no effects. See docs/dev/realtime-fx-plan.md.
    std::vector<std::shared_ptr<AudioEffect>> buildEffectChain() const;

    // Set one parameter on the cue's STORED effect of the given type key
    // (eq / compressor / reverb / delay), creating that effect with default
    // params if the cue doesn't have one yet. Takes effect on the next fire.
    // paramId is one of the effect's parameterIds() (e.g. compressor "ratio",
    // eq "eq3_gain"), or the special "enabled" (value != 0 => on). Returns
    // false on an unknown type or unknown paramId — so a remote setter can't
    // silently no-op. Used by the OSC /quewi/cue/<n>/fx/<type>/<param> verb.
    bool setEffectParam(const QString &typeKey, const QString &paramId, float value);

    // JSON summary of the cue's effect chain for remote discovery: each
    // effect's type, name, enabled flag, and params (id, label, current value,
    // min, max). Built from the stored chain. Used by /quewi/cue/<n>/fx/list.
    QJsonObject effectChainSummary() const;

    std::shared_ptr<AudioFile> audioFile() const { return m_file; }

    quint64 currentVoiceId() const { return m_currentVoiceId; }
    void    setCurrentVoiceId(quint64 id) { m_currentVoiceId = id; }

private:
    QString m_filePath;
    double  m_gainDb         = 0.0;
    double  m_fadeInSeconds  = 0.0;
    double  m_fadeOutSeconds = 0.0;
    double  m_trimInSeconds  = 0.0;
    double  m_trimOutSeconds = 0.0;
    double  m_pan            = 0.0;
    bool    m_loop           = false;
    QByteArray m_outputDeviceId;   // empty = use AudioEngine default

    // Object audio
    bool   m_objAudio    = false;
    QUuid  m_speakerPatchId;
    double m_objAzimuth   = 0.0;   // -180..+180, 0 = front, +90 = right
    double m_objElevation = 0.0;   // -90..+90, 0 = ear-level
    double m_objSpread    = 0.0;   // 0..1
    AudioTrajectory m_trajectory;

    // Linear length matches whatever the operator set; mixer pads.
    QList<double> m_outputGainsDb;

    // Editable audio-editor session (multitrack regions/effects). Empty
    // until the operator opens the editor and makes an edit.
    QJsonObject m_editorModelJson;

    std::shared_ptr<AudioFile> m_file;
    quint64                    m_currentVoiceId = 0;
};

} // namespace quewi::audio
