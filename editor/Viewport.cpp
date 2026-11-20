// The 3D view (see Viewport.h): OpenGL set-up (shaders, the line buffer), the bodies' meshes on the
// GPU and their surface textures, picking the frame's bodies by their meshes, and the drawing of one
// frame - background, floor, bodies, see-through marks, joint and sensor glyphs, the probe's drawing,
// outlines, the ghosts of the magnet and of a shape being drawn, the gizmo and the 2D overlay. Input
// is in ViewportInput.cpp and ViewportTools.cpp, lights and cameras in ViewportLights.cpp, shadows in
// ViewportShadows.cpp, the play frame, the orbit pivot and following a body in ViewportPlay.cpp.
#include "Viewport.h"
#include "UiLanguage.h"

#include <QOpenGLTimeMonitor>

#include "JointGlyphs.h"
#include "MeshNormals.h"
#include "SensorGlyphs.h"
#include "Shaders.h"
#include "core/Probe.h"

#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QImageReader>
#include <QMatrix3x3>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>

using ke::Vector3;

namespace {

// Where the ray first meets a body's mesh (t along the ray): the ray taken into the body's frame, the
// mesh's box first, then its triangles.
bool rayBody(const ke::Frame::Body& b, const Ray& ray, float& t) {
    if (!b.mesh || b.mesh->empty()) return false;
    const ke::Matrix3x3 Rt = b.rot.toMatrix3x3().transposed();
    const Vector3 o = Rt * (ray.origin - b.pos), d = Rt * ray.dir;
    float tNear, tFar;
    if (!intersectBox(Ray{o, d}, b.mesh->bounds(), tNear, tFar)) return false;
    const ke::TriMesh& m = *b.mesh;
    t = ke::kInf;
    for (const auto& tri : m.triangles) {
        float ti;
        if (rayTriangle(o, d, m.positions[tri[0]], m.positions[tri[1]], m.positions[tri[2]], ti) && ti < t) t = ti;
    }
    return t < ke::kInf;
}

// A sleeping body looks as it did awake, as in edit mode; the probe's layer "Sleeping"
// (ke::DrawLayer::Sleeping) draws the sleepers darker - a debugging view, not the normal one.
float sleepDim(const ke::Frame::Body& b) { return b.sleeping && ke::Probe::layerOn(ke::DrawLayer::Sleeping) ? 0.55f : 1.0f; }

QVector3D qv(const Vector3& v) { return QVector3D(v.x, v.y, v.z); }

} // namespace

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
Viewport::Viewport(QWidget* parent) : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
    setMouseTracking(true); // the navigation cube and the hints follow the mouse
    setAcceptDrops(true);   // an image dropped on an object becomes its texture (ViewportTextureDrop.cpp)
    flyTimer_ = new QTimer(this);
    flyTimer_->setInterval(15);
    connect(flyTimer_, &QTimer::timeout, this, &Viewport::flyTick);
}

Viewport::~Viewport() {
    makeCurrent();
    if (maskTex_) glDeleteTextures(1, &maskTex_);
    if (maskFbo_) glDeleteFramebuffers(1, &maskFbo_);
    releaseImageTextures();
    releaseShadowMaps();
    meshCache_.clear(); // GL objects must die while the context is current
    for (QOpenGLTimeMonitor*& m : gpuClock_) delete m, m = nullptr; // its queries too: a child QObject would outlive the context
    doneCurrent();
}

// Vertex attributes are bound by name (attribute k = attributes[k]) rather than with
// layout(location), which GLSL 1.30 does not have.
static void buildProgram(QOpenGLShaderProgram& p, const QString& vs, const QString& fs, const char* name,
                         std::initializer_list<const char*> attributes) {
    int location = 0;
    for (const char* a : attributes) p.bindAttributeLocation(a, location++);
    if (!p.addShaderFromSourceCode(QOpenGLShader::Vertex, vs) || !p.addShaderFromSourceCode(QOpenGLShader::Fragment, fs) ||
        !p.link())
        qWarning("Shader %s failed: %s", name, qPrintable(p.log()));
}

// The mesh shader with exactly the shadow lookups this frame needs. `slotKinds` says, per shadow slot,
// what it holds: 'M' a depth map (a sun, a spotlight), 'C' a lamp's cube, '-' nothing ("----": no
// shadow at all, the shadow code left out). Unused lookups are not free - the software renderer runs
// every branch of a shader, and all eight made each frame ten times slower (283 ms against 26) - so
// only the used ones are compiled in, and the program is rebuilt when a tick changes them: once, in a
// few milliseconds.
void Viewport::buildMeshShader(const QString& slotKinds) {
    meshSlots_ = slotKinds;
    QString defines;
    for (int s = 0; s < slotKinds.size(); ++s)
        if (slotKinds[s] != '-') defines += QString("#define KE_SLOT%1_%2\n").arg(s).arg(slotKinds[s] == 'C' ? "CUBE" : "MAP");
    if (!defines.isEmpty()) defines = "#define KE_SHADOWS\n" + defines;
    meshProg_.removeAllShaders();
    buildProgram(meshProg_, glslHeader_ + shaderSource("mesh.vert"), glslHeader_ + defines + shaderSource("mesh.frag"), "mesh",
                 {"aPos", "aNormal", "aColor"});
}

