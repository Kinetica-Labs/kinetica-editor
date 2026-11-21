// Read GLSL from Qt resources on the GUI thread, when an OpenGL program is compiled.
#include "Shaders.h"
#include <QFile>
#include <QDebug>

QString shaderSource(const char* name) {
    QFile file(QString(":/shaders/") + QString::fromLatin1(name));
    if (!file.open(QIODevice::ReadOnly)) qFatal("Missing embedded shader: %s", name);
    return QString::fromUtf8(file.readAll());
}
