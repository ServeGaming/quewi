#pragma once

#include <QByteArray>
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;

namespace quewi::ui {

// The soundboard's "Send to mic" settings (MicRouting). Edits apply live.
class MicRoutingDialog : public QDialog {
    Q_OBJECT
public:
    // boardOutputId: the soundboard's own output, to warn if it's picked as
    // the mic device too.
    explicit MicRoutingDialog(const QByteArray &boardOutputId, QWidget *parent = nullptr);

    // Short label for the soundboard toolbar button ("Mic: Off", "Mic: CABLE…").
    static QString buttonText();

private:
    void refresh();
    QSlider *dbSlider(double value);

    QByteArray m_boardOutput;
    QComboBox *m_device      = nullptr;
    QLabel    *m_noCable     = nullptr;
    QLabel    *m_warning     = nullptr;
    QCheckBox *m_monitor     = nullptr;
    QSlider   *m_sfxGain     = nullptr;
    QLabel    *m_sfxGainVal  = nullptr;
    QCheckBox *m_passthrough = nullptr;
    QComboBox *m_input       = nullptr;
    QSlider   *m_inputGain   = nullptr;
    QLabel    *m_inputGainVal = nullptr;
    QLabel    *m_howTo       = nullptr;
};

} // namespace quewi::ui
