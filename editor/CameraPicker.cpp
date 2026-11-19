// The "Смотрю через" list in the corner of the 3D view (see CameraPicker.h).
#include "CameraPicker.h"

#include "Icons.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>

// Where the list sits in the view: under the two lines of the scene's name and caption.
static constexpr int kLeft = 10, kTop = 52;

CameraPicker::CameraPicker(QWidget* view) : QObject(view) {
    // 1. A rounded, half-transparent frame on top of the view, like the big ▶ ⏸ ■ buttons.
    frame_ = new QFrame(view);
    frame_->setObjectName("cameraPicker");
    auto* row = new QHBoxLayout(frame_);
    row->setContentsMargins(10, 4, 6, 4);
    row->setSpacing(8);

    // 2. The words, and the list itself.
    auto* label = new QLabel("Смотрю через:");
    row->addWidget(label);
    list_ = new QComboBox;
    list_->setObjectName("cameraPickerList");
    list_->setToolTip("Через чей глаз показан вид: свободная камера редактора или одна из камер сцены. "
                      "Esc — назад к виду редактора");
    list_->setCursor(Qt::PointingHandCursor);
    list_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    row->addWidget(list_);

    // 3. A pick from the user (not a refill from the scene) asks to look through that camera.
    connect(list_, &QComboBox::activated, this, [this](int index) {
        if (!filling_) emit chosen(list_->itemData(index).toUInt());
    });

    frame_->move(kLeft, kTop);
    showCameras({}, 0);
}

void CameraPicker::showCameras(const std::vector<Choice>& cameras, uint32_t current) {
    filling_ = true;
    // 1. Refill the list only when the scene's cameras changed (a move of the view does not).
    if (cameras != shown_ || list_->count() == 0) {
        list_->clear();
        list_->addItem("Вид редактора", 0u);
        for (const Choice& c : cameras) list_->addItem(sceneIcon(SceneIcon::Camera, 16), c.name, c.cameraId);
        shown_ = cameras;
    }
    // 2. Point at the camera the view looks through now.
    const int at = list_->findData(current);
    list_->setCurrentIndex(at >= 0 ? at : 0);
    frame_->setVisible(!cameras.empty() || current != 0); // nothing to look through: not in the way
    frame_->adjustSize();
    filling_ = false;
}
