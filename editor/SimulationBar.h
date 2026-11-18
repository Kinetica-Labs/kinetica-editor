#pragma once
// The status row of the simulation under the 3D view (MainWindowPlay.cpp builds it): the mode (▶ or ⏸),
// a hint of what to do now, and «Сохранить положения · K» while the scene plays. It sits outside the
// view, never over the picture or where the mouse picks; play, pause and stop stay on the top bar.
#include <QFrame>
class QAction;
class QLabel;
class QPushButton;

class SimulationBar : public QFrame {
    Q_OBJECT
public:
    SimulationBar(QWidget* parent, QAction* keep);
    void setState(bool playing, bool paused, bool canKeep);
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    void fitText();
    QLabel* mode_;
    QLabel* hint_;
    QPushButton* keep_;
};
