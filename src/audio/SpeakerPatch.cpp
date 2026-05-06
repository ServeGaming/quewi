#include "audio/SpeakerPatch.h"

#include <QJsonDocument>

namespace quewi::audio {

namespace {

QList<Speaker> stereo() {
    return {
        Speaker{0, -30, 0, 1.0f},
        Speaker{1,  30, 0, 1.0f},
    };
}

QList<Speaker> surround5_1() {
    // L, R, C, [LFE skipped — VBAP doesn't pan to it], LS, RS.
    // LFE = channel 3, sent a downmix in real-world setups; for v1 we
    // omit it from VBAP and the audio engine will leave channel 3 silent.
    return {
        Speaker{0, -30,   0, 1.0f},
        Speaker{1,  30,   0, 1.0f},
        Speaker{2,   0,   0, 1.0f},
        Speaker{4, -110,  0, 1.0f},
        Speaker{5,  110,  0, 1.0f},
    };
}

QList<Speaker> surround7_1() {
    return {
        Speaker{0, -30,   0, 1.0f},  // L
        Speaker{1,  30,   0, 1.0f},  // R
        Speaker{2,   0,   0, 1.0f},  // C
        // ch 3 = LFE (omitted)
        Speaker{4,  -90,  0, 1.0f},  // LSS (side surround left)
        Speaker{5,   90,  0, 1.0f},  // RSS
        Speaker{6, -150,  0, 1.0f},  // LRS (rear surround left)
        Speaker{7,  150,  0, 1.0f},  // RRS
    };
}

QList<Speaker> atmos7_1_4() {
    auto list = surround7_1();
    // Four height speakers at ±45° front, ±135° rear, +45° elevation.
    list.append(Speaker{ 8,  -45,  45, 1.0f});  // TFL — top front left
    list.append(Speaker{ 9,   45,  45, 1.0f});  // TFR
    list.append(Speaker{10, -135,  45, 1.0f});  // TRL
    list.append(Speaker{11,  135,  45, 1.0f});  // TRR
    return list;
}

} // namespace

QList<Speaker> readSpeakers(const core::PatchManager *patches,
                            const QUuid &patchId)
{
    if (!patches || patchId.isNull()) return {};
    if (!patches->contains(patchId))  return {};
    const auto patch = patches->patch(patchId);
    if (patch.category != core::PatchManager::Category::SpeakerArray) return {};

    // The "speakers" field is stored as a QJsonArray when the dialog
    // writes it, but a JSON show-file roundtrip walks it through
    // QVariantMap → QJsonObject → QVariantMap, which decays the inner
    // array into a QVariantList of QVariantMap. Handle both shapes so
    // newly-edited and freshly-loaded shows behave identically.
    auto readEntry = [](const QVariantMap &o) -> Speaker {
        Speaker s;
        s.channel      = o.value(QStringLiteral("channel")).toInt();
        s.azimuthDeg   = static_cast<float>(o.value(QStringLiteral("azimuthDeg")).toDouble());
        s.elevationDeg = static_cast<float>(o.value(QStringLiteral("elevationDeg")).toDouble());
        s.distance     = static_cast<float>(o.value(QStringLiteral("distance"), 1.0).toDouble());
        return s;
    };

    const auto v = patch.fields.value(QStringLiteral("speakers"));
    QList<Speaker> out;
    if (v.userType() == QMetaType::QVariantList) {
        const auto list = v.toList();
        out.reserve(list.size());
        for (const auto &x : list) out.append(readEntry(x.toMap()));
    } else {
        const auto arr = v.toJsonArray();
        out.reserve(arr.size());
        for (const auto &x : arr) {
            const auto o = x.toObject();
            QVariantMap m;
            for (auto it = o.constBegin(); it != o.constEnd(); ++it)
                m.insert(it.key(), it.value().toVariant());
            out.append(readEntry(m));
        }
    }
    return out;
}

QVariantMap toPatchFields(const QString &templateKey,
                          const QList<Speaker> &speakers)
{
    QJsonArray arr;
    for (const auto &s : speakers) {
        QJsonObject o;
        o.insert(QStringLiteral("channel"),      s.channel);
        o.insert(QStringLiteral("azimuthDeg"),   s.azimuthDeg);
        o.insert(QStringLiteral("elevationDeg"), s.elevationDeg);
        o.insert(QStringLiteral("distance"),     s.distance);
        arr.append(o);
    }
    QVariantMap out;
    out.insert(QStringLiteral("templateKey"), templateKey);
    out.insert(QStringLiteral("speakers"),    arr);
    return out;
}

QList<Speaker> templateSpeakers(const QString &templateKey)
{
    if (templateKey == QLatin1String("5.1"))   return surround5_1();
    if (templateKey == QLatin1String("7.1"))   return surround7_1();
    if (templateKey == QLatin1String("7.1.4")) return atmos7_1_4();
    return stereo();
}

QStringList templateKeys()
{
    return { QStringLiteral("stereo"),
             QStringLiteral("5.1"),
             QStringLiteral("7.1"),
             QStringLiteral("7.1.4") };
}

QString templateLabel(const QString &key)
{
    if (key == QLatin1String("stereo")) return QObject::tr("Stereo (L / R)");
    if (key == QLatin1String("5.1"))    return QObject::tr("5.1 surround");
    if (key == QLatin1String("7.1"))    return QObject::tr("7.1 surround");
    if (key == QLatin1String("7.1.4"))  return QObject::tr("Atmos 7.1.4 (with height)");
    return key;
}

} // namespace quewi::audio
