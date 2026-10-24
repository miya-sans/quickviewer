#ifndef CAPTIONFILE_H
#define CAPTIONFILE_H

#include <QList>
#include <QString>

/**
 * @brief The CaptionCandidate struct
 * One caption of a caption file. A file has several of them when it is split by separator lines
 * (enable_multiline_caption of sd-scripts). The name is the comment of the separator line before it
 */
struct CaptionCandidate
{
    QString name;
    QString text;
};

/**
 * @brief The CaptionFile class
 * The caption file of an image: the ".txt" file with the same name in the same folder, as sd-scripts reads it.
 * Lines starting with "---===---" separate caption candidates, and the text after "#" on them is a comment
 */
class CaptionFile
{
public:
    static const QString Separator;

    CaptionFile();
    /**
     * @brief captionPath "folder/image.png" => "folder/image.txt"
     */
    static QString captionPath(const QString& imagePath);
    /**
     * @brief parse splits a caption into its candidates (at least one).
     * A leading empty candidate is dropped, so that the separator line at the beginning names the first one
     */
    static QList<CaptionCandidate> parse(const QString& text);
    /**
     * @brief serialize joins the candidates with separator lines, with "\n" line ends
     */
    static QString serialize(const QList<CaptionCandidate>& candidates);

    /**
     * @brief load reads the caption file of an image. A missing file is an empty caption
     */
    void load(const QString& imagePath);
    /**
     * @brief save writes the candidates with the line ends, the final line end and the BOM of the file read
     * @return false when the file could not be written
     */
    bool save(const QList<CaptionCandidate>& candidates);

    QString imagePath() const { return m_imagePath; }
    QString path() const { return m_path; }
    bool exists() const { return m_exists; }
    const QList<CaptionCandidate>& candidates() const { return m_candidates; }
    /**
     * @brief savedText the serialized text of the file on the disk (without the changes in the editor)
     */
    QString savedText() const { return m_savedText; }

private:
    QString m_imagePath;
    QString m_path;
    bool m_exists;
    QList<CaptionCandidate> m_candidates;
    QString m_savedText;
    bool m_crlf;
    bool m_finalNewline;
    bool m_bom;
};

#endif // CAPTIONFILE_H
