// The role bar (see RoleBar.h): the picture buttons of the rigid body and its collider in one row,
// and the helpers that read and set a component on an entity.
#include "RoleBar.h"

#include "scene/SceneActions.h"

#include <QBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QToolButton>

namespace {

const char* kTips[] = {
    "Твёрдое тело: не гнётся; падает, сталкивается, катится (сталкивается своим коллайдером)",
    "Коллайдер: чем форма сталкивается. Один — неподвижное препятствие; вместе с «Твёрдым» — движущееся тело",
};

QToolButton* roleButton(RoleIcon role) {
    auto* b = new QToolButton;
    b->setCheckable(true);
    b->setIcon(roleIcon(role, 36));
    b->setIconSize(QSize(36, 36));
    b->setText(roleName(role));
    b->setToolTip(roleTip(role));
    b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    b->setObjectName("roleButton");
    return b;
}

} // namespace

QString roleName(RoleIcon role) {
    const char* names[] = {"Твёрдое", "Коллайдер"};
    return names[int(role)];
}

QString roleTitle(RoleIcon role) {
    const char* titles[] = {"Твёрдое тело", "Коллайдер"};
    return titles[int(role)];
}

QString roleTip(RoleIcon role) { return kTips[int(role)]; }

// The buttons are the core's roles, in the same order (ke::Role).
bool roleEnabled(const ke::Entity& e, RoleIcon role) { return ke::hasRole(e, ke::Role(int(role))); }

RoleBar::RoleBar(QWidget* parent) : QWidget(parent) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(4);
    auto row = [this, col](const QString& title, std::initializer_list<RoleIcon> roles) {
        auto* label = new QLabel(title);
        label->setObjectName("roleRowLabel");
        col->addWidget(label);
        auto* line = new QHBoxLayout;
        line->setSpacing(4);
        for (RoleIcon role : roles) {
            QToolButton* b = roleButton(role);
            connect(b, &QToolButton::clicked, this, [this, role] { emit roleClicked(role); });
            buttons_[int(role)] = b;
            line->addWidget(b);
        }
        col->addLayout(line);
    };
    row("тело и чем сталкивается", {RoleIcon::Rigid, RoleIcon::Collider});
}

void RoleBar::setRoles(const ke::Entity& e) {
    for (int k = 0; k < int(RoleIcon::Count); ++k) {
        const QSignalBlocker quiet(buttons_[k]);
        buttons_[k]->setChecked(roleEnabled(e, RoleIcon(k)));
    }
}
