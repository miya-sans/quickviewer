#ifndef PROMPTREADER_H
#define PROMPTREADER_H

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QPair>
#include <QSize>
#include <QString>
#include <QStringList>

typedef QList<QPair<QString, QString> > PromptPairs;

/**
 * @brief The PromptInfo class
 * The generation data (prompts and settings) of an image made with Stable Diffusion tools
 */
class PromptInfo
{
public:
    /**
     * @brief Tool which made the image, e.g. 'A1111 webUI', 'ComfyUI'
     */
    QString Tool;
    QString Positive;
    QString Negative;
    /**
     * @brief PositiveParts the texts of each text encoder, e.g. ("Clip G", ...), ("Clip L", ...), when they differ
     */
    PromptPairs PositiveParts;
    PromptPairs NegativeParts;
    /**
     * @brief Settings in the order of the tool, e.g. ("Steps", "25"), ("Sampler", "Euler a")
     */
    PromptPairs Settings;
    /**
     * @brief SettingText the settings as one text, e.g. 'Steps: 25, Sampler: Euler a, ...'
     */
    QString SettingText;

    bool isValid() const { return !Positive.isEmpty() || !Negative.isEmpty(); }
    /**
     * @brief setting the value of the first key found in Settings
     */
    QString setting(const QStringList& keys) const;
    /**
     * @brief toA1111Text the prompts and settings in the format of A1111 webUI ('Read generation parameters')
     */
    QString toA1111Text() const;
};

/**
 * @brief The PromptReader class
 * Reads the generation data like SD Prompt Reader (https://github.com/receyuki/stable-diffusion-prompt-reader):
 * A1111 webUI/Forge, ComfyUI, NovelAI, InvokeAI, Easy Diffusion, Fooocus, StableSwarmUI and Draw Things,
 * from PNG, JPEG and WebP files
 */
class PromptReader
{
public:
    /**
     * @brief read
     * @param bytes the image file
     * @param image the decoded image in its original size, for the data hidden in the alpha channel (NovelAI).
     *  It is ignored when it is null
     */
    static PromptInfo read(const QByteArray& bytes, const QImage& image = QImage());
};

#endif // PROMPTREADER_H
