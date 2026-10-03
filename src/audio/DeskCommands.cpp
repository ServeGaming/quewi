#include "audio/DeskCommands.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace quewi::audio {

namespace {

using Do   = TriggerAction::DeskDo;
using Type = core::LightingDesk::Type;

QString tr(const char *s) { return QCoreApplication::translate("DeskCommands", s); }

constexpr int kKeyReleaseMs = 50;   // same press length HeliOSC uses

DeskSend osc(const QString &address, QVariantList args = {}, int delayMs = 0)
{
    DeskSend s;
    s.address = address;
    s.args = std::move(args);
    s.delayMs = delayMs;
    return s;
}

// Press, then release after `ms`.
void press(std::vector<DeskSend> &out, const QString &address, int ms)
{
    out.push_back(osc(address, {1.0f}));
    out.push_back(osc(address, {0.0f}, ms));
}

int holdMs(const TriggerAction &a) { return int(std::lround(std::clamp(a.hold, 0.02, 30.0) * 1000.0)); }
float level01(const TriggerAction &a) { return float(std::clamp(a.level, 0, 100)) / 100.0f; }

bool needsNumber(Do d)
{
    switch (d) {
    case Do::GoToCue: case Do::SubLevel: case Do::SubBump: case Do::FaderLevel:
    case Do::FaderBump: case Do::Macro:
        return true;
    default:
        return false;
    }
}

std::vector<DeskSend> eos(const core::LightingDesk &desk, const TriggerAction &a)
{
    std::vector<DeskSend> out;
    const QString n = a.number;
    const int list = std::max(1, a.list);
    switch (a.deskDo) {
    case Do::Go:   press(out, QStringLiteral("/eos/key/go_0"), kKeyReleaseMs); break;
    case Do::Stop: press(out, QStringLiteral("/eos/key/stop"), kKeyReleaseMs); break;
    case Do::Back: press(out, QStringLiteral("/eos/key/back"), kKeyReleaseMs); break;
    case Do::GoToCue:
        out.push_back(osc(QStringLiteral("/eos/cue/%1/%2/fire").arg(list).arg(n)));
        break;
    case Do::SubLevel:
        out.push_back(osc(QStringLiteral("/eos/sub/%1").arg(n), {level01(a)}));
        break;
    case Do::SubBump:
        press(out, QStringLiteral("/eos/sub/%1/fire").arg(n), holdMs(a));
        break;
    case Do::FaderLevel:
    case Do::FaderBump: {
        // Point quewi's own OSC fader bank at that page first (cheap, and it
        // means a remote re-paging its bank can't move ours).
        const int count = std::max(10, n.toInt());
        out.push_back(osc(QStringLiteral("/eos/fader/%1/config/%2/%3")
                              .arg(desk.eosFaderBank).arg(list).arg(count)));
        const QString fader = QStringLiteral("/eos/fader/%1/%2").arg(desk.eosFaderBank).arg(n);
        if (a.deskDo == Do::FaderLevel) out.push_back(osc(fader, {level01(a)}));
        else                            press(out, fader + QStringLiteral("/fire"), holdMs(a));
        break;
    }
    case Do::Macro:
        press(out, QStringLiteral("/eos/macro/%1/fire").arg(n), kKeyReleaseMs);
        break;
    case Do::GoList: {
        // Eos has no "GO on list N" — not over OSC, and the command line's
        // Go_CueList (seen in macros) just leaves the number behind (it
        // selected channel 2 on a Nomad). ETC's way: press the GO button of
        // the playback fader the list is loaded on.
        const int count = std::max(10, n.toInt());
        out.push_back(osc(QStringLiteral("/eos/fader/%1/config/%2/%3")
                              .arg(desk.eosFaderBank).arg(list).arg(count)));
        press(out, QStringLiteral("/eos/fader/%1/%2/fire").arg(desk.eosFaderBank).arg(n),
              kKeyReleaseMs);
        break;
    }
    case Do::Command: {
        // newcmd clears whatever is half-typed on the desk first, so our
        // command can't be glued onto it.
        QString c = a.text.trimmed();
        if (!c.endsWith(QLatin1String("Enter"), Qt::CaseInsensitive) && !c.endsWith(QLatin1Char('#')))
            c += QStringLiteral(" Enter");
        out.push_back(osc(QStringLiteral("/eos/newcmd"), {c}));
        break;
    }
    }
    return out;
}

std::vector<DeskSend> ma3(const core::LightingDesk &desk, const TriggerAction &a)
{
    const QString address = desk.ma3Prefix.isEmpty()
        ? QStringLiteral("/cmd") : QStringLiteral("/%1/cmd").arg(desk.ma3Prefix);
    QString cmd;
    switch (a.deskDo) {
    case Do::Go:      cmd = QStringLiteral("Go+"); break;
    case Do::Back:    cmd = QStringLiteral("Go-"); break;
    case Do::GoList:  cmd = QStringLiteral("Go+ Sequence %1").arg(std::max(1, a.list)); break;
    case Do::GoToCue: cmd = QStringLiteral("Goto Cue %1 Sequence %2").arg(a.number).arg(std::max(1, a.list)); break;
    case Do::Command: cmd = a.text.trimmed(); break;
    default:          return {};
    }
    return {osc(address, {cmd})};
}

std::vector<DeskSend> msc(const TriggerAction &a)
{
    DeskSend s;
    s.type = DeskSend::Type::Msc;
    switch (a.deskDo) {
    case Do::Go:
        s.mscCommand = 0x01;                       // GO, no cue = next
        break;
    case Do::GoToCue:
        s.mscCommand = 0x01;                       // GO cue n [list m]
        s.mscPayload = a.number.toLatin1();
        s.mscPayload.append(char(0x00));
        s.mscPayload.append(QByteArray::number(std::max(1, a.list)));
        break;
    case Do::Stop:
        s.mscCommand = 0x02;                       // STOP (everything)
        break;
    case Do::Macro: {
        bool ok = false;
        const int m = a.number.toInt(&ok);
        if (!ok || m < 0 || m > 127) return {};
        s.mscCommand = 0x07;                       // FIRE macro n
        s.mscPayload = QByteArray(1, char(m));
        break;
    }
    default:
        return {};
    }
    return {s};
}

} // namespace

