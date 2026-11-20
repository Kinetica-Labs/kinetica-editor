#pragma once
// OpenGL viewport: the orbit camera with its control schemes and the navigation cube; the robot's
// bodies as lit meshes (smooth or faceted, textured; see-through marks such as an IK goal after the
// rest); the floor grid; the outline of the selected and hovered bodies; joint and sensor glyphs; the
// probe's debug lines, points and labels; the colliders as green guide lines and the IK goals' paths;
// lights and cameras as wireframes; the gizmo; the ghosts of the magnet's link and of a shape being
// drawn; a 2D overlay with the scene's name, its clock and the frame rate. In edit mode the frame is
// the scene graph as authored (EditView.h), in play the running world's (ke::Frame). The scene's
// lights shade the meshes and the view can look through a camera of the scene (ViewportLights.cpp);
// the shadows are in ViewportShadows.cpp, the bodies' textures in ViewportTextures.cpp and a picture
// dropped on an object in ViewportTextureDrop.cpp, the mouse and keys in ViewportInput.cpp and
// ViewportTools.cpp, the play frame, the orbit pivot and following a body in ViewportPlay.cpp, what
// the Laboratory draws (its marker, contact dots, probe labels) in ViewportLab.cpp.
// Needs only OpenGL 3.0 (GLSL 1.30): runs on any GPU of the last ~15 years and on the CPU through
// Mesa llvmpipe (--software-gl). A 3.3 core context is used when the driver offers one.

#include "ColliderGuides.h"
#include "ControlScheme.h"
#include "EditView.h"
#include "Gizmo.h"
#include "NavCube.h"
#include "OrbitCamera.h"
#include "SceneMarkers.h"
#include "ViewportTools.h"
#include "sim/Frame.h"

#include <QColor>
#include <QElapsedTimer>

#include <array>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QVector3D>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>

class QTimer;

class Viewport : public QOpenGLWidget, protected QOpenGLExtraFunctions {
    Q_OBJECT
public:
    explicit Viewport(QWidget* parent = nullptr);
    ~Viewport() override;

    void setSnapshot(std::shared_ptr<const ke::Frame> s);
    const std::shared_ptr<const ke::Frame>& snapshot() const { return snap_; }
    void frameScene();
    // What "Показать всё" (F) and a new scene frame: the scene builder's objects instead of the whole
    // world box (an invalid box: the world box again).
    void setFocusBox(const ke::AABB& box) { focusBox_ = box; }
    float renderFps() const { return fps_; }
    const QString& renderer() const { return renderer_; } // GL_RENDERER: the GPU, or llvmpipe
    // The performance plate (ViewportHud.cpp): frames a second, frame times, paint and step, speed, renderer.
    void setShowHud(bool on);
    bool showHud() const { return showHud_; }
    double liveFps() const;                     // over the last 30 painted frames
    double realtime() const { return realtime_; } // scene seconds a wall second, over the last second (0: editing)
    // The GPU's time for a frame and for its shadow pass, from timer queries two frames old (-1: none, a GL without
    // timer queries). A view bound by the GPU shows here, not in paintMs: the CPU only hands the frame over.
    float gpuMs() const { return gpuMs_; }
    float gpuShadowMs() const { return gpuShadowMs_; }
    float paintMs() const { return paintMs_; } // one frame's painting on the window's thread, the last second's mean

    // Interaction (used by the ViewportTool strategies; ViewportInput.cpp)
    OrbitCamera& camera() { return camera_; }
    bool followBody(uint32_t object); // 0 restores the independent scene camera
    uint32_t followedBody() const { return followedBody_; }

