// One image choice/drop assigns appearance; advanced path and scale stay optional.
#include "TexturePanel.h"
#include "UiLanguage.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QPainter>
#include <QDateTime>
#include <algorithm>

namespace {
bool validPath(const QString& path) {
    return !path.contains('"') && !path.contains('\n') && !path.contains('\r');
}
QImage thumbnail(const QString& path) {
    QImageReader reader(path); reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 64000000) return {};
    reader.setScaledSize(size.scaled(96, 64, Qt::KeepAspectRatio));
    return reader.read();
}
}
TexturePanel::TexturePanel(QWidget* parent) : QWidget(parent) {
    setAcceptDrops(true);
    auto* col = new QVBoxLayout(this); col->setContentsMargins(0, 0, 0, 0); col->setSpacing(6);
    auto* row = new QHBoxLayout;
    preview_ = new QLabel; preview_->setFixedSize(64, 48); preview_->setAlignment(Qt::AlignCenter);
    preview_->setObjectName("texturePreview");
    auto* choose = new QPushButton(uiText("Kinetica", "Выбрать изображение…")); choose->setObjectName("textureChoose");
    row->addWidget(preview_); row->addWidget(choose, 1); col->addLayout(row);
    kind_ = new QComboBox; kind_->setObjectName("textureKind");
    kind_->addItems({uiText("Kinetica", "Без текстуры"), uiText("Kinetica", "Шахматная"), uiText("Kinetica", "Изображение"), uiText("Kinetica", "Крапинка (для стерео)"), uiText("Kinetica", "Камень (гранит)")}); col->addWidget(kind_);
    status_ = new QLabel; status_->setWordWrap(true); col->addWidget(status_);
    auto* advanced = new QToolButton; advanced->setText(uiText("Kinetica", "Масштаб и путь"));
    advanced->setCheckable(true); advanced->setArrowType(Qt::RightArrow);
    advanced->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* details = new QWidget; auto* form = new QFormLayout(details); form->setContentsMargins(0, 0, 0, 0);
    repeats_ = new QDoubleSpinBox; repeats_->setObjectName("textureRepeats");
    repeats_->setRange(0.001, 10000); repeats_->setDecimals(3); repeats_->setValue(8); repeats_->setSuffix(uiText("Kinetica", " / м"));
    repeats_->setToolTip(uiText("Kinetica", "Число повторений рисунка на метр; две шахматные клетки составляют один период."));
    file_ = new QLineEdit; file_->setObjectName("textureFile");
    auto* fit = new QPushButton(uiText("Kinetica", "По размеру объекта"));
    form->addRow(uiText("Kinetica", "Повторения"), repeats_); form->addRow(fit); form->addRow(uiText("Kinetica", "Файл"), file_);
    buildBumpControls(col, form);
    col->addWidget(advanced); col->addWidget(details); details->hide();
    connect(advanced, &QToolButton::toggled, this, [advanced, details](bool on) {
        details->setVisible(on); advanced->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    });
    auto changed = [this] { refresh(); if (!filling_) emit edited(); };
    connect(kind_, &QComboBox::currentIndexChanged, this, changed);
    connect(repeats_, &QDoubleSpinBox::valueChanged, this, changed);
    connect(file_, &QLineEdit::editingFinished, this, changed);
    connect(fit, &QPushButton::clicked, this, [this] { repeats_->setValue(fitRepeats_); });
    connect(choose, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, uiText("Kinetica", "Текстура поверхности"), directory_,
            uiText("Kinetica", "Изображения (*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff);;Все файлы (*)"));
        if (!file.isEmpty()) assignImage(file);
    });
    refresh();
}
void TexturePanel::refresh() {
    repeats_->setEnabled(kind_->currentIndex() != 0 || bump_->isChecked()); file_->setEnabled(kind_->currentIndex() == 2);
    if (kind_->currentIndex() == 2) {
        const QString path = QDir(directory_).filePath(file_->text());
        const QFileInfo info(path);
        const QString key = path + ':' + QString::number(info.lastModified().toMSecsSinceEpoch());
        if (previewKey_ != key) { previewImage_ = validPath(path) ? thumbnail(path) : QImage(); previewKey_ = key; }
        preview_->setPixmap(QPixmap::fromImage(previewImage_).scaled(64, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        preview_->setToolTip(path);
        status_->setText(previewImage_.isNull() ? uiText("Kinetica", "Файл не читается. Выберите изображение до 64 Мп.") : info.fileName());
        status_->setToolTip(path);
        return;
    }
    previewKey_.clear(); previewImage_ = {};
    QPixmap icon(64, 48); icon.fill(palette().base().color());
    if (kind_->currentIndex() == 1 || kind_->currentIndex() == 3) {
        QPainter painter(&icon);
        for (int y = 0; y < 12; ++y) for (int x = 0; x < 16; ++x) { // the checker's squares, or the speckle's random greys
            uint32_t h = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^ 0x9e3779b9u;
            h ^= h >> 16, h *= 0x7feb352du, h ^= h >> 15, h *= 0x846ca68bu, h ^= h >> 16;
            const int grey = kind_->currentIndex() == 3 ? 20 + int((h >> 8) % 220) : ((x / 2 + y / 2) % 2 ? 240 : 24);
            painter.fillRect(x * 4, y * 4, 4, 4, QColor(grey, grey, grey));
        }
    }
    preview_->setPixmap(icon); preview_->setToolTip(uiText("Kinetica", "Перетащите сюда своё изображение")); status_->setToolTip({});
    const char* said[] = {uiText("Kinetica", "Перетащите PNG/JPEG сюда или на объект."), uiText("Kinetica", "Встроенный шахматный рисунок."), "",
                          uiText("Kinetica", "Встроенная крапинка: серые клетки без периода — по ней стереокамера находит глубину."),
                          uiText("Kinetica", "Встроенный гранит: шум в четыре октавы и вкрапления — выглядит как камень, стерео по нему работает.")};
    status_->setText(said[kind_->currentIndex()]);
}
void TexturePanel::setTexture(const ke::SurfaceTexture& value, const QString& directory, ke::Vector3 size) {
    filling_ = true; directory_ = directory;
    bump_->setChecked(value.bumpHeight > 0);
    bumpHeight_->setValue(value.bumpHeight > 0 ? value.bumpHeight * 1000 : 1);
    bumpHeight_->setEnabled(value.bumpHeight > 0);
    heightFile_->setText(QString::fromStdString(value.heightFile));
    normalFile_->setText(QString::fromStdString(value.normalFile));
    fitRepeats_ = 1.0 / std::max(0.0001f, ke::maxComp(size));
    kind_->setCurrentIndex(int(value.kind)); repeats_->setValue(value.repeats);
    file_->setText(QString::fromStdString(value.file)); refresh(); filling_ = false;
}
bool TexturePanel::assignImage(const QString& file) {
    if (!validPath(file) || thumbnail(file).isNull()) {
        status_->setText(uiText("Kinetica", "Файл не читается. Выберите изображение до 64 Мп без кавычек в пути.")); return false;
    }
    filling_ = true;
    file_->setText(file); kind_->setCurrentIndex(2); repeats_->setValue(fitRepeats_);
    refresh(); filling_ = false; emit edited(); return true;
}
void TexturePanel::writeTo(ke::SurfaceTexture& value) const {
    value.kind = ke::TextureKind(kind_->currentIndex()); value.repeats = float(repeats_->value());
    if (validPath(file_->text())) value.file = file_->text().toStdString();
    value.bumpHeight = bump_->isChecked() ? float(bumpHeight_->value() * 0.001) : 0;
    if (validPath(heightFile_->text())) value.heightFile = heightFile_->text().toStdString();
    if (validPath(normalFile_->text())) value.normalFile = normalFile_->text().toStdString();
}
void TexturePanel::dragEnterEvent(QDragEnterEvent* e) {
    const auto urls = e->mimeData()->urls();
    if (urls.size() == 1 && urls.front().isLocalFile()) { e->setDropAction(Qt::CopyAction); e->accept(); }
}
void TexturePanel::dropEvent(QDropEvent* e) {
    const auto urls = e->mimeData()->urls();
    if (urls.size() == 1 && urls.front().isLocalFile() && assignImage(urls.front().toLocalFile())) { e->setDropAction(Qt::CopyAction); e->accept(); }
}
