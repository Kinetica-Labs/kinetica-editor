#pragma once
// The role bar: the quick palette of components at the top of the "Объект" tab - big picture
// buttons that say what the selected shape is: a rigid body and its collider (a collider alone: a
// fixed obstacle; neither: the shape is only drawn). One click adds or removes a component; the same
// components are listed below as cards, and "+ Добавить компонент" offers them too.
#include "Icons.h"

#include <QWidget>

class QToolButton;

class RoleBar : public QWidget {
    Q_OBJECT
public:
    explicit RoleBar(QWidget* parent = nullptr);
    void setRoles(const ke::Entity& e); // lights the buttons of the entity's roles; not a click

signals:
    void roleClicked(RoleIcon role);

private:
    QToolButton* buttons_[int(RoleIcon::Count)] = {};
};

bool roleEnabled(const ke::Entity& e, RoleIcon role);
QString roleName(RoleIcon role);           // "Твёрдое", "Коллайдер"
QString roleTitle(RoleIcon role);          // the card's title: "Твёрдое тело", "Коллайдер" ...
QString roleTip(RoleIcon role);            // one sentence: what the component does
