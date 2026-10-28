#pragma once

#include "License.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

namespace quewi {

// The first thing a licensed build shows when there's no valid trial key on
// this computer: type the key, quewi activates it online and carries on.
// Nothing else of quewi exists yet (no window, no OSC, no show).
class ActivationDialog : public QDialog {
    Q_OBJECT
public:
    explicit ActivationDialog(const license::Status &status, QWidget *parent = nullptr);
    // For tests: point it at another server / key.
    license::Client *client() const { return m_client; }
    void setPublicKey(const QByteArray &k) { m_publicKey = k; }

private:
    void activate();
    void onReply(const license::Reply &r);

    license::Client *m_client;
    QByteArray   m_publicKey;
    QLabel      *m_message = nullptr;
    QLineEdit   *m_key = nullptr;
    QLabel      *m_error = nullptr;
    QPushButton *m_go = nullptr;
};

// Before the main window: true = a valid license is in place (or licensing
// is off in this build); false = the user gave up (quit).
bool runActivationGate();

} // namespace quewi
