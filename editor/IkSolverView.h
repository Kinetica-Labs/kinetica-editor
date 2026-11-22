#pragma once
// The inverse kinematics solver at work, under an IK goal's card (ke::IkResult, scene/Kinematics.h): the
// method in one line - damped least squares, dtheta = J^T (J J^T + lambda^2 I)^-1 e - what the last solve
// came to (rounds, the miss left, Yoshikawa's manipulability: how far the pose is from a singularity) and
// its residual round by round on a log scale, the tolerance a dashed line: the walk from the start and the
// fast fall once near.
#include "scene/Kinematics.h"

#include <QWidget>

class QLabel;

class IkSolverView : public QWidget {
public:
    explicit IkSolverView(QWidget* parent = nullptr);
    // The last solve of a goal whose chain has `freedoms` (fewer than six: the point only).
    void show(const ke::IkResult& r, int freedoms);
    const std::vector<ke::Real>& residual() const { return residual_; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QLabel* text_;
    std::vector<ke::Real> residual_;
    QRect plotRect() const;
};
