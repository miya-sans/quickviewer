#include <QtGui>
#include <random>
#include <QSvgRenderer>

#include "volumemanager.h"
#include "ResizeHalf.h"
#include "qvapplication.h"
#include "qzimg.h"
#include "pagemanager.h"
#include "fileloader.h"
#include "fileloaderdirectory.h"
#include "svgnative/SVGDocument.h"
#include "svgnative/ports/qt/QSVGRenderer.h"

using namespace SVGNative;
static ImageContent pathThrough(ImageContent ic) { return ic; }

VolumeManager::VolumeManager(QObject *parent, IFileLoader* loader, PageManager* pageManager)
    : QObject(parent)
    , m_cnt(0)
    , m_imageCache(qApp->MaxImagesCache())
    , m_loader(loader)
    , m_cacheMode(CacheMode::Normal)
    , m_pageManager(pageManager)
    , m_enumerated(false)
    , m_openedWithSpecifiedImageFile(false)
{
    m_volumePath = m_loader->volumePath();
    connect(&m_watcher, SIGNAL(finished()), this, SLOT(on_enmumerated()));
}

VolumeManager::~VolumeManager() {
    m_imageCache.clear();
    // the images still being decoded use the loader
    m_imageCache.waitForDiscarded();
    if(m_loader) {
        m_loader->deleteLater();
        m_loader = nullptr;
    }
}

void VolumeManager::enumerate()
{
    m_filelist = m_loader->contents();
    m_enumerated = true;
    sortForReady(qApp->ImageSortBy());
}

ImageContent VolumeManager::getImageBeforeEnmumerate(QString subfilename)
{
    m_subfilename = subfilename;
    m_currentCacheSync = VolumeManager::futureLoadImageFromFileVolume(this, subfilename, QSize());
    enumerate();
    return m_currentCacheSync;
}

void VolumeManager::on_enmumerated()
{
    // foreach(const QString& fl, m_filelist) {
    //     m_imageMetadataList << QvImageMetadata(this, fl);
    // }
    m_imageCache.insert(m_subfilename, QtConcurrent::run(pathThrough, m_currentCacheSync));
    findImageByName(m_subfilename);
    setCacheMode(VolumeManager::Normal);
    on_ready();
    m_pageManager->on_pageEnumerated();
}

// #ifdef Q_OS_WIN
// #include <Shlwapi.h>

// static bool fileNameDescendingLessThan(const QvImageMetadata& m1, const QvImageMetadata& m2)
// {
//     std::wstring ss1(m1.filename().toStdWString());
//     std::wstring ss2(m2.filename().toStdWString());
//     return ::StrCmpLogicalW(ss1.c_str(), ss2.c_str()) > 0;
// }
// #else

// static bool fileNameDescendingLessThan(const QvImageMetadata& m1, const QvImageMetadata& m2)
// {
//     return m1.filename() > m2.filename();
// }

// #endif


static bool fileSizeLessThan(const QvImageMetadata& m1, const QvImageMetadata& m2)
{
    QvImageMetadata& mm1 = const_cast<QvImageMetadata&>(m1);
    QvImageMetadata& mm2 = const_cast<QvImageMetadata&>(m2);
    return mm1.getFileSize() < mm2.getFileSize();
}
static bool fileSizeDescendingLessThan(const QvImageMetadata& m1, const QvImageMetadata& m2)
{
    QvImageMetadata& mm1 = const_cast<QvImageMetadata&>(m1);
    QvImageMetadata& mm2 = const_cast<QvImageMetadata&>(m2);
    return mm1.getFileSize() > mm2.getFileSize();
}
static bool modifiedTimeLessThan(const QvImageMetadata& m1, const QvImageMetadata& m2)
{
    QvImageMetadata& mm1 = const_cast<QvImageMetadata&>(m1);
    QvImageMetadata& mm2 = const_cast<QvImageMetadata&>(m2);
    return mm1.getMTime() < mm2.getMTime();
}
static bool modifiedTimeDescendingLessThan(const QvImageMetadata& m1, const QvImageMetadata& m2)
{
    QvImageMetadata& mm1 = const_cast<QvImageMetadata&>(m1);
    QvImageMetadata& mm2 = const_cast<QvImageMetadata&>(m2);
    return mm1.getMTime() > mm2.getMTime();
}



