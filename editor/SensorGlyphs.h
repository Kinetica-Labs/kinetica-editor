#pragma once
// The sensors in the 3D view (ke::SensorVis) as lines, the way the joints are drawn:
//   Энкодер - a ring about its axis with 8 ticks round it and a spoke that turns with its reading, the
//             shaft's own angle (a linear encoder's spoke turns a radian per 10 mm of travel);
//   IMU     - its chip, flat in the chip's x-y plane, and its three axes: x red, y green, z blue;
//   Сонар   - its two transducers and its beam, the 7.5-degree cone, as long as its echo - faint out to
//             the full 4 m when nothing answers.
// Edited, the sensors stand where the graph puts them (ke::sensorVisuals); playing, where the bodies
// carrying them are now (ke::Frame::sensors). Lines are pairs of points, x y z 0 each.
#include "OrbitCamera.h"
#include "sim/Simulation.h"

#include <vector>

struct SensorGlyphLines {
    std::vector<float> encoder, chip, axis[3], beam, faint;
};

void appendSensorGlyph(SensorGlyphLines& out, const ke::SensorVis& v);
// Where the ray meets the ball round the sensor's glyph (t along the ray).
bool sensorHit(const ke::SensorVis& v, const Ray& ray, float& t);
