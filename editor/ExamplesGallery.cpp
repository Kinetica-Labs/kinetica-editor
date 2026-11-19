// The gallery of examples (see ExamplesGallery.h): tiles, the kind filter, and the pictures made in
// the background by the editor's own program.
#include "ExamplesGallery.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const QSize kThumb(240, 150);
const char* kEditorCategory = "Сцены редактора";

// The names of the editor's own scenes; another file is shown by its file name.
QString sceneTitle(const QString& baseName) {
    if (baseName == "robot-arm") return "Рука-манипулятор";
    if (baseName == "ur5e") return "UR5e: цель ИК, схват, кубики";
    if (baseName == "textures") return "Текстуры";
    return baseName;
}

QIcon placeholder(const QString& title) {
    QPixmap pm(kThumb);
    pm.fill(QColor(44, 48, 56));
    QPainter p(&pm);
    p.setPen(QColor(120, 128, 140));
    p.drawText(pm.rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap, title + "\n\nкартинка готовится…");
    return QIcon(pm);
}

} // namespace

ExamplesGallery::ExamplesGallery(const QString& examplesDir, QWidget* parent) : QDialog(parent) {
    setWindowTitle("Примеры");
    resize(1100, 720);
    auto* col = new QVBoxLayout(this);
    auto* hint = new QLabel("Нажмите на картинку — сцена откроется. Сцены редактора можно менять и сохранять.");
    hint->setObjectName("galleryHint");
    col->addWidget(hint);
    tabs_ = new QTabBar;
    tabs_->setObjectName("galleryTabs");
    col->addWidget(tabs_);
    list_ = new QListWidget;
    list_->setObjectName("galleryList");
    list_->setViewMode(QListView::IconMode);
    list_->setIconSize(kThumb);
    list_->setGridSize(QSize(kThumb.width() + 24, kThumb.height() + 44));
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setWordWrap(true);
    list_->setUniformItemSizes(true);
    col->addWidget(list_, 1);
    collect(examplesDir);
    buildTiles();
    connect(tabs_, &QTabBar::currentChanged, this, &ExamplesGallery::filter);
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        chosen_ = item->data(Qt::UserRole).toInt();
        accept();
    });
    watchdog_ = new QTimer(this);
    watchdog_->setSingleShot(true);
    connect(watchdog_, &QTimer::timeout, this, [this] { finishRunning(false); });
    startNext();
}

ExamplesGallery::~ExamplesGallery() {
    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->disconnect(this);
        process_->kill();
        process_->waitForFinished(2000);
    }
}

QString ExamplesGallery::thumbnailDir() {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbnails";
}

// The scenes by name, the first start's scene at the very front.
void ExamplesGallery::collect(const QString& examplesDir) {
    QStringList files = QDir(examplesDir).entryList({"*.kscene"}, QDir::Files, QDir::Name);
    files.sort();
    if (files.removeOne("ur5e.kscene")) files.prepend("ur5e.kscene");
    categories_ << "Все" << kEditorCategory;
    for (const QString& f : files) examples_.push_back({sceneTitle(QFileInfo(f).completeBaseName()), kEditorCategory, examplesDir + "/" + f});
}

void ExamplesGallery::buildTiles() {
    for (const QString& c : categories_) tabs_->addTab(c);
    for (size_t i = 0; i < examples_.size(); ++i) {
        const Example& e = examples_[i];
        const QString picture = thumbnailPath(e);
        auto* item = new QListWidgetItem(QFileInfo::exists(picture) ? QIcon(picture) : placeholder(e.title), e.title, list_);
        item->setData(Qt::UserRole, int(i));
        item->setToolTip(e.category + " — можно менять и сохранять");
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        items_.push_back(item);
        if (!QFileInfo::exists(picture)) queue_.push_back(int(i));
    }
}

void ExamplesGallery::filter(int tab) {
    const QString category = tab > 0 ? categories_.value(tab) : QString();
    for (size_t i = 0; i < items_.size(); ++i) items_[i]->setHidden(!category.isEmpty() && examples_[i].category != category);
}

// One file per example; "v1" changes when the pictures are made differently.
QString ExamplesGallery::thumbnailPath(const Example& e) const {
    const QFileInfo info(e.sceneFile);
    return thumbnailDir() + QString("/scene-%1-%2-v1.png").arg(info.completeBaseName()).arg(info.lastModified().toSecsSinceEpoch());
}

// A scene as it was authored.
QStringList ExamplesGallery::thumbnailArguments(const Example& e, const QString& out) const {
    return {"--thumbnail", "--size", "480x300", "--screenshot", out, "--scene", e.sceneFile, "--edit"};
}

void ExamplesGallery::startNext() {
    if (running_ >= 0) return;
    if (queue_.empty()) {
        emit thumbnailsDone();
        return;
    }
    running_ = queue_.front();
    queue_.pop_front();
    QDir().mkpath(thumbnailDir());
    process_ = new QProcess(this);
    connect(process_, &QProcess::finished, this, [this](int code) { finishRunning(code == 0); });
    process_->start(QCoreApplication::applicationFilePath(), thumbnailArguments(examples_[size_t(running_)],
                                                                                thumbnailPath(examples_[size_t(running_)])));
    watchdog_->start(90000); // a scene that takes longer keeps its placeholder
}

void ExamplesGallery::finishRunning(bool saved) {
    if (running_ < 0) return;
    watchdog_->stop();
    const QString picture = thumbnailPath(examples_[size_t(running_)]);
    if (saved && QFileInfo::exists(picture)) items_[size_t(running_)]->setIcon(QIcon(picture));
    if (process_) {
        process_->disconnect(this);
        if (process_->state() != QProcess::NotRunning) process_->kill();
        process_->deleteLater();
        process_ = nullptr;
    }
    running_ = -1;
    startNext();
}
