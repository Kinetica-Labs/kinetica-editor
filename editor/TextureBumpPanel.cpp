// Bump from the existing image by default; an optional grayscale height image gives control. A normal map - the
// surface's normals as a picture, the way ready texture sets bring their relief - has its own row.
#include "TexturePanel.h"
#include "UiLanguage.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

void TexturePanel::buildBumpControls(QVBoxLayout* col, QFormLayout* form) {
    auto* row = new QHBoxLayout;
    bump_ = new QCheckBox(uiText("Kinetica", "Рельеф (bump)")); bump_->setObjectName("textureBump");
    bump_->setToolTip(uiText("Kinetica", "Визуальная выпуклость из яркости текстуры. Геометрия и столкновения остаются прежними."));
    bumpHeight_ = new QDoubleSpinBox; bumpHeight_->setObjectName("textureBumpHeight");
    bumpHeight_->setRange(0.01, 50); bumpHeight_->setDecimals(2); bumpHeight_->setValue(1);
    bumpHeight_->setSuffix(uiText("Kinetica", " мм")); bumpHeight_->setEnabled(false);
    bumpHeight_->setToolTip(uiText("Kinetica", "Кажущаяся высота рельефа: белое выше чёрного. Начните с 1 мм."));
    row->addWidget(bump_); row->addWidget(bumpHeight_); col->addLayout(row);
    heightFile_ = new QLineEdit; heightFile_->setObjectName("textureHeightFile");
    heightFile_->setPlaceholderText(uiText("Kinetica", "Из яркости текущей текстуры"));
    auto* map = new QHBoxLayout; map->addWidget(heightFile_);
    auto* choose = new QPushButton(uiText("Kinetica", "Выбрать…")); choose->setObjectName("textureHeightChoose"); map->addWidget(choose);
    form->addRow(uiText("Kinetica", "Карта высот"), map);
    connect(bump_, &QCheckBox::toggled, this, [this](bool on) {
        bumpHeight_->setEnabled(on); refresh();
        if (!filling_) emit edited();
    });
    connect(bumpHeight_, &QDoubleSpinBox::valueChanged, this, [this] { if (!filling_) emit edited(); });
    connect(heightFile_, &QLineEdit::editingFinished, this, [this] { if (!filling_) emit edited(); });
    connect(choose, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, uiText("Kinetica", "Карта высот: белое выше, чёрное ниже"), directory_,
            uiText("Kinetica", "Изображения (*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff);;Все файлы (*)"));
        if (file.isEmpty()) return;
        filling_ = true; heightFile_->setText(file); bump_->setChecked(true); filling_ = false; emit edited();
    });
    normalFile_ = new QLineEdit; normalFile_->setObjectName("textureNormalFile");
    normalFile_->setPlaceholderText(uiText("Kinetica", "Нет"));
    normalFile_->setToolTip(uiText("Kinetica", "Карта нормалей (normal map): сиреневая картинка из набора текстур, зелёный канал — вверх (OpenGL). Только освещение: геометрия прежняя."));
    auto* normals = new QHBoxLayout; normals->addWidget(normalFile_);
    auto* pick = new QPushButton(uiText("Kinetica", "Выбрать…")); pick->setObjectName("textureNormalChoose"); normals->addWidget(pick);
    form->addRow(uiText("Kinetica", "Карта нормалей"), normals);
    connect(normalFile_, &QLineEdit::editingFinished, this, [this] { if (!filling_) emit edited(); });
    connect(pick, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, uiText("Kinetica", "Карта нормалей: зелёный канал — вверх"), directory_,
            uiText("Kinetica", "Изображения (*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff);;Все файлы (*)"));
        if (file.isEmpty()) return;
        filling_ = true; normalFile_->setText(file); filling_ = false; emit edited();
    });
}
