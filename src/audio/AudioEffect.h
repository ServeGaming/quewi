#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QPair>
#include <memory>
#include <optional>

namespace quewi::audio {

// Abstract base for all audio effects in the editor's effects rack.
// process() is called on the audio thread — no allocations, no Qt signals.
// Parameter changes from the UI go through setParameterValue(); the effect
// makes them atomic or double-buffered internally as needed.
class AudioEffect : public QObject {
    Q_OBJECT
public:
    // Append-only: the show file stores the string key (typeKey), not the
    // number, but keep the order stable anyway.
    enum class Type { Eq, Compressor, Reverb, Delay,
                      Distortion, LoFi, PitchShift, Tremolo };
    Q_ENUM(Type)

    explicit AudioEffect(QObject *parent = nullptr) : QObject(parent) {}
    ~AudioEffect() override = default;

    static std::unique_ptr<AudioEffect> create(Type t, QObject *parent = nullptr);

    // Canonical stable type keys used in the show file, the editor model JSON,
    // and the OSC API: "eq" / "compressor" / "reverb" / "delay" / "distortion"
    // / "lofi" / "pitch" / "tremolo". One source of truth so AudioCue,
    // AudioEngine, and the OSC handlers can't drift.
    static std::optional<Type> typeFromKey(const QString &key);
    static QString             typeKey(Type t);
    static QList<Type>         allTypes();

    // One effect as stored in the show: {type, enabled, params{id: value}}.
    // fromJson builds it (unknown type → nullptr; params missing from the
    // JSON keep their defaults, unknown param ids are ignored).
    QJsonObject toJson() const;
    static std::unique_ptr<AudioEffect> fromJson(const QJsonObject &o);

    virtual Type    type()    const = 0;
    virtual QString name()    const = 0;
    virtual bool    isEnabled() const { return m_enabled; }
    virtual void    setEnabled(bool e) { m_enabled = e; emit enabledChanged(e); }

    // Called once before any process() calls, and again if sample rate changes.
    virtual void prepare(int sampleRate) = 0;

    // Process interleaved stereo float buffer in-place (L,R,L,R,...).
    // numFrames is the number of stereo frames (buffer size = numFrames * 2).
    virtual void process(float *data, int numFrames) = 0;

    // Reset delay-line / envelope state without changing parameters.
    virtual void reset() = 0;

    virtual QStringList        parameterIds()                       const = 0;
    virtual QString            parameterLabel(const QString &id)    const = 0;
    virtual float              parameterValue(const QString &id)    const = 0;
    virtual void               setParameterValue(const QString &id, float v) = 0;
    virtual QPair<float,float> parameterRange(const QString &id)    const = 0;
    virtual float              parameterDefault(const QString &id)  const = 0;
    // How many decimal places to show in the UI (0 = integer knob).
    virtual int                parameterDecimals(const QString &)   const { return 1; }

signals:
    void enabledChanged(bool);
    void parameterChanged(const QString &id, float value);

protected:
    bool m_enabled = true;
};

} // namespace quewi::audio
