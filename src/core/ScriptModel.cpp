#include "core/ScriptModel.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>

namespace quewi::core {

ScriptModel::ScriptModel(QObject *parent) : QObject(parent) {}
ScriptModel::~ScriptModel() = default;

QString ScriptModel::fileName() const
{
    return m_path.isEmpty() ? QString() : QFileInfo(m_path).fileName();
}

bool ScriptModel::loadFromFile(const QString &path, QString *errorOut)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    const Format fmt = (ext == QLatin1String("pdf")) ? Format::Pdf
                                                     : Format::PlainText;

    if (fmt == Format::Pdf) {
        // PDFs are rendered by the viewer (QPdfView) — the model only
        // needs the path. Verify the file exists; the viewer will
        // surface decode errors itself.
        QFile f(path);
        if (!f.exists()) {
            if (errorOut) *errorOut = tr("PDF not found: %1").arg(path);
            return false;
        }
        m_path = path;
        m_text.clear();
        m_lineCount = 0;
        m_format = Format::Pdf;
        emit scriptChanged();
        emit annotationsChanged();
        return true;
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorOut) *errorOut = f.errorString();
        return false;
    }
    const auto bytes = f.readAll();
    if (bytes.isEmpty()) {
        if (errorOut) *errorOut = tr("Script file is empty.");
        return false;
    }
    m_path = path;
    m_text = QString::fromUtf8(bytes);
    // Normalise CRLF / CR to LF so line numbering is consistent across
    // platforms — a script saved on Windows shouldn't appear shifted by
    // one line per paragraph break.
    m_text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    m_text.replace(QChar('\r'), QChar('\n'));
    m_lineCount = m_text.count(QChar('\n')) + 1;
    m_format = Format::PlainText;

    // Re-snapshot existing snippets — annotations may have come from
    // a previous load of a slightly different file.
    for (auto &a : m_annotations) {
        if (a.line > 0) a.snippet = lineSnippet(a.line);
    }

    emit scriptChanged();
    emit annotationsChanged();
    return true;
}

void ScriptModel::clear()
{
    m_path.clear();
    m_text.clear();
    m_lineCount = 0;
    m_format = Format::Unknown;
    m_annotations.clear();
    emit scriptChanged();
    emit annotationsChanged();
}

int ScriptModel::annotationIndexForLine(int line) const
{
    for (int i = 0; i < m_annotations.size(); ++i)
        if (m_annotations[i].line == line) return i;
    return -1;
}

int ScriptModel::annotationIndexForCue(const QUuid &cueId) const
{
    for (int i = 0; i < m_annotations.size(); ++i)
        if (m_annotations[i].cueId == cueId) return i;
    return -1;
}

void ScriptModel::setAnnotation(const QUuid &cueId, int line)
{
    if (cueId.isNull() || line <= 0) return;
    const int existing = annotationIndexForCue(cueId);
    Annotation a;
    a.cueId   = cueId;
    a.line    = line;
    a.snippet = lineSnippet(line);
    if (existing >= 0) m_annotations[existing] = a;
    else               m_annotations.append(a);
    emit annotationsChanged();
}

void ScriptModel::setPdfAnnotation(const QUuid &cueId, int page, double yFraction)
{
    if (cueId.isNull() || page < 0) return;
    const int existing = annotationIndexForCue(cueId);
    Annotation a;
    a.cueId     = cueId;
    a.page      = page;
    a.yFraction = qBound(0.0, yFraction, 1.0);
    a.snippet   = QStringLiteral("p%1").arg(page + 1);
    if (existing >= 0) m_annotations[existing] = a;
    else               m_annotations.append(a);
    emit annotationsChanged();
}

void ScriptModel::removeAnnotation(const QUuid &cueId)
{
    const int existing = annotationIndexForCue(cueId);
    if (existing < 0) return;
    m_annotations.removeAt(existing);
    emit annotationsChanged();
}

void ScriptModel::clearLine(int line)
{
    const auto before = m_annotations.size();
    m_annotations.erase(std::remove_if(m_annotations.begin(), m_annotations.end(),
        [line](const Annotation &a) { return a.line == line; }),
        m_annotations.end());
    if (m_annotations.size() != before) emit annotationsChanged();
}

QString ScriptModel::lineSnippet(int line) const
{
    if (line <= 0 || m_text.isEmpty()) return {};
    int idx = 0;
    int currentLine = 1;
    while (currentLine < line) {
        const int nl = m_text.indexOf(QChar('\n'), idx);
        if (nl < 0) return {};
        idx = nl + 1;
        ++currentLine;
    }
    int end = m_text.indexOf(QChar('\n'), idx);
    if (end < 0) end = m_text.size();
    QString s = m_text.mid(idx, end - idx);
    if (s.size() > 60) s = s.left(57) + QStringLiteral("…");
    return s;
}

QJsonObject ScriptModel::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("path"), m_path);
    QJsonArray ann;
    for (const auto &a : m_annotations) {
        QJsonObject ao;
        ao.insert(QStringLiteral("cue"),     a.cueId.toString(QUuid::WithoutBraces));
        if (a.line > 0) {
            ao.insert(QStringLiteral("line"), a.line);
        } else {
            ao.insert(QStringLiteral("page"), a.page);
            ao.insert(QStringLiteral("y"),    a.yFraction);
        }
        ao.insert(QStringLiteral("snippet"), a.snippet);
        ann.append(ao);
    }
    o.insert(QStringLiteral("annotations"), ann);
    return o;
}

void ScriptModel::fromJson(const QJsonObject &o)
{
    m_annotations.clear();
    const QString path = o.value(QStringLiteral("path")).toString();
    if (!path.isEmpty()) {
        QString err;
        if (!loadFromFile(path, &err)) {
            // Path stored but file not loadable (moved / missing). Keep
            // the path in the model so the UI can prompt the user to
            // relocate. m_text stays empty so the viewer will show its
            // "missing" placeholder.
            m_path = path;
            m_text.clear();
            m_lineCount = 0;
            emit scriptChanged();
        }
    }
    const auto arr = o.value(QStringLiteral("annotations")).toArray();
    for (const auto &v : arr) {
        const auto ao = v.toObject();
        Annotation a;
        a.cueId     = QUuid(ao.value(QStringLiteral("cue")).toString());
        a.line      = ao.value(QStringLiteral("line")).toInt(0);
        a.page      = ao.value(QStringLiteral("page")).toInt(0);
        a.yFraction = ao.value(QStringLiteral("y")).toDouble(0.0);
        a.snippet   = ao.value(QStringLiteral("snippet")).toString();
        if (!a.cueId.isNull() && (a.line > 0 || ao.contains(QStringLiteral("page"))))
            m_annotations.append(a);
    }
    emit annotationsChanged();
}

} // namespace quewi::core