void VolumeManager::sort(qvEnums::ImageSortBy sortBy)
{
    sortForReady(sortBy);
    on_ready();
}

void VolumeManager::sortForReady(qvEnums::ImageSortBy sortBy)
{
    m_imageMetadataList.clear();
    foreach(const QString& fl, m_filelist) {
        m_imageMetadataList << QvImageMetadata(this, fl);
    }
    // Creating a QCollator is expensive, so share one for all comparisons
    QCollator col;
    col.setNumericMode(true);
    switch(sortBy) {
    case qvEnums::SortByFileName:
        std::sort(m_filelist.begin(), m_filelist.end(), [&col](const QString& m1, const QString& m2) {
            return col.compare(m1, m2) < 0;
        });
        break;
    case qvEnums::SortByFileNameDescending:
        std::sort(m_filelist.begin(), m_filelist.end(), [&col](const QString& m1, const QString& m2) {
            return col.compare(m1, m2) > 0;
        });
        break;
    case qvEnums::SortByFileSize:
        std::stable_sort(m_imageMetadataList.begin(), m_imageMetadataList.end(), fileSizeLessThan);
        break;
    case qvEnums::SortByFileSizeDescending:
        std::stable_sort(m_imageMetadataList.begin(), m_imageMetadataList.end(), fileSizeDescendingLessThan);
        break;
    case qvEnums::SortByModifiedTime:
        std::stable_sort(m_imageMetadataList.begin(), m_imageMetadataList.end(), modifiedTimeLessThan);
        break;
    case qvEnums::SortByModifiedTimeDescending:
        std::stable_sort(m_imageMetadataList.begin(), m_imageMetadataList.end(), modifiedTimeDescendingLessThan);
        break;
    }
    m_cnt = 0;
    m_imageCache.clear();
}

void VolumeManager::startSlideShow()
{
    if(!qApp->SlideShowRandomly())
        return;
    m_randomfilelist = m_filelist;
    m_randomfilelist.detach();
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(m_randomfilelist.begin(), m_randomfilelist.end(), g);
    m_cnt = 0;
    m_imageCache.clear();
    on_ready();
}

void VolumeManager::stopSlideShow()
{
    if(!qApp->SlideShowRandomly())
        return;
    m_randomfilelist.clear();
    m_cnt = 0;
    m_imageCache.clear();
    on_ready();
}

QString VolumeManager::getIndexedFileName(int idx) {
    if(idx < 0 || idx >= m_filelist.size())
        return "";
    if(!m_randomfilelist.isEmpty())
        return m_randomfilelist[idx];
    if(qApp->ImageSortBy() == qvEnums::SortByFileName
        || qApp->ImageSortBy() == qvEnums::SortByFileNameDescending)
        return m_filelist[idx];
    else
        return m_imageMetadataList[idx].filename();
}

int VolumeManager::indexOfFileName(const QString& name)
{
    if(!m_randomfilelist.isEmpty())
        return m_randomfilelist.indexOf(name);
    if(qApp->ImageSortBy() == qvEnums::SortByFileName
        || qApp->ImageSortBy() == qvEnums::SortByFileNameDescending)
        return m_filelist.indexOf(name);
    for(int i = 0; i < m_imageMetadataList.size(); i++) {
        if(m_imageMetadataList[i].filename() == name)
            return i;
    }
    return -1;
}

