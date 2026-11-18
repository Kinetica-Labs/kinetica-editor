#pragma once
// How a joint is drawn (the CAD convention for mates): what it lets the two links do, not just where
// it is. A hinge is its axis and a ring around it, with the arc of its limits (or an arrowhead: the
// way its angle counts); a slider is its direction with arrowheads at both ends and ticks at its
// travel limits; a ball is three rings; a weld is a small cube. Line pairs in the world, x y z 0 per
// point, for Viewport::drawLines - the same glyphs in edit mode (from the graph) and in play (from
// the solver's joints).
#include "sim/Simulation.h"

#include <QJsonObject>

#include <vector>

// The glyph of one joint; `size` is the ring's radius [m].
void appendJointGlyph(std::vector<float>& lines, const ke::JointVis& j, float size);

// The generic joint drawn as a gizmo, freedom by freedom in frame b, coloured by axis as a gizmo is
// (x red, y green, z blue): a travel is an arrow - short and dim when locked, long with heads when
// free, a span with end ticks when limited, a spring's zigzag beside it when driven; a turn is a
// ring - none when locked, whole when free, the arc of its limits when limited, an arrowhead when
// driven - with a spoke at the turn it has now. Debug adds both frames as short axes and the line
// between their origins (the solver's error).
struct JointGizmoLines {
    std::vector<float> axis[3]; // x, y, z
    std::vector<float> dim;     // locked travel
    std::vector<float> debug;   // the frames and the gap between them
};
void appendJointGizmo(JointGizmoLines& out, const ke::JointVis& j, float size, bool debug);
// A box's twelve edges: the magnet's ghost of the shape about to be put.
void appendBoxEdges(std::vector<float>& lines, const ke::Vector3& centre, const ke::Quaternion& rotation, const ke::Vector3& size);

// A joint's drawing data as JSON for the API (assembly_preview) and back: the kind's word, anchors,
// axis, the angle's zero, size, the main freedom's limits, both frames and every freedom.
QJsonObject jointVisJson(const ke::JointVis& v, const char* kindWord);
ke::JointVis jointVisFromJson(const QJsonObject& j);
