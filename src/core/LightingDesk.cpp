#include "core/LightingDesk.h"

#include <QCoreApplication>
#include <QSettings>

#include <algorithm>

namespace quewi::core {

QString LightingDesk::typeKey(Type t)
{
    switch (t) {
    case Type::Ma3:    return QStringLiteral("ma3");
    case Type::Ma2Msc: return QStringLiteral("ma2msc");
    case Type::Eos:    break;
    }
    return QStringLiteral("eos");
}

LightingDesk::Type LightingDesk::typeFromKey(const QString &key)
{
    const QString k = key.trimmed().toLower();
    if (k == QLatin1String("ma3")) return Type::Ma3;
    if (k == QLatin1String("ma2msc") || k == QLatin1String("msc")) return Type::Ma2Msc;
    return Type::Eos;
}

QString LightingDesk::typeName() const
{
    switch (type) {
    case Type::Ma3:    return QCoreApplication::translate("LightingDesk", "grandMA3");
    case Type::Ma2Msc: return QCoreApplication::translate("LightingDesk", "grandMA2 / MSC");
    case Type::Eos:    break;
    }
    return QCoreApplication::translate("LightingDesk", "ETC Eos / Ion / Nomad");
}

QString LightingDesk::summary() const
{
    if (type == Type::Ma2Msc)
        return QCoreApplication::translate("LightingDesk", "%1 on MIDI %2")
            .arg(typeName(), midiPort.isEmpty()
                     ? QCoreApplication::translate("LightingDesk", "(first port)") : midiPort);
    return QCoreApplication::translate("LightingDesk", "%1 at %2:%3")
        .arg(typeName(), host).arg(port);
}

QJsonObject LightingDesk::toJson() const
{
    return QJsonObject{
        {QStringLiteral("type"),         typeKey(type)},
        {QStringLiteral("host"),         host},
        {QStringLiteral("port"),         port},
        {QStringLiteral("ma3Prefix"),    ma3Prefix},
        {QStringLiteral("midiPort"),     midiPort},
        {QStringLiteral("mscDeviceId"),  mscDeviceId},
        {QStringLiteral("eosFaderBank"), eosFaderBank},
    };
}

LightingDesk LightingDesk::fromJson(const QJsonObject &o)
{
    LightingDesk d;
    d.type         = typeFromKey(o.value(QStringLiteral("type")).toString());
    d.host         = o.value(QStringLiteral("host")).toString(d.host).trimmed();
    d.port         = std::clamp(o.value(QStringLiteral("port")).toInt(d.port), 1, 65535);
    d.ma3Prefix    = o.value(QStringLiteral("ma3Prefix")).toString().trimmed().remove(QLatin1Char('/'));
    d.midiPort     = o.value(QStringLiteral("midiPort")).toString();
    d.mscDeviceId  = std::clamp(o.value(QStringLiteral("mscDeviceId")).toInt(d.mscDeviceId), 0, 0x7F);
    d.eosFaderBank = std::clamp(o.value(QStringLiteral("eosFaderBank")).toInt(d.eosFaderBank), 1, 99);
    return d;
}

LightingDesk LightingDesk::load()
{
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    QJsonObject o;
    s.beginGroup(QStringLiteral("lighting/desk"));
    for (const auto &key : s.childKeys()) o.insert(key, QJsonValue::fromVariant(s.value(key)));
    s.endGroup();
    // QSettings hands numbers back as strings from the registry/ini.
    for (const auto key : {QStringLiteral("port"), QStringLiteral("mscDeviceId"),
                           QStringLiteral("eosFaderBank")})
        if (o.contains(key)) o.insert(key, o.value(key).toVariant().toInt());
    return fromJson(o);
}

void LightingDesk::save() const
{
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    s.beginGroup(QStringLiteral("lighting/desk"));
    const auto o = toJson();
    for (auto it = o.begin(); it != o.end(); ++it) s.setValue(it.key(), it.value().toVariant());
    s.endGroup();
}

} // namespace quewi::core