bool VolumeManager::updateFileList(const QStringList& contents, const QStringList& added, const QSet<QString>& removed, const QStringList& changed)
{
    if(added.isEmpty() && removed.isEmpty() && changed.isEmpty())
        return false;

    // the file of the counter, or the nearest remaining one (the next ones first)
    QString current;
    for(int i = m_cnt; i < m_filelist.size() && current.isEmpty(); i++) {
        QString name = getIndexedFileName(i);
        if(!removed.contains(name))
            current = name;
    }
    for(int i = m_cnt-1; i >= 0 && current.isEmpty(); i--) {
        QString name = getIndexedFileName(i);
        if(!removed.contains(name))
            current = name;
    }

    foreach(const QString& name, removed)
        m_imageCache.remove(name);
    foreach(const QString& name, changed)
        m_imageCache.remove(name);
    if(FileLoaderDirectory* loader = dynamic_cast<FileLoaderDirectory*>(m_loader))
        loader->setContents(contents);

    // The new files are inserted into the sorted lists: with thousands of files, sorting all again is slow,
    // and it would read the size or time of every file again
    const qvEnums::ImageSortBy sortBy = qApp->ImageSortBy();
    if(sortBy == qvEnums::SortByFileName || sortBy == qvEnums::SortByFileNameDescending) {
        if(!removed.isEmpty()) {
            m_filelist.removeIf([&removed](const QString& name) { return removed.contains(name); });
            m_imageMetadataList.removeIf([&removed](const QvImageMetadata& md) { return removed.contains(md.filename()); });
        }
        QCollator col;
        col.setNumericMode(true);
        const bool descending = sortBy == qvEnums::SortByFileNameDescending;
        auto less = [&col, descending](const QString& m1, const QString& m2) {
            int c = col.compare(m1, m2);
            return descending ? c > 0 : c < 0;
        };
        foreach(const QString& name, added) {
            int pos = std::upper_bound(m_filelist.begin(), m_filelist.end(), name, less) - m_filelist.begin();
            m_filelist.insert(pos, name);
            // not used when sorted by name, but kept with the same files
            m_imageMetadataList.insert(qMin(pos, (int)m_imageMetadataList.size()), QvImageMetadata(this, name));
        }
    } else {
        m_filelist = contents;
        QSet<QString> changedSet(changed.begin(), changed.end());
        m_imageMetadataList.removeIf([&removed, &changedSet](const QvImageMetadata& md) {
            return removed.contains(md.filename()) || changedSet.contains(md.filename());
        });
        bool (*less)(const QvImageMetadata&, const QvImageMetadata&) = nullptr;
        switch(sortBy) {
        case qvEnums::SortByFileSize: less = fileSizeLessThan; break;
        case qvEnums::SortByFileSizeDescending: less = fileSizeDescendingLessThan; break;
        case qvEnums::SortByModifiedTime: less = modifiedTimeLessThan; break;
        case qvEnums::SortByModifiedTimeDescending: less = modifiedTimeDescendingLessThan; break;
        default: break;
        }
        foreach(const QString& name, added + changed) {
            QvImageMetadata md(this, name);
            int pos = less ? std::upper_bound(m_imageMetadataList.begin(), m_imageMetadataList.end(), md, less) - m_imageMetadataList.begin()
                           : m_imageMetadataList.size();
            m_imageMetadataList.insert(pos, md);
        }
    }
    if(!m_randomfilelist.isEmpty()) {
        // the slide show in random order shows the new files somewhere later
        if(!removed.isEmpty())
            m_randomfilelist.removeIf([&removed](const QString& name) { return removed.contains(name); });
        std::random_device rd;
        std::mt19937 g(rd());
        foreach(const QString& name, added) {
            int from = qMin(m_cnt+1, (int)m_randomfilelist.size());
            m_randomfilelist.insert(std::uniform_int_distribution<int>(from, (int)m_randomfilelist.size())(g), name);
        }
    }

    int idx = current.isEmpty() ? -1 : indexOfFileName(current);
    m_cnt = idx >= 0 ? idx : qBound(0, m_cnt, qMax(0, (int)m_filelist.size()-1));
    return true;
}

QStringList VolumeManager::dropFailedImages(const QStringList& names)
{
    QStringList dropped;
    foreach(QString name, names) {
        if(!m_imageCache.contains(name) || !m_imageCache.object(name).isFinished())
            continue;
        ImageContent ic = m_imageCache.object(name).result();
        if(ic.Image.isNull() && ic.Movie.isNull()) {
            m_imageCache.remove(name);
            dropped << name;
        }
    }
    return dropped;
}

