#include <QTest>
#include <QJsonDocument>
#include <QSignalSpy>

#include "audio/AudioCue.h"
#include "audio/AudioEditorModel.h"
#include "audio/AudioEffect.h"
#include "audio/EffectPresets.h"

#include <cmath>
#include <set>
#include <vector>

using namespace quewi::audio;

// The effects rack: every effect type round-trips through the show file,
// the four slider effects do what they say on real signals, every built-in
// preset only names real parameters, effect edits count as edits (they used
// to be dropped when the editor closed), and a cue playing its own render
// doesn't apply the rack a second time.
class EffectsTests : public QObject {
    Q_OBJECT

    static constexpr int kSr = 48000;
    static constexpr double kPi = 3.14159265358979323846;

    // Interleaved stereo sine, same on both channels.
    static std::vector<float> sine(double hz, int frames, float amp = 0.5f)
    {
        std::vector<float> v(size_t(frames) * 2);
        for (int i = 0; i < frames; ++i) {
            const float s = amp * float(std::sin(2 * kPi * hz * i / kSr));
            v[size_t(i) * 2] = v[size_t(i) * 2 + 1] = s;
        }
        return v;
    }
    // Upward zero crossings of the left channel from `from` → frequency.
    static double frequencyOf(const std::vector<float> &v, int from)
    {
        const int frames = int(v.size() / 2);
        int crossings = 0, first = -1, last = -1;
        for (int i = from + 1; i < frames; ++i) {
            if (v[size_t(i - 1) * 2] <= 0.f && v[size_t(i) * 2] > 0.f) {
                if (first < 0) first = i;
                last = i; ++crossings;
            }
        }
        if (crossings < 2) return 0.0;
        return double(crossings - 1) * kSr / double(last - first);
    }
    static std::unique_ptr<AudioEffect> make(AudioEffect::Type t,
                                             std::initializer_list<std::pair<const char *, float>> params)
    {
        auto fx = AudioEffect::create(t);
        fx->prepare(kSr);
        for (const auto &[id, v] : params) fx->setParameterValue(QString::fromLatin1(id), v);
        return fx;
    }

private slots:
    void everyTypeRoundTripsThroughJson()
    {
        for (const auto t : AudioEffect::allTypes()) {
            const QString key = AudioEffect::typeKey(t);
            QVERIFY2(!key.isEmpty(), "every type has a key");
            QCOMPARE(AudioEffect::typeFromKey(key).value(), t);

            auto fx = AudioEffect::create(t);
            QVERIFY(fx);
            QCOMPARE(fx->type(), t);
            // Move every parameter off its default, save, load, compare.
            for (const QString &id : fx->parameterIds()) {
                const auto [lo, hi] = fx->parameterRange(id);
                fx->setParameterValue(id, lo + (hi - lo) * 0.3f);
            }
            fx->setEnabled(false);
            const auto back = AudioEffect::fromJson(fx->toJson());
            QVERIFY(back);
            QCOMPARE(back->type(), t);
            QVERIFY(!back->isEnabled());
            for (const QString &id : fx->parameterIds())
                QVERIFY2(std::abs(back->parameterValue(id) - fx->parameterValue(id)) < 1e-4f,
                         qPrintable(key + QLatin1Char('.') + id));
        }
        QVERIFY(!AudioEffect::fromJson(QJsonObject{{QStringLiteral("type"), QStringLiteral("nope")}}));
    }

    void newEffectsStayQuietOnSilenceAndBoundedOnLoud()
    {
        using T = AudioEffect::Type;
        for (const auto t : { T::Distortion, T::LoFi, T::PitchShift, T::Tremolo }) {
            auto fx = make(t, {});
            std::vector<float> silence(4800 * 2, 0.f);
            fx->process(silence.data(), 4800);
            for (float s : silence) QCOMPARE(s, 0.f);

            auto loud = sine(440, 9600, 1.0f);
            fx->reset();
            fx->process(loud.data(), 9600);
            for (float s : loud) {
                QVERIFY(std::isfinite(s));
                QVERIFY2(std::abs(s) <= 1.5f, qPrintable(AudioEffect::typeKey(t)));
            }
        }
    }