    // Which mouse button moves the camera, and how (ControlScheme.h).
    void setControlScheme(ControlScheme s) { scheme_ = s; updateHint(); }
    ControlScheme controlScheme() const { return scheme_; }
    CameraMove cameraMoveFor(Qt::MouseButton b, Qt::KeyboardModifiers m) const { return ::cameraMoveFor(scheme_, b, m); }
    // What the cursor is on (an object, a body), else the floor: the camera orbits and zooms around it.
    QVector3D pointUnderCursor(const QPointF& pos) const;
    bool flying() const { return rightHeld_ && !flyKeys_.empty(); } // the right button held with W A S D
    bool flewThisPress() const { return flew_; } // W A S D were used while the right button was down (no menu then)
    // Looks along ball k of the navigation cube (+X, -X, +Y, -Y, +Z, -Z): the numpad views too.
    void viewAlong(int k);
    const NavCube& navCube() const { return navCube_; }
    // What the mouse buttons and keys do right now, for the status bar (hintChanged when it changes).
    QString mouseHint() const;
    // Play: the nearest body of the physics engine the mouse may grab hit by the ray (its mesh's
    // triangles; a body only a few pixels wide is taken when the ray passes near it).
    bool pickBody(const Ray& ray, int& body, ke::Vector3& hit) const;
    // Any body under the cursor, static ones and drawn-only geometry too (not the marks): index into the frame's bodies.
    bool pickAnyBody(const Ray& ray, int& body, ke::Vector3& hit) const;
    // The scene builder's feedback (indices into the frame's bodies): the bodies of the selected thing
    // get an outline; in edit mode the body under the mouse a lighter one and geometry without a role a faint one.
    void setHighlightBodies(const std::vector<int>& bodies) { highlight_ = bodies; update(); }
    void setHoverBodies(const std::vector<int>& bodies) { hoverBodies_ = bodies; update(); }
    void setGhostBodies(const std::vector<int>& bodies) { ghostBodies_ = bodies; update(); }
    // Per body (index into the frame's bodies): locked, the mouse passes through it to what is behind.
    void setUnpickableBodies(const std::vector<char>& flags) { unpickable_ = flags; }
    bool unpickable(int body) const { return body >= 0 && body < int(unpickable_.size()) && unpickable_[size_t(body)]; }
    // The collider wireframes: pairs of points, x y z state (ColliderGuides::State: moving, fixed, asleep).
    void setColliderGuides(std::vector<float> lines);
    const std::vector<float>& colliderGuides() const { return colliderGuides_; }
    // The collider mode's points (SceneBuilder::shapeHandles; none: the mode is off): drawn over everything, each dragged
    // along its line - the press, the moves, the release and Esc reported as shapeHandleDragged.
    void setShapeHandles(std::vector<ShapeHandle> handles);
    const std::vector<ShapeHandle>& shapeHandles() const { return handles_; }
    bool colliderMode() const { return !handles_.empty() && !handles_.front().plugin; } // the points are a collider's
    int shapeHandleAt(const QPointF& pos) const;                   // the point within 9 px of the pixel (-1: none)
    float reachAlong(const ShapeHandle& h, const QPointF& pos) const; // where the mouse ray passes nearest its line
    ke::Vector3 handlePoint(const ShapeHandle& h, const QPointF& pos) const; // where the mouse takes it: on its line, or (free) in the plane facing the eye
    QPointF pixelOf(const ke::Vector3& world, bool* inFront = nullptr) const;
    void setHoveredHandle(int i);
    // The IK goals' paths (Entity::path): segments, x y z 0 per point - drawn over the scene, faint where hidden.
    void setPathLines(std::vector<float> lines) { pathLines_ = std::move(lines), update(); }
    void setOpensFile(std::function<bool(const QString&)> opens) { opensFile_ = std::move(opens); } // which dropped files are fileDropped
    // A picture over the view (view_draw): a plugin's or an agent's lines, points and words, in world metres, by key - the
    // same key again replaces it. Drawn while the scene plays: gone at Stop, as everything of a run.
    struct Drawing {
        std::vector<float> strip, segments; // four floats a point (lines.vert): a line through them, or pairs
        std::vector<ke::Vector3> points;
        QString text;
        ke::Vector3 at{0.0f};
        QColor colour{255, 146, 40}; // a plugin's orange
        float size = 6;
        bool play = false; // drawn while the scene ran (played or paused)
    };
    void setDrawing(const QString& key, Drawing d);
    int clearDrawings(const QString& prefix); // every key that starts so ("": all); how many went
    void clearPlayDrawings();                 // Stop: what was drawn while the scene ran goes with the run
    size_t drawings() const { return drawings_.size(); }
    bool hasDrawing(const QString& key) const { return drawings_.count(key) > 0; }
    const Drawing* drawing(const QString& key) const { // what stands under the key (a check reads its points)
        const auto at = drawings_.find(key);
        return at == drawings_.end() ? nullptr : &at->second;
    }
    // A plugin's tool (PluginHost): while on, a left click calls its command `op` with the point, the normal and the
    // object under the mouse; Esc, another tool of the shelf or making shapes leaves it.
    // drags: its command takes `to` - a press, a drag and a release give it two points, a line between them drawn meanwhile.
    void setPluginTool(const QString& op, const QString& label, bool drags = false);
    bool pluginToolDrags() const { return toolDrags_; }
    const QString& pluginTool() const { return toolOp_; }
    bool surfaceAt(const QPointF& pos, ke::Vector3& point, ke::Vector3& normal, uint32_t& object) const; // an object, else the floor
    void clickTool(const QPointF& pos);
    void dragTool(const QPointF& from, const QPointF& to); // a drag tool's two points: under the press, under the release
    // The 2D text over the view (the scene's name, the axes, the navigation cube); off for the gallery's pictures.
    void setOverlayVisible(bool on) { overlayVisible_ = on; update(); }
    // While an agent drives the editor through the API the gizmo is not drawn: nobody takes its handles then, and its
    // arrows stood over the hand the agent moved, hiding what it did.
    void setAgentDriving(bool driving) { // said at every call of an agent: a paint only when it changes - a paint a call
        if (agentDriving_ == driving) return; // made every call wait for the one before it to be drawn (7 ms, not 0.05)
        agentDriving_ = driving, update();
    }
    // The Laboratory (ViewportLab.cpp). While it is open a click - a press and a release without a
    // drag - is reported as labClicked, besides what the click does anyway; the thing its card shows
    // gets a ring in the view; and the words the debug layers write (Probe labels) are drawn.
    void setLabInspect(bool on) { labInspect_ = on; }
    bool labInspect() const { return labInspect_; }
    void setLabMarker(bool on, const ke::Vector3& at = ke::Vector3(0.0f));
    bool labMarkerShown() const { return labMarker_; }

