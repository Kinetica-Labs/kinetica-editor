#pragma once
// "Коллайдер — чем сталкивается": the component that says what the body collides with, apart from
// what it looks like (Houdini's collision guide of a packed RBD object, Unity's Collider next to its
// MeshRenderer). A row of icon buttons picks the kind - Авто (the geometry itself), Коробка, Сфера,
// Капсула, Выпуклая оболочка, Цилиндр - and "Подогнать под геометрию" sizes it to the geometry;
// switched off, size, offset and turn are typed in. Choosing a collider never changes the geometry,
// and the geometry's dropdown never changes the collider. What the physics engine gets of it is
// ke::colliderShapes (sim/RobotWorld.h).
#include "physics/PhysicsWorld.h"
#include "scene/SceneGraph.h"

#include <QWidget>

class QButtonGroup;
class QCheckBox;
class QLabel;
class Vec3Row;

class ColliderPanel : public QWidget {
    Q_OBJECT
public:
    explicit ColliderPanel(QWidget* parent = nullptr);
    void setCollider(const ke::ColliderRole& c); // does not report a change
    void writeTo(ke::ColliderRole& c) const;     // the kind, the fit and the numbers; not `enabled`
    // What "Авто" turned into for this geometry ("сфера", "выпуклая оболочка"), shown next to it.
    void setAutoResult(const QString& what);

signals:
    void edited(); // the user changed the kind, the fit or a number

private:
    void buildKindRow(class QVBoxLayout* col);
    void buildManualFields(class QVBoxLayout* col);
    void updateVisibility(); // the fields only when they mean something for the chosen kind
    void onKindClicked(int kind);

    QButtonGroup* kinds_ = nullptr;
    QLabel* kindName_ = nullptr;
    QCheckBox* fit_ = nullptr;
    QWidget* manual_ = nullptr; // size, offset, turn: shown when not fitted to the geometry
    QWidget* sizeRow_ = nullptr;
    Vec3Row *size_ = nullptr, *offset_ = nullptr, *rotation_ = nullptr;
    ke::ColliderKind kind_ = ke::ColliderKind::Auto;
    QString autoResult_;
    bool filling_ = false;
};

// The kinds as the editor names them: "Авто", "Коробка", "Сфера", "Капсула", "Выпуклая оболочка", "Цилиндр".
QString colliderName(ke::ColliderKind k);
// A shape of the physics engine in words, lower case: "коробка", "сфера", "капсула", "выпуклая оболочка".
QString collisionShapeName(ke::ShapeDesc::Kind k);