    void pitchShiftMovesPitchNotSpeed()
    {
        for (const auto [semis, expect] : { std::pair{12.f, 880.0}, std::pair{-12.f, 220.0},
                                            std::pair{7.f, 440.0 * std::pow(2.0, 7.0 / 12.0)} }) {
            auto fx = make(AudioEffect::Type::PitchShift, { {"semitones", semis}, {"mix", 1.f} });
            auto v = sine(440, kSr);                // one second
            fx->process(v.data(), kSr);
            const double f = frequencyOf(v, kSr / 4);   // past the start-up window
            QVERIFY2(std::abs(f - expect) / expect < 0.06,
                     qPrintable(QStringLiteral("%1 st: got %2 Hz, want %3").arg(semis).arg(f).arg(expect)));
        }
        // Zero semitones is exactly transparent.
        auto fx = make(AudioEffect::Type::PitchShift, { {"semitones", 0.f} });
        auto v = sine(440, 1000), orig = v;
        fx->process(v.data(), 1000);
        QVERIFY(v == orig);
    }

    void loFiQuantises()
    {
        auto fx = make(AudioEffect::Type::LoFi, { {"bits", 2.f}, {"downsample", 1.f}, {"mix", 1.f} });
        auto v = sine(100, 4800, 0.9f);
        fx->process(v.data(), 4800);
        std::set<float> levels(v.begin(), v.end());
        QVERIFY2(levels.size() <= 5, "2 bits = a handful of levels");
        for (float s : levels) QVERIFY(std::abs(s * 2.f - std::round(s * 2.f)) < 1e-6f);
    }

    void tremoloStereoIsAnAutoPan()
    {
        auto fx = make(AudioEffect::Type::Tremolo, { {"rate", 2.f}, {"depth", 1.f}, {"stereo", 1.f} });
        std::vector<float> dc(kSr * 2, 0.8f);       // constant level: gain is visible directly
        fx->process(dc.data(), kSr);
        float minL = 1, maxL = 0;
        bool opposite = false;
        for (int i = 0; i < kSr; ++i) {
            const float l = dc[size_t(i) * 2], r = dc[size_t(i) * 2 + 1];
            minL = std::min(minL, l); maxL = std::max(maxL, l);
            if (l < 0.05f && r > 0.75f) opposite = true;
        }
        QVERIFY(minL < 0.05f && maxL > 0.75f);      // full depth swing
        QVERIFY2(opposite, "left down while right up");
    }

    void distortionDrivesQuietSignalsUp()
    {
        auto fx = make(AudioEffect::Type::Distortion,
                       { {"drive", 1.f}, {"tone", 1.f}, {"mix", 1.f}, {"output", 0.f} });
        auto v = sine(220, 9600, 0.1f);
        fx->process(v.data(), 9600);
        float peak = 0;
        for (int i = 4800; i < 9600; ++i) peak = std::max(peak, std::abs(v[size_t(i) * 2]));
        QVERIFY2(peak > 0.5f, "heavy drive saturates a -20 dB signal");
        QVERIFY(peak <= 1.01f);
    }

    void builtInPresetsNameOnlyRealParameters()
    {
        const auto presets = EffectPresets::builtIns();
        QVERIFY(presets.size() >= 20);
        QSet<QString> names;
        for (const auto &p : presets) {
            QVERIFY2(!names.contains(p.name), qPrintable(p.name));
            names.insert(p.name);
            QVERIFY(!p.group.isEmpty());
            QVERIFY(!p.effects.isEmpty());
            for (const auto &v : p.effects) {
                const QJsonObject o = v.toObject();
                auto fx = AudioEffect::fromJson(o);
                QVERIFY2(fx, qPrintable(p.name + QStringLiteral(": unknown effect type")));
                const auto ids = fx->parameterIds();
                const QJsonObject params = o.value(QStringLiteral("params")).toObject();
                for (auto it = params.begin(); it != params.end(); ++it) {
                    QVERIFY2(ids.contains(it.key()),
                             qPrintable(p.name + QStringLiteral(": no parameter ") + it.key()));
                    const auto [lo, hi] = fx->parameterRange(it.key());
                    const double val = it.value().toDouble();
                    QVERIFY2(val >= lo - 1e-6 && val <= hi + 1e-6,
                             qPrintable(p.name + QStringLiteral(": ") + it.key() + QStringLiteral(" out of range")));
                }
            }
        }
    }

