#ifndef PAGEMANAGER_H
#define PAGEMANAGER_H

#include <QtGui>
#include "volumemanager.h"
#include "volumemanagerbuilder.h"

class VolumeManager;
class ImageView;

typedef QHash<QString, QPair<qint64, qint64>> FileStamps;

/**
 * @brief FolderScan lists the images of the watched folder on a worker thread, and compares them with the current volume there.
 * Image names are like FileLoaderDirectory::contents(): relative paths with native separators in subfolders
 */
struct FolderScan
{
    // input
    QString volume;
    // the folder of the images of the current volume, empty if no images are shown
    QString realPath;
    bool subfolders = false;
    // the images of the current volume (the loader's list, so that a change is found by its address)
    QStringList known;
    FileStamps knownStamps;

    // output
    bool exists = false;
    QStringList images;
    // size and modification time (msecs) of each image, to find files still being written
    FileStamps stamps;
    QStringList added;
    QSet<QString> removed;
    // images whose size or time changed since the previous scan
    QStringList changed;
    // new empty files (being created), to be listed again
    bool pendingFiles = false;
};

class PageManagerProtocol
{
public:
    virtual int size()=0;
    virtual int currentPage()=0;
    virtual QString volumePath()=0;
    virtual QString currentPagePath()=0;
};


class PageManager : public QObject, public PageManagerProtocol
{
    Q_OBJECT
public:
    PageManager(QObject* parent);

    // Volumes
    bool loadVolume(QString path, bool coverOnly=false);
    bool loadVolumeWithFile(QString path, bool prohibitProhibit2Page=false);
    void nextVolume();
    void prevVolume();
    /**
     * @brief reloadVolume re-enumerates the current folder volume.
     * The displayed image is kept if it still exists, otherwise the nearest remaining image is selected.
     */
    void reloadVolume();
    /**
     * @brief copyCurrentPagesTo copies the displayed images into the folder (images in archives are extracted).
     * Existing files are not overwritten, the copies are renamed like "page01 (2).jpg"
     * @param copiedFiles paths of the copied files
     * @return error message, or empty if succeeded
     */
    QString copyCurrentPagesTo(const QString& folder, QStringList& copiedFiles);

    // Pages
    bool nextPage();
    bool prevPage();
    void fastForwardPage();
    void fastBackwardPage();
    void selectPage(int pageNum, VolumeManager::CacheMode cacheMode=VolumeManager::Normal);
    void firstPage();
    void lastPage();
    void nextOnlyOnePage();
    void prevOnlyOnePage();
    void reloadCurrentPage(bool pageNext = true);
    void addNewPage(ImageContent ic, bool pageNext);
    void clearPages();
    QSize viewportSize();
    void setImageView(ImageView* view){m_imaveView = view;}
    void bookProgress();
    void sort(qvEnums::ImageSortBy sortBy);
    virtual bool eventFilter(QObject *obj, QEvent *event) override;

    // Get String
    int currentPageCount() { return m_pages.size(); }
    int currentPage() override { return m_currentPage; }
    QVector<ImageContent>& currentPageContent() { return m_pages; }
    QString currentPagePath() override {
        if(!m_fileVolume || m_pages.isEmpty())
            return "";
        return QDir::toNativeSeparators(m_fileVolume->getPathByFileName(m_pages[0].Path));
    }
    QString currentPageName() { return m_pages.isEmpty() ? "" : m_pages[0].Path; }

    /**
     * @brief currentPageNumAsString: for the label text on PageBar
     * @return (10-11/2182)
     *      or (10/2182)
     */
    QString currentPageNumAsString() const;
    /**
     * @brief currentPageStatusAsString: for statusbar
     * @return some1.jpg (10-11/2182)[WIDTHxHEIGHT] | some2.jpg [WIDTHxHEIGHT]
     *      or some1.jpg (10/2182)[WIDTHxHEIGHT]
     */
    QString currentPageStatusAsString() const;
    QString pageSignage(int page) const;

    QString volumePath() override { return m_fileVolume ? m_fileVolume->volumePath() : ""; }
    QString realVolumePath() { return m_fileVolume ? m_fileVolume->realVolumePath() : ""; }
    bool isArchive() {
        if(!m_fileVolume) return false;
        return m_fileVolume->isArchive();
    }
    bool isFolder() {
        if(!m_fileVolume) return false;
        return !m_fileVolume->isArchive();
    }

    int size() override { return m_fileVolume ? m_fileVolume->size() : 0; }
    bool canDualView() const;
    void dispose() {
//        if(m_fileVolume && m_volumes.empty()) {
//            delete m_fileVolume;
//            m_pages.resize(0);
//        }
        m_pages.resize(0);
        m_fileVolume = nullptr;
        m_volumes.clear();
        m_watchedVolume.clear();
        m_watchTimer.stop();
        m_scanPending = false;
        setWatchedDirectories(QStringList());
    }
    QStringList enumVolumes(QDir dir);

signals:
    /**
     * @brief pagesNolongerNeeded pages is no longer needed. page will be cleared
     */
    void pagesNolongerNeeded();

    void readyForPaint();
    /**
     * @brief pageChanged pages have been changed
     */
    void pageChanged();
    /**
     * @brief volumeChanged the volume has been changed
     */
    void volumeChanged(QString path);
    /**
     * @brief addNewPage add a new page. if it is dual view, 2 times called
     * @param pageNum
     */
    void pageAdded(ImageContent ic, bool pageNext);

public slots:
    void on_pageEnumerated();
    void onSlideShowStarted();
    void onSlideShowStopped();
    /**
     * @brief updateWatcher watches the folder of the current volume to follow added/removed images
     */
    void updateWatcher();

private slots:
    void onWatcher_directoryChanged(const QString& path);
    void onWatchTimer_timeout();

private:
    VolumeManager* addVolumeCache(QString path, bool onlyCover, bool immediate);
    VolumeManager* createVolume(QString path, bool onlyCover);
    void setWatchedDirectories(const QStringList& dirs);
    void onFolderScanned(const FolderScan& scan);
    /**
     * @brief applyFolderChanges updates the current volume with the images of the folder now.
     * The cached images and the displayed pages are kept when possible
     * @param invalid images to be read again (their files changed)
     */
    void applyFolderChanges(const FolderScan& scan, const QStringList& invalid);
    void onVolumeBuiltWithFile(VolumeManager* newer, QString pathbase, QString qpath, int generation);
    /**
     * @brief younger page number
     */
    int m_currentPage;

    bool m_wideImage;
    bool m_prohibit2Pages;
    QVector<ImageContent> m_pages;
    TimeOrderdCacheFuturePtr<QString, VolumeManager> m_volumes;
    QStringList m_volumenames;

    VolumeManager* m_fileVolume;
    ImageView * m_imaveView;

    bool m_waitForReloaded;
    int m_loadGeneration;

    // live update of the folder being viewed
    QFileSystemWatcher m_watcher;
    QTimer m_watchTimer;
    QString m_watchedVolume;
    // the folder is listed on its own thread, not to block the page turns
    QThreadPool m_scanPool;
    bool m_scanRunning;
    bool m_scanPending;
    FileStamps m_scanStamps;
    // images added recently, and how many times more they may be read again if they could not be decoded
    QHash<QString, int> m_freshImages;

//    VolumeManagerBuilder m_builderForAssoc;
};

#endif // PAGEMANAGER_H
