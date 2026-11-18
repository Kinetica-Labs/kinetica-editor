#pragma once
// The GLSL sources of shaders/, embedded as Qt resources at build time: no file is read at run time.
#include <QString>

QString shaderSource(const char* name);
