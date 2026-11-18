// Bump from the existing image by default; an optional grayscale height image gives control.
#include "TexturePanel.h"
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
    bump_ = new QCheckBox("Рельеф (bump)"); bump_->setObjectName("textureBump");
    bump_->setToolTip("Визуальная выпуклость из яркости текстуры. Геометрия и столкновения остаются прежними.");
    bumpHeight_ = new QDoubleSpinBox; bumpHeight_->setObjectName("textureBumpHeight");
    bumpHeight_->setRange(0.01, 50); bumpHeight_->setDecimals(2); bumpHeight_->setValue(1);
    bumpHeight_->setSuffix(" мм"); bumpHeight_->setEnabled(false);
    bumpHeight_->setToolTip("Кажущаяся высота рельефа: белое выше чёрного. Начните с 1 мм.");
    row->addWidget(bump_); row->addWidget(bumpHeight_); col->addLayout(row);
    heightFile_ = new QLineEdit; heightFile_->setObjectName("textureHeightFile");
    heightFile_->setPlaceholderText("Из яркости текущей текстуры");
    auto* map = new QHBoxLayout; map->addWidget(heightFile_);
    auto* choose = new QPushButton("Выбрать…"); choose->setObjectName("textureHeightChoose"); map->addWidget(choose);
    form->addRow("Карта высот", map);
    connect(bump_, &QCheckBox::toggled, this, [this](bool on) {
        bumpHeight_->setEnabled(on); refresh();
        if (!filling_) emit edited();
    });
    connect(bumpHeight_, &QDoubleSpinBox::valueChanged, this, [this] { if (!filling_) emit edited(); });
    connect(heightFile_, &QLineEdit::editingFinished, this, [this] { if (!filling_) emit edited(); });
    connect(choose, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, "Карта высот: белое выше, чёрное ниже", directory_,
            "Изображения (*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff);;Все файлы (*)");
        if (file.isEmpty()) return;
        filling_ = true; heightFile_->setText(file); bump_->setChecked(true); filling_ = false; emit edited();
    });
}
