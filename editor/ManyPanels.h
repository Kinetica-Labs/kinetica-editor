#pragma once
// The inspector's card of a group (see SceneBuilder.h): how many things it holds, [Склеить в одно тело] (its rigid
// members become one body, ke::Group::glued) and [Разгруппировать]. The card of an array is a FieldForm
// (SceneBuilder::arrayFields, SceneBuilderPanels.cpp).
#include "scene/SceneGraph.h"

#include <QWidget>

class QLabel;
class QPushButton;

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
