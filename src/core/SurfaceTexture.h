#pragma once
// Rendering-only material coordinates. They follow the object and are inherited by fragments;
// contact, mass, friction and all physics equations remain independent of this descriptor.
#include "math/Matrix3x3.h"
#include <cstring>
#include <string>

namespace ke {
// Checker: two grey squares; Image: a picture's file; Speckle: cells of the body's colour times a random grey (a hash
// of the cell, no period) - the texture a stereo camera can match anywhere, as the speckle painted on parts for stereo
// and image correlation; Stone: the colour times a granite - value noise in four octaves, from a base cell down to an
// eighth of it, and round dark and light grains of the finest size - a floor that looks like one and that stereo matches.
enum class TextureKind { None, Checker, Image, Speckle, Stone };
struct SurfaceTexture {
    TextureKind kind = TextureKind::None;
    std::string file;
    Real repeats = 8; // pattern periods per metre in the original object's local frame
    std::string heightFile; // empty: use the colour texture's brightness
    Real bumpHeight = 0; // apparent height range in metres; shading only, never displacement
    Matrix3x3 frame;
    Vector3 offset;
};
// The same in every word: a run of bodies of one surface is one state of the renderer, set once.
inline bool operator==(const SurfaceTexture& a, const SurfaceTexture& b) {
    return a.kind == b.kind && a.repeats == b.repeats && a.bumpHeight == b.bumpHeight && a.file == b.file && a.heightFile == b.heightFile &&
           std::memcmp(&a.frame, &b.frame, sizeof a.frame) == 0 && std::memcmp(&a.offset, &b.offset, sizeof a.offset) == 0;
}
} // namespace ke