    void effectEditsCountAsEditsButLoadingDoesnt()
    {
        AudioEditorModel model;
        auto *track = model.addTrack(QStringLiteral("T"));
        QSignalSpy edited(&model, &AudioEditorModel::effectsEdited);
        QVERIFY(!model.effectsDirty());

        auto *fx = track->addEffect(AudioEffect::Type::Reverb);
        QVERIFY(model.effectsDirty());
        QVERIFY(edited.count() >= 1);
        QVERIFY2(!model.isDirty(), "an effect edit doesn't mean the audio needs rendering");

        model.markClean();
        const int before = edited.count();
        fx->setParameterValue(QStringLiteral("wet"), 0.7f);
        QVERIFY(model.effectsDirty());
        QVERIFY(edited.count() > before);

        model.markClean();
        fx->setEnabled(false);
        QVERIFY2(model.effectsDirty(), "bypass is an edit too");

        // Presets replace the chain.
        model.markClean();
        track->setEffectsFromJson(EffectPresets::builtIns().first().effects);
        QVERIFY(model.effectsDirty());
        QCOMPARE(int(track->effects().size()), EffectPresets::builtIns().first().effects.size());

        // Loading a saved session is not an edit, and keeps the render path.
        model.setBouncedPath(QStringLiteral("C:/show/render.wav"));
        const QJsonObject saved = model.toJson();
        AudioEditorModel loaded;
        loaded.fromJson(saved);
        QVERIFY(!loaded.effectsDirty());
        QCOMPARE(loaded.bouncedPath(), QStringLiteral("C:/show/render.wav"));
        QCOMPARE(int(loaded.track(0)->effects().size()), int(track->effects().size()));
        // ...and its effects still report edits afterwards.
        loaded.track(0)->effects().front()->setEnabled(false);
        QVERIFY(loaded.effectsDirty());
    }

    void cuePlayingItsOwnRenderSkipsTheLiveRack()
    {
        AudioEditorModel model;
        auto *track = model.addTrack();
        track->addEffect(AudioEffect::Type::Reverb);
        track->addEffect(AudioEffect::Type::Delay);

        AudioCue cue;
        cue.setField(QStringLiteral("filePath"), QStringLiteral("C:/show/original.wav"));
        cue.setEditorModelJson(model.toJson());
        QCOMPARE(int(cue.buildEffectChain().size()), 2);   // plays original: rack live
        QVERIFY(!cue.effectsBakedIn());

        model.setBouncedPath(QStringLiteral("C:/show/render.wav"));
        cue.setEditorModelJson(model.toJson());
        QCOMPARE(int(cue.buildEffectChain().size()), 2);   // render exists, not playing it yet

        cue.setField(QStringLiteral("filePath"), QStringLiteral("C:/show/render.wav"));
        QVERIFY(cue.effectsBakedIn());
        QVERIFY2(cue.buildEffectChain().empty(), "the render already contains the rack");
        // Remote discovery still shows the rack as edited.
        QCOMPARE(cue.effectChainSummary().value(QStringLiteral("effects")).toArray().size(), 2);
    }

    // QFileInfo's == calls any two missing files "the same" — which made a
    // cue drop its rack whenever neither file existed yet.
    void sameFileComparesPathsEvenForMissingFiles()
    {
        QVERIFY(!AudioCue::sameFile(QStringLiteral("C:/nope/a.wav"), QStringLiteral("C:/nope/b.wav")));
        QVERIFY(AudioCue::sameFile(QStringLiteral("C:/nope/x/../a.wav"), QStringLiteral("C:/nope/a.wav")));
        QVERIFY(!AudioCue::sameFile(QString(), QString()));
#ifdef Q_OS_WIN
        QVERIFY(AudioCue::sameFile(QStringLiteral("C:/Show/A.wav"), QStringLiteral("c:\\show\\a.WAV")));
#endif
    }
};

QTEST_MAIN(EffectsTests)
#include "test_effects.moc"