void VolumeManager::on_ready()
{
    if(!m_enumerated)
        enumerate();
    if(m_cnt < 0 || m_cnt >= m_filelist.size() || m_loader->contents().size()==0)
        return;

//    qDebug() << "on_ready: m_cnt" << m_cnt;
    QList<int> offsets;
    switch(m_cacheMode) {
    case CacheMode::Normal:
        offsets = {0, 1, 2, 3, -1, -2, 4, 5, -3, -4, 6, 7, -5, -6};
        while(offsets.size() > qApp->MaxImagesCache())
            offsets.removeLast();
        break;
    case CacheMode::NormalForward:
        offsets = {10, 11, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7};
        if(offsets.size() > qApp->MaxImagesCache())  {
            QMutableListIterator<int> i(offsets);
            while (i.hasNext()) {
                if (i.next() >= qApp->MaxImagesCache())
                    i.remove();
            }
        }
        break;
    case CacheMode::NormalBackward:
        offsets = {-9, -10, -7, -8, 0, 1, -1, -2, -3, -4, -5, -6};
        if(offsets.size() > qApp->MaxImagesCache())  {
            QMutableListIterator<int> i(offsets);
            while (i.hasNext()) {
                if (i.next() < -qApp->MaxImagesCache()+2)
                    i.remove();
            }
        }
        break;
    case CacheMode::FastForward:
        offsets = {0, 1, 10, 11, -10, -9, 20, 21, -20, 19};
        while(offsets.size() > qApp->MaxImagesCache())
            offsets.removeLast();
        break;
    case CacheMode::CreateThumbnail:
        m_currentCacheSync = futureLoadImageFromFileVolume(this, m_filelist[0], QSize());
        return;
    case CacheMode::CoverOnly:
        offsets = {0, 1};
        foreach (const int of, offsets) {
            int cnt = m_cnt+of;
            if(cnt < 0 || cnt >= m_filelist.size())
                continue;
            QString name = getIndexedFileName(cnt);
            ImageContent ic = futureLoadImageFromFileVolume(this, name, QSize());
            m_imageCache.insert(name, QtConcurrent::run(pathThrough, ic));
        }
        return;
    default:
        break;
    }
    limitPrefetchByMemory(offsets);
    foreach (const int of, offsets) {
        int cnt = m_cnt+of;
        if(cnt < 0 || cnt >= m_filelist.size())
            continue;
        QString name = getIndexedFileName(cnt);
        if(qApp->Effect() < qvEnums::UsingFixedShader && m_imageCache.contains(name) && m_imageCache.object(name).isFinished() ) {
            ImageContent ic = m_imageCache.object(name).result();
            if(ic.ImportSize.isValid()) {
                QSize pageSize = m_pageManager->viewportSize();
                QSize resized = ic.Info.Orientation==6 || ic.Info.Orientation==8 ? QSize(pageSize.height(), pageSize.width()) : pageSize;
                resized.setWidth(ic.ImportSize.width()*resized.height()/ic.ImportSize.height());

                if(ic.ResizedImage.size() != resized && !ic.Image.isNull()) {
                    qDebug() << ic.ResizedImage.size() << resized;
                    m_imageCache.insert(name, QtConcurrent::run(futureReizeImage, ic, m_pageManager->viewportSize()));
                }
            }
        }
        if(m_imageCache.checkShouldBeInserted(name)) {
//            qDebug() << "on_ready()" << m_filelist[cnt];
            m_imageCache.insertNoChecked(name, QtConcurrent::run(
                 futureLoadImageFromFileVolume,
                 this,
                 name,
                 (m_pageManager && qApp->Effect() < qvEnums::UsingFixedShader)
                     ? m_pageManager->viewportSize()
                     : QSize())
            );
        }
    }
    QString current = getIndexedFileName(m_cnt);
    m_currentCache = m_imageCache.object(current);
}

// Memory for the images read ahead and cached. e.g. 7 images of 7224x4064 (112MB each) on 64bit
#ifdef Q_PROCESSOR_X86_64
#define PREFETCH_MEMORY_BUDGET (800LL*1024*1024)
#else
#define PREFETCH_MEMORY_BUDGET (300LL*1024*1024)
#endif
// the current page, the next ones for the dual view and the previous one are always read
#define MIN_PREFETCH_IMAGES 4

