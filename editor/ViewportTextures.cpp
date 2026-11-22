// The bodies' surfaces (ke::SurfaceTexture) on the graphics card: pictures read into textures once
// and again only when their file changes, and a body's pattern, picture and bump handed to the mesh
// shader. The GL resources belong to the view's context (Viewport.h); dropping a picture on an object
// is ViewportTextureDrop.cpp.
#include "Viewport.h"

#include <QDebug>
#include <QFileInfo>
#include <QDateTime>
#include <QImageReader>
#include <QMatrix3x3>

namespace {
QVector3D qv(const ke::Vector3& v) { return QVector3D(float(v.x), float(v.y), float(v.z)); }
} // namespace

void Viewport::releaseImageTextures() {
    for (auto& entry : imageTextures_)
        if (entry.second.id) glDeleteTextures(1, &entry.second.id);
    imageTextures_.clear();
}

// A picture as a texture, read once and again only when its file changes: at most 1024 px a side,
// repeating, mipmapped. 0: no such file, or it cannot be read.
GLuint Viewport::imageTexture(const QString& file) {
    const QFileInfo info(file);
    const QString key = info.absoluteFilePath();
    const qint64 stamp = info.exists() ? info.lastModified().toMSecsSinceEpoch() : -1;
    auto old = imageTextures_.find(key);
    if (old != imageTextures_.end() && old->second.stamp == stamp) return old->second.id;
    ImageTexture& entry = imageTextures_[key];
    if (entry.id) glDeleteTextures(1, &entry.id);
    entry.id = 0;
    entry.stamp = stamp;
    QImageReader reader(key);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 64000000) {
        qWarning() << "Texture unavailable or too large:" << key;
        return 0;
    }
    if (size.width() > 1024 || size.height() > 1024) reader.setScaledSize(size.scaled(1024, 1024, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (image.isNull()) {
        qWarning() << "Texture decode failed:" << key << reader.errorString();
        return 0;
    }
    image = image.convertToFormat(QImage::Format_RGBA8888).mirrored();
    glGenTextures(1, &entry.id);
    glBindTexture(GL_TEXTURE_2D, entry.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width(), image.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, image.constBits());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);
    return entry.id;
}

// A body's surface (ke::SurfaceTexture) into the mesh shader: the pattern or the picture projected
// along the three axes of its frame (mesh.frag), its bump and its normal map. The colour map on unit 4, the height
// map on unit 3, the normal map on unit 2; a missing picture leaves the body's own colour, a missing map a flat surface.
void Viewport::setSurfaceTexture(const ke::SurfaceTexture& texture) {
    const QString colorFile = QString::fromStdString(texture.file), heightFile = QString::fromStdString(texture.heightFile);
    const QString normalFile = QString::fromStdString(texture.normalFile);
    const bool useColor = texture.kind == ke::TextureKind::Image, useHeight = texture.bumpHeight > 0 && !heightFile.isEmpty();
    const bool newColor = useColor && !imageTextures_.count(QFileInfo(colorFile).absoluteFilePath());
    const bool newHeight = useHeight && !imageTextures_.count(QFileInfo(heightFile).absoluteFilePath());
    const bool newNormal = !normalFile.isEmpty() && !imageTextures_.count(QFileInfo(normalFile).absoluteFilePath());
    // Room for the three maps before any is bound: an eviction cannot take one this draw needs.
    if (imageTextures_.size() >= 22 && (newColor || newHeight || newNormal)) releaseImageTextures();
    glActiveTexture(GL_TEXTURE0);
    const GLuint albedo = useColor ? imageTexture(colorFile) : 0, height = useHeight ? imageTexture(heightFile) : 0;
    const GLuint normals = normalFile.isEmpty() ? 0 : imageTexture(normalFile);
    const int kind = useColor && !albedo ? 0 : int(texture.kind);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, albedo);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, height);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, normals);
    glActiveTexture(GL_TEXTURE0);
    // A height map chosen and missing: no bump, rather than the colour's brightness in its place.
    const float bump = texture.heightFile.empty() || height ? float(texture.bumpHeight) : 0.0f;
    QMatrix3x3 frame;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) frame(i, j) = texture.frame.m[i][j];
    meshProg_.setUniformValue("uBumpHeight", bump);
    meshProg_.setUniformValue("uHeightMapEnabled", height ? 1 : 0);
    meshProg_.setUniformValue("uHeightMap", 3);
    meshProg_.setUniformValue("uNormalMap", 2);
    meshProg_.setUniformValue("uNormalMapEnabled", normals ? 1 : 0);
    meshProg_.setUniformValue("uAlbedo", 4);
    meshProg_.setUniformValue("uTextureFrame", frame);
    meshProg_.setUniformValue("uTextureOffset", qv(texture.offset));
    meshProg_.setUniformValue("uTextureRepeats", float(texture.repeats));
    meshProg_.setUniformValue("uTextureKind", kind);
}
