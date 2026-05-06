#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QUuid>

namespace quewi::core {

// A stage manager's script with cue-line annotations. The user opens a
// .txt (and later .rtf / .docx / .pdf) script, then in Edit mode clicks
// a line to bind it to the selected cue. In Follow mode the viewer
// auto-scrolls so the line for the next cue stays in view.
//
// v0.6.0 stores the script as plain UTF-8 text addressable by 1-based
// line number — the natural unit a stage manager already uses ("Q12 is
// on page 4 line 31"). The path is persisted relative to the show
// file when possible so the show is portable.
class ScriptModel : public QObject {
    Q_OBJECT
public:
    enum class Format { PlainText, Pdf, Unknown };

    struct Annotation {
        QUuid       cueId;
        int         line      = 0;     // 1-based line, plain text only
        int         page      = 0;     // 0-based page, PDF only
        double      yFraction = 0.0;   // 0..1 within page, PDF only
        QString     snippet;           // 60-char preview for sanity checks
    };

    explicit ScriptModel(QObject *parent = nullptr);
    ~ScriptModel() override;

    bool        hasScript() const { return !m_path.isEmpty(); }
    QString     path()      const { return m_path; }
    QString     fileName()  const;
    QString     text()      const { return m_text; }
    int         lineCount() const { return m_lineCount; }
    Format      format()    const { return m_format; }

    // Loads the script from disk. Plain UTF-8 text only in v0.6.0;
    // unknown extensions fall back to "treat as text". Returns false +
    // error string on read failure or empty file.
    bool loadFromFile(const QString &path, QString *errorOut = nullptr);
    void clear();

    const QList<Annotation>& annotations() const { return m_annotations; }
    int         annotationIndexForLine(int line) const;
    int         annotationIndexForCue(const QUuid &cueId) const;

    // Bind / rebind / unbind. setAnnotation overwrites any existing
    // entry for cueId; removeAnnotation drops it; clearLine drops every
    // annotation on that line. Emits annotationsChanged afterwards.
    void setAnnotation(const QUuid &cueId, int line);
    void setPdfAnnotation(const QUuid &cueId, int page, double yFraction);
    void removeAnnotation(const QUuid &cueId);
    void clearLine(int line);

    // Persistence (called by ShowFile).
    QJsonObject toJson() const;
    void        fromJson(const QJsonObject &o);

signals:
    void scriptChanged();           // path / text reloaded
    void annotationsChanged();

private:
    QString             m_path;
    QString             m_text;
    int                 m_lineCount = 0;
    Format              m_format    = Format::Unknown;
    QList<Annotation>   m_annotations;

    QString lineSnippet(int line) const;
};

} // namespace quewi::core
