#pragma once
// The magnet of the scene builder: a machine or a robot built from shapes in seconds, link by link. With the magnet on, the polygon under the mouse shows the ghost of what a click
// would put there - a ready link (ke::LinkPreset: a hinge about the polygon's normal, a knee, a wheel,
// four wheels for a chassis, a slider, a weld, a ball) with its joint's glyph - and the ghost moves the
// way its joint lets it (swings, slides, spins): what the joint does is seen before the click. Which
// link is suggested by the place (ke::suggestPreset), unless a key or the menu pinned one.
//
// The left button pressed on a polygon: a plain click puts the suggested link; held or dragged, the
// pie menu (PieMenu) opens round the press point and the release puts the link the mouse points to -
// eight choices in fixed places, so the hand learns to flick to one. Two parts that stand are joined by
// «Сустав» (JointTool), with the pieces in AssemblyGhost.h shared. Keys: 1-8 pin a ready link, 0 back to the suggestion, A turns a
// copied link's axis, Tab flips it, M mirrors the link to the other side, Esc leaves the magnet. One
// click is one undo step.
//
// Everything goes through the API (EditorOps), as an agent's or a script's call would: the ghost is
// assembly_preview, the click assembly_preset - with the same arguments.
#include <QObject>
#include <QPointF>

#include "math/Math.h"
#include "AssemblyGhost.h"
#include "scene/Assembly.h"

#include <cstdint>
#include <vector>

class EditorOps;
class PieMenu;
class QAction;
class QJsonObject;
class QMenu;
class QTimer;
class SceneBuilder;
class Viewport;

class MagnetController : public QObject {
    Q_OBJECT
public:
    MagnetController(EditorOps* ops, SceneBuilder* builder, Viewport* view, QObject* parent);
    QAction* action() const { return action_; } // the toolbar's "Магнит", on / off, with its menu
    void setActive(bool on);

signals:
    void statusMessage(const QString& text); // empty: the magnet has nothing to say
    void started();                          // turned on: «Сустав» goes off (one tool of the view's mode at a time)

private:
    QJsonObject placeArgs(const ToolPlace& p, int preset) const; // the API's arguments for a preset at a place
    int suggestion(const ToolPlace& p) const;  // the pinned preset, else the place's suggestion
    void preview(QPointF pos);
    void showPreview(const ToolPlace& p, int preset);
    void animate();                            // the ghost moved by its joint, every 33 ms
    void press(QPointF pos);
    void drag(QPointF pos);
    void release(QPointF pos);
    void openPie();
    void put(const ToolPlace& p, int preset);
    void key(int key);
    void pin(int preset);
    QString choiceText(int preset) const;
    QMenu* buildMenu();

    EditorOps* ops_;
    SceneBuilder* builder_;
    Viewport* view_;
    PieMenu* pie_;
    QTimer* pieTimer_;
    QTimer* animation_;
    QAction* action_ = nullptr;
    bool active_ = false;
    QAction* presetActs_[ke::kMagnetCount + 1] = {}; // 0: the suggestion, then the elements (ke::kMagnetPresets)
    int pinned_ = -1;  // an element chosen by a key or the menu (-1: the suggestion); indices are into kMagnetPresets
    int axis_ = -1;    // A: a copied link's axis (-1: the preset's own)
    bool flip_ = false, mirror_ = false;
    QPointF last_;
    bool hasLast_ = false;
    ToolPlace pressed_; // where the left button went down (found: the pie may open)
    QPointF pressPos_;
    bool pieOpen_ = false;
    std::vector<GhostPart> ghost_;
    std::vector<float> glyphs_;
    float phase_ = 0;
};
