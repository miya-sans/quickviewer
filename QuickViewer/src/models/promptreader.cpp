#include <QtCore>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>

#include "promptreader.h"

namespace {

// ---------------------------------------------------------------------------
// Inflate (RFC 1951), in the style of puff.c of zlib.
// Compressed PNG text chunks and the gzip data of NovelAI need it, and qUncompress() can't read gzip.

const qsizetype MAX_INFLATED_SIZE = 64 * 1024 * 1024;

struct Huffman
{
    short count[16];
    short symbol[288];
};

class Inflater
{
public:
    Inflater(const uchar* data, qsizetype size) : m_in(data), m_size(size) {}
    bool run();
    QByteArray output() const { return m_out; }

private:
    int bits(int need);
    int decode(const Huffman& h);
    bool stored();
    bool codes(const Huffman& lencode, const Huffman& distcode);
    bool fixed();
    bool dynamic();
    static int construct(Huffman& h, const short* length, int n);

    const uchar* m_in;
    qsizetype m_size;
    qsizetype m_pos = 0;
    quint32 m_bitBuf = 0;
    int m_bitCount = 0;
    bool m_error = false;
    QByteArray m_out;
};

int Inflater::bits(int need)
{
    quint32 val = m_bitBuf;
    while(m_bitCount < need) {
        if(m_pos >= m_size) {
            m_error = true;
            return 0;
        }
        val |= quint32(m_in[m_pos++]) << m_bitCount;
        m_bitCount += 8;
    }
    m_bitBuf = val >> need;
    m_bitCount -= need;
    return int(val & ((1u << need) - 1));
}

int Inflater::decode(const Huffman &h)
{
    int code = 0, first = 0, index = 0;
    for(int len = 1; len < 16; len++) {
        code |= bits(1);
        if(m_error)
            return -1;
        int count = h.count[len];
        if(code - count < first)
            return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

int Inflater::construct(Huffman &h, const short *length, int n)
{
    for(int len = 0; len < 16; len++)
        h.count[len] = 0;
    for(int symbol = 0; symbol < n; symbol++)
        h.count[length[symbol]]++;
    if(h.count[0] == n)
        return 0;
    int left = 1;
    for(int len = 1; len < 16; len++) {
        left <<= 1;
        left -= h.count[len];
        if(left < 0)
            return left;
    }
    short offs[16];
    offs[1] = 0;
    for(int len = 1; len < 15; len++)
        offs[len + 1] = offs[len] + h.count[len];
    for(int symbol = 0; symbol < n; symbol++)
        if(length[symbol] != 0)
            h.symbol[offs[length[symbol]]++] = short(symbol);
    return left;
}

bool Inflater::stored()
{
    m_bitBuf = 0;
    m_bitCount = 0;
    if(m_pos + 4 > m_size)
        return false;
    qsizetype len = m_in[m_pos] | (m_in[m_pos + 1] << 8);
    qsizetype nlen = m_in[m_pos + 2] | (m_in[m_pos + 3] << 8);
    m_pos += 4;
    if(len != (~nlen & 0xffff) || m_pos + len > m_size || m_out.size() + len > MAX_INFLATED_SIZE)
        return false;
    m_out.append(reinterpret_cast<const char*>(m_in + m_pos), len);
    m_pos += len;
    return true;
}

bool Inflater::codes(const Huffman &lencode, const Huffman &distcode)
{
    static const short LENGTH_BASE[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short LENGTH_EXTRA[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short DIST_BASE[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const short DIST_EXTRA[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for(;;) {
        int symbol = decode(lencode);
        if(symbol < 0)
            return false;
        if(symbol < 256) {
            m_out.append(char(symbol));
        } else if(symbol == 256) {
            return true;
        } else {
            symbol -= 257;
            if(symbol >= 29)
                return false;
            int len = LENGTH_BASE[symbol] + bits(LENGTH_EXTRA[symbol]);
            int dsym = decode(distcode);
            if(dsym < 0 || dsym >= 30)
                return false;
            qsizetype dist = DIST_BASE[dsym] + bits(DIST_EXTRA[dsym]);
            if(m_error || dist > m_out.size())
                return false;
            qsizetype from = m_out.size() - dist;
            for(int i = 0; i < len; i++)
                m_out.append(m_out.at(from + i));
        }
        if(m_out.size() > MAX_INFLATED_SIZE)
            return false;
    }
}

bool Inflater::fixed()
{
    static const QPair<Huffman, Huffman> tables = [] {
        Huffman lencode, distcode;
        short lengths[288];
        int symbol = 0;
        for(; symbol < 144; symbol++)
            lengths[symbol] = 8;
        for(; symbol < 256; symbol++)
            lengths[symbol] = 9;
        for(; symbol < 280; symbol++)
            lengths[symbol] = 7;
        for(; symbol < 288; symbol++)
            lengths[symbol] = 8;
        construct(lencode, lengths, 288);
        for(symbol = 0; symbol < 30; symbol++)
            lengths[symbol] = 5;
        construct(distcode, lengths, 30);
        return qMakePair(lencode, distcode);
    }();
    return codes(tables.first, tables.second);
}

bool Inflater::dynamic()
{
    static const short ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short lengths[320];
    Huffman lencode, distcode;
    int nlen = bits(5) + 257;
    int ndist = bits(5) + 1;
    int ncode = bits(4) + 4;
    if(m_error || nlen > 286 || ndist > 30)
        return false;
    int index = 0;
    for(; index < ncode; index++)
        lengths[ORDER[index]] = short(bits(3));
    for(; index < 19; index++)
        lengths[ORDER[index]] = 0;
    if(m_error || construct(lencode, lengths, 19) != 0)
        return false;
    index = 0;
    while(index < nlen + ndist) {
        int symbol = decode(lencode);
        if(symbol < 0)
            return false;
        if(symbol < 16) {
            lengths[index++] = short(symbol);
            continue;
        }
        short len = 0;
        if(symbol == 16) {
            if(index == 0)
                return false;
            len = lengths[index - 1];
            symbol = 3 + bits(2);
        } else if(symbol == 17) {
            symbol = 3 + bits(3);
        } else {
            symbol = 11 + bits(7);
        }
        if(m_error || index + symbol > nlen + ndist)
            return false;
        while(symbol--)
            lengths[index++] = len;
    }
    if(lengths[256] == 0)
        return false;
    int err = construct(lencode, lengths, nlen);
    if(err && (err < 0 || nlen != lencode.count[0] + lencode.count[1]))
        return false;
    err = construct(distcode, lengths + nlen, ndist);
    if(err && (err < 0 || ndist != distcode.count[0] + distcode.count[1]))
        return false;
    return codes(lencode, distcode);
}

bool Inflater::run()
{
    int last;
    do {
        last = bits(1);
        int type = bits(2);
        if(m_error)
            return false;
        bool ok = type == 0 ? stored() : type == 1 ? fixed() : type == 2 ? dynamic() : false;
        if(!ok)
            return false;
    } while(!last);
    return true;
}

QByteArray inflateZlib(const QByteArray& data)
{
    const uchar* d = reinterpret_cast<const uchar*>(data.constData());
    // deflate without a preset dictionary
    if(data.size() < 2 || (d[0] & 0x0f) != 8 || ((d[0] << 8) | d[1]) % 31 != 0 || (d[1] & 0x20))
        return QByteArray();
    Inflater inflater(d + 2, data.size() - 2);
    return inflater.run() ? inflater.output() : QByteArray();
}

QByteArray inflateGzip(const QByteArray& data)
{
    const uchar* d = reinterpret_cast<const uchar*>(data.constData());
    const qsizetype size = data.size();
    if(size < 18 || d[0] != 0x1f || d[1] != 0x8b || d[2] != 8)
        return QByteArray();
    const int flags = d[3];
    qsizetype pos = 10;
    if(flags & 0x04) {
        if(pos + 2 > size)
            return QByteArray();
        pos += 2 + (d[pos] | (d[pos + 1] << 8));
    }
    for(int nameFlag : {0x08, 0x10}) {
        if(flags & nameFlag) {
            while(pos < size && d[pos])
                pos++;
            pos++;
        }
    }
    if(flags & 0x02)
        pos += 2;
    if(pos >= size)
        return QByteArray();
    Inflater inflater(d + pos, size - pos);
    return inflater.run() ? inflater.output() : QByteArray();
}

// ---------------------------------------------------------------------------
// Metadata of the image file

struct Metadata
{
    /**
     * @brief texts PNG text chunks, and "comment" of JPEG
     */
    QHash<QString, QString> texts;
    /**
     * @brief exif TIFF data of EXIF
     */
    QByteArray exif;
    bool png = false;
    int width = 0;
    int height = 0;
};

QString decodeText(const QByteArray& bytes)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder(bytes);
    if(decoder.hasError())
        return QString::fromLatin1(bytes);
    return text;
}

QByteArray copyBytes(const uchar* d, qsizetype size)
{
    return QByteArray(reinterpret_cast<const char*>(d), size);
}

void readPng(const QByteArray& bytes, Metadata& meta)
{
    const uchar* d = reinterpret_cast<const uchar*>(bytes.constData());
    const qsizetype size = bytes.size();
    meta.png = true;
    qsizetype pos = 8;
    // text chunks can follow the image data, so every chunk is checked
    while(pos + 12 <= size) {
        const quint64 length = qFromBigEndian<quint32>(d + pos);
        const QByteArray type = copyBytes(d + pos + 4, 4);
        const qsizetype start = pos + 8;
        if(length > quint64(size - start - 4))
            break;
        if(type == "IHDR" && length >= 13) {
            meta.width = qFromBigEndian<quint32>(d + start);
            meta.height = qFromBigEndian<quint32>(d + start + 4);
        } else if(type == "tEXt" || type == "zTXt" || type == "iTXt") {
            const QByteArray data = copyBytes(d + start, length);
            const qsizetype sep = data.indexOf('\0');
            if(sep > 0) {
                const QString key = QString::fromLatin1(data.left(sep));
                if(type == "tEXt") {
                    meta.texts.insert(key, decodeText(data.mid(sep + 1)));
                } else if(type == "zTXt") {
                    meta.texts.insert(key, decodeText(inflateZlib(data.mid(sep + 2))));
                } else if(sep + 3 <= data.size()) {
                    // keyword, compression flag, compression method, language, translated keyword, text
                    const bool compressed = data.at(sep + 1) != 0;
                    const qsizetype language = data.indexOf('\0', sep + 3);
                    const qsizetype translated = language < 0 ? -1 : data.indexOf('\0', language + 1);
                    if(translated >= 0) {
                        QByteArray text = data.mid(translated + 1);
                        meta.texts.insert(key, QString::fromUtf8(compressed ? inflateZlib(text) : text));
                    }
                }
            }
        } else if(type == "eXIf") {
            meta.exif = copyBytes(d + start, length);
        } else if(type == "IEND") {
            break;
        }
        pos = start + qsizetype(length) + 4;
    }
}

void readJpeg(const QByteArray& bytes, Metadata& meta)
{
    const uchar* d = reinterpret_cast<const uchar*>(bytes.constData());
    const qsizetype size = bytes.size();
    qsizetype pos = 2;
    while(pos + 4 <= size && d[pos] == 0xff) {
        const uchar marker = d[pos + 1];
        if(marker == 0xff) {
            pos++;
            continue;
        }
        // the image data begins
        if(marker == 0xda || marker == 0xd9)
            break;
        const qsizetype length = (d[pos + 2] << 8) | d[pos + 3];
        if(length < 2 || pos + 2 + length > size)
            break;
        const uchar* segment = d + pos + 4;
        const qsizetype segmentSize = length - 2;
        if(marker == 0xe1 && segmentSize > 6 && memcmp(segment, "Exif\0\0", 6) == 0 && meta.exif.isEmpty()) {
            meta.exif = copyBytes(segment + 6, segmentSize - 6);
        } else if(marker == 0xfe) {
            meta.texts.insert("comment", decodeText(copyBytes(segment, segmentSize)));
        } else if(marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc && segmentSize >= 5) {
            meta.height = (segment[1] << 8) | segment[2];
            meta.width = (segment[3] << 8) | segment[4];
        }
        pos += 2 + length;
    }
}

void readWebp(const QByteArray& bytes, Metadata& meta)
{
    const uchar* d = reinterpret_cast<const uchar*>(bytes.constData());
    const qsizetype size = bytes.size();
    qsizetype pos = 12;
    while(pos + 8 <= size) {
        const QByteArray fourcc = copyBytes(d + pos, 4);
        const quint64 length = qFromLittleEndian<quint32>(d + pos + 4);
        const qsizetype start = pos + 8;
        if(length > quint64(size - start))
            break;
        if(fourcc == "EXIF") {
            meta.exif = copyBytes(d + start, length);
            if(meta.exif.startsWith(QByteArray("Exif\0\0", 6)))
                meta.exif = meta.exif.mid(6);
        } else if(fourcc == "VP8X" && length >= 10) {
            meta.width = 1 + (d[start + 4] | (d[start + 5] << 8) | (d[start + 6] << 16));
            meta.height = 1 + (d[start + 7] | (d[start + 8] << 8) | (d[start + 9] << 16));
        }
        pos = start + qsizetype(length) + (length & 1);
    }
}

struct ExifData
{
    QByteArray userComment;
    QByteArray model;
};

ExifData parseExif(const QByteArray& tiff)
{
    ExifData exif;
    const uchar* d = reinterpret_cast<const uchar*>(tiff.constData());
    const qsizetype size = tiff.size();
    if(size < 8)
        return exif;
    bool littleEndian;
    if(d[0] == 'I' && d[1] == 'I')
        littleEndian = true;
    else if(d[0] == 'M' && d[1] == 'M')
        littleEndian = false;
    else
        return exif;
    auto u16 = [&](quint64 offset) -> quint32 {
        if(offset + 2 > quint64(size))
            return 0;
        return littleEndian ? qFromLittleEndian<quint16>(d + offset) : qFromBigEndian<quint16>(d + offset);
    };
    auto u32 = [&](quint64 offset) -> quint32 {
        if(offset + 4 > quint64(size))
            return 0;
        return littleEndian ? qFromLittleEndian<quint32>(d + offset) : qFromBigEndian<quint32>(d + offset);
    };
    auto value = [&](quint64 entry) -> QByteArray {
        static const int UNIT[13] = {0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8};
        const quint32 type = u16(entry + 2);
        if(type == 0 || type > 12)
            return QByteArray();
        const quint64 total = quint64(u32(entry + 4)) * UNIT[type];
        const quint64 offset = total <= 4 ? entry + 8 : u32(entry + 8);
        if(offset + total > quint64(size))
            return QByteArray();
        return copyBytes(d + offset, qsizetype(total));
    };
    auto readIfd = [&](quint64 ifd, const std::function<void(quint32, quint64)>& onEntry) {
        const quint32 count = u16(ifd);
        for(quint32 i = 0; i < count && i < 1024; i++) {
            const quint64 entry = ifd + 2 + i * 12;
            if(entry + 12 > quint64(size))
                break;
            onEntry(u16(entry), entry);
        }
    };
    quint64 exifIfd = 0;
    readIfd(u32(4), [&](quint32 tag, quint64 entry) {
        if(tag == 0x8769)
            exifIfd = u32(entry + 8);
        else if(tag == 0x0110)
            exif.model = value(entry);
    });
    if(exifIfd) {
        readIfd(exifIfd, [&](quint32 tag, quint64 entry) {
            if(tag == 0x9286)
                exif.userComment = value(entry);
        });
    }
    return exif;
}

QString trimNulls(QString text)
{
    while(text.endsWith(QChar(0)))
        text.chop(1);
    return text.trimmed();
}

QString decodeUserComment(const QByteArray& comment)
{
    if(comment.isEmpty())
        return QString();
    const QByteArray head = comment.left(8);
    QByteArray body = comment.mid(8);
    if(head == QByteArray("UNICODE\0", 8)) {
        // UTF-16. A1111 (piexif) writes big endian, but some tools follow the byte order of TIFF
        bool bigEndian = true;
        if(body.startsWith("\xff\xfe")) {
            bigEndian = false;
            body = body.mid(2);
        } else if(body.startsWith("\xfe\xff")) {
            body = body.mid(2);
        } else {
            // the higher byte of ASCII characters is zero
            int evenZeros = 0, oddZeros = 0;
            for(qsizetype i = 0; i < qMin<qsizetype>(body.size(), 512); i++) {
                if(body.at(i) == 0)
                    (i & 1 ? oddZeros : evenZeros)++;
            }
            bigEndian = evenZeros >= oddZeros;
        }
        QStringDecoder decoder(bigEndian ? QStringDecoder::Utf16BE : QStringDecoder::Utf16LE);
        return trimNulls(decoder(body));
    }
    if(head == QByteArray("ASCII\0\0\0", 8) || head == QByteArray(8, '\0'))
        return trimNulls(decodeText(body));
    return trimNulls(decodeText(comment));
}

QString unescapeXml(QString text)
{
    static thread_local const QRegularExpression numeric("&#(x?)([0-9a-fA-F]+);");
    QRegularExpressionMatch match;
    qsizetype from = 0;
    while((match = numeric.match(text, from)).hasMatch()) {
        bool ok = false;
        uint code = match.captured(2).toUInt(&ok, match.captured(1).isEmpty() ? 10 : 16);
        QString replacement = ok ? QString::fromUcs4(reinterpret_cast<const char32_t*>(&code), 1) : QString();
        text.replace(match.capturedStart(), match.capturedLength(), replacement);
        from = match.capturedStart() + replacement.size();
    }
    return text.replace("&quot;", "\"").replace("&apos;", "'").replace("&lt;", "<").replace("&gt;", ">").replace("&amp;", "&");
}

// ---------------------------------------------------------------------------
// Settings

QJsonObject parseJsonObject(const QString& text)
{
    QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    return doc.isObject() ? doc.object() : QJsonObject();
}

QJsonObject toJsonObject(const QHash<QString, QString>& texts)
{
    QJsonObject object;
    for(auto it = texts.cbegin(); it != texts.cend(); ++it)
        object.insert(it.key(), it.value());
    return object;
}

QString jsonText(const QJsonValue& value)
{
    switch(value.type()) {
    case QJsonValue::String:
        return value.toString();
    case QJsonValue::Double: {
        // integers like seeds are kept exactly
        const qint64 none = (std::numeric_limits<qint64>::min)();
        const qint64 integer = value.toInteger(none);
        if(integer != none)
            return QString::number(integer);
        return QString::number(value.toDouble(), 'g', 10);
    }
    case QJsonValue::Bool:
        return value.toBool() ? "true" : "false";
    case QJsonValue::Array:
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    case QJsonValue::Object:
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    default:
        return QString();
    }
}

/**
 * @brief withSize adds "Size" like '1024x1536' made from the width and the height
 */
QJsonObject withSize(QJsonObject object, const QString& width, const QString& height)
{
    QString w = jsonText(object.value(width)), h = jsonText(object.value(height));
    if(!w.isEmpty() && !h.isEmpty() && !object.contains("Size"))
        object.insert("Size", QString("%1x%2").arg(w, h));
    return object;
}

struct SettingKey
{
    QString label;
    QStringList keys;
};

/**
 * @brief settingsFromJson the main settings with common labels first, then the other values with their own keys
 */
PromptPairs settingsFromJson(const QJsonObject& object, const QList<SettingKey>& mainKeys, const QStringList& skipKeys)
{
    PromptPairs pairs;
    QSet<QString> used(skipKeys.begin(), skipKeys.end());
    for(const SettingKey& key : mainKeys) {
        for(const QString& name : key.keys) {
            if(!object.contains(name) || used.contains(name))
                continue;
            used.insert(name);
            QString text = jsonText(object.value(name));
            if(!text.isEmpty()) {
                pairs << qMakePair(key.label, text);
                break;
            }
        }
    }
    for(auto it = object.begin(); it != object.end(); ++it) {
        if(used.contains(it.key()))
            continue;
        QString text = jsonText(it.value());
        if(!text.isEmpty())
            pairs << qMakePair(it.key(), text);
    }
    return pairs;
}

/**
 * @brief joinSettings the settings as one line like A1111, values with commas are quoted
 */
QString joinSettings(const PromptPairs& pairs)
{
    QStringList items;
    for(const auto& pair : pairs) {
        QString value = pair.second;
        value.replace('\n', ' ');
        if(value.contains(',') || value.contains(':'))
            value = "\"" + QString(value).replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
        items << pair.first + ": " + value;
    }
    return items.join(", ");
}

// ---------------------------------------------------------------------------
// A1111 webUI and Forge

// The images are read in worker threads, so each thread has its own regular expressions
const QRegularExpression& a1111ParamExp()
{
    // the same as 're_param_code' of A1111 webUI
    static thread_local const QRegularExpression exp(R"(\s*(\w[\w \-/]+):\s*("(?:\\.|[^\\"])+"|[^,]*)(?:,|$))");
    return exp;
}

PromptPairs parseA1111Settings(const QString& text)
{
    PromptPairs pairs;
    QSet<QString> keys;
    for(const QString& line : text.split('\n')) {
        QRegularExpressionMatchIterator it = a1111ParamExp().globalMatch(line);
        while(it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            QString key = match.captured(1).trimmed();
            QString value = match.captured(2).trimmed();
            if(value.size() >= 2 && value.startsWith('"') && value.endsWith('"')) {
                QJsonDocument doc = QJsonDocument::fromJson("[" + value.toUtf8() + "]");
                value = doc.isArray() ? doc.array().at(0).toString() : value.mid(1, value.size() - 2);
            }
            if(key.isEmpty() || keys.contains(key))
                continue;
            keys.insert(key);
            pairs << qMakePair(key, value);
        }
    }
    return pairs;
}

PromptInfo parseA1111(QString raw)
{
    PromptInfo info;
    raw.replace("\r\n", "\n");
    raw = raw.trimmed();
    QString body = raw, setting;
    const qsizetype steps = raw.indexOf("\nSteps:");
    if(raw.startsWith("Steps:")) {
        body.clear();
        setting = raw;
    } else if(steps >= 0) {
        body = raw.left(steps);
        setting = raw.mid(steps + 1);
    } else {
        // A1111 webUI takes the last line as the settings when it has 3 or more parameters
        const qsizetype lastLine = raw.lastIndexOf('\n');
        if(lastLine >= 0 && parseA1111Settings(raw.mid(lastLine + 1)).size() >= 3) {
            body = raw.left(lastLine);
            setting = raw.mid(lastLine + 1);
        }
    }
    if(body.startsWith("Negative prompt:")) {
        info.Negative = body.mid(16).trimmed();
    } else {
        const qsizetype negative = body.indexOf("\nNegative prompt:");
        if(negative >= 0) {
            info.Positive = body.left(negative).trimmed();
            info.Negative = body.mid(negative + 17).trimmed();
        } else {
            info.Positive = body.trimmed();
        }
    }
    info.SettingText = setting.trimmed();
    info.Settings = parseA1111Settings(info.SettingText);

    const QString version = info.setting({"Version"});
    if(version.startsWith("neo", Qt::CaseInsensitive))
        info.Tool = "Forge Neo";
    else if(version.size() > 1 && version.at(0) == 'f' && version.at(1).isDigit())
        info.Tool = "Forge";
    else
        info.Tool = "A1111 webUI";
    return info;
}

// ---------------------------------------------------------------------------
// JSON formats

PromptInfo parseEasyDiffusion(QJsonObject object)
{
    PromptInfo info;
    info.Tool = "Easy Diffusion";
    const bool lower = object.contains("prompt");
    const QString prompt = lower ? "prompt" : "Prompt";
    const QString negative = lower ? "negative_prompt" : "Negative Prompt";
    const QString modelKey = lower ? "use_stable_diffusion_model" : "Stable Diffusion model";
    const QString width = lower ? "width" : "Width";
    const QString height = lower ? "height" : "Height";
    info.Positive = jsonText(object.value(prompt)).trimmed();
    info.Negative = jsonText(object.value(negative)).trimmed();
    // only the file name of the model
    QString model = jsonText(object.value(modelKey));
    model = model.mid(qMax(model.lastIndexOf('/'), model.lastIndexOf('\\')) + 1);
    if(!model.isEmpty())
        object.insert(modelKey, model);
    object = withSize(object, width, height);
    info.Settings = settingsFromJson(object, {
        {"Steps", {"num_inference_steps", "Steps"}},
        {"Sampler", {"sampler_name", "Sampler"}},
        {"CFG scale", {"guidance_scale", "Guidance Scale"}},
        {"Seed", {"seed", "Seed"}},
        {"Size", {"Size"}},
        {"Model", {modelKey}},
    }, {prompt, negative, width, height});
    return info;
}

void splitInvokePrompt(const QString& prompt, PromptInfo& info)
{
    // "positive [negative]"
    static thread_local const QRegularExpression exp(R"(^(.*?)\[(.*?)\]$)", QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch match = exp.match(prompt.trimmed());
    if(match.hasMatch()) {
        info.Positive = match.captured(1).trimmed();
        info.Negative = match.captured(2).trimmed();
    } else {
        info.Positive = prompt.trimmed();
    }
}

PromptInfo parseInvokeAI(const QHash<QString, QString>& texts)
{
    PromptInfo info;
    info.Tool = "InvokeAI";
    if(texts.contains("invokeai_metadata")) {
        QJsonObject object = parseJsonObject(texts.value("invokeai_metadata"));
        info.Positive = jsonText(object.value("positive_prompt")).trimmed();
        info.Negative = jsonText(object.value("negative_prompt")).trimmed();
        QJsonObject model = object.value("model").toObject();
        QString modelName = jsonText(model.contains("model_name") ? model.value("model_name") : model.value("name"));
        if(!modelName.isEmpty())
            object.insert("model", modelName);
        object = withSize(object, "width", "height");
        info.Settings = settingsFromJson(object, {
            {"Steps", {"steps"}},
            {"Sampler", {"scheduler"}},
            {"CFG scale", {"cfg_scale"}},
            {"Seed", {"seed"}},
            {"Size", {"Size"}},
            {"Model", {"model"}},
        }, {"positive_prompt", "negative_prompt", "width", "height"});
    } else if(texts.contains("sd-metadata")) {
        QJsonObject object = parseJsonObject(texts.value("sd-metadata"));
        QJsonObject image = object.value("image").toObject();
        QJsonValue prompt = image.value("prompt");
        splitInvokePrompt(prompt.isArray() ? jsonText(prompt.toArray().at(0).toObject().value("prompt")) : jsonText(prompt), info);
        object.remove("image");
        for(auto it = image.begin(); it != image.end(); ++it)
            object.insert(it.key(), it.value());
        object = withSize(object, "width", "height");
        info.Settings = settingsFromJson(object, {
            {"Steps", {"steps"}},
            {"Sampler", {"sampler"}},
            {"CFG scale", {"cfg_scale"}},
            {"Seed", {"seed"}},
            {"Size", {"Size"}},
            {"Model", {"model_weights"}},
        }, {"prompt", "width", "height"});
    } else if(texts.contains("Dream")) {
        // "prompt [negative]" -s 30 -S 1234 -W 512 -H 512 -C 7.5 -A k_lms
        static thread_local const QRegularExpression dreamExp(R"exp("(.*?)"\s*(.*?)$)exp", QRegularExpression::DotMatchesEverythingOption);
        static thread_local const QRegularExpression optionExp(R"(-(\w+)\s+([\w.-]+))");
        QRegularExpressionMatch match = dreamExp.match(texts.value("Dream"));
        if(!match.hasMatch())
            return info;
        splitInvokePrompt(match.captured(1), info);
        QHash<QString, QString> options;
        QRegularExpressionMatchIterator it = optionExp.globalMatch(match.captured(2));
        while(it.hasNext()) {
            QRegularExpressionMatch option = it.next();
            options.insert(option.captured(1), option.captured(2));
        }
        for(const auto& key : QList<QPair<QString, QString>>{{"Steps", "s"}, {"Seed", "S"}, {"CFG scale", "C"}, {"Sampler", "A"}}) {
            if(options.contains(key.second))
                info.Settings << qMakePair(key.first, options.value(key.second));
        }
        if(options.contains("W") && options.contains("H"))
            info.Settings << qMakePair(QString("Size"), QString("%1x%2").arg(options.value("W"), options.value("H")));
    }
    return info;
}

PromptInfo parseFooocus(QJsonObject object)
{
    PromptInfo info;
    info.Tool = "Fooocus";
    info.Positive = jsonText(object.value("prompt")).trimmed();
    info.Negative = jsonText(object.value("negative_prompt")).trimmed();
    object = withSize(object, "width", "height");
    if(!object.contains("Size")) {
        // e.g. "(1024, 1024)"
        static thread_local const QRegularExpression resolutionExp(R"((\d+)\D+(\d+))");
        QRegularExpressionMatch match = resolutionExp.match(jsonText(object.value("resolution")));
        if(match.hasMatch())
            object.insert("Size", QString("%1x%2").arg(match.captured(1), match.captured(2)));
    }
    info.Settings = settingsFromJson(object, {
        {"Steps", {"steps"}},
        {"Sampler", {"sampler"}},
        {"Scheduler", {"scheduler"}},
        {"CFG scale", {"guidance_scale", "cfg"}},
        {"Seed", {"seed"}},
        {"Size", {"Size"}},
        {"Model", {"base_model"}},
    }, {"prompt", "negative_prompt", "full_prompt", "full_negative_prompt", "width", "height", "resolution"});
    return info;
}

PromptInfo parseSwarmUI(const QJsonObject& root)
{
    PromptInfo info;
    info.Tool = "StableSwarmUI";
    QJsonObject params = root.value("sui_image_params").toObject();
    info.Positive = jsonText(params.value("prompt")).trimmed();
    info.Negative = jsonText(params.value("negativeprompt")).trimmed();
    params = withSize(params, "width", "height");
    info.Settings = settingsFromJson(params, {
        {"Steps", {"steps"}},
        {"Sampler", {"sampler", "comfyuisampler", "autowebuisampler"}},
        {"Scheduler", {"scheduler", "comfyuischeduler", "autowebuischeduler"}},
        {"CFG scale", {"cfgscale"}},
        {"Seed", {"seed"}},
        {"Size", {"Size"}},
        {"Model", {"model"}},
    }, {"prompt", "negativeprompt", "width", "height"});
    return info;
}

PromptInfo parseDrawThings(QJsonObject object)
{
    PromptInfo info;
    info.Tool = "Draw Things";
    info.Positive = jsonText(object.value("c")).trimmed();
    info.Negative = jsonText(object.value("uc")).trimmed();
    info.Settings = settingsFromJson(object, {
        {"Steps", {"steps"}},
        {"Sampler", {"sampler"}},
        {"CFG scale", {"scale"}},
        {"Shift", {"shift"}},
        {"Seed", {"seed"}},
        {"Size", {"size"}},
        {"Model", {"model"}},
    }, {"c", "uc"});
    return info;
}

PromptInfo parseNovelAI(const QJsonObject& root, int width, int height)
{
    // the settings are in "Comment" as JSON
    QJsonObject data = root;
    if(root.value("Comment").isString()) {
        const QJsonObject comment = parseJsonObject(root.value("Comment").toString());
        for(auto it = comment.begin(); it != comment.end(); ++it)
            data.insert(it.key(), it.value());
    }
    PromptInfo info;
    info.Tool = "NovelAI";
    info.Positive = jsonText(data.contains("prompt") ? data.value("prompt") : data.value("Description")).trimmed();
    info.Negative = jsonText(data.value("uc")).trimmed();
    if(!data.contains("width") && width > 0 && height > 0) {
        data.insert("width", width);
        data.insert("height", height);
    }
    data = withSize(data, "width", "height");
    info.Settings = settingsFromJson(data, {
        {"Steps", {"steps"}},
        {"Sampler", {"sampler"}},
        {"Scheduler", {"noise_schedule"}},
        {"CFG scale", {"scale"}},
        {"Seed", {"seed"}},
        {"Size", {"Size"}},
        {"Model", {"Source"}},
    }, {"prompt", "uc", "Description", "Comment", "Software", "Title", "width", "height",
        "v4_prompt", "v4_negative_prompt", "signed_hash"});
    return info;
}

// ---------------------------------------------------------------------------
// ComfyUI: the prompt is found by following the graph back from the sampler

class ComfyGraph
{
public:
    explicit ComfyGraph(const QJsonObject& nodes) : m_nodes(nodes) {}
    PromptInfo parse(int width, int height);

private:
    QJsonObject inputs(const QString& id) const { return m_nodes.value(id).toObject().value("inputs").toObject(); }
    QString type(const QString& id) const { return m_nodes.value(id).toObject().value("class_type").toString(); }
    static bool isLink(const QJsonValue& value);
    static QString linkTarget(const QJsonValue& value);
    QString linked(const QString& id, const QString& key) const;
    QJsonValue scalar(const QString& id, const QString& key, int depth = 0) const;
    QString text(const QString& id, const QString& key, bool negative, int depth = 0) const;
    void conditioning(const QString& id, bool negative, QStringList& texts, PromptPairs& parts, int depth);
    bool isSampler(const QString& id) const;
    QString findSampler() const;

    QJsonObject m_nodes;
    QString m_guidance;
};

bool ComfyGraph::isLink(const QJsonValue &value)
{
    if(!value.isArray())
        return false;
    QJsonArray link = value.toArray();
    return link.size() == 2 && (link.at(0).isString() || link.at(0).isDouble()) && link.at(1).isDouble();
}

QString ComfyGraph::linkTarget(const QJsonValue &value)
{
    QJsonValue id = value.toArray().at(0);
    return id.isString() ? id.toString() : jsonText(id);
}

QString ComfyGraph::linked(const QString &id, const QString &key) const
{
    if(id.isEmpty())
        return QString();
    QJsonValue value = inputs(id).value(key);
    return isLink(value) ? linkTarget(value) : QString();
}

QJsonValue ComfyGraph::scalar(const QString &id, const QString &key, int depth) const
{
    QJsonValue value = inputs(id).value(key);
    if(!isLink(value) || depth > 16)
        return isLink(value) ? QJsonValue() : value;
    // a node which gives the value, e.g. a primitive or a seed generator
    const QString source = linkTarget(value);
    const QJsonObject in = inputs(source);
    QStringList names = {key, "value", "Value", "int", "float", "number"};
    if(key.contains("seed"))
        names << "seed" << "noise_seed";
    for(const QString& name : names) {
        if(in.contains(name))
            return scalar(source, name, depth + 1);
    }
    return QJsonValue();
}

QString ComfyGraph::text(const QString &id, const QString &key, bool negative, int depth) const
{
    QJsonValue value = inputs(id).value(key);
    if(value.isString())
        return value.toString();
    if(!isLink(value) || depth > 16)
        return QString();
    const QString source = linkTarget(value);
    const QJsonObject in = inputs(source);
    // SDXL Prompt Styler has both prompts
    if(in.contains("text_positive") || in.contains("text_negative"))
        return text(source, negative ? "text_negative" : "text_positive", negative, depth + 1);
    const QStringList names = {"populated_text", "text", "string", "value", "prompt", "wildcard_text"};
    for(const QString& name : names) {
        if(in.contains(name)) {
            QString result = text(source, name, negative, depth + 1);
            if(!result.isEmpty())
                return result;
        }
    }
    // nodes which concatenate texts, e.g. "Text Concatenate"
    QStringList parts;
    for(auto it = in.begin(); it != in.end(); ++it) {
        if(it.key().startsWith("text") || it.key().startsWith("string")) {
            QString part = text(source, it.key(), negative, depth + 1);
            if(!part.isEmpty())
                parts << part;
        }
    }
    return parts.join(in.value("delimiter").isString() ? in.value("delimiter").toString() : QString(", "));
}

void ComfyGraph::conditioning(const QString &id, bool negative, QStringList &texts, PromptPairs &parts, int depth)
{
    if(id.isEmpty() || depth > 32)
        return;
    const QString nodeType = type(id);
    const QJsonObject in = inputs(id);
    auto addPart = [&](const QString& label, const QString& key) {
        QString part = text(id, key, negative).trimmed();
        if(!part.isEmpty())
            parts << qMakePair(label, part);
    };
    if(nodeType == "CLIPTextEncodeSDXL") {
        addPart("Clip G", "text_g");
        addPart("Clip L", "text_l");
        return;
    }
    if(nodeType == "CLIPTextEncodeSDXLRefiner") {
        addPart("Refiner", "text");
        return;
    }
    if(nodeType == "CLIPTextEncodeFlux") {
        addPart("T5", "t5xxl");
        addPart("Clip L", "clip_l");
        if(m_guidance.isEmpty())
            m_guidance = jsonText(scalar(id, "guidance"));
        return;
    }
    // an empty conditioning, e.g. the negative of Flux
    if(nodeType == "ConditioningZeroOut")
        return;
    // text encoders take a CLIP model and a text
    if(in.contains("clip")) {
        const QStringList keys = {"text", "prompt"};
        for(const QString& key : keys) {
            if(in.contains(key)) {
                QString result = text(id, key, negative).trimmed();
                if(!result.isEmpty() && !texts.contains(result))
                    texts << result;
                return;
            }
        }
    }
    if(nodeType == "FluxGuidance" && m_guidance.isEmpty())
        m_guidance = jsonText(scalar(id, "guidance"));
    // conditioning nodes, e.g. ConditioningCombine, ConditioningSetArea, ControlNetApplyAdvanced
    const QString role = negative ? "negative" : "positive";
    for(auto it = in.begin(); it != in.end(); ++it) {
        if((it.key().startsWith("conditioning") || it.key() == role) && isLink(it.value()))
            conditioning(linkTarget(it.value()), negative, texts, parts, depth + 1);
    }
}

bool ComfyGraph::isSampler(const QString &id) const
{
    const QJsonObject in = inputs(id);
    // SamplerCustomAdvanced
    if(in.contains("guider") && in.contains("sigmas"))
        return true;
    return in.contains("positive") && in.contains("negative") && in.contains("model")
            && (in.contains("seed") || in.contains("noise_seed") || in.contains("steps"));
}

QString ComfyGraph::findSampler() const
{
    // the sampler nearest to the saved image, e.g. the second pass of hires fix
    QStringList outputs, previews;
    for(auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
        const QString nodeType = type(it.key());
        if(nodeType.contains("SaveImage") || nodeType.contains("Image Save") || nodeType == "SDPromptSaver")
            outputs << it.key();
        else if(nodeType.contains("PreviewImage"))
            previews << it.key();
    }
    for(const QString& output : outputs + previews) {
        QStringList queue = {output};
        QSet<QString> visited = {output};
        while(!queue.isEmpty()) {
            const QString id = queue.takeFirst();
            if(id != output && isSampler(id))
                return id;
            const QJsonObject in = inputs(id);
            for(auto it = in.begin(); it != in.end(); ++it) {
                if(!isLink(it.value()))
                    continue;
                const QString source = linkTarget(it.value());
                if(!visited.contains(source)) {
                    visited.insert(source);
                    queue << source;
                }
            }
        }
    }
    for(auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
        if(isSampler(it.key()))
            return it.key();
    }
    return QString();
}

PromptInfo ComfyGraph::parse(int width, int height)
{
    PromptInfo info;
    info.Tool = "ComfyUI";
    const QString sampler = findSampler();
    if(sampler.isEmpty())
        return info;
    // SamplerCustomAdvanced takes the settings from its noise, guider, sampler and sigmas
    const QString guider = linked(sampler, "guider");
    const QString noise = linked(sampler, "noise");
    const QString samplerSelect = linked(sampler, "sampler");
    const QString sigmas = linked(sampler, "sigmas");
    auto value = [&](const QStringList& nodes, const QStringList& keys) -> QString {
        for(const QString& node : nodes) {
            if(node.isEmpty())
                continue;
            const QJsonObject in = inputs(node);
            for(const QString& key : keys) {
                QString result = in.contains(key) ? jsonText(scalar(node, key)) : QString();
                if(!result.isEmpty())
                    return result;
            }
        }
        return QString();
    };
    auto firstOf = [](const QStringList& ids) {
        for(const QString& id : ids)
            if(!id.isEmpty())
                return id;
        return QString();
    };
    auto finish = [](QStringList texts, const PromptPairs& parts, QString& prompt, PromptPairs& promptParts) {
        QStringList partTexts;
        for(const auto& part : parts)
            partTexts << part.second;
        partTexts.removeDuplicates();
        // the texts of SDXL are shown separately only when they differ
        if(partTexts.size() > 1)
            promptParts = parts;
        texts = partTexts + texts;
        texts.removeDuplicates();
        prompt = texts.join("\n").trimmed();
    };

    QStringList texts;
    PromptPairs parts;
    conditioning(firstOf({linked(sampler, "positive"), linked(guider, "positive"), linked(guider, "conditioning"), linked(guider, "cond1")}),
                 false, texts, parts, 0);
    finish(texts, parts, info.Positive, info.PositiveParts);
    texts.clear();
    parts.clear();
    conditioning(firstOf({linked(sampler, "negative"), linked(guider, "negative")}), true, texts, parts, 0);
    finish(texts, parts, info.Negative, info.NegativeParts);

    // the model, LoRAs and the shift of the sampling (e.g. ModelSamplingSD3) on the way to the loader
    QString model, shift;
    QStringList loras;
    QString node = firstOf({linked(sampler, "model"), linked(guider, "model"), linked(sigmas, "model")});
    for(int depth = 0; !node.isEmpty() && depth < 32; depth++) {
        const QJsonObject in = inputs(node);
        if(in.contains("ckpt_name") || in.contains("unet_name")) {
            model = jsonText(scalar(node, in.contains("ckpt_name") ? "ckpt_name" : "unet_name"));
            break;
        }
        if(shift.isEmpty() && in.contains("shift"))
            shift = jsonText(scalar(node, "shift"));
        if(in.contains("lora_name")) {
            QString lora = jsonText(scalar(node, "lora_name"));
            QString strength = jsonText(scalar(node, "strength_model"));
            loras << (strength.isEmpty() ? lora : QString("%1: %2").arg(lora, strength));
        }
        node = linked(node, "model");
    }

    const QList<QPair<QString, QString>> settings = {
        {"Steps", value({sampler, sigmas}, {"steps"})},
        {"Sampler", value({sampler, samplerSelect}, {"sampler_name"})},
        {"Scheduler", value({sampler, sigmas}, {"scheduler"})},
        {"CFG scale", value({sampler, guider}, {"cfg"})},
        {"Guidance", m_guidance},
        {"Shift", shift},
        {"Seed", value({sampler, noise}, {"seed", "noise_seed"})},
        {"Size", width > 0 && height > 0 ? QString("%1x%2").arg(width).arg(height) : QString()},
        {"Model", model},
        {"Lora", loras.join(", ")},
        {"Denoise", value({sampler, sigmas}, {"denoise"})},
    };
    for(const auto& setting : settings) {
        if(!setting.second.isEmpty())
            info.Settings << setting;
    }
    return info;
}

// ---------------------------------------------------------------------------
// NovelAI hides the data in the lowest bits of the alpha channel, column by column

class AlphaBitReader
{
public:
    explicit AlphaBitReader(const QImage& image) : m_image(image) {}
    QByteArray read(qsizetype count)
    {
        QByteArray bytes;
        for(qsizetype i = 0; i < count; i++) {
            int value = 0;
            for(int bit = 0; bit < 8; bit++) {
                if(m_x >= m_image.width())
                    return bytes;
                value = (value << 1) | (qAlpha(m_image.pixel(m_x, m_y)) & 1);
                if(++m_y >= m_image.height()) {
                    m_y = 0;
                    m_x++;
                }
            }
            bytes.append(char(value));
        }
        return bytes;
    }

private:
    const QImage& m_image;
    int m_x = 0;
    int m_y = 0;
};

PromptInfo readStealth(const QImage& image)
{
    if(image.isNull() || !image.hasAlphaChannel() || image.width() * qint64(image.height()) < 256)
        return PromptInfo();
    AlphaBitReader reader(image);
    const QByteArray magic = reader.read(15);
    const bool compressed = magic == "stealth_pngcomp";
    if(!compressed && magic != "stealth_pnginfo")
        return PromptInfo();
    const QByteArray length = reader.read(4);
    if(length.size() != 4)
        return PromptInfo();
    const qint64 byteLength = qFromBigEndian<quint32>(length.constData()) / 8;
    if(byteLength <= 0 || byteLength > image.width() * qint64(image.height()) / 8)
        return PromptInfo();
    QByteArray data = reader.read(byteLength);
    if(compressed)
        data = inflateGzip(data);
    const QString text = QString::fromUtf8(data).trimmed();
    // NovelAI writes JSON, the stealth extension of A1111 webUI writes its parameters
    if(text.startsWith('{'))
        return parseNovelAI(parseJsonObject(text), image.width(), image.height());
    return parseA1111(text);
}

PromptInfo readFromExif(const Metadata& meta, const ExifData& exif, const QImage& image)
{
    if(meta.texts.contains("comment")) {
        QJsonObject fooocus = parseJsonObject(meta.texts.value("comment"));
        if(fooocus.contains("prompt"))
            return parseFooocus(fooocus);
    }
    PromptInfo stealth = readStealth(image);
    if(stealth.isValid())
        return stealth;
    const QString comment = decodeUserComment(exif.userComment);
    if(comment.isEmpty())
        return PromptInfo();
    if(comment.contains("sui_image_params"))
        return parseSwarmUI(parseJsonObject(comment));
    if(comment.startsWith('{'))
        return parseEasyDiffusion(parseJsonObject(comment));
    return parseA1111(comment);
}

PromptInfo readFromPngTexts(const Metadata& meta, const ExifData& exif, const QImage& image)
{
    const QHash<QString, QString>& texts = meta.texts;
    if(texts.contains("parameters")) {
        const QString parameters = texts.value("parameters");
        if(parameters.contains("sui_image_params"))
            return parseSwarmUI(parseJsonObject(parameters));
        PromptInfo info = parseA1111(parameters);
        if(texts.contains("prompt"))
            info.Tool = "ComfyUI";
        if(texts.contains("postprocessing")) {
            const QString postprocessing = texts.value("postprocessing").trimmed();
            info.Settings << parseA1111Settings(postprocessing);
            info.SettingText = info.SettingText.isEmpty() ? postprocessing : info.SettingText + ", " + postprocessing;
        }
        return info;
    }
    if(texts.contains("negative_prompt") || texts.contains("Negative Prompt"))
        return parseEasyDiffusion(toJsonObject(texts));
    if(texts.contains("invokeai_metadata") || texts.contains("sd-metadata") || texts.contains("Dream"))
        return parseInvokeAI(texts);
    if(texts.value("Software") == "NovelAI")
        return parseNovelAI(toJsonObject(texts), meta.width, meta.height);
    if(texts.contains("prompt"))
        return ComfyGraph(parseJsonObject(texts.value("prompt"))).parse(meta.width, meta.height);
    if(texts.contains("Comment")) {
        QJsonObject fooocus = parseJsonObject(texts.value("Comment"));
        if(fooocus.contains("prompt"))
            return parseFooocus(fooocus);
    }
    if(texts.contains("XML:com.adobe.xmp")) {
        // Draw Things puts JSON in exif:UserComment of XMP
        static thread_local const QRegularExpression commentExp(R"(<exif:UserComment.*?<rdf:li[^>]*>(.*?)</rdf:li>)",
                                                   QRegularExpression::DotMatchesEverythingOption);
        QRegularExpressionMatch match = commentExp.match(texts.value("XML:com.adobe.xmp"));
        if(match.hasMatch()) {
            QJsonObject object = parseJsonObject(unescapeXml(match.captured(1)));
            if(object.contains("c"))
                return parseDrawThings(object);
        }
    }
    return readFromExif(meta, exif, image);
}

} // namespace

QString PromptInfo::setting(const QStringList &keys) const
{
    for(const QString& key : keys) {
        for(const auto& pair : Settings) {
            if(pair.first.compare(key, Qt::CaseInsensitive) == 0)
                return pair.second;
        }
    }
    return QString();
}

QString PromptInfo::toA1111Text() const
{
    QStringList lines;
    if(!Positive.isEmpty())
        lines << Positive;
    if(!Negative.isEmpty())
        lines << "Negative prompt: " + Negative;
    if(!SettingText.isEmpty())
        lines << SettingText;
    return lines.join('\n');
}

PromptInfo PromptReader::read(const QByteArray &bytes, const QImage &image)
{
    Metadata meta;
    if(bytes.startsWith("\x89PNG\r\n\x1a\n"))
        readPng(bytes, meta);
    else if(bytes.startsWith("\xff\xd8"))
        readJpeg(bytes, meta);
    else if(bytes.size() >= 12 && bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WEBP")
        readWebp(bytes, meta);
    else
        return PromptInfo();
    if(meta.width <= 0 && !image.isNull()) {
        meta.width = image.width();
        meta.height = image.height();
    }

    const ExifData exif = parseExif(meta.exif);
    PromptInfo info;
    // StableSwarmUI used to write its JSON in the Model tag of EXIF
    const QString exifModel = trimNulls(decodeText(exif.model));
    if(exifModel.contains("sui_image_params"))
        info = parseSwarmUI(parseJsonObject(exifModel));
    else if(meta.png)
        info = readFromPngTexts(meta, exif, image);
    else
        info = readFromExif(meta, exif, image);

    if(!info.isValid())
        return PromptInfo();
    if(info.SettingText.isEmpty())
        info.SettingText = joinSettings(info.Settings);
    return info;
}
