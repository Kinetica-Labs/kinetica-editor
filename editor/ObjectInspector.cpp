// The "Объект" block (see ObjectInspector.h): one row with the eye, the lock, the name and the
// colour swatch, then position and rotation.
#include "ObjectInspector.h"
#include "UiLanguage.h"

#include "Icons.h"
#include "InspectorWidgets.h"

#include <QBoxLayout>
#include <algorithm>
#include <QColorDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>

namespace {

QToolButton* toggle(const QString& tip) {
    auto* b = new QToolButton;
    b->setCheckable(true);
    b->setAutoRaise(true);
    b->setIconSize(QSize(20, 20));
    b->setToolTip(tip);
    return b;
}

QString swatchStyle(const QColor& c) {
    return QString("QPushButton { background: %1; border: 1px solid #555b66; border-radius: 4px; min-width: 28px; }").arg(c.name());
}

} // namespace

ObjectInspector::ObjectInspector(QWidget* parent) : QWidget(parent) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(8);
    col->addWidget(sectionTitle(uiText("Kinetica", "Объект")));

    auto* top = new QHBoxLayout;
    top->setSpacing(4);
    visible_ = toggle(uiText("Kinetica", "Видимый: скрытое не участвует в расчёте"));
    locked_ = toggle(uiText("Kinetica", "Замок: мышь не выделяет и не двигает (пол, фон)"));
    name_ = new QLineEdit;
    name_->setPlaceholderText(uiText("Kinetica", "имя"));
    colour_ = new QPushButton;
    colour_->setToolTip(uiText("Kinetica", "Цвет"));
    top->addWidget(visible_);
    top->addWidget(locked_);
    top->addWidget(name_, 1);
    top->addWidget(colour_);
    col->addLayout(top);

    auto* form = form_ = new QFormLayout;
    form->setSpacing(6);
    position_ = new Vec3Row(-50, 50, 0.05, 3); // millimetres: a 1 cm gap is 0.045, not 0.04
    rotation_ = new Vec3Row(-360, 360, 5, 0);
    form->addRow(uiText("Kinetica", "Где, м"), position_);
    form->addRow(uiText("Kinetica", "Поворот, °"), rotation_);
    tags_ = new QLineEdit;
    tags_->setObjectName("objectTags");
    tags_->setPlaceholderText(uiText("Kinetica", "слова через пробел: мишень, не-трогать"));
    tags_->setToolTip(uiText("Kinetica", "Метки: по ним объект находят скрипты и плагины (scene_list {tag})"));
    form->addRow(uiText("Kinetica", "Метки"), tags_);
    col->addLayout(form);

    connect(name_, &QLineEdit::textEdited, this, &ObjectInspector::edited);
    connect(tags_, &QLineEdit::editingFinished, this, [this] { // a word half typed is no tag yet
        if (tags_->isModified()) tags_->setModified(false), emit edited();
    });
    connect(position_, &Vec3Row::edited, this, &ObjectInspector::edited);
    connect(rotation_, &Vec3Row::edited, this, &ObjectInspector::edited);
    connect(colour_, &QPushButton::clicked, this, &ObjectInspector::pickColour);
    for (QToolButton* b : {visible_, locked_})
        connect(b, &QToolButton::toggled, this, [this] {
            updateToggleIcons();
            emit edited();
        });
}

void ObjectInspector::setObject(const ke::SceneObject& o) {
    const QSignalBlocker a(visible_), b(locked_);
    name_->setText(QString::fromStdString(o.name));
    name_->setPlaceholderText(uiText("Kinetica", "имя"));
    visible_->setChecked(o.visible);
    locked_->setChecked(o.locked);
    colourValue_ = QColor::fromRgbF(o.color.x, o.color.y, o.color.z);
    colour_->setStyleSheet(swatchStyle(colourValue_));
    position_->setValue(o.position);
    rotation_->setValue(o.rotationDeg);
    QStringList tags;
    for (const std::string& t : o.tags) tags << QString::fromStdString(t);
    tags_->setText(tags.join(' '));
    tags_->setModified(false);
    updateToggleIcons();
}

void ObjectInspector::showPlace(bool shown) {
    form_->setRowVisible(position_, shown);
    form_->setRowVisible(rotation_, shown);
}

void ObjectInspector::writeTo(ke::SceneObject& o) const {
    o.name = name_->text().toStdString();
    o.visible = visible_->isChecked();
    o.locked = locked_->isChecked();
    o.color = ke::Vector3(float(colourValue_.redF()), float(colourValue_.greenF()), float(colourValue_.blueF()));
    o.position = position_->value();
    o.rotationDeg = rotation_->value();
    o.tags.clear();
    for (const QString& word : tags_->text().split(' ', Qt::SkipEmptyParts)) { // what is no tag - a quote in it - is left out
        const std::string tag = word.toStdString();
        if (ke::tagWord(tag) && std::find(o.tags.begin(), o.tags.end(), tag) == o.tags.end()) o.tags.push_back(tag);
    }
}

void ObjectInspector::showMixed(bool name, int positionMask, int rotationMask) {
    if (name) {
        name_->clear();
        name_->setPlaceholderText(uiText("Kinetica", "— разные имена —"));
    }
    for (int k = 0; k < 3; ++k) {
        if (positionMask & (1 << k)) ::showMixed(position_->spin(k));
        if (rotationMask & (1 << k)) ::showMixed(rotation_->spin(k));
    }
}

void ObjectInspector::pickColour() {
    const QColor c = QColorDialog::getColor(colourValue_, this, uiText("Kinetica", "Цвет"));
    if (!c.isValid()) return;
    colourValue_ = c;
    colour_->setStyleSheet(swatchStyle(c));
    emit edited();
}

void ObjectInspector::updateToggleIcons() {
    visible_->setIcon(controlIcon(visible_->isChecked() ? ControlIcon::Visible : ControlIcon::Hidden, 20));
    locked_->setIcon(controlIcon(locked_->isChecked() ? ControlIcon::Locked : ControlIcon::Unlocked, 20));
}
