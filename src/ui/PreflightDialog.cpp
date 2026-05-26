#include "ui/PreflightDialog.h"

#include "ui/Theme.h"
#include "audio/AudioCue.h"
#include "audio/AudioFile.h"
#include "audio/SpeakerPatch.h"
#include "core/CueList.h"
#include "core/PatchManager.h"
#include "core/ScriptModel.h"
#include "core/Workspace.h"
#include "cues/Cue.h"
#include "cues/FadeCue.h"
#include "cues/GroupCue.h"
#include "cues/TargetingCue.h"
#include "lighting/LightCue.h"
#include "midi/MidiCue.h"
#include "osc/OscCue.h"
#include "video/VideoCue.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace quewi::ui {

namespace {
struct Issue {
    enum Severity { Ok, Warning, Error };
    Severity severity;
    QString  cueLabel;
    QString  message;
};

QString cueLabel(cues::Cue *c)
{
    return QStringLiteral("%1  %2  [%3]")
        .arg(QString::number(c->number(), 'f', 2),
             c->name().isEmpty() ? c->typeName() : c->name(),
             c->typeName());
}

QList<Issue> walk(core::Workspace *ws)
{
    QList<Issue> out;
    if (!ws) return out;

    // Workspace-level checks — script path resolution.
    if (auto *sm = ws->scriptModel(); sm && sm->hasScript()) {
        const auto p = sm->path();
        if (!QFileInfo::exists(p)) {
            out.append({Issue::Warning, QStringLiteral("script"),
                QObject::tr("script file missing on disk — %1").arg(p)});
        }
    }

    // Workspace-level — total decoded audio residency vs. budget.
    // Estimate compressed-file disk size × 8 as a generous upper bound
    // for decoded float32 PCM. Cheaper than touching every decoder and
    // catches "this show won't fit in RAM" before the operator hits GO
    // and discovers half their cues silently weren't pre-warmed.
    {
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        const qint64 budgetBytes = qint64(
            s.value(QStringLiteral("audio/memoryBudgetMB"), 512).toInt())
            * 1024 * 1024;
        qint64 estimated = 0;
        for (const auto &list : ws->cueLists()) {
            for (int row = 0; row < list->cueCount(); ++row) {
                if (auto *ac = qobject_cast<audio::AudioCue *>(list->cueAt(row))) {
                    const QFileInfo fi(ac->filePath());
                    if (fi.exists()) estimated += fi.size() * 8;
                }
            }
        }
        if (estimated > budgetBytes) {
            out.append({Issue::Warning, QStringLiteral("memory"),
                QObject::tr("estimated decoded audio (~%1 MB) exceeds the "
                            "%2 MB budget. Cues that don't fit will decode "
                            "lazily on GO; raise the cap in Preferences.")
                    .arg(estimated / (1024 * 1024))
                    .arg(budgetBytes / (1024 * 1024))});
        }
    }

    for (const auto &list : ws->cueLists()) {
        for (int row = 0; row < list->cueCount(); ++row) {
            auto *c = list->cueAt(row);
            if (!c) continue;
            const auto label = cueLabel(c);

            if (auto *ac = qobject_cast<audio::AudioCue *>(c)) {
                if (ac->filePath().isEmpty()) {
                    out.append({Issue::Error, label, QObject::tr("audio: no file selected")});
                } else if (!QFileInfo::exists(ac->filePath())) {
                    out.append({Issue::Error, label, QObject::tr("audio: file missing — %1").arg(ac->filePath())});
                } else if (QFileInfo(ac->filePath()).size() == 0) {
                    out.append({Issue::Error, label, QObject::tr("audio: file is zero bytes — %1").arg(ac->filePath())});
                } else if (auto file = ac->audioFile();
                           file && file->state() == audio::AudioFile::State::Failed) {
                    out.append({Issue::Error, label, QObject::tr("audio: decode failed — %1").arg(file->errorString())});
                }
                // Object-audio cue must resolve to a real speaker patch.
                if (ac->objectAudioEnabled() && ws && ws->patches()) {
                    const auto speakers = audio::readSpeakers(ws->patches(),
                                                              ac->speakerPatchId());
                    if (speakers.isEmpty()) {
                        out.append({Issue::Warning, label,
                            QObject::tr("audio: object-audio is on but speaker patch is empty")});
                    }
                }
                // Trim sanity check.
                const auto fIn  = ac->trimInSeconds();
                const auto fOut = ac->trimOutSeconds();
                if (fOut > 0.0 && fIn >= fOut) {
                    out.append({Issue::Warning, label,
                        QObject::tr("audio: trim-in (%1 s) >= trim-out (%2 s) — cue won't play")
                            .arg(fIn).arg(fOut)});
                }
            } else if (auto *oc = qobject_cast<osc::OscCue *>(c)) {
                const auto dv = oc->destination();
                if (dv.host.isEmpty()) {
                    out.append({Issue::Error, label, QObject::tr("osc: empty host")});
                }
                if (dv.port == 0) {
                    out.append({Issue::Error, label, QObject::tr("osc: port is 0")});
                }
                if (oc->field(QStringLiteral("address")).toString().isEmpty()) {
                    out.append({Issue::Error, label, QObject::tr("osc: empty address")});
                }
            } else if (auto *vc = qobject_cast<video::VideoCue *>(c)) {
                if (vc->filePath().isEmpty()) {
                    out.append({Issue::Error, label, QObject::tr("video: no file")});
                } else if (!QFileInfo::exists(vc->filePath())) {
                    out.append({Issue::Error, label, QObject::tr("video: file missing — %1").arg(vc->filePath())});
                }
            } else if (auto *ic = qobject_cast<video::ImageCue *>(c)) {
                if (ic->filePath().isEmpty()) {
                    out.append({Issue::Error, label, QObject::tr("image: no file")});
                } else if (!QFileInfo::exists(ic->filePath())) {
                    out.append({Issue::Error, label, QObject::tr("image: file missing — %1").arg(ic->filePath())});
                }
            } else if (auto *lc = qobject_cast<lighting::LightCue *>(c)) {
                if (lc->channels().isEmpty()) {
                    out.append({Issue::Warning, label, QObject::tr("light: no channels set")});
                }
            } else if (auto *fc = qobject_cast<cues::FadeCue *>(c)) {
                if (fc->targetId().isNull()) {
                    out.append({Issue::Error, label, QObject::tr("fade: no target")});
                }
            } else if (auto *tc = qobject_cast<cues::TargetingCue *>(c)) {
                if (tc->targetId().isNull()) {
                    out.append({Issue::Error, label, QObject::tr("%1: no target")
                        .arg(c->typeName().toLower())});
                }
            } else if (auto *gc = qobject_cast<cues::GroupCue *>(c)) {
                if (gc->childIds().isEmpty()) {
                    out.append({Issue::Warning, label, QObject::tr("group: no children")});
                }
            } else if (auto *mc = qobject_cast<midi::MidiCue *>(c)) {
                if (mc->bytes().isEmpty()) {
                    out.append({Issue::Error, label, QObject::tr("midi: no bytes")});
                }
            }

            if (c->preWait() > 60.0) {
                out.append({Issue::Warning, label,
                    QObject::tr("pre-wait is %1 s — typo?").arg(c->preWait())});
            }
        }
    }
    return out;
}
} // namespace