void VolumeManager::limitPrefetchByMemory(QList<int>& offsets)
{
    int count;
    QString current = getIndexedFileName(m_cnt);
    if(m_imageCache.contains(current) && m_imageCache.object(current).isFinished()) {
        // estimate the size of the images in the volume from the current one
        const ImageContent ic = m_imageCache.object(current).result();
        qint64 bytes = ic.Image.sizeInBytes() + ic.ResizedImage.sizeInBytes();
        count = bytes > 0 ? (int)qMax<qint64>(MIN_PREFETCH_IMAGES, PREFETCH_MEMORY_BUDGET / bytes) : offsets.size();
        m_imageCache.setMaxSize(qBound(MIN_PREFETCH_IMAGES, count, qApp->MaxImagesCache()));
    } else {
        // the size is unknown until the current image is loaded, the rest is read ahead at the next page
        count = MIN_PREFETCH_IMAGES;
    }
    if(count >= offsets.size())
        return;
    // keep the nearest pages (the next one before the previous one), in the original order
    QList<int> nearest = offsets;
    std::stable_sort(nearest.begin(), nearest.end(), [](int a, int b) {
        return qAbs(a) != qAbs(b) ? qAbs(a) < qAbs(b) : a > b;
    });
    nearest = nearest.mid(0, count);
    QMutableListIterator<int> it(offsets);
    while(it.hasNext()) {
        if(!nearest.contains(it.next()))
            it.remove();
    }
}

const ImageContent VolumeManager::getIndexedImageContent(int idx)
{
    if(idx < 0 || idx >= m_filelist.size())
        return ImageContent();
    QString name = getIndexedFileName(idx);
    // The page may not have been prefetched (e.g. moving backward in dual view)
    if(!m_imageCache.contains(name)) {
        ImageContent ic = futureLoadImageFromFileVolume(this, name, QSize());
        m_imageCache.insert(name, QtConcurrent::run(pathThrough, ic));
        return ic;
    }
//    future_image cache = m_imageCache[idx];
    future_image& cache = m_imageCache.object(name);
//    if(!cache.isFinished())
//        cache.waitForFinished();
    return cache.result();
}

void VolumeManager::moveToThread(QThread *targetThread)
{
    QObject::moveToThread(targetThread);
    m_loader->moveToThread(targetThread);
}

bool VolumeManager::nextPage()
{
//    qDebug() << "nextPage: " << m_cnt << m_filelist.size() <<  "prevCache.size()" << m_prevCache.size() << "nextCache.size()" << m_nextCache.size();
    if(m_cnt >= m_filelist.size()-1 || m_loader->contents().size()==0)
        return false;
    m_cnt++;
    on_ready();
    return true;
}

bool VolumeManager::prevPage()
{
//    qDebug() << "prevPage: " << m_cnt << m_filelist.size() <<  "prevCache.size()" << m_prevCache.size() << "nextCache.size()" << m_nextCache.size();
    if(m_cnt <= 0 || m_loader->contents().size()==0)
        return false;
    m_cnt--;
    on_ready();
    return true;
}

bool VolumeManager::findPageByIndex(int idx)
{
    if(m_cnt == idx)
        return true;
    if(idx < 0 || idx >= m_filelist.size())
        return false;
    m_cnt = idx;
//    bool result = findImageByIndex(idx);
    on_ready();
    return true;
}

bool VolumeManager::findImageByIndex(int idx) {
    if(idx < 0 || idx >= m_filelist.size())
        return false;
    m_cnt = idx;
    on_ready();
    return true;
}

bool VolumeManager::findImageByName(QString name) {
    int idx = m_filelist.indexOf(QDir::toNativeSeparators(name));
    return findImageByIndex(idx);
}

QString VolumeManager::FullPathToVolumePath(QString path)
{
    if(!path.contains("::")) {
        return path;
    }
    return path.left(path.indexOf("::"));
}

QString VolumeManager::FullPathToSubFilePath(QString path)
{
    if(!path.contains("::")) {
        return "";
    }
    return path.mid(path.indexOf("::")+2);
}

