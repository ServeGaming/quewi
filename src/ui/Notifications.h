#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVector>

namespace quewi::ui {

// In-process notification log. Anything that wants to surface a
// non-blocking error or warning during a show — failed cue, OSC
// timeout, audio engine reset, MIDI port disappeared — calls
// Notifications::instance().post(level, source, message). The
// MainWindow status bar shows the most recent entry briefly; the
// Help → Notifications dialog shows the full history.
//
// The design doc ("No modal dialogs during a show") drives this:
// QMessageBox::warning is unacceptable mid-cue, so engines and
// dispatchers route here instead.
class Notifications : public QObject {
    Q_OBJECT
public:
    enum class Level { Info, Warn, Error };
    Q_ENUM(Level)

    struct Entry {
        QDateTime when;
        Level     level = Level::Info;
        QString   source;
        QString   message;
    };

    static Notifications &instance();

    void post(Level level, const QString &source, const QString &message);
    QVector<Entry> recent(int max = 100) const;
    void clear();

signals:
    void posted(const quewi::ui::Notifications::Entry &entry);
    void cleared();

private:
    explicit Notifications(QObject *parent = nullptr);

    QVector<Entry> m_entries;
};

} // namespace quewi::ui