    // Edit mode: the scene as authored, nothing simulated. The mouse selects objects through the
    // picker the scene builder gives (every object on a ray, nearest first) and drags the gizmo.
    void setEditMode(bool on);
    bool editMode() const { return editMode_; }
    // The line under the title in edit mode.
    // While the scene plays: a coloured frame round the view (ViewportPlay.cpp).
    void setPlayFrame(bool on);
    bool playFrame() const { return playFrame_; }
    // Drawn on the CPU (Mesa llvmpipe), not on a GPU.
    bool softwareRenderer() const { return renderer_.contains("llvmpipe", Qt::CaseInsensitive); }
    // While the camera orbits: a small mark on the point it turns around (on = false: none).
    void setOrbitPivot(const QVector3D& pivot, bool on);
    bool orbitPivotShown() const { return orbiting_; }
    void setEditPicker(std::function<std::vector<PickHit>(const Ray&)> picker) { picker_ = std::move(picker); }
    // The assembly magnet: its tool takes the left button in edit mode; the ghost of the link about to
    // be put and its joint's glyph, line pairs x y z 0 (empty: nothing under the mouse).
    void setMagnetMode(bool on);
    void setMagnetPreview(std::vector<float> ghost, std::vector<float> joint);
    // Making things with the mouse (CreateTool, ViewportTools.h): on, its tool takes the left button in edit
    // mode and makes `what` after `what`, step by step, until Esc or a right click with nothing begun,
    // another tool or the magnet, or play.
    void setCreateMode(bool on, const CreateWhat& what = {});
    bool createMode() const { return createMode_; }
    const CreateWhat& createWhat() const { return createWhat_; }
    // Joints drawn with both their frames and the gap between them (the solver's error).
    void setJointDebug(bool on) { jointDebug_ = on; update(); }
    std::vector<PickHit> pickAll(const Ray& ray) const { return picker_ ? picker_(ray) : std::vector<PickHit>(); }
    uint32_t pickEntity(const Ray& ray, ke::Vector3* hit = nullptr) const;
    // A click: the nearest object under the cursor; a second click on the same spot (or Alt+click)
    // takes the next one behind it along the ray, and so on round. Emits entityClicked.
    uint32_t clickSelect(const QPointF& pos, bool next);
    void setHoveredEntity(uint32_t id);
    Gizmo& gizmo() { return gizmo_; }
    GizmoView gizmoView() const { return GizmoView{camera_, size()}; }
    // What the gizmo sits on: the selected object (visible = there is one that may be edited).
    void setGizmoTarget(bool visible, const ke::Vector3& position, const ke::Quaternion& rotation);
    bool gizmoShown() const { return gizmoVisible_ && gizmo_.mode() != GizmoMode::Select && !magnetMode_ && !createMode_; }
    // Keyboard transforms (G, Shift+R, Shift+S): see Gizmo.h.
    void startModal(GizmoMode m);
    void finishModal();
    void cancelModal();
    void cancelGizmoDrag(); // Esc or the right button during a handle drag: the object goes back