static void parseExifTextExtents(QImage& img, easyexif::EXIFInfo& info)
{
    info.Make = img.text("Make").toStdString();
    info.Model = img.text("Model").toStdString();
    info.Orientation = img.text("Software").toInt();
    info.BitsPerSample = img.text("BitsPerSample").toInt();
    info.Software = img.text("Software").toStdString();
    info.DateTime = img.text("DateTime").toStdString();
    info.ExposureTime = img.text("ExposureTime").toDouble();
    info.FNumber = img.text("FNumber").toDouble();
    info.ISOSpeedRatings = img.text("ISOSpeedRatings").toInt();
    info.Flash = img.text("Flash").toInt();
    info.ImageWidth = img.text("ImageWidth").toInt();
    info.ImageHeight = img.text("ImageHeight").toInt();
}


static QZimg::FilterMode ShaderEffect2FilterMode(qvEnums::ShaderEffect effect)
{
    switch (effect) {
    case qvEnums::CpuBicubic: return QZimg::ResizeBicubic;
    case qvEnums::CpuSpline16: return QZimg::ResizeSpline16;
    case qvEnums::CpuSpline36: return QZimg::ResizeSpline36;
    case qvEnums::CpuLanczos3: return QZimg::ResizeLanczos3;
    case qvEnums::BilinearAndCpuBicubic: return QZimg::ResizeBicubic;
    case qvEnums::BilinearAndCpuSpline16: return QZimg::ResizeSpline16;
    case qvEnums::BilinearAndCpuSpline36: return QZimg::ResizeSpline36;
    case qvEnums::BilinearAndCpuLanczos: return QZimg::ResizeLanczos3;
    default:return QZimg::ResizeBicubic;
    }
}
/**
 * @brief resizeHalf shrinks the image to half its size with ResizeHalf
 * @return null image if failed
 */
static QImage resizeHalf(QImage src)
{
    if(src.width() < 32 || src.height() < 2)
        return QImage();
    switch(src.depth()) {
    case 32:
        // ResizeHalf requires a width of multiples of 4 and an even height
        src = src.copy(QRect(0, 0, src.width() >> 2 << 2, src.height() >> 1 << 1));
        break;
    default:
        if(src.format() != QImage::Format::Format_Grayscale8 && src.format() != QImage::Format::Format_RGB888) {
            src = src.convertToFormat(QImage::Format::Format_RGB888);
        }
        // ResizeHalf requires a width of multiples of 16 and an even height
        src = src.copy(QRect(0, 0, src.width() >> 4 << 4, src.height() >> 1 << 1));
        break;
    }
    if(src.isNull())
        return QImage();

    QSize srcSize = src.size();
    QImage half = QImage(srcSize.width()/2, srcSize.height()/2, src.format());
    if(half.isNull())
        return QImage();
    ResizeHalf::FMT fmt = (ResizeHalf::FMT)(src.depth() >> 3);
    ResizeHalf resizer(fmt);
    resizer.resizeHV(half.bits(), src.bits(), src.width(), srcSize.height(), half.bytesPerLine(), src.bytesPerLine());
    return half;
}

