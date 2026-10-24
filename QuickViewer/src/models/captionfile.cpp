#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QStringList>

#include "captionfile.h"

// the default multiline_caption_separator of sd-scripts
const QString CaptionFile::Separator = QStringLiteral("---===---");

/**
 * @brief trimBlankLines removes the blank lines at the beginning and the end, and the spaces at the end of each line
 * (sd-scripts reads the candidates in this way, and keeps the spaces at the beginning of the lines)
 */
static QString trimBlankLines(const QStringList& lines)
{
    QStringList trimmed;
    for(const QString& line : lines) {
        qsizetype end = line.size();
        while(end > 0 && line.at(end - 1).isSpace())
            end--;
        trimmed << line.left(end);
    }
    while(!trimmed.isEmpty() && trimmed.first().isEmpty())
        trimmed.removeFirst();
    while(!trimmed.isEmpty() && trimmed.last().isEmpty())
        trimmed.removeLast();
    return trimmed.join('\n');
}

CaptionFile::CaptionFile()
    : m_exists(false)
    , m_crlf(false)
    , m_finalNewline(false)
    , m_bom(false)
{
    m_candidates << CaptionCandidate();
}

QString CaptionFile::captionPath(const QString &imagePath)
{
    QFileInfo info(imagePath);
    return info.dir().filePath(info.completeBaseName() + ".txt");
}

QList<CaptionCandidate> CaptionFile::parse(const QString &text)
{
    QList<CaptionCandidate> candidates;
    CaptionCandidate current;
    QStringList lines;
    for(const QString& line : text.split('\n')) {
        const QString stripped = line.trimmed();
        if(!stripped.startsWith(Separator)) {
            lines << line;
            continue;
        }
        current.text = trimBlankLines(lines);
        candidates << current;
        lines.clear();
        // "---===--- # name"
        const QString rest = stripped.mid(Separator.size());
        const qsizetype hash = rest.indexOf('#');
        current.name = (hash >= 0 ? rest.mid(hash + 1) : rest).trimmed();
    }
    current.text = trimBlankLines(lines);
    candidates << current;
    if(candidates.size() > 1 && candidates.first().name.isEmpty() && candidates.first().text.isEmpty())
        candidates.removeFirst();
    return candidates;
}

QString CaptionFile::serialize(const QList<CaptionCandidate> &candidates)
{
    QStringList parts;
    for(int i = 0; i < candidates.size(); i++) {
        const CaptionCandidate& candidate = candidates[i];
        const QString name = candidate.name.simplified();
        // the first candidate needs a separator line only for its name
        if(i > 0 || !name.isEmpty())
            parts << (name.isEmpty() ? Separator : QString("%1 # %2").arg(Separator, name));
        parts << candidate.text;
    }
    return parts.join('\n');
}

void CaptionFile::load(const QString &imagePath)
{
    m_imagePath = imagePath;
    m_path = captionPath(imagePath);
    m_exists = false;
    m_crlf = false;
    m_finalNewline = false;
    m_bom = false;
    QString text;
    QFile file(m_path);
    if(file.open(QIODevice::ReadOnly)) {
        m_exists = true;
        QByteArray bytes = file.readAll();
        if(bytes.startsWith("\xEF\xBB\xBF")) {
            m_bom = true;
            bytes.remove(0, 3);
        }
        text = QString::fromUtf8(bytes);
        m_crlf = text.contains("\r\n");
        text.replace("\r\n", "\n");
        m_finalNewline = text.endsWith('\n');
    }
    m_candidates = parse(text);
    m_savedText = serialize(m_candidates);
}

bool CaptionFile::save(const QList<CaptionCandidate> &candidates)
{
    const QString text = serialize(candidates);
    QString content = text;
    if(m_finalNewline && !content.isEmpty())
        content += '\n';
    if(m_crlf)
        content.replace('\n', "\r\n");
    QByteArray bytes = content.toUtf8();
    if(m_bom)
        bytes.prepend("\xEF\xBB\xBF");

    QSaveFile file(m_path);
    if(!file.open(QIODevice::WriteOnly))
        return false;
    file.write(bytes);
    if(!file.commit())
        return false;
    m_exists = true;
    m_candidates = candidates;
    m_savedText = text;
    return true;
}
