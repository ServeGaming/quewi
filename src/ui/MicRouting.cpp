#include "ui/MicRouting.h"

#include <QSettings>

namespace quewi::ui {

MicRouting *MicRouting::instance()
{
    static MicRouting *s = new MicRouting;   // app lifetime
    return s;
}

MicRouting::MicRouting()
{
    const QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    m_deviceId    = s.value(QStringLiteral("soundboard/mic/device")).toByteArray();
    m_monitor     = s.value(QStringLiteral("soundboard/mic/monitor"), true).toBool();
    m_sfxGainDb   = s.value(QStringLiteral("soundboard/mic/sfxGainDb"), 0.0).toDouble();
    m_passthrough = s.value(QStringLiteral("soundboard/mic/passthrough"), false).toBool();
    m_inputId     = s.value(QStringLiteral("soundboard/mic/input")).toByteArray();
    m_inputGainDb = s.value(QStringLiteral("soundboard/mic/inputGainDb"), 0.0).toDouble();
}

void MicRouting::save()
{
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    s.setValue(QStringLiteral("soundboard/mic/device"), m_deviceId);
    s.setValue(QStringLiteral("soundboard/mic/monitor"), m_monitor);
    s.setValue(QStringLiteral("soundboard/mic/sfxGainDb"), m_sfxGainDb);
    s.setValue(QStringLiteral("soundboard/mic/passthrough"), m_passthrough);
    s.setValue(QStringLiteral("soundboard/mic/input"), m_inputId);
    s.setValue(QStringLiteral("soundboard/mic/inputGainDb"), m_inputGainDb);
}

void MicRouting::setDeviceId(const QByteArray &id)
{
    if (id == m_deviceId) return;
    m_deviceId = id;
    save();
    emit changed();
    emit inputChanged();   // the passthrough follows the device
}

void MicRouting::setMonitor(bool on)
{
    if (on == m_monitor) return;
    m_monitor = on; save(); emit changed();
}

void MicRouting::setSfxGainDb(double db)
{
    if (qFuzzyCompare(db + 100.0, m_sfxGainDb + 100.0)) return;
    m_sfxGainDb = db; save(); emit changed();
}

void MicRouting::setPassthrough(bool on)
{
    if (on == m_passthrough) return;
    m_passthrough = on; save(); emit changed(); emit inputChanged();
}

void MicRouting::setInputDeviceId(const QByteArray &id)
{
    if (id == m_inputId) return;
    m_inputId = id; save(); emit changed(); emit inputChanged();
}

void MicRouting::setInputGainDb(double db)
{
    if (qFuzzyCompare(db + 100.0, m_inputGainDb + 100.0)) return;
    m_inputGainDb = db; save(); emit changed();
}

bool MicRouting::looksVirtual(const QString &d)
{
    for (const char *k : { "CABLE", "VB-Audio", "VoiceMeeter", "Voicemeeter", "Virtual",
                           "Voicemod", "Loopback", "BlackHole" })
        if (d.contains(QLatin1String(k), Qt::CaseInsensitive)) return true;
    return false;
}

} // namespace quewi::ui