    // Lights and cameras drawn as wireframes (SceneMarkers.h), from the scene builder.
    void setSceneMarkers(std::vector<SceneMarker> markers) { markers_ = std::move(markers); update(); }
    const std::vector<SceneMarker>& sceneMarkers() const { return markers_; }
    // Looking through a camera of the scene: the view is that camera - its eye, its turn (roll too)
    // and its lens. Moving the view then moves the camera (lookThroughMoved), as Blender's "lock
    // camera to view". Off: the editor's own view comes back as it was.
    void setLookThrough(bool on, const ke::Vector3& eye, const ke::Vector3& forward, const ke::Vector3& up, float fovDeg,
                        float nearClip, float farClip);
    bool lookingThrough() const { return lookThrough_; }
    bool sunShadowDrawn() const { return sunShadowDrawn_; } // the last frame had a sun's shadow map
    // Shadows are drawn for at most this many lights a frame: each costs a render pass (a lamp six).
    static constexpr int kMaxShadowLights = 4;
    // The lights (indices into the snapshot's lights) that asked for a shadow and got none in the last
    // frame, because more than kMaxShadowLights were ticked.
    const std::vector<int>& lightsWithoutShadow() const { return shadowLess_; }

signals:
    void grabStarted(int body, ke::Vector3 point);    // play: the left button took a body (its engine index)
    void grabMoved(ke::Vector3 target);
    void grabReleased();
    void goalDragged(uint32_t goal, ke::Vector3 position, ke::Quaternion turn, bool end); // play: the mouse drags an IK goal (GoalTool)
    void bodyClicked(int body);                       // play: left click on a body (index into the frame's bodies)
    void sensorClicked(uint32_t id);                  // playing: left click on a sensor's glyph
    void entityClicked(uint32_t id);                  // edit mode: a click selected this object (0: empty space); play: a see-through goal clicked
    void entityHovered(uint32_t id);                  // edit mode: the object under the mouse changed
    void gizmoStarted();                              // a handle, a Shift+drag or a keyboard transform began
    void gizmoMoved(const GizmoPose& pose);           // where it has taken the object so far
    void gizmoFinished();                             // confirmed: one step of the undo history
    void gizmoCancelled();                            // Esc / right button: put the object back
    void shapeHandleDragged(int index, ke::Vector3 point, int phase); // a point taken: 0 pressed, 1 moved, 2 released, 3 Esc
    void toolClicked(const QString& op, ke::Vector3 point, ke::Vector3 normal, uint32_t object); // a plugin's tool used
    void toolDragged(const QString& op, ke::Vector3 point, ke::Vector3 normal, uint32_t object, ke::Vector3 to, uint32_t toObject);
    void pluginToolChanged(const QString& op); // its tool taken or left (empty)
    void cloneDragStarted();                          // Shift was held when the handle drag began
    void cloneDragFinished(QPointF pos);              // ... and it ended here: ask how many copies
    void editContextMenu(QPointF pos);                // edit mode: a right click without a drag
    void entityToggled(uint32_t id);                  // edit mode: Shift / Ctrl + click on an object
    void boxSelected(QRectF rect, Qt::KeyboardModifiers modifiers); // edit mode: the selection box, released
    void frameSelectedRequested();                    // edit mode: a double click (F)
    void hintChanged(QString hint);                   // what the buttons and keys do now changed
    // Looking through a camera, the view was moved (orbit, pan, zoom, fly, an axis view): the camera goes there.
    void lookThroughMoved(ke::Vector3 eye, ke::Vector3 forward, ke::Vector3 up);
    // The GPU / driver cannot do OpenGL 3.0: the window offers a restart in software mode.
    void openGLUnsupported(QString renderer);
    // The lights left without a shadow changed (indices into the snapshot's lights; empty: none).
    void shadowsLeftOut(std::vector<int> lights);
    // The magnet: the mouse moved with no button; the left button pressed (Ctrl: connect two shapes),
    // dragged, released; a key of the magnet (1-8 a ready link, 0 the suggested one, A the axis, Tab
    // flip, M mirror, Esc out).
    void magnetMoved(QPointF pos);
    void magnetPressed(QPointF pos, Qt::KeyboardModifiers modifiers);
    void magnetDragged(QPointF pos);
    void magnetReleased(QPointF pos);
    void magnetKey(int key);
    // Making things: one is drawn - a shape's centre (standing on the surface) and size; a light's or a
    // camera's place and the point it looks at.
    void created(CreateWhat what, ke::Vector3 position, ke::Vector3 size, ke::Vector3 target);
    // Edit mode: an image file dropped on an object (ViewportTextureDrop.cpp) - its texture.
    void imageDropped(uint32_t id, QString file);
    void fileDropped(QString file); // a file the window opens (setOpensFile): a model, a robot (URDF)...
    // The Laboratory is open and the left button was clicked here (pressed and released within 4 px).
    void labClicked(QPointF pos);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    bool event(QEvent* e) override; // a keyboard transform takes the keys its shortcuts would get
    void dragEnterEvent(QDragEnterEvent* e) override; // ViewportTextureDrop.cpp
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    struct Buffer {
        QOpenGLVertexArrayObject vao;
        QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
        int count = 0;
    };
    // A body's mesh on the GPU: expanded triangles, smooth or faceted normals, a model's own colours.
    struct GpuMesh {
        std::unique_ptr<QOpenGLVertexArrayObject> vao;
        std::unique_ptr<QOpenGLBuffer> vbo;
        int count = 0;
        // The mesh is held here too, so its address (the cache key) cannot be taken by a new mesh
        // while the entry exists; the entry is dropped once only the cache holds the mesh.
        std::shared_ptr<const ke::TriMesh> mesh;
    };
    struct ImageTexture { GLuint id = 0; qint64 stamp = 0; };

