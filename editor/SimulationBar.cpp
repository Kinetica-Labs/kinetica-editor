// The simulation's status row (SimulationBar.h): mode and pose saving belong to the window's chrome,
// never to the scene's picture or its picking area.
#include "SimulationBar.h"
#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>

SimulationBar::SimulationBar(QWidget* parent, QAction* keep) : QFrame(parent) {
    setObjectName("simulationBar");
    setFixedHeight(36);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(10, 3, 8, 3); row->setSpacing(12);
    mode_ = new QLabel; mode_->setObjectName("simulationMode");
    mode_->setMinimumWidth(54);
    hint_ = new QLabel; hint_->setObjectName("simulationHint");
    hint_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    keep_ = new QPushButton("Сохранить положения · K"); keep_->setObjectName("keepButton");
    keep_->setAccessibleName("Сохранить положения из симуляции");
    keep_->setToolTip(keep->toolTip()); keep_->setCursor(Qt::PointingHandCursor);
    connect(keep_, &QPushButton::clicked, keep, &QAction::trigger);
    connect(keep, &QAction::changed, this, [this, keep] { keep_->setEnabled(keep->isEnabled()); });
    row->addWidget(mode_); row->addWidget(hint_, 1); row->addWidget(keep_);
    setState(false, false, false);
}
void SimulationBar::setState(bool playing, bool paused, bool canKeep) {
    const bool running = playing || paused;
    mode_->setText(paused ? "Пауза" : playing ? "Пуск" : "Правка"); // the button's word: one name for the mode
    mode_->setStyleSheet(running ? "color: #ffd58a; font-weight: 600;" : "color: #b8c4d0; font-weight: 600;");
    hint_->setText(!running ? "Пуск запустит симуляцию." : !note_.isEmpty() ? note_ : canKeep ? "Стоп вернёт исходную сцену." : "Готовая сцена.");
    hint_->setStyleSheet(running && !note_.isEmpty() ? "color: #7cff9a;" : QString());
    setToolTip(running && canKeep ? "Правки в игре не сохранятся после Стоп. Сохранить положения тел: K." : hint_->text());
    mode_->setToolTip(toolTip()); hint_->setToolTip(toolTip());
    keep_->setVisible(canKeep);
    fitText();
}
void SimulationBar::setNote(const QString& note) {
    note_ = note;
}

void SimulationBar::fitText() {
    hint_->setVisible(width() >= 520);
    keep_->setText(width() < 340 ? "Сохранить · K" : "Сохранить положения · K");
}
void SimulationBar::resizeEvent(QResizeEvent* event) {
    QFrame::resizeEvent(event);
    fitText();
}
