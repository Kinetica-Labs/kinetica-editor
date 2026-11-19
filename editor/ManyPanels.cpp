// The "Группа" card of the inspector (see ManyPanels.h).
#include "ManyPanels.h"
#include "RuPlural.h"

#include "Icons.h"
#include "InspectorWidgets.h"

#include <QBoxLayout>
#include <QLabel>
#include <QPushButton>

using namespace ke;

// ---------------------------------------------------------------------------
// Группа
// ---------------------------------------------------------------------------
GroupPanel::GroupPanel(QWidget* parent) : QWidget(parent) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    members_ = new QLabel;
    members_->setObjectName("roleRowLabel");
    col->addWidget(members_);
    glue_ = new QPushButton("Склеить в одно тело");
    glue_->setObjectName("glueButton");
    glue_->setCheckable(true);
    glue_->setToolTip("Твёрдые части группы станут одним телом: падают и катятся вместе, как склеенные");
    connect(glue_, &QPushButton::toggled, this, &GroupPanel::gluedToggled);
    auto* ungroup = new QPushButton("Разгруппировать");
    ungroup->setToolTip("Ctrl+Shift+G: объекты останутся на своих местах, группа исчезнет");
    connect(ungroup, &QPushButton::clicked, this, &GroupPanel::ungroupClicked);
    auto* row = new QHBoxLayout;
    row->addWidget(glue_);
    row->addWidget(ungroup);
    col->addLayout(row);
}

void GroupPanel::setGroup(const Group& g, int members) {
    const QSignalBlocker quiet(glue_);
    members_->setText("Внутри " + ruPlural(members, "объект", "объекта", "объектов") +
                      ". Щелчок по объекту в сцене выбирает группу, ещё щелчок — сам объект.");
    members_->setWordWrap(true);
    glue_->setChecked(g.glued);
    glue_->setText(g.glued ? "Склеено в одно тело" : "Склеить в одно тело");
}
