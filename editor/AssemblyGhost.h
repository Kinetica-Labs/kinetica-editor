#pragma once
// What the view's two assembly tools share - the magnet (MagnetController: a ready link on a face) and «Сустав» (JointTool:
// two standing parts joined): the shape under the mouse with its polygon's normal, a part's ghost as edges that its joint
// moves (swings, slides, wobbles), an arrow drawn in the world, and the vectors of the API's JSON. The tools themselves
// are in their own files.
#include "math/Math.h"

#include <QJsonArray>
#include <QPointF>

#include <cstdint>
#include <vector>

class SceneBuilder;
class Viewport;
namespace ke {
struct Entity;
struct SceneGraph;
}

struct ToolPlace {
    uint32_t target = 0;
    ke::Vector3 point{0.0f}, normal{0.0f};
    bool found = false;
};
ToolPlace placeUnder(SceneBuilder* builder, Viewport* view, QPointF pos); // the shape under the mouse, its point and normal

// A part's ghost: its edges as point pairs, and how its joint moves it (ke::JointType as int; a wheel spins).
struct GhostPart {
    std::vector<ke::Vector3> points;
    ke::Vector3 anchor{0.0f}, axis{0.0f}, reference{0.0f};
    int type = 1;
    bool spins = false;
    float travel = 0;
};
// The edges of an entity: a box's twelve; any other shape's mesh, every triangle's (its box when too fine to redraw).
std::vector<ke::Vector3> ghostEdges(const ke::SceneGraph& g, const ke::Entity& e);
// The parts moved as their joints let them at `phase` (s): a hinge swings ±30° (a wheel spins), a slider goes out and
// back over its travel, a ball wobbles about two axes, a weld stays - x y z 0 per point, for Viewport::setMagnetPreview.
std::vector<float> ghostLines(const std::vector<GhostPart>& parts, float phase);

// An arrow from `a` to `b`: the shaft and a head of two strokes `head` long, in the plane facing the eye.
void appendArrow(std::vector<float>& lines, const ke::Vector3& a, const ke::Vector3& b, const ke::Vector3& eye, float head);
ke::Vector3 across(const ke::Vector3& axis); // any unit direction square to the axis

QJsonArray jsonOf(const ke::Vector3& v);
ke::Vector3 vecOf(const QJsonValue& v);