static ImageContent loadWithSpecifiedFormat(QString path, QSize pageSize, QByteArray bytes, QString aformat, uint loopcount)
{
    for(;;) {
        int maxTextureSize = qApp->MaxTextureSize();
        easyexif::EXIFInfo info;
        QBuffer buffer(&bytes);

        // I think the excessive normalization of recent years is really ridiculous.
        // Calling what we've traditionally called JPEG something else, like JFIF, is causing confusion for many people.
        // And it hasn't helped solve any of the problems with the JPEG file format.
        // The incompatibility with EXIF remains unresolved.
        if (aformat == "jif"
            || aformat == "jfif"
            || aformat == "jfi") {
            aformat = "jpg";
        }
        QImageReader reader(&buffer, aformat.toUtf8());

//        QElapsedTimer et_canRead; et_canRead.start();
        if(!reader.canRead()) {
            aformat = "";
            break;
        }

        if(aformat == "svg") {
            QSize baseSize = reader.size();
            QSize maxSize(3840, 2160);
            QSize size = baseSize.scaled(maxSize, Qt::KeepAspectRatio);
            QImage image;
            if (qApp->HowToLoadSVG() == "imageformat") {
                reader.setScaledSize(size);
                image = reader.read();
            } else if (qApp->HowToLoadSVG() == "qsvg") {
                // SVG is drawn using QGraphicsSvgItem so QImage is not needed, but I want the resolution of the graphics.
                QSvgRenderer* renderer = new QSvgRenderer(bytes);
                image = QImage(size, QImage::Format_ARGB32);
                {
                    QPainter painter(&image);
                    renderer->render(&painter);
                }
            } else {
                // offline rendering into QImage
                auto renderer = std::shared_ptr<QSVGRenderer>(new QSVGRenderer);
                auto svgDocument = SVGDocument::CreateSVGDocument(bytes.constData(), renderer);

                // render twice size
                image = QImage(size, QImage::Format_ARGB32);
                {
                    QPainter painter(&image);
                    painter.setWindow(0, 0, baseSize.width(), baseSize.height());
                    renderer->SetPainter(&painter);
                    svgDocument->Render();
                }
            }

            ImageContent ic(image, path, size, info, bytes.length());
            return ic;
        }

//        qint64 t_canRead = et_canRead.elapsed();
//        // Emptying the format of QImageReader will get the format of the internal QImageIoHandler
//        reader.setFormat("");

//        QElapsedTimer et_supportsAnimation; et_supportsAnimation.start();
        if(reader.supportsAnimation()) {
            QvMovie movie = QvMovie(bytes, aformat.toUtf8());
            ImageContent ic(path, bytes.length());
            ic.Movie = movie;
            ic.BaseSize = ic.ImportSize = reader.size();
            return ic;
        }
//        qint64 t_supportsAnimation = et_supportsAnimation.elapsed();
//        qDebug() << path << t_canRead << t_supportsAnimation;

        if(aformat == "apng") {
            bool lodepng_exist = IFileLoader::isImageFile("lodepng");
            aformat = lodepng_exist ? "lodepng" : "png";
            break;
        }
        // turbjpeg can turbo rescaling when loading
        QSize baseSize = reader.size();
        QSize loadingSize = baseSize;
        // qrawspeed plugin can also load rescaled raw images(using built in thumbnail),
        // but usualy thumbnails are too small, so we don't use
        if(reader.format() == TURBO_JPEG_FMT) {
            if(!qApp->UseFastDCTForJPEG())
                reader.setQuality(0);
            while(loadingSize.width() > maxTextureSize || loadingSize.height() > maxTextureSize)
                loadingSize = QSize((loadingSize.width()+1) >> 1,(loadingSize.height()+1) >> 1);
            reader.setScaledSize(loadingSize);
        } else if(!qApp->DontShrinkForLargeImage() && baseSize.isValid()
                  && (baseSize.width() > maxTextureSize || baseSize.height() > maxTextureSize)
                  && reader.supportsOption(QImageIOHandler::ScaledSize)) {
            // Decoding a reduced image is much faster and lighter than shrinking it after loading (e.g. JPEG)
            loadingSize = baseSize.scaled(maxTextureSize, maxTextureSize, Qt::KeepAspectRatio);
            reader.setScaledSize(loadingSize);
        }
        QImage src;
        ImageContent ic(path, bytes.length());
        {
            QImage tmp = reader.read();
            if(tmp.isNull() && !aformat.startsWith("tif")) {
                // QImage processing sometimes fails.
                // A reader can't read the same data twice, so retry once with a new one
                qDebug() << "[0]" << path << reader.errorString();
                QBuffer retryBuffer(&bytes);
                QImageReader retry(&retryBuffer, aformat.toUtf8());
                retry.setQuality(reader.quality());
                if(reader.scaledSize().isValid())
                    retry.setScaledSize(reader.scaledSize());
                tmp = retry.read();
            }
            if(tmp.isNull())
                return ic;
            if(baseSize.isEmpty())
                baseSize = loadingSize = tmp.size();
            // 16 bits per channel images are shown with 8 bits per channel
            if(tmp.depth() > 32)
                tmp = tmp.convertToFormat(tmp.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
            src = QZimg::toPackedImage(tmp);
            if(src.isNull()) return ImageContent(path, bytes.length());
        }

        // parsing JPEG EXIF
        if(src.width() > 0 && IFileLoader::isExifJpegImageFile(path)) {
            info.parseFrom(reinterpret_cast<const unsigned char*>(bytes.constData()), bytes.length());
        }

        if(src.width() > 0 && IFileLoader::isExifRawImageFile(path)) {
            parseExifTextExtents(src, info);
        }


    //    ImageContent ic(QPixmap::fromImage(src), path, baseSize, info);
        ic.BaseSize = baseSize;
        ic.Info = info;
        if(src.isNull())
            return ic;
        if(qApp->DontShrinkForLargeImage() || (src.width() <= maxTextureSize && src.height() <= maxTextureSize)) {
            ic = ImageContent(src, path, baseSize, info, bytes.length());
        } else {
            // resample for too big images, halving until the image fits in the texture size
            while(src.width() > maxTextureSize || src.height() > maxTextureSize) {
                QImage half = resizeHalf(src);
                if(half.isNull())
                    return ImageContent(path, bytes.length());
                src = half;
            }
            ic.Image = src;
            ic.ImportSize = src.size();
        }
        // CPU resizing before Page Viewing
        if(!pageSize.isEmpty() && !ic.Image.isNull()) {
            QSize newsize = ic.Info.Orientation==6 || ic.Info.Orientation==8 ? QSize(pageSize.height(), pageSize.width()) : pageSize;
            ic.ResizeMode = qApp->Effect();
            ic.ResizedImage = QZimg::scaled(ic.Image, newsize, Qt::KeepAspectRatio, ShaderEffect2FilterMode(qApp->Effect()));
        }
        return ic;
    }
    if(!loopcount)
        return ImageContent(path, bytes.length());
    return loadWithSpecifiedFormat(path, pageSize, bytes, aformat, loopcount-1);
}



static ImageContent futureLoadImageFromFileVolumeImpl(VolumeManager* volume, QString path, QSize pageSize)
{
//    qDebug() << "futureLoadImageFromFileVolume" << path << QThread::currentThread();

    QByteArray bytes = volume->loadByteArrayByName(path);
    if(bytes.isNull() || bytes.isEmpty())
        return ImageContent();
    QString aformat = IFileLoader::isExifJpegImageFile(path) && IFileLoader::isImageFile("turbojpeg")
            ? TURBO_JPEG_FMT : QFileInfo(path.toLower()).suffix();
    // extention "png" might be a apng
    if(aformat == "png" && IFileLoader::isImageFile("apng")) {
        aformat = "apng";
    }
//    if(aformat == "png") {
//        bool lodepng_exist = IFileLoader::isImageFile("lodepng");
//        aformat = lodepng_exist ? "lodepng" : "png";
//    }
    ImageContent ic = loadWithSpecifiedFormat(path, pageSize, bytes, aformat, 5);
    // the data hidden in the alpha channel (NovelAI) can be read only from the image in its original size
    ic.Prompt = PromptReader::read(bytes, ic.ImportSize == ic.BaseSize ? ic.Image : QImage());
    return ic;
}

ImageContent VolumeManager::futureLoadImageFromFileVolume(VolumeManager* volume, QString path, QSize pageSize)
{
    QElapsedTimer et_load; et_load.start();
    ImageContent ic = futureLoadImageFromFileVolumeImpl(volume, path, pageSize);
    qint64 t_load = et_load.elapsed();

    qDebug() << "futureLoadImageFromFileVolume" << path << t_load << "ms, ResizedImage=" << !ic.ResizedImage.isNull();
    return ic;
}

ImageContent VolumeManager::futureReizeImage(ImageContent ic, QSize pageSize)
{
//    qDebug() << "futureReizeImage:" << ic.Path;
    QSize newsize = ic.Info.Orientation==6 || ic.Info.Orientation==8 ? QSize(pageSize.height(), pageSize.width()) : pageSize;
    ic.ResizeMode = qApp->Effect();
    ic.ResizedImage = QZimg::scaled(ic.Image, newsize, Qt::KeepAspectRatio, ShaderEffect2FilterMode(qApp->Effect()));
    return ic;
}