    void buildShaders();
    void buildMeshShader(const QString& slotKinds);
    void beginFrame();
    void drawBackground();
    void drawFloor(const QMatrix4x4& vp);
    void drawLines(const std::vector<float>& data, GLenum mode, const QMatrix4x4& vp, const QVector4D& color);
    GpuMesh& gpuMesh(const std::shared_ptr<const ke::TriMesh>& m, bool smooth);
    void pruneMeshCache(); // needs the GL context current (paintGL)
    void setSurfaceTexture(const ke::SurfaceTexture& texture);
    GLuint imageTexture(const QString& file);
    void releaseImageTextures();
    void drawBodies(const QMatrix4x4& view, const QMatrix4x4& proj);
    void drawSeeThrough(const QMatrix4x4& view, const QMatrix4x4& proj);
    void drawOverlays3D(const QMatrix4x4& vp);
    void drawDebugProbe(const QMatrix4x4& vp); // the probe's lines and points
    // Silhouette outlines: selection (orange), hover (faint white), geometry without a role (fainter).
    void drawHighlight(const QMatrix4x4& vp);
    bool ensureMaskTarget(int w, int h);
    void buildMaskTriangles();
    void appendBodyTriangles(const ke::Frame::Body& b, std::vector<float>& out) const;
    void drawGizmo(const QMatrix4x4& vp);
    void drawJoints(const QMatrix4x4& vp);
    void drawSensors(const QMatrix4x4& vp);
    void drawGhosts(const QMatrix4x4& vp); // what a click would make: the magnet's link, the shape being drawn
    // ViewportLights.cpp: the wireframes of lights and cameras, the view through a camera, the lights.
    void drawSceneMarkers(const QMatrix4x4& vp);
    void syncLookThrough();                    // the view moved while looking through: say where to
    void applySceneLights(const QMatrix4x4& view); // the mesh shader's lights for this frame
    // ViewportShadows.cpp: the shadow maps of the lights with "Отбрасывает тени" ticked.
    void renderShadows();                      // choose the lights, draw their maps
    std::vector<int> pickShadowLights(std::vector<int>& without) const;
    bool renderDepthMap(int slot, const ke::Frame::LightInfo& light);
    bool renderDistanceCube(int slot, const ke::Frame::LightInfo& light);
    bool ensureDepthMap(int slot);
    bool ensureDistanceCube(int slot);
    void drawShadowCasters(QOpenGLShaderProgram& program);
    void bindShadowMaps(const QMatrix4x4& view); // the maps and their matrices, into the mesh shader
    void releaseShadowMaps();
    void drawEditLabel(class QPainter& p); // the live amount of a drag next to the cursor
    bool modalKey(QKeyEvent* e);
    void dragKey(QKeyEvent* e);        // X / Y / Z during a handle drag
    bool flyKey(QKeyEvent* e, bool down);
    void flyTick();
    void stopFlying();
    bool navPress(QMouseEvent* e);     // the navigation cube takes the press
    void updateHint();
    void drawSelectionBox(class QPainter& p);
    void drawShapeHandles(class QPainter& p);
    void modalDrag(); // the keyboard changed the transform: recompute it at the cursor
    bool playHandleAt(const QPointF& pos) const; // play: a press there takes the gizmo's handle
    uint32_t pickHandle(const Ray& ray, float& distance) const; // play: a see-through goal on the ray (0: none)
    ViewportTool& toolFor(Qt::MouseButton button, const QPointF& pos);
    void drawOverlay();
    void drawAxesAndHints(class QPainter& p);
    void drawPlayFrame(class QPainter& p);  // ViewportPlay.cpp
    void drawOrbitPivot(class QPainter& p);
    void drawContactDots(class QPainter& p);
    void drawProbeLabels(class QPainter& p);
    void drawLabMarker(class QPainter& p);     // ViewportLab.cpp
    void drawColliderGuides(const QMatrix4x4& vp);
    void drawPaths(const QMatrix4x4& vp);
    void drawGoalDrops(const QMatrix4x4& vp); // a plumb line from each IK goal to what is under it
    static constexpr size_t kContactDotsMost = 3000; // more contacts than this: no dots
    void countFrame(qint64 paintNs);
    void recordFrameInterval();
    void beginGpuFrame();  // ViewportHud.cpp: the finished frames' GPU times read, this frame's first mark
    void markGpuShadows(); // after the shadow pass
    void endGpuFrame();
    void drawHud(QPainter& p);
    void updateBodyOrbit();
    static QMatrix4x4 modelMatrix(const ke::Frame::Body& b); // the body's pose: its mesh into the world
    QMatrix4x4 viewMatrix() const { return camera_.view(); }
    QMatrix4x4 projMatrix() const { return camera_.projection(float(width()) / std::max(1, height())); }

