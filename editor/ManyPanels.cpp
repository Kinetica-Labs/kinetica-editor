// The "Группа" card of the inspector (see ManyPanels.h).
#include "ManyPanels.h"
#include "UiLanguage.h"

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
    glue_ = new QPushButton(uiText("Kinetica", "Склеить в одно тело"));
    glue_->setObjectName("glueButton");
    glue_->setCheckable(true);
    glue_->setToolTip(uiText("Kinetica", "Твёрдые части группы станут одним телом: падают и катятся вместе, как склеенные"));
    connect(glue_, &QPushButton::toggled, this, &GroupPanel::gluedToggled);
    auto* ungroup = new QPushButton(uiText("Kinetica", "Разгруппировать"));
    ungroup->setToolTip(uiText("Kinetica", "Ctrl+Shift+G: объекты останутся на своих местах, группа исчезнет"));
    connect(ungroup, &QPushButton::clicked, this, &GroupPanel::ungroupClicked);
    auto* row = new QHBoxLayout;
    row->addWidget(glue_);
    row->addWidget(ungroup);
    col->addLayout(row);
}

void GroupPanel::setGroup(const Group& g, int members) {
    const QSignalBlocker quiet(glue_);
    members_->setText(uiText("Kinetica", "Внутри ") + uiCount(members, UiCount::Object) +
                      uiText("Kinetica", ". Щелчок по объекту в сцене выбирает группу, ещё щелчок — сам объект."));
    members_->setWordWrap(true);
    glue_->setChecked(g.glued);
    glue_->setText(g.glued ? uiText("Kinetica", "Склеено в одно тело") : uiText("Kinetica", "Склеить в одно тело"));
}
