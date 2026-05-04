#include "ui/FindReplaceDialog.h"

#include "core/CueList.h"
#include "core/UndoCommands.h"
#include "core/Workspace.h"
#include "cues/Cue.h"
#include "osc/OscCue.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUndoStack>
#include <QVBoxLayout>

namespace quewi::ui {

FindReplaceDialog::FindReplaceDialog(core::Workspace *workspace, QWidget *parent)
    : QDialog(parent), m_workspace(workspace)
{
    setWindowTitle(tr("Find and replace"));
    resize(480, 280);

    auto *root = new QVBoxLayout(this);
    auto *form = new QFormLayout();
    m_find = new QLineEdit(this);
    form->addRow(tr("Find"), m_find);
    m_replace = new QLineEdit(this);
    form->addRow(tr("Replace"), m_replace);
    root->addLayout(form);

    m_caseSensitive = new QCheckBox(tr("Case sensitive"), this);
    root->addWidget(m_caseSensitive);

    auto *scopeBox = new QVBoxLayout();
    auto *scopeLabel = new QLabel(tr("Search in:"), this);
    scopeLabel->setStyleSheet(QStringLiteral("color:#A8AEBA; font-size:11px;"));
    scopeBox->addWidget(scopeLabel);
    m_scopeNames = new QCheckBox(tr("Cue names"), this);   m_scopeNames->setChecked(true);
    m_scopeNotes = new QCheckBox(tr("Notes"), this);       m_scopeNotes->setChecked(true);
    m_scopeOsc   = new QCheckBox(tr("OSC address + args"), this); m_scopeOsc->setChecked(true);
    scopeBox->addWidget(m_scopeNames);
    scopeBox->addWidget(m_scopeNotes);
    scopeBox->addWidget(m_scopeOsc);
    root->addLayout(scopeBox);

    m_summary = new QLabel(QStringLiteral(" "), this);
    m_summary->setStyleSheet(QStringLiteral("color:#A8AEBA; font-size:11px;"));
    root->addWidget(m_summary);

    auto *btnRow = new QHBoxLayout();
    auto *findBtn    = new QPushButton(tr("Find"), this);
    auto *replaceBtn = new QPushButton(tr("Replace all"), this);
    btnRow->addWidget(findBtn);
    btnRow->addWidget(replaceBtn);
    btnRow->addStretch(1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    btnRow->addWidget(bb);
    root->addLayout(btnRow);

    connect(findBtn,    &QPushButton::clicked, this, &FindReplaceDialog::doFind);
    connect(replaceBtn, &QPushButton::clicked, this, &FindReplaceDialog::doReplaceAll);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_find->setFocus();
}

FindReplaceDialog::~FindReplaceDialog() = default;

void FindReplaceDialog::doFind()       { apply(false); }
void FindReplaceDialog::doReplaceAll() { apply(true); }

int FindReplaceDialog::apply(bool replace)
{
    if (!m_workspace) return 0;
    const auto needle = m_find->text();
    if (needle.isEmpty()) {
        m_summary->setText(tr("Enter a search string."));
        return 0;
    }
    const auto cs = m_caseSensitive->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    const auto repl = m_replace->text();

    int matches = 0;
    int changed = 0;
    auto *stack = m_workspace->undoStack();
    if (replace) stack->beginMacro(tr("Find and replace"));

    auto consider = [&](cues::Cue *c, const QString &fieldKey) {
        const auto cur = c->field(fieldKey).toString();
        if (cur.isEmpty()) return;
        if (!cur.contains(needle, cs)) return;
        ++matches;
        if (!replace) return;
        QString next = cur;
        next.replace(needle, repl, cs);
        if (next != cur) {
            stack->push(new core::EditCueFieldCommand(c, fieldKey, cur, next));
            ++changed;
        }
    };

    for (const auto &list : m_workspace->cueLists()) {
        for (int row = 0; row < list->cueCount(); ++row) {
            auto *c = list->cueAt(row);
            if (!c) continue;
            if (m_scopeNames->isChecked()) consider(c, QStringLiteral("name"));
            if (m_scopeNotes->isChecked()) consider(c, QStringLiteral("notes"));
            if (m_scopeOsc->isChecked() && qobject_cast<osc::OscCue *>(c)) {
                consider(c, QStringLiteral("address"));
                consider(c, QStringLiteral("rawArgs"));
            }
        }
    }

    if (replace) stack->endMacro();

    if (replace) {
        m_summary->setText(tr("Replaced %1 of %2 match%3.").arg(changed).arg(matches)
            .arg(matches == 1 ? QString() : QStringLiteral("es")));
    } else {
        m_summary->setText(tr("%1 match%2.").arg(matches)
            .arg(matches == 1 ? QString() : QStringLiteral("es")));
    }
    return changed;
}

} // namespace quewi::ui
