#include "ui/AboutDialog.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSysInfo>
#include <QVBoxLayout>

namespace quewi::ui {

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("About quewi"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setSizeGripEnabled(false);
    setModal(true);

    auto *root = new QHBoxLayout(this);
    // Symmetric 20-px margins — the original 20/20/20/16 looked
    // visually misaligned next to other dialogs that use square
    // padding.
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(20);

    // ── Icon column ───────────────────────────────────────────────────
    auto *iconLabel = new QLabel(this);
    QPixmap icon(QStringLiteral(":/icons/quewi.png"));
    if (!icon.isNull()) {
        iconLabel->setPixmap(icon.scaled(96, 96,
            Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    iconLabel->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    iconLabel->setFixedWidth(112);
    root->addWidget(iconLabel);

    // ── Text column ───────────────────────────────────────────────────
    auto *col = new QVBoxLayout();
    col->setSpacing(6);

    auto *name = new QLabel(QStringLiteral("<h2 style='margin:0'>quewi</h2>"), this);
    name->setTextFormat(Qt::RichText);
    col->addWidget(name);

    auto *version = new QLabel(this);
    version->setText(tr("Version %1 · Qt %2 · %3")
        .arg(QApplication::applicationVersion(),
             QString::fromLatin1(qVersion()),
             QSysInfo::prettyProductName()));
    version->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::tokens().ink60.name()));
    col->addWidget(version);

    col->addSpacing(8);

    auto *blurb = new QLabel(this);
    blurb->setWordWrap(true);
    blurb->setText(tr(
        "Theatre cueing software — sound, light, video, and OSC, "
        "driven from a single GO button."));
    col->addWidget(blurb);

    auto *license = new QLabel(this);
    license->setWordWrap(true);
    license->setTextFormat(Qt::RichText);
    license->setOpenExternalLinks(true);
    license->setText(tr(
        "<p style='margin:0'>Licensed under the "
        "<a href='https://www.gnu.org/licenses/agpl-3.0.html'>GNU AGPL v3</a>. "
        "Source available at "
        "<a href='https://github.com/ServeGaming/quewi'>github.com/ServeGaming/quewi</a>.</p>"));
    col->addWidget(license);

    auto *credits = new QLabel(this);
    credits->setTextFormat(Qt::RichText);
    credits->setOpenExternalLinks(true);
    credits->setText(tr(
        "<p style='margin:8px 0 0 0; color: %1;'>"
        "Built with Qt, RtMidi, and a pile of hand-rolled OSC.</p>")
        .arg(Theme::tokens().ink60.name()));
    col->addWidget(credits);

    col->addStretch(1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    col->addWidget(buttons);

    root->addLayout(col, 1);

    setMinimumWidth(QFontMetrics(font()).horizontalAdvance(QChar('M')) * 56);
}

} // namespace quewi::ui