void Viewport::buildShaders() {
    const QString h = glslHeader_;
    buildProgram(bgProg_, h + shaderSource("background.vert"), h + shaderSource("background.frag"), "background", {});
    buildMeshShader("----");
    buildProgram(lineProg_, h + shaderSource("lines.vert"), h + shaderSource("lines.frag"), "line", {"aPos"});
    buildProgram(outlineProg_, h + shaderSource("background.vert"), h + shaderSource("outline.frag"), "outline", {});
    buildProgram(shadowProg_, h + shaderSource("shadow.vert"), h + shaderSource("shadow.frag"), "shadow", {"aPos"});
    buildProgram(distanceProg_, h + shaderSource("shadow-distance.vert"), h + shaderSource("shadow-distance.frag"), "distance", {"aPos"});
}

void Viewport::initializeGL() {
    initializeOpenGLFunctions();
    renderer_ = QString::fromLatin1(reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    const QSurfaceFormat f = context()->format();
    const int version = f.majorVersion() * 10 + f.minorVersion();
    if (version < 30) {
        glOk_ = false; // nothing below would work: say so, offer the software renderer
        QTimer::singleShot(0, this, [this] { emit openGLUnsupported(renderer_); });
        return;
    }
    // Qt gives a 3.3 core context on normal drivers; old drivers and Qt's llvmpipe give 3.0.
    const bool core = version >= 33 && f.profile() == QSurfaceFormat::CoreProfile;
    glslHeader_ = core ? "#version 330 core\n" : "#version 130\n";
    buildShaders();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    bg_.vao.create();
    // Lines: x y z and a fourth float per point (the glyphs' format), refilled for every draw.
    lines_.vao.create();
    lines_.vbo.create();
    lines_.vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    lines_.vao.bind();
    lines_.vbo.bind();
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    lines_.vao.release();
}

void Viewport::resizeGL(int, int) {}

// ---------------------------------------------------------------------------
// Picking the frame's bodies
// ---------------------------------------------------------------------------
bool Viewport::pickAnyBody(const Ray& ray, int& body, Vector3& hit) const {
    body = -1;
    if (!snap_) return false;
    float best = ke::kInf;
    for (int i = 0; i < int(snap_->bodies.size()); ++i) {
        const ke::Frame::Body& b = snap_->bodies[size_t(i)];
        float t;
        if (!b.marker && !unpickable(i) && rayBody(b, ray, t) && t < best) best = t, body = i;
    }
    if (body >= 0) hit = ray.at(best);
    return body >= 0;
}

uint32_t Viewport::pickHandle(const Ray& ray, float& distance) const {
    uint32_t id = 0;
    distance = ke::kInf;
    if (!snap_ || editMode_) return 0;
    for (const ke::Frame::Body& b : snap_->bodies) {
        float t;
        if (b.marker && b.opacity < 1 && rayBody(b, ray, t) && t < distance) distance = t, id = b.object;
    }
    return id;
}

// The nearest body hides those behind it: a static one in front leaves nothing to grab. Small or
// distant bodies are only a few pixels wide, so when the ray misses every mesh the body whose centre
// passes closest to it within ~15 pixels on screen is taken, the anchor inside it towards the cursor.
bool Viewport::pickBody(const Ray& ray, int& body, Vector3& hit) const {
    const auto grabbable = [](const ke::Frame::Body& b) { return b.movable && b.body >= 0 && !b.marker && b.mesh; };
    int index;
    if (pickAnyBody(ray, index, hit)) {
        const ke::Frame::Body& b = snap_->bodies[size_t(index)];
        body = grabbable(b) ? b.body : -1;
        return body >= 0;
    }
    body = -1;
    if (!snap_) return false;
    const float pixel = 2.0f * std::tan(qDegreesToRadians(camera_.fovDeg() * 0.5f)) / float(std::max(1, height()));
    float bestRatio = 1.0f;
    for (const ke::Frame::Body& b : snap_->bodies) {
        if (!grabbable(b)) continue;
        const float t = ke::dot(b.pos - ray.origin, ray.dir);
        if (t <= 0) continue;
        const float radius = 0.5f * ke::length(b.mesh->bounds().extent());
        const float ratio = ke::length(b.pos - ray.at(t)) / (radius + 15.0f * pixel * t);
        if (ratio >= bestRatio) continue;
        bestRatio = ratio, body = b.body;
        const Vector3 off = ray.at(t) - b.pos;
        const float l = ke::length(off), r = 0.8f * radius;
        hit = b.pos + (l > r ? off * (r / l) : off);
    }
    return body >= 0;
}

// ---------------------------------------------------------------------------
// Outlines
// ---------------------------------------------------------------------------
// The silhouette outlines, as in Blender or Maya: the outlined objects are drawn flat into a mask
// (selected in red, hovered in green, role-less in blue), then a full-screen pass lights the pixels
// just outside each mask - 2 px of orange around the selection, a faint white line around the object
// under the mouse, a fainter one around geometry that has no role yet. Drawn over everything.
void Viewport::drawHighlight(const QMatrix4x4& vp) {
    if (highlight_.empty() && hoverBodies_.empty() && ghostBodies_.empty()) return;
    const qreal dpr = devicePixelRatioF();
    const int w = int(width() * dpr), h = int(height() * dpr);
    if (!ensureMaskTarget(w, h)) return;
    buildMaskTriangles();
    glBindFramebuffer(GL_FRAMEBUFFER, maskFbo_);
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    for (int c = 0; c < 3; ++c) { // each list into its own channel
        glColorMask(c == 0, c == 1, c == 2, GL_FALSE);
        drawLines(maskTriangles_[c], GL_TRIANGLES, vp, QVector4D(1, 1, 1, 1));
    }
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glViewport(0, 0, w, h);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    outlineProg_.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, maskTex_);
    outlineProg_.setUniformValue("uMask", 0);
    outlineProg_.setUniformValue("uTexel", QVector2D(1.0f / float(w), 1.0f / float(h)));
    outlineProg_.setUniformValue("uRadius", float(2.0 * dpr));
    outlineProg_.setUniformValue("uSelected", QVector4D(1.0f, 0.6f, 0.16f, 1.0f));
    outlineProg_.setUniformValue("uHover", QVector4D(1.0f, 1.0f, 1.0f, 0.55f));
    outlineProg_.setUniformValue("uGhost", QVector4D(0.82f, 0.86f, 0.94f, 0.22f));
    bg_.vao.bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    bg_.vao.release();
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

bool Viewport::ensureMaskTarget(int w, int h) {
    if (w <= 0 || h <= 0) return false;
    if (maskFbo_ && w == maskW_ && h == maskH_) return true;
    if (!maskTex_) glGenTextures(1, &maskTex_);
    glBindTexture(GL_TEXTURE_2D, maskTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // soft edges: the outline is anti-aliased
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (!maskFbo_) glGenFramebuffers(1, &maskFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, maskFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, maskTex_, 0);
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    maskW_ = w;
    maskH_ = h;
    return ok;
}

// The outlined bodies' triangles in world space, rebuilt only when the snapshot or the lists change.
void Viewport::buildMaskTriangles() {
    const std::vector<int>* lists[3] = {&highlight_, &hoverBodies_, &ghostBodies_};
    bool same = maskSerial_ == snapSerial_;
    for (int c = 0; c < 3; ++c) same = same && maskLists_[c] == *lists[c];
    if (same) return;
    maskSerial_ = snapSerial_;
    for (int c = 0; c < 3; ++c) {
        maskLists_[c] = *lists[c];
        maskTriangles_[c].clear();
        for (int i : *lists[c])
            if (i >= 0 && i < int(snap_->bodies.size())) appendBodyTriangles(snap_->bodies[size_t(i)], maskTriangles_[c]);
    }
}

void Viewport::appendBodyTriangles(const ke::Frame::Body& b, std::vector<float>& out) const {
    if (!b.mesh) return;
    const ke::Matrix3x3 R = b.rot.toMatrix3x3();
    for (const auto& tri : b.mesh->triangles)
        for (int k = 0; k < 3; ++k) {
            const Vector3 p = b.pos + R * b.mesh->positions[tri[k]];
            out.insert(out.end(), {p.x, p.y, p.z, 0.0f});
        }
}

// ---------------------------------------------------------------------------
// Glyphs, the gizmo, the magnet, the probe's drawing
// ---------------------------------------------------------------------------
// The gizmo: over everything (no depth test), see-through where its pieces say so.
void Viewport::drawGizmo(const QMatrix4x4& vp) {
    if (!gizmoVisible_ || magnetMode_ || createMode_ || agentDriving_) return; // the magnet's glyphs stand where its handles would
    const int count = gizmo_.pieces(gizmoView(), gizmoPieces_);
    if (count == 0) return;
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (int i = 0; i < count; ++i) {
        const GizmoPiece& piece = gizmoPieces_[size_t(i)];
        if (!piece.triangles.empty())
            drawLines(piece.triangles, GL_TRIANGLES, vp, QVector4D(piece.color.x, piece.color.y, piece.color.z, piece.alpha));
    }
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

// The amount of the drag in words next to the cursor ("X +0.35 м"), and the typed number; the size of a
// shape being drawn.
void Viewport::drawEditLabel(QPainter& p) {
    QString text = createMode_ ? createTool_.sizeText(createWhat_) : gizmo_.dragging() ? QString::fromStdString(gizmo_.dragLabel()) : QString();
    if (editTool_.draggingHandle()) { // a point's value as it goes, by the cursor (Cinema 4D's HUD)
        const ShapeHandle& h = editTool_.draggedHandle();
        const Vector3 at = editTool_.draggedTo();
        const QString name = h.plugin ? h.label : QString(h.what);
        text = h.free ? QString("%1 %2 %3 %4").arg(name).arg(double(at.x), 0, 'f', 3).arg(double(at.y), 0, 'f', 3).arg(double(at.z), 0, 'f', 3)
                      : QString(uiText("Kinetica", "%1 %2 м")).arg(name).arg(double(ke::dot(at - h.centre, h.axis)) * (h.both ? 2.0 : 1.0), 0, 'f', 3);
    }
    if (text.isEmpty()) return;
    if (!typed_.isEmpty()) text = "= " + typed_ + "   " + text;
    const QFontMetrics fm(p.font());
    const QRectF box(lastMouse_ + QPointF(18, -30), QSizeF(fm.horizontalAdvance(text) + 16, fm.height() + 8));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(20, 22, 27, 215));
    p.drawRoundedRect(box, 5, 5);
    p.setPen(QColor(255, 222, 120));
    p.drawText(box, Qt::AlignCenter, text);
}

// Each joint as what it lets the links do (JointGlyphs.h), over everything: a generic joint as a
// gizmo of its six freedoms coloured by axis (its frames too when debugging joints), any other in its
// type's colour.
void Viewport::drawJoints(const QMatrix4x4& vp) {
    if (snap_->joints.empty()) return;
    const QVector4D colors[6] = {{1.0f, 0.55f, 0.2f, 1}, {0.3f, 0.8f, 1.0f, 1}, {0.4f, 1.0f, 0.45f, 1},
                                 {0.9f, 0.45f, 1.0f, 1}, {1.0f, 0.9f, 0.35f, 1}, {0.9f, 0.9f, 0.9f, 1}};
    const QVector4D axes[3] = {{1.0f, 0.35f, 0.3f, 1}, {0.4f, 0.95f, 0.4f, 1}, {0.35f, 0.6f, 1.0f, 1}};
    glDisable(GL_DEPTH_TEST);
    JointGizmoLines gizmo;
    std::vector<float> typed[6];
    for (const auto& j : snap_->joints) {
        if (j.generic) appendJointGizmo(gizmo, j, float(j.size), jointDebug_);
        else appendJointGlyph(typed[std::clamp(int(j.type), 0, 5)], j, float(j.size));
    }
    for (int k = 0; k < 3; ++k) drawLines(gizmo.axis[k], GL_LINES, vp, axes[k]);
    drawLines(gizmo.dim, GL_LINES, vp, QVector4D(0.6f, 0.62f, 0.66f, 0.8f));
    drawLines(gizmo.debug, GL_LINES, vp, QVector4D(1.0f, 1.0f, 1.0f, 0.9f));
    for (int type = 0; type < 6; ++type) drawLines(typed[type], GL_LINES, vp, colors[type]);
    glEnable(GL_DEPTH_TEST);
}

// The sensors: an encoder's ring orange, a chip and transducers light grey, an IMU's axes x red, y
// green, z blue, a sonar's beam green to its echo (faint grey to its range when nothing answers).
void Viewport::drawSensors(const QMatrix4x4& vp) {
    if (snap_->sensors.empty()) return;
    SensorGlyphLines lines;
    for (const auto& s : snap_->sensors) appendSensorGlyph(lines, s);
    const QVector4D axes[3] = {{1.0f, 0.35f, 0.3f, 1}, {0.4f, 0.95f, 0.4f, 1}, {0.35f, 0.6f, 1.0f, 1}};
    glDisable(GL_DEPTH_TEST);
    drawLines(lines.encoder, GL_LINES, vp, QVector4D(1.0f, 0.62f, 0.22f, 1));
    drawLines(lines.chip, GL_LINES, vp, QVector4D(0.85f, 0.87f, 0.9f, 1));
    for (int k = 0; k < 3; ++k) drawLines(lines.axis[k], GL_LINES, vp, axes[k]);
    drawLines(lines.beam, GL_LINES, vp, QVector4D(0.45f, 0.95f, 0.55f, 1));
    drawLines(lines.faint, GL_LINES, vp, QVector4D(0.6f, 0.62f, 0.66f, 0.45f));
    glEnable(GL_DEPTH_TEST);
}

// The magnet's ghost: the edges of the link about to be put (white) and its joint (yellow); the shape
// being drawn (white), or the cross where it would start.
void Viewport::drawGhosts(const QMatrix4x4& vp) {
    if (!editMode_ || !(magnetMode_ || createMode_)) return;
    glDisable(GL_DEPTH_TEST);
    drawLines(createMode_ ? createTool_.ghost() : magnetGhost_, GL_LINES, vp, QVector4D(0.95f, 0.97f, 1.0f, 0.9f));
    drawLines(magnetJoint_, GL_LINES, vp, QVector4D(1.0f, 0.85f, 0.25f, 1.0f));
    glEnable(GL_DEPTH_TEST);
}

// Whatever the world drew through ke::Probe this frame (contact points and normals, body bounds,
// joint frames ...). The line shader takes one colour per call, so the segments are batched by
// colour; a point is a small cross of three segments of its size. Drawn twice: faint through
// everything (X-ray: the half of a cross under the floor, an arrow inside a body), then solid where
// nothing is in front of it.
void Viewport::drawDebugProbe(const QMatrix4x4& vp) {
    const auto& P = snap_->probe;
    if (P.lines.empty() && P.points.empty()) return;
    std::map<std::array<int, 3>, std::vector<float>> batches; // colour (0..255) -> segments
    auto key = [](const Vector3& c) {
        return std::array<int, 3>{int(std::clamp(float(c.x), 0.0f, 1.0f) * 255), int(std::clamp(float(c.y), 0.0f, 1.0f) * 255),
                                  int(std::clamp(float(c.z), 0.0f, 1.0f) * 255)};
    };
    for (const ke::Probe::Line& l : P.lines) {
        auto& d = batches[key(l.color)];
        d.insert(d.end(), {l.a.x, l.a.y, l.a.z, 0, l.b.x, l.b.y, l.b.z, 0});
    }
    for (const ke::Probe::Point& p : P.points) {
        auto& d = batches[key(p.color)];
        for (int a = 0; a < 3; ++a) {
            Vector3 e(0.0f);
            e[a] = 0.5f * p.size;
            const Vector3 p0 = p.p - e, p1 = p.p + e;
            d.insert(d.end(), {p0.x, p0.y, p0.z, 0, p1.x, p1.y, p1.z, 0});
        }
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (const float alpha : {0.3f, 1.0f}) {
        if (alpha < 1.0f) glDisable(GL_DEPTH_TEST);
        else glEnable(GL_DEPTH_TEST);
        for (const auto& [c, d] : batches) drawLines(d, GL_LINES, vp, QVector4D(c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, alpha));
    }
    glDisable(GL_BLEND);
}

void Viewport::drawLines(const std::vector<float>& data, GLenum mode, const QMatrix4x4& vp, const QVector4D& color) {
    if (data.empty()) return;
    lineProg_.bind();
    lineProg_.setUniformValue("uVP", vp);
    lineProg_.setUniformValue("uColor", color);
    lines_.vao.bind();
    lines_.vbo.bind();
    lines_.vbo.allocate(data.data(), int(data.size() * sizeof(float)));
    glDrawArrays(mode, 0, GLsizei(data.size() / 4));
    lines_.vao.release();
}

// ---------------------------------------------------------------------------
// The snapshot
// ---------------------------------------------------------------------------
void Viewport::setSnapshot(std::shared_ptr<const ke::Frame> s) {
    snap_ = std::move(s);
    ++snapSerial_;
    if (snap_ && (!framedOnce_ || snap_->sceneName != lastSceneName_)) { // another scene: framed anew
        framedOnce_ = true;
        lastSceneName_ = snap_->sceneName;
        frameScene();
    }
    updateBodyOrbit();
    update();
}

// ---------------------------------------------------------------------------
// Meshes and their textures on the GPU
// ---------------------------------------------------------------------------
QMatrix4x4 Viewport::modelMatrix(const ke::Frame::Body& b) {
    const ke::Matrix3x3 R = b.rot.toMatrix3x3();
    return QMatrix4x4(R.m[0][0], R.m[0][1], R.m[0][2], b.pos.x, R.m[1][0], R.m[1][1], R.m[1][2], b.pos.y, R.m[2][0], R.m[2][1],
                      R.m[2][2], b.pos.z, 0, 0, 0, 1);
}

void Viewport::pruneMeshCache() {
    // Meshes of bodies that are gone (another frame, another scene): only the cache still holds them.
    for (auto it = meshCache_.begin(); it != meshCache_.end();) {
        if (it->second.mesh.use_count() == 1) it = meshCache_.erase(it);
        else ++it;
    }
}

// A body's mesh on the GPU, made once per mesh and shading (MeshNormals.h): expanded triangles -
// position, normal, and a model's own colour a corner when it has one.
Viewport::GpuMesh& Viewport::gpuMesh(const std::shared_ptr<const ke::TriMesh>& m, bool smooth) {
    GpuMesh& g = meshCache_[{m.get(), smooth}];
    if (g.vao) return g;
    g.mesh = m;
    const std::vector<Vector3> normals = meshNormals(*m, smooth);
    const bool coloured = m->coloured();
    const int stride = coloured ? 9 : 6;
    std::vector<float> v;
    v.reserve(m->triangles.size() * 3 * size_t(stride));
    for (size_t t = 0; t < m->triangles.size(); ++t)
        for (int k = 0; k < 3; ++k) {
            const Vector3& p = m->positions[m->triangles[t][k]];
            const Vector3& n = normals[3 * t + size_t(k)];
            v.insert(v.end(), {p.x, p.y, p.z, n.x, n.y, n.z});
            if (coloured) v.insert(v.end(), {m->triangleColors[t].x, m->triangleColors[t].y, m->triangleColors[t].z});
        }
    g.vao = std::make_unique<QOpenGLVertexArrayObject>();
    g.vao->create();
    g.vao->bind();
    g.vbo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    g.vbo->create();
    g.vbo->bind();
    g.vbo->allocate(v.data(), int(v.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    if (coloured) {
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride * sizeof(float), reinterpret_cast<void*>(6 * sizeof(float)));
    }
    g.vao->release();
    g.count = int(v.size() / size_t(stride));
    return g;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void Viewport::drawBackground() {
    glDisable(GL_DEPTH_TEST);
    bgProg_.bind();
    bg_.vao.bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    bg_.vao.release();
    glEnable(GL_DEPTH_TEST);
}

// Every opaque body from its mesh at its pose, in its colour (a model's own colours where it has
// them) and with its surface; the see-through ones wait for drawSeeThrough.
void Viewport::drawBodies(const QMatrix4x4& view, const QMatrix4x4& proj) {
    meshProg_.bind();
    meshProg_.setUniformValue("uView", view);
    meshProg_.setUniformValue("uProj", proj);
    meshProg_.setUniformValue("uAlpha", 1.0f);
    for (const ke::Frame::Body& b : snap_->bodies) {
        if (!b.mesh || b.opacity < 1) continue;
        setSurfaceTexture(b.texture);
        meshProg_.setUniformValue("uModel", modelMatrix(b));
        meshProg_.setUniformValue("uColor", qv(b.color) * sleepDim(b));
        meshProg_.setUniformValue("uVertexColor", b.mesh->coloured() ? 1 : 0);
        GpuMesh& g = gpuMesh(b.mesh, b.smooth);
        g.vao->bind();
        glDrawArrays(GL_TRIANGLES, 0, g.count);
        g.vao->release();
    }
    meshProg_.setUniformValue("uVertexColor", 0);
}

// See-through bodies (an IK goal around the hand it drives): after everything opaque, blended with no
// depth write, inner walls first so the far side shows through the near one.
void Viewport::drawSeeThrough(const QMatrix4x4& view, const QMatrix4x4& proj) {
    seeThrough_.clear();
    for (const ke::Frame::Body& b : snap_->bodies)
        if (b.opacity < 1 && b.mesh) seeThrough_.push_back(&b);
    if (seeThrough_.empty()) return;
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    meshProg_.bind();
    setSurfaceTexture({});
    meshProg_.setUniformValue("uView", view);
    meshProg_.setUniformValue("uProj", proj);
    for (const ke::Frame::Body* b : seeThrough_) {
        meshProg_.setUniformValue("uModel", modelMatrix(*b));
        meshProg_.setUniformValue("uColor", qv(b->color));
        meshProg_.setUniformValue("uAlpha", float(b->opacity));
        GpuMesh& g = gpuMesh(b->mesh, b->smooth);
        g.vao->bind();
        for (GLenum face : {GL_FRONT, GL_BACK}) glCullFace(face), glDrawArrays(GL_TRIANGLES, 0, g.count);
        g.vao->release();
    }
    meshProg_.setUniformValue("uAlpha", 1.0f);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

// One frame: the shadow maps, the GL state and the background, then the floor, the bodies, the
// glyphs and editing aids on top, and the 2D overlay last.
void Viewport::paintGL() {
    if (!glOk_) {
        QPainter p(this);
        p.fillRect(rect(), QColor(26, 28, 33));
        p.setPen(QColor(230, 200, 120));
        p.drawText(rect(), Qt::AlignCenter,
                   QString(uiText("Kinetica", "OpenGL 3.0 недоступен (%1).\nЗапустите с ключом --software-gl — рендер на процессоре.")).arg(renderer_));
        return;
    }
    QElapsedTimer painting;
    painting.start();
    syncLookThrough();
    beginGpuFrame();
    if (snap_) {
        pruneMeshCache();
        renderShadows();
        markGpuShadows();
        QString slotKinds; // what each shadow slot holds this frame: the mesh shader compiles only those in
        for (const ShadowMap& m : shadowMaps_) slotKinds += m.light < 0 ? '-' : m.cube ? 'C' : 'M';
        if (slotKinds != meshSlots_) buildMeshShader(slotKinds);
    }
    beginFrame();
    if (!snap_) return;
    const QMatrix4x4 view = viewMatrix(), proj = projMatrix(), vp = proj * view;
    applySceneLights(view);
    drawFloor(vp);
    drawBodies(view, proj);
    drawSeeThrough(view, proj);
    drawOverlays3D(vp);
    glBindVertexArray(0);
    glUseProgram(0);
    if (overlayVisible_) drawOverlay();
    endGpuFrame();
    countFrame(painting.nsecsElapsed());
}

// QPainter (the 2D overlay) may have changed the GL state during the previous frame: set it again.
void Viewport::beginFrame() {
    glViewport(0, 0, int(width() * devicePixelRatioF()), int(height() * devicePixelRatioF()));
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glClearColor(0.1f, 0.11f, 0.13f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawBackground();
}

// The floor: a faint grid on the world box's bottom, and the box's floor rectangle. Only its floor:
// the camera often stands inside the box, and its vertical edges would cut across the view.
void Viewport::drawFloor(const QMatrix4x4& vp) {
    const ke::AABB& d = snap_->domain;
    const Vector3 c = d.center(), e = d.extent();
    const float y = d.lo.y, half = std::max(e.x, e.z);
    if (!d.valid() || !(half > 0)) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    std::vector<float> g;
    const float step = std::pow(10.0f, std::floor(std::log10(half))) * 0.5f;
    const int n = int(half / step);
    for (int i = -n; i <= n; ++i) {
        const float o = float(i) * step;
        g.insert(g.end(), {c.x + o, y, c.z - n * step, 0, c.x + o, y, c.z + n * step, 0});
        g.insert(g.end(), {c.x - n * step, y, c.z + o, 0, c.x + n * step, y, c.z + o, 0});
    }
    drawLines(g, GL_LINES, vp, QVector4D(0.5f, 0.55f, 0.62f, 0.18f));
    const Vector3 lo = d.lo, hi = d.hi;
    drawLines({lo.x, y, lo.z, 0, hi.x, y, lo.z, 0, hi.x, y, lo.z, 0, hi.x, y, hi.z, 0,
               hi.x, y, hi.z, 0, lo.x, y, hi.z, 0, lo.x, y, hi.z, 0, lo.x, y, lo.z, 0},
              GL_LINES, vp, QVector4D(0.62f, 0.72f, 0.9f, 0.3f));
    glDisable(GL_BLEND);
}

// The glyphs and the probe's drawing over the bodies, then the editing aids.
void Viewport::drawOverlays3D(const QMatrix4x4& vp) {
    drawJoints(vp);
    drawSensors(vp);
    drawDebugProbe(vp);
    drawHighlight(vp);
    drawColliderGuides(vp);
    drawPaths(vp);
    drawOverlayLines(vp);
    drawGoalDrops(vp);
    drawSceneMarkers(vp);
    drawGhosts(vp);
    drawGizmo(vp);
}

// The frames painted over the last second and what one took on the window's thread, on average (with a software
// renderer the drawing itself; with a GPU what it took to hand the frame over).
void Viewport::countFrame(qint64 paintNs) {
    ++fpsFrames_;
    paintNs_ += paintNs;
    recordFrameInterval();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - fpsLast_ > 1000) {
        fps_ = float(fpsFrames_) * 1000.0f / float(now - fpsLast_);
        paintMs_ = float(double(paintNs_) * 1e-6 / std::max(fpsFrames_, 1));
        const double scene = snap_ && !editMode_ ? double(snap_->time) : -1;
        realtime_ = scene >= 0 && sceneAtSecond_ >= 0 && fpsLast_ > 0 ? (scene - sceneAtSecond_) * 1000.0 / double(now - fpsLast_) : 0;
        sceneAtSecond_ = scene;
        fpsFrames_ = 0, paintNs_ = 0;
        fpsLast_ = now;
    }
}

// ---------------------------------------------------------------------------
// 2D overlay
// ---------------------------------------------------------------------------
// The scene's name and under it a creating tool's next step, or in play the clock (the mode itself is said once, by the
// simulation bar under the view; the frame's numbers by the HUD, F3); then the navigation cube and the marks.
void Viewport::drawOverlay() {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = p.font();
    f.setPointSizeF(10.5);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(235, 238, 245));
    p.drawText(14, 24, QString::fromStdString(snap_->sceneName));
    f.setBold(false);
    f.setPointSizeF(9);
    p.setFont(f);
    p.setPen(QColor(170, 176, 188));
    const double time = double(snap_->time);
    if (editMode_ && createMode_) p.drawText(14, 42, createTool_.hint(createWhat_)); // what the next click does
    else if (!editMode_) p.drawText(14, 42, QString(uiText("Kinetica", "t = %1 с")).arg(time, 0, 'f', 2));
    if (!snap_->grips.empty()) { // the gripper as it reports itself, playing or paused (an edited scene's frame has none)
        using S = ke::Frame::GripState;
        const ke::Frame::GripInfo& g = snap_->grips.front();
        const QString text = g.state == S::Holding ? uiText("Kinetica", "Схват: держит ✓") : g.state == S::Empty ? uiText("Kinetica", "Схват: сомкнут — пусто, промах")
                           : g.state == S::Closing ? uiText("Kinetica", "Схват: смыкается…") : uiText("Kinetica", "Схват: открыт (пробел — сомкнуть)");
        const QColor colour = g.state == S::Holding ? QColor(110, 220, 120) : g.state == S::Empty ? QColor(255, 160, 80) : QColor(170, 176, 188);
        f.setBold(g.state == S::Holding || g.state == S::Empty);
        p.setFont(f);
        p.setPen(colour);
        p.drawText(14, 60, text);
        f.setBold(false);
        p.setFont(f);
        p.setPen(QColor(170, 176, 188));
    }
    drawAxesAndHints(p);
    drawOrbitPivot(p);
    drawContactDots(p);
    drawProbeLabels(p);
    drawLabMarker(p);
    drawHud(p);
    drawPlayFrame(p);
    p.end();
}

// The navigation cube in the top-right corner, the selection box while it is drawn, and the live
// amount of a drag next to the cursor. What the buttons do is in the status bar (hintChanged).
void Viewport::drawAxesAndHints(QPainter& p) {
    drawOverlayMarks(p);
    navCube_.draw(p, viewMatrix(), size(), navPressed_ ? navPressHit_ : navHover_);
    if (editMode_) {
        drawSelectionBox(p);
        drawShapeHandles(p);
        drawToolDrag(p);
        drawEditLabel(p);
    }
}

// The collider mode's points: discs over everything, lit under the mouse and while dragged.
void Viewport::drawShapeHandles(QPainter& p) {
    if (handles_.empty()) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const int dragged = editTool_.draggingHandle() ? editTool_.draggedIndex() : -1;
    for (const ShapeHandle& h : handles_) { // what a point rides on, under the points: a dial's circle
        QPolygonF line;
        for (const ke::Vector3& w : h.guide) {
            bool front = false;
            const QPointF at = pixelOf(w, &front);
            if (front) line << at;
        }
        p.setPen(QPen(QColor(255, 146, 40, 170), 1.2));
        p.drawPolyline(line);
    }
    for (size_t i = 0; i < handles_.size(); ++i) {
        bool front = false;
        const QPointF at = pixelOf(handles_[i].centre + handles_[i].axis * handles_[i].reach, &front);
        if (!front) continue;
        const bool lit = int(i) == hoveredHandle_ || int(i) == dragged;
        p.setPen(QPen(QColor(18, 40, 24), 1.5));
        p.setBrush(lit ? QColor(255, 214, 90) : handles_[i].plugin ? QColor(255, 146, 40) : QColor(110, 232, 130)); // a plugin's: C4D's orange
        p.drawEllipse(at, lit ? 7.5 : 6.0, lit ? 7.5 : 6.0);
    }
    p.restore();
}

// A drag tool's line while it is dragged: from the point pressed to the one under the mouse, and how long it is.
void Viewport::drawToolDrag(QPainter& p) {
    if (!pluginTool_.dragging()) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(255, 146, 40), 2.0));
    p.drawLine(pluginTool_.from(), pluginTool_.to());
    p.setBrush(QColor(255, 146, 40));
    p.drawEllipse(pluginTool_.from(), 4.0, 4.0);
    p.drawEllipse(pluginTool_.to(), 4.0, 4.0);
    Vector3 a, b, n;
    uint32_t object = 0;
    if (surfaceAt(pluginTool_.from(), a, n, object) && surfaceAt(pluginTool_.to(), b, n, object)) {
        p.setPen(QColor(255, 214, 90));
        p.drawText(pluginTool_.to() + QPointF(12, -8), QLocale().toString(double(ke::length(b - a)), 'f', 3) + uiText("Kinetica", " м"));
    }
    p.restore();
}

void Viewport::drawSelectionBox(QPainter& p) {
    if (!editTool_.boxSelecting()) return;
    p.save();
    p.setPen(QPen(QColor(120, 180, 255), 1.0, Qt::DashLine));
    p.setBrush(QColor(64, 150, 255, 40));
    p.drawRect(editTool_.boxRect());
    p.restore();
}

void Viewport::setColliderGuides(std::vector<float> lines) {
    for (auto& part : guidesByState_) part.clear();
    for (size_t i = 0; i + 8 <= lines.size(); i += 8) { // one segment: two points of x y z state
        const int state = std::clamp(int(lines[i + 3]), 0, 2);
        guidesByState_[state].insert(guidesByState_[state].end(), lines.begin() + long(i), lines.begin() + long(i + 8));
    }
    colliderGuides_ = std::move(lines);
    update();
}

// The collider wireframes (Houdini's collision guide) where they are in front of the geometry, and
// faintly where the geometry hides them, so a barrel around a sphere reads whole. Pale green: a
// body that moves; grey: a fixed obstacle; dim green: a body the engine put to sleep.
void Viewport::drawColliderGuides(const QMatrix4x4& vp) {
    if (colliderGuides_.empty()) return;
    const QVector4D colours[3] = {{0.486f, 1.0f, 0.604f, 0.7f}, {0.74f, 0.76f, 0.80f, 0.6f}, {0.486f, 1.0f, 0.604f, 0.3f}};
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for (int state = 0; state < 3; ++state) {
        QVector4D faint = colours[state];
        faint.setW(faint.w() * 0.55f); // a collider sits inside its geometry: the hidden part must still read
        glDepthFunc(GL_GREATER);       // hidden parts first, faint
        drawLines(guidesByState_[state], GL_LINES, vp, faint);
        glDepthFunc(GL_LEQUAL);
        drawLines(guidesByState_[state], GL_LINES, vp, colours[state]);
    }
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

// Where a goal stands over the world - the depth a picture does not show: a plumb line from each IK goal down to what is
// under it (a cube's top, the table, the floor; not the robot's own links) and a cross there, over everything, faint where
// hidden. A newcomer moving the UR5e's goal with W A S D missed a cube five times running: on the screen the hand was
// over it, in the world beside it.
void Viewport::drawGoalDrops(const QMatrix4x4& vp) {
    if (!snap_) return;
    std::vector<float> lines;
    for (const ke::Frame::Body& goal : snap_->bodies) {
        if (!goal.marker) continue;
        const Ray down{goal.pos, Vector3(0, -1, 0)};
        float ground = goal.pos.y - snap_->domain.lo.y, t;
        for (const ke::Frame::Body& b : snap_->bodies)
            if (!b.marker && !b.robot && rayBody(b, down, t) && t > 0 && t < ground) ground = t;
        const Vector3 at = down.at(ground);
        const float r = 0.03f;
        for (const Vector3& p : {goal.pos, at, at - Vector3(r, 0, 0), at + Vector3(r, 0, 0), at - Vector3(0, 0, r), at + Vector3(0, 0, r)})
            lines.insert(lines.end(), {p.x, p.y, p.z, 0.0f}); // the line shader's vertex: x y z and a spare
    }
    if (lines.empty()) return;
    const QVector4D colour(1.0f, 0.85f, 0.25f, 0.9f), faint(1.0f, 0.85f, 0.25f, 0.3f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_GREATER);
    drawLines(lines, GL_LINES, vp, faint);
    glDepthFunc(GL_LEQUAL);
    drawLines(lines, GL_LINES, vp, colour);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

// The goals' paths in cyan, over everything: the hidden stretches faint, as the collider guides.
// The pictures of view_draw: lines in the scene's depth (behind a body faint, as the paths), points and words over it.
void Viewport::setDrawing(const QString& key, Drawing d) {
    drawings_[key] = std::move(d);
    update();
}

void Viewport::clearPlayDrawings() {
    for (auto it = drawings_.begin(); it != drawings_.end();) it = it->second.play ? drawings_.erase(it) : std::next(it);
    update();
}

int Viewport::clearDrawings(const QString& prefix) {
    int gone = 0;
    for (auto it = drawings_.begin(); it != drawings_.end();)
        it = it->first.startsWith(prefix) ? (++gone, drawings_.erase(it)) : std::next(it);
    if (gone) update();
    return gone;
}

void Viewport::drawOverlayLines(const QMatrix4x4& vp) {
    if (drawings_.empty()) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for (const auto& [key, d] : drawings_) {
        const QVector4D seen(d.colour.redF(), d.colour.greenF(), d.colour.blueF(), 1.0f), hidden(seen.x(), seen.y(), seen.z(), 0.35f);
        glDepthFunc(GL_GREATER);
        drawLines(d.strip, GL_LINE_STRIP, vp, hidden), drawLines(d.segments, GL_LINES, vp, hidden);
        glDepthFunc(GL_LEQUAL);
        drawLines(d.strip, GL_LINE_STRIP, vp, seen), drawLines(d.segments, GL_LINES, vp, seen);
    }
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void Viewport::drawOverlayMarks(QPainter& p) {
    if (drawings_.empty()) return;
    const QMatrix4x4 view = camera_.view(), projection = camera_.projection(float(width()) / std::max(1, height()));
    const auto screen = [&](const Vector3& w, QPointF& out) {
        const QVector3D eye = view.map(QVector3D(w.x, w.y, w.z));
        if (eye.z() > -1e-3f) return false; // behind the eye
        const QVector3D n = projection.map(eye);
        out = QPointF((n.x() + 1) * 0.5 * width(), (1 - n.y()) * 0.5 * height());
        return true;
    };
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QPointF at;
    for (const auto& [key, d] : drawings_) {
        p.setPen(Qt::NoPen);
        p.setBrush(d.colour);
        for (const Vector3& w : d.points)
            if (screen(w, at)) p.drawEllipse(at, d.size * 0.5, d.size * 0.5);
        if (d.text.isEmpty() || !screen(d.at, at)) continue;
        p.setPen(QColor(0, 0, 0, 200));
        p.drawText(at + QPointF(7, -5), d.text);
        p.setPen(d.colour);
        p.drawText(at + QPointF(6, -6), d.text);
    }
    p.restore();
}

void Viewport::drawPaths(const QMatrix4x4& vp) {
    if (pathLines_.empty()) return;
    const QVector4D colour(0.35f, 0.85f, 1.0f, 0.95f), faint(0.35f, 0.85f, 1.0f, 0.35f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_GREATER);
    drawLines(pathLines_, GL_LINES, vp, faint);
    glDepthFunc(GL_LEQUAL);
    drawLines(pathLines_, GL_LINES, vp, colour);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}
