#pragma once
// A stereo camera with a body of its own, as one is bought (the DEPZ of the factory cell, a ZED, a RealSense): the
// housing - a model file, drawn and collided as any model, made in the camera mount's frame (+x out of the lenses, +y
// along the baseline, +z up, the lenses on its mid-line) - and the stereo sensor its child on the lenses' axis, 1 mm
// before the housing's front, so the two eyes see out of it, not into it. Without a model the housing is a box around
// the two lenses. Put free (fixed in the world, looking at a point, its baseline level) or against a face of a shape
// (its back on the face, looking out along the face's normal; welded to the shape when the shape moves). A plain edit
// of a SceneGraph, as Assembly.h's: the editor's menu and the API's assembly_camera make the same one.
#include "scene/Assembly.h"

#include <string>

namespace ke {

struct CameraHousing {
    std::string model;               // the housing's OBJ or STL in the mount's frame, metres; empty: a box
    std::string name = "Стереокамера";
    Real baseline = 0.10_r;          // between the two eyes, m (SensorObject::baseline)
    Real fovDeg = 65;                // horizontal (SensorObject::fovDeg)
    int width = 320, height = 240;   // the images, pixels
    Vector3 color{0.05_r, 0.05_r, 0.05_r}; // the housing's colour where its model has none
};

struct PlacedCamera {
    uint32_t housing = 0, sensor = 0, joint = 0; // joint: the weld to a moving shape it was put on (0: none)
    std::string problem;                         // why nothing was put: an unreadable model, no place
};

// Free: the housing's centre at `position`, its +x toward `lookAt`, its +y level; fixed in the world.
PlacedCamera addStereoCamera(SceneGraph& g, const CameraHousing& h, const Vector3& position, const Vector3& lookAt);
// Against a face of `target` (snapToFace / snapToSurface): the housing's back on the face at its anchor, looking out
// along the normal, +y level when the normal is not vertical (else along the face's first direction across).
PlacedCamera mountStereoCamera(SceneGraph& g, const CameraHousing& h, uint32_t target, const FaceSnap& at);

} // namespace ke
