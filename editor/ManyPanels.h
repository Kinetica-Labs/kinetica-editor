#pragma once
// The inspector's cards for many objects at once (see SceneBuilder.h):
//   "Группа" - how many things it holds, [Склеить в одно тело] (its rigid members become one body,
//              ke::Group::glued) and [Разгруппировать];
//   "Массив" - the pattern as three icon buttons (Ряд, Сетка, Круг), how many copies, the step or
//              the spacing, the circle's radius, the turn of each next copy, the random scatter, and
//              [Разобрать на объекты]. Only the fields of the chosen pattern are shown.
#include "scene/SceneGraph.h"

#include <QWidget>

class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QSpinBox;
class QToolButton;
class Vec3Row;

class GroupPanel : public QWidget {
    Q_OBJECT
public:
    explicit GroupPanel(QWidget* parent = nullptr);
    void setGroup(const ke::Group& g, int members); // fills; not an edit

signals:
    void gluedToggled(bool glued);
    void ungroupClicked();

private:
    QLabel* members_ = nullptr;
    QPushButton* glue_ = nullptr;
};

class ArrayPanel : public QWidget {
    Q_OBJECT
public:
    explicit ArrayPanel(QWidget* parent = nullptr);
    void setArray(const ke::ArrayObject& a, const QString& templateName); // fills; not an edit
    void writeTo(ke::ArrayObject& a) const; // the fields into the array (its object part stays)

signals:
    void edited();
    void explodeClicked();

private:
    void choosePattern(ke::ArrayPattern p);
    void showPatternRows();

    ke::ArrayPattern pattern_ = ke::ArrayPattern::Line;
    QToolButton* patterns_[3] = {};
    QFormLayout* form_ = nullptr;
    QSpinBox* counts_[3] = {};
    QWidget* countRow_ = nullptr;
    Vec3Row* step_ = nullptr;
    QDoubleSpinBox* radius_ = nullptr;
    Vec3Row* turn_ = nullptr;
    QDoubleSpinBox* jitter_ = nullptr;
    QLabel* template_ = nullptr;
};