    std::shared_ptr<const ke::Frame> snap_;
    uint64_t snapSerial_ = 0; // every snapshot gets a new serial: the outline mask is rebuilt once per snapshot

    QOpenGLShaderProgram meshProg_, lineProg_, bgProg_, outlineProg_, shadowProg_, distanceProg_;
    Buffer bg_, lines_;
    std::map<std::pair<const ke::TriMesh*, bool>, GpuMesh> meshCache_; // by mesh and smooth
    std::map<QString, ImageTexture> imageTextures_;
    std::vector<const ke::Frame::Body*> seeThrough_; // reused every frame

    QString renderer_;   // OpenGL renderer (GPU name, or llvmpipe on the CPU)
    bool glOk_ = true;   // OpenGL 3.0 or newer available
    QString glslHeader_; // "#version 330 core" on 3.3 core contexts, "#version 130" on 3.0
    OrbitCamera camera_;
    OrbitCamera sceneOrbit_, bodyOrbit_;
    uint32_t followedBody_ = 0;
    bool bodyOrbitSaved_ = false;

    CameraTool cameraTool_;
    GrabTool grabTool_;
    GoalTool goalTool_;
    EditTool editTool_;
    MagnetTool magnetTool_;
    CreateTool createTool_;
    bool magnetMode_ = false, jointDebug_ = false, createMode_ = false;
    CreateWhat createWhat_;
    std::vector<float> magnetGhost_, magnetJoint_;
    ViewportTool* grabbed_ = nullptr; // tool that received the press, keeps the drag
    bool framedOnce_ = false;
    std::string lastSceneName_;    // the scene is framed again when another one is loaded
    ke::AABB focusBox_;            // what to frame when valid (the builder's objects)
    std::vector<int> highlight_;   // bodies outlined as selected
    std::vector<int> hoverBodies_, ghostBodies_;
    bool overlayVisible_ = true, agentDriving_ = false;
    std::vector<char> unpickable_;          // per body: the mouse passes through
    std::vector<float> colliderGuides_;     // x y z state per point, two points a segment
    std::vector<ShapeHandle> handles_;      // the collider mode's points
    int hoveredHandle_ = -1;
    std::vector<float> guidesByState_[3];   // the same lines sorted by state: one colour per draw
    std::vector<float> pathLines_;
    std::map<QString, Drawing> drawings_;
    PluginTool pluginTool_;
    QString toolOp_, toolLabel_;
    bool toolDrags_ = false;
    void drawToolDrag(class QPainter& p);
    void drawOverlayLines(const QMatrix4x4& vp);
    void drawOverlayMarks(QPainter& p);
    QString draggedImage_;                  // an image file being dragged over the view
    QString draggedFile_;                   // ... or a file the window opens
    std::function<bool(const QString&)> opensFile_;
    bool labInspect_ = false;               // the Laboratory is open: clicks are reported (labClicked)
    bool labMarker_ = false;                // the ring round what the Laboratory's card shows
    ke::Vector3 labMarkerAt_{0.0f};
    QPointF labPress_;                      // where the left button went down, to tell a click from a drag
    bool labPressed_ = false;
    ControlScheme scheme_ = ControlScheme::Simple;
    NavCube navCube_;
    int navHover_ = NavCube::kOutside, navPressHit_ = NavCube::kOutside;
    bool navPressed_ = false, navDragged_ = false;
    QPoint navLast_;
    bool rightHeld_ = false, flew_ = false; // the right button is down; W A S D were used during it
    std::set<int> flyKeys_;
    QTimer* flyTimer_ = nullptr;
    QElapsedTimer flyClock_;
    QString lastHint_;
    bool editMode_ = false, gizmoVisible_ = false;
    Gizmo gizmo_;
    std::vector<GizmoPiece> gizmoPieces_; // reused every frame: drawing the gizmo allocates nothing
    std::function<std::vector<PickHit>(const Ray&)> picker_;
    uint32_t hoveredEntity_ = 0;
    QPointF lastMouse_;
    QString typed_; // the number typed during a keyboard transform
    bool playFrame_ = false;   // the play mode's frame round the view
    bool orbiting_ = false;    // the camera orbits: the pivot is marked
    QVector3D orbitPivot_;
    std::vector<SceneMarker> markers_;
    std::vector<float> markerSolid_, markerFaint_; // reused every frame
    bool lookThrough_ = false;
    OrbitCamera editorCamera_;                 // the editor's own view while looking through a camera
    QVector3D lookEye_, lookForward_, lookUp_; // the view as last set or reported (moves are the difference)
    // Shadows: one slot per light that casts one this frame (ViewportShadows.cpp).
    struct ShadowMap {
        int light = -1;                   // which of the snapshot's lights (-1: the slot is unused)
        bool cube = false;                // a lamp: a cube of distances; a sun or a spotlight: a depth map
        QMatrix4x4 viewProj;              // world -> the light's clip space (sun, spotlight)
        float nearClip = 0, farClip = 0;  // a spotlight's lens (0 0 for a sun); a lamp's reach in farClip
        ke::Vector3 lampAt;               // a lamp's position
        GLuint fbo = 0, depth = 0;        // the depth map
        GLuint cubeFbo = 0, cubeTex = 0, cubeDepth = 0; // the cube of distances and its depth buffer
    };
    ShadowMap shadowMaps_[kMaxShadowLights];
    std::vector<int> shadowLess_;              // ticked, but over the budget in the last frame
    bool sunShadowDrawn_ = false;
    QString meshSlots_;                        // the shadow slots compiled into the mesh shader (M map, C cube, - none)
    // The outline mask: an offscreen target the outlined objects are drawn into, and their triangles
    // in world space (selected, hovered, without a role), rebuilt only when the scene or the lists change.
    GLuint maskFbo_ = 0, maskTex_ = 0;
    int maskW_ = 0, maskH_ = 0;
    std::vector<float> maskTriangles_[3];
    uint64_t maskSerial_ = ~0ull;
    std::vector<int> maskLists_[3];
    // Cycling through the objects on one ray: the last click and what it found.
    QPointF lastClickPos_;
    QElapsedTimer lastClickTime_;
    QMatrix4x4 lastClickView_;
    std::vector<uint32_t> lastClickHits_;
    int lastClickIndex_ = -1;

    // fps
    qint64 fpsLast_ = 0;
    int fpsFrames_ = 0;
    float fps_ = 0, paintMs_ = 0;
    qint64 paintNs_ = 0;
    QElapsedTimer frameClock_;                  // since the last painted frame
    std::array<float, 120> frameMs_{};          // the last 120 frames' intervals, a ring
    size_t frameAt_ = 0, frameCount_ = 0;
    bool showHud_ = false; // F3: off at first, as Unity's Stats and Godot's monitors - a corner of numbers was noise to a newcomer
    double realtime_ = 0, sceneAtSecond_ = -1;  // the scene's time when the last second began
    class QOpenGLTimeMonitor* gpuClock_[2] = {}; // two frames' marks in turn: one read while the other records
    bool gpuPending_[2] = {};
    int gpuAt_ = 0;
    bool gpuTimed_ = false;                      // this frame records marks
    float gpuMs_ = -1, gpuShadowMs_ = -1;
};
