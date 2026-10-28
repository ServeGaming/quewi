#include "ActivationDialog.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace quewi {

ActivationDialog::ActivationDialog(const license::Status &status, QWidget *parent)
    : QDialog(parent)
    , m_client(new license::Client(this))
    , m_publicKey(license::builtInPublicKey())
{
    setWindowTitle(tr("quewi — trial key"));
    setModal(true);
    setMinimumWidth(520);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(28, 24, 28, 20);
    root->setSpacing(12);

    auto *head = new QHBoxLayout;
    auto *icon = new QLabel(this);
    icon->setPixmap(QPixmap(QStringLiteral(":/icons/quewi.png"))
                        .scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    head->addWidget(icon, 0, Qt::AlignTop);
    auto *title = new QLabel(tr("Welcome to quewi"), this);
    QFont f = title->font();
    f.setPointSizeF(f.pointSizeF() * 1.6);
    f.setBold(true);
    title->setFont(f);
    head->addWidget(title, 1, Qt::AlignVCenter);
    root->addLayout(head);

    m_message = new QLabel(status.message(), this);
    m_message->setWordWrap(true);
    root->addWidget(m_message);

    m_key = new QLineEdit(this);
    m_key->setObjectName(QStringLiteral("licenseKey"));
    m_key->setPlaceholderText(QStringLiteral("QW-XXXX-XXXX-XXXX-XXXX"));
    m_key->setClearButtonEnabled(true);
    // A previous key is offered again when it was this computer's (expired
    // ones: the admin may have extended it).
    if (!license::Store::key().isEmpty()) m_key->setText(license::formatKey(license::Store::key()));
    QFont mono = m_key->font();
    mono.setPointSizeF(mono.pointSizeF() * 1.25);
    mono.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
    m_key->setFont(mono);
    root->addWidget(m_key);

    auto *machine = new QLabel(tr("This computer: %1. A key works on one computer, for 14 days "
                                  "from when it's first entered. Updates don't change that.")
                                   .arg(license::machineName()), this);
    machine->setWordWrap(true);
    machine->setEnabled(false);          // quieter text
    root->addWidget(machine);

    m_error = new QLabel(this);
    m_error->setObjectName(QStringLiteral("licenseError"));
    m_error->setWordWrap(true);
    m_error->setStyleSheet(QStringLiteral("color:#e0796b;"));
    m_error->hide();
    root->addWidget(m_error);

    auto *buttons = new QDialogButtonBox(this);
    m_go = buttons->addButton(tr("Activate"), QDialogButtonBox::AcceptRole);
    m_go->setDefault(true);
    auto *quit = buttons->addButton(tr("Quit"), QDialogButtonBox::RejectRole);
    root->addWidget(buttons);

    connect(m_go, &QPushButton::clicked, this, &ActivationDialog::activate);
    connect(quit, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_key, &QLineEdit::returnPressed, this, &ActivationDialog::activate);
    connect(m_key, &QLineEdit::textChanged, this, [this] { m_error->hide(); });
    connect(m_client, &license::Client::activated, this, &ActivationDialog::onReply);
}

void ActivationDialog::activate()
{
    const QString key = license::normalizeKey(m_key->text());
    if (key.size() < 8) {
        m_error->setText(tr("Type the whole key, e.g. QW-ABCD-EFGH-JKMN-PQRS."));
        m_error->show();
        return;
    }
    m_go->setEnabled(false);
    m_key->setEnabled(false);
    m_go->setText(tr("Activating…"));
    m_client->activate(key);
}

void ActivationDialog::onReply(const license::Reply &r)
{
    m_go->setEnabled(true);
    m_key->setEnabled(true);
    m_go->setText(tr("Activate"));
    if (!r.ok || r.token.isEmpty()) {
        m_error->setText(r.error.isEmpty() ? tr("Activation didn't work. Try again.") : r.error);
        m_error->show();
        return;
    }
    if (r.serverTime > 0) license::Store::noteTime(r.serverTime);
    const qint64 now = std::max(QDateTime::currentSecsSinceEpoch(), license::Store::lastSeen());
    license::Claims c;
    const auto p = license::verifyToken(r.token, m_publicKey, license::machineHash(), now, &c);
    if (p != license::Problem::None) {
        license::Status st;
        st.problem = p;
        st.claims = c;
        st.now = now;
        m_error->setText(p == license::Problem::BadSignature
                             ? tr("The license server's answer didn't check out. Try again, or contact us.")
                             : st.message());
        m_error->show();
        return;
    }
    license::Store::save(r.token, m_key->text());
    accept();
}

bool runActivationGate()
{
    if (!license::enabled()) return true;
    for (;;) {
        const auto st = license::evaluate();
        if (st.ok()) {
            license::markStartedValid();
            return true;
        }
        ActivationDialog dlg(st);
        if (dlg.exec() != QDialog::Accepted) return false;
        // Loop: re-evaluate what was just saved, the same way every start does.
    }
}

} // namespace quewi
