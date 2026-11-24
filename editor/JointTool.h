#pragma once
// «Сустав» (the toolbar's): two parts that stand joined in two clicks. The mouse over a part says what it guesses by the
// place: a box face's centre - a hinge about its normal (a
// shaft), an edge - a hinge along it where the mouse points (a lid), a corner - a ball, a cylinder's side - its axis, a
// sphere - its centre; crosses mark the face's snap points, the axis is an arrow, a label by the cursor says the step.
// The first click puts the axis; a rubber band follows the mouse; over the other part its ghost moves as the joint lets
// it and the label forecasts Play; the second click joins them (assembly_joint) - or, the parts standing apart and the
// click on a snap point of the other, brings the smaller there face to face first (Fusion's Joint; one undo step,
// scene_batch). Keys: 0-8 the kind (the guess, hinge, slider, ball, weld, cylinder, universal, planar, shock), A another
// axis, Tab flips it, M the other part moves, Ctrl no snap, Shift one more, Esc back a step or out. A joint card's «Указать
// в виде…» (pickJoint) is one click that puts that joint there. The magnet (MagnetController) shares the view's mode and
// the pieces in AssemblyGhost.h; one of the two is on at a time.
#include "AssemblyGhost.h"

#include "scene/JointModel.h"
#include "math/Quaternion.h"

#include <QObject>
#include <QPointF>
#include <QString>

class EditorOps;
class QAction;
class QTimer;
class SceneBuilder;
class Viewport;

class JointTool : public QObject {
    Q_OBJECT
public:
    JointTool(EditorOps* ops, SceneBuilder* builder, Viewport* view, QObject* parent);
    QAction* action() const { return action_; } // the toolbar's «Сустав ▾», its menu the kinds
    void setActive(bool on);
    void pickJoint(uint32_t joint); // «Указать в виде…» of a joint's card

signals:
    void statusMessage(const QString& text);
    void started(); // turned on: the magnet goes off (one tool of the view's mode at a time)

private:
    struct JoinSnap {
        ToolPlace at;
        std::vector<ke::Vector3> axes;  // the guessed axis first, then the face's normal and its two directions (A cycles)
        ke::JointType type = ke::JointType::Hinge;
        int kind = 3;                   // 0 face centre, 1 edge, 2 corner, 3 the point itself, 4 a cylinder's axis, 5 a sphere's centre
        std::vector<ke::Vector3> marks; // every snap point of the face, drawn as dots
    };
    // The smaller part turned face to face about its own snap point and moved onto the other's (Fusion's Joint).
    struct Align {
        bool on = false;
        uint32_t mover = 0, base = 0;
        ke::Quaternion turn{};
        ke::Vector3 from{0.0f}, to{0.0f}, axis{0.0f};
    };
    JoinSnap joinSnap(const ToolPlace& p) const;
    ke::Vector3 joinAxis(const JoinSnap& s) const;
    ke::JointType joinKind(const JoinSnap& s) const;
    QString joinWord(const JoinSnap& s) const; // the kind's word for assembly_joint
    QString joinText(const JoinSnap& s) const; // the kind in the window's words
    uint32_t moverOf(uint32_t first, uint32_t second) const; // the smaller moves, unless M swapped it
    // What Play will do with the joint about to be made, in words; `centre`: the mover's centre if it is moved first.
    QString playForecast(uint32_t second, const ke::Vector3* centre = nullptr) const;
    Align alignTo(const JoinSnap& second) const;
    void preview(QPointF pos);
    void press(QPointF pos, Qt::KeyboardModifiers modifiers);
    void release(QPointF pos);
    void key(int key);
    void joinPress(const ToolPlace& p);
    void showJoint(const ToolPlace& p);

    EditorOps* ops_;
    SceneBuilder* builder_;
    Viewport* view_;
    QAction* action_ = nullptr;
    QTimer* animation_ = nullptr;
    bool joining_ = false;          // on: a plain click joins
    QString joinType_ = "auto";     // auto (the guess), hinge, slider, ball, weld, cylinder, universal, planar, shock
    JoinSnap joinFrom_;             // the first click's place
    int joinAxis_ = 0;              // A: which of the snap's axes
    bool flip_ = false;             // Tab
    bool swapMover_ = false;        // M
    bool keepJoining_ = false;      // Shift at the second click: one more joint after this one
    uint32_t repick_ = 0;           // a joint whose place the next click sets (pickJoint), else 0
    QPointF last_;
    bool hasLast_ = false;
    std::vector<GhostPart> ghost_;
    std::vector<float> glyphs_;
    float phase_ = 0;
};