PreflightDialog::PreflightDialog(core::Workspace *workspace, QWidget *parent)
    : QDialog(parent), m_workspace(workspace)
{
    setWindowTitle(tr("Pre-flight"));
    resize(640, 500);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(14);

    m_summary = new QLabel(this);
    m_summary->setStyleSheet(QStringLiteral(
        "font-size:14px; font-weight:600; padding:6px 0;"));
    root->addWidget(m_summary);

    m_results = new QListWidget(this);
    // No more alternating banding — same uniform-row + divider
    // treatment we landed on for the main cue list in v0.9.36 so
    // the two views feel like part of the same product.
    m_results->setAlternatingRowColors(false);
    m_results->setStyleSheet(QStringLiteral(
        "QListWidget::item { padding: 8px 10px; "
        "                    border-bottom: 1px solid %1; }")
        .arg(Theme::tokens().divider.name()));
    root->addWidget(m_results, 1);

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);
    m_recheck = new QPushButton(tr("Re-check"), this);
    btnRow->addWidget(m_recheck);
    btnRow->addStretch(1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    btnRow->addWidget(bb);
    root->addLayout(btnRow);

    connect(m_recheck, &QPushButton::clicked, this, &PreflightDialog::runChecks);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    runChecks();
}

PreflightDialog::~PreflightDialog() = default;

void PreflightDialog::runChecks()
{
    m_results->clear();
    const auto issues = walk(m_workspace);
    int errors = 0, warnings = 0;
    for (const auto &i : issues) {
        if (i.severity == Issue::Error) ++errors;
        else if (i.severity == Issue::Warning) ++warnings;
        const QString prefix = (i.severity == Issue::Error) ? QStringLiteral("✕")
                              : (i.severity == Issue::Warning) ? QStringLiteral("⚠")
                              : QStringLiteral("✓");
        auto *item = new QListWidgetItem(
            QStringLiteral("%1  %2 — %3").arg(prefix, i.cueLabel, i.message));
        if (i.severity == Issue::Error) item->setForeground(QColor(0xFF, 0x5A, 0x5A));
        else if (i.severity == Issue::Warning) item->setForeground(QColor(0xF2, 0xC9, 0x4C));
        m_results->addItem(item);
    }

    const auto &tk = Theme::tokens();
    if (errors == 0 && warnings == 0) {
        m_summary->setText(tr("✓  Ready — no problems found."));
        m_summary->setStyleSheet(QStringLiteral(
            "color:%1; font-size:14px; font-weight:600; padding:6px 0;")
            .arg(tk.running.name()));
    } else {
        m_summary->setText(tr("%1 problem%2 · %3 warning%4")
            .arg(errors).arg(errors == 1 ? QString() : QStringLiteral("s"))
            .arg(warnings).arg(warnings == 1 ? QString() : QStringLiteral("s")));
        m_summary->setStyleSheet(QStringLiteral(
            "color:%1; font-size:14px; font-weight:600; padding:6px 0;")
            .arg(errors > 0 ? tk.errBright.name() : tk.warn.name()));
    }
}

} // namespace quewi::ui