bool deskSupports(Type type, Do what)
{
    switch (type) {
    case Type::Eos:
        return true;
    case Type::Ma3:
        return what == Do::Go || what == Do::Back || what == Do::GoToCue || what == Do::Command
            || what == Do::GoList;
    case Type::Ma2Msc:
        return what == Do::Go || what == Do::GoToCue || what == Do::Stop || what == Do::Macro;
    }
    return false;
}

std::vector<DeskSend> deskSends(const core::LightingDesk &desk, const TriggerAction &a, QString *error)
{
    // On Eos, "another cue list" means a fader: it needs the fader number.
    if (a.kind == TriggerAction::Kind::Desk && a.deskDo == Do::GoList
        && desk.type == Type::Eos && a.number.trimmed().isEmpty()) {
        if (error) *error = tr("pick the fader that cue list is loaded on");
        return {};
    }
    auto fail = [&](const QString &why) {
        if (error) *error = why;
        return std::vector<DeskSend>{};
    };
    if (a.kind != TriggerAction::Kind::Desk) return fail(tr("not a desk action"));
    if (!deskSupports(desk.type, a.deskDo))
        return fail(tr("%1 can't do \"%2\"").arg(desk.typeName(), TriggerAction::deskDoName(a.deskDo)));
    if (needsNumber(a.deskDo) && a.number.trimmed().isEmpty())
        return fail(tr("\"%1\" needs a number").arg(TriggerAction::deskDoName(a.deskDo)));
    if (a.deskDo == Do::Command && a.text.trimmed().isEmpty())
        return fail(tr("the desk command is empty"));

    std::vector<DeskSend> out;
    switch (desk.type) {
    case Type::Eos:    out = eos(desk, a); break;
    case Type::Ma3:    out = ma3(desk, a); break;
    case Type::Ma2Msc: out = msc(a);       break;
    }
    if (out.empty()) return fail(tr("couldn't build that for %1").arg(desk.typeName()));
    return out;
}

} // namespace quewi::audio
