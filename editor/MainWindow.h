#pragma once
// The editor's main window: it owns the simulation thread (SimController), the 3D view (Viewport)
// and the scene builder, and wires them to the toolbars, menus and docks. What each part of the
// window does is written next to its functions below; the .cpp files are split by topic.

#include "ControlScheme.h"
#include "Gizmo.h"
#include "SimController.h"

#include <map>
#include <QMainWindow>
#include <QStringList>

#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class PlotPanel;
class SceneBuilder;
class Viewport;
struct CreateWhat;
class QAction;
class QComboBox;
class QGroupBox;
class QLabel;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // Where the editor's own scenes and models are: examples/ next to the program, else the source tree's (MainWindowStart.cpp).
    static QString examplesDir();
    MainWindow();
    ~MainWindow() override;

    // Automation: the builder's scene (the --scene file, else a new one), simulated `frames` frames (none with
    // setEditScreenshot), a screenshot saved, quit.
    void runAutomation(int frames, const QString& screenshotPath);
    // The builder's first scene (MainWindowStart.cpp): the --scene file; on the very first start of
    // an interactive session the welcome scene with the first-minute bubble; else an empty floor.
    void startBuilder(bool interactive = false);
    void startJournal(); // this window's session of the journal; a crashed one before it offered back
    // The first-minute bubble even if it was seen before (the command line's --first-start).
    void setForceFirstStart(bool on) { forceFirstStart_ = on; }
    QString firstMinuteText() const; // what the bubble says now (empty: gone)
    void startPlugins();             // a person's window: the editor's and the user's plugins (MainWindowPlugins.cpp)
    // "Примеры": the gallery of every ready-made scene; a click opens one.
    void openGallery();
    void runGalleryShot(const QString& file); // the gallery with all its pictures as a PNG, then quit
    // Only the 3D view, no window on the screen: the gallery's pictures (--thumbnail). Before show().
    void setThumbnailMode();
    // The builder starts on this scene file instead of an empty floor (the command line's --scene).
    void setStartScene(const QString& path) { startScene_ = path; }
    // The screenshot of the automation shows the whole window (panels too), not only the 3D view.
    void setScreenshotWholeWindow(bool on) { auto_.wholeWindow = on; }
    // The screenshot of the builder's scene is taken in edit mode (nothing simulated).
    void setEditScreenshot(bool on) { auto_.editOnly = on; }
    // The screenshot looks through the scene's active camera (the first camera when none is).
    void setScreenshotThroughCamera(bool on) { auto_.throughCamera = on; }
    // The right-click menu of the edit mode: the tools, the coordinate system, and what can be done
    // to the selected object. Shares its state with the toolbar and the keys. The caller owns it.
    class QMenu* buildEditContextMenu();
    // The API (MainWindowApi.cpp): the operations every button, the magnet and a script share, and
    // the server agents reach them through (--api [port], 127.0.0.1 only). False: the port is taken.
    class EditorOps* operators() const { return ops_; }
    bool startApi(quint16 port);
    quint16 apiPort(); // the API's port, the server started on a free one when it is not serving (the console's Python)
    // A frame that came back all one colour: nothing was drawn (MainWindowPlay.cpp).
    static bool looksBlank(const QImage& frame);
    // On the GPU: a blank view asks whether to restart with the software renderer (`pretend`: ask anyway).
    void checkBlankView(bool pretend = false);

protected:
    void resizeEvent(class QResizeEvent* e) override; // the top bar fits the new width

private:
    // UI construction
    void connectViewport();       // the mouse joint, the software-OpenGL restart
    void buildActions();
    void buildResultsDock();
    void buildSceneBuilderDock(); // the "Конструктор": shapes with roles, no code (SceneBuilder)
    void connectSceneBuilder();
    // The big "Создать" bar (MainWindowToolbar.cpp): shapes, lights, camera, run / pause / stop / step,
    // undo / redo, the examples; on a narrow window it drops captions and shrinks icons, never cuts words.
    void buildMainToolbar();
    void addShapeButton(class QToolBar* tb, const std::vector<QAction*>& shapes); // "Фигура ▾": the primitives and a model
    // The primitives of «Фигура ▾»: each draws its shape with the mouse in the view (CreateTool,
    // ViewportTools.h) until Esc; a drawn shape is one call of scene_add.
    std::vector<QAction*> shapeCreateActions();
    QAction* createAction(const CreateWhat& what, const QIcon& icon, const QString& text, const QString& tip, const QString& name);
    void startCreating(const CreateWhat& what);
    void addDrawn(const CreateWhat& what, const ke::Vector3& position, const ke::Vector3& size, const ke::Vector3& target);
    void addLightButtons(class QToolBar* tb); // "Свет ▾" (Солнце, Лампа, Прожектор) and "Камера"
    void addSensorButton(class QToolBar* tb); // "Датчик ▾": the six kinds on the selected shape or its joint
    class QToolButton* shapesButton_ = nullptr;
    void addCompactButtons(class QToolBar* tb);
    void setToolbarLevel(int level);
    void fitMainToolbar();
    void buildToolShelf();        // the small bar at the left: select, move, rotate, scale, world / own axes
    void setTool(GizmoMode m);
    void updateToolShelf();
    void onModeChanged();         // edit / playing / paused: the buttons, the viewport, the status line
    void showEditContextMenu(const QPointF& pos);
    void addObjectActions(class QMenu* menu);
    // Many objects at once (MainWindowControls.cpp): group, array, instance, and the selection
    // helpers - in the context menu and in "Правка"; the popover after a Shift + gizmo drag.
    void addManyActions(class QMenu* menu);
    void addSelectHelpers(class QMenu* menu);
    void showClonePopover(const QPointF& pos);
    // A beginner sees the builder and the 3D view; "Эксперт" brings back the frame's numbers and the
    // probe's raw channels, "Графики" the readings and plots at the bottom.
    void buildLayoutActions();
    void setGraphsVisible(bool on);
    // The layout buttons at the right end of the menu line, as in VS Code's title bar
    // (MainWindowLayout.cpp): the left, bottom and right dock areas close and open with what was
    // open in them; a menu lists every panel, the status line, the tool shelf and "only the 3D view".
    void addLayoutButtons();
    void buildLayoutMenu(class QMenu* menu);
    bool dockAreaOpen(Qt::DockWidgetArea area) const;
    void setDockAreaOpen(Qt::DockWidgetArea area, bool open);
    void showDock(const QString& name);
    void syncLayoutButtons();
    void setOnlyViewport(bool on);
    void buildSceneMenu();
    void buildRunActions(); // buildActions, step by step
    void buildMenus();
    // Runtime
    void poll();
    void onSnapshot(const std::shared_ptr<const ke::Frame>& s);
    void togglePlay();

    // File operations
    void importModel(); // an OBJ / STL model as a new object of the builder
    void exportCsv();
    void screenshot();
    void showHelp();
    void showAbout();
    QImage windowImage(); // the window with the 3D view's overlay, for screenshots

    // The first minute (MainWindowStart.cpp)
    void startFirstMinute();
    void showFirstMinuteStep();
    void onFirstMinuteOp(const QString& name, const QJsonObject& args, const QJsonObject& answer);
    void finishFirstMinute();

    // Keys and mouse schemes (MainWindowControls.cpp)
    void buildEditMenu();                   // "Правка": every key as an action
    void buildViewKeys(class QMenu* menu);  // numpad views, gizmo size
    void buildControlsMenu();               // Вид -> Управление
    void buildCamerasMenu();                // Вид -> Камеры: the editor's view or one of the scene's cameras
    void buildCameraPicker();               // "Смотрю через: ..." in the corner of the 3D view
    void showShadowBudget(const std::vector<int>& lights); // lights ticked for a shadow but over the budget
    // The play mode made unmistakable (MainWindowPlay.cpp): the frame, the banner and big ▶ ⏸ ■ on
    // the view, K to keep the simulation's poses, Ctrl+K to find a command, Вид → Рендер на процессоре.
    void buildPlayExtras();
    void updateSimulationBar();
    void applyControlScheme(ControlScheme s);
    void appendShortcutTips();
    void connectViewportControls();

    // The Laboratory (MainWindowLab.cpp): a physics debugger in the spirit of PhysX PVD - the probe's
    // debug layers by group, the card of a clicked contact or body, the timeline of the last frames,
    // the live profiler. Opened by «Лаборатория» on the top bar or F8; a beginner never sees it closed.
    void buildLaboratory();
    void connectLaboratory();
    void setLaboratoryVisible(bool on);
    void labRecord(const std::vector<std::shared_ptr<const ke::Frame>>& frames); // every published frame
    bool labOnSnapshot(); // before a frame is drawn; false: a kept frame stays on screen
    void onLabClicked(QPointF pos);           // a click in the view: the contact under it, else the body
    void showContactCard(const ke::Frame& s, int index);
    // What the card says about a body; `live` ones come from the simulation thread, the others from the
    // kept frame only (its pose and sleep).
    struct LabBodyFacts {
        int body = -1;                 // index into the frame's bodies
        uint32_t object = 0;           // the entity it is
        bool live = false, alive = true, fixed = false, sleeping = false;
        float mass = 0;
        float height = std::nanf("");  // the centre of mass above the floor (live: the simulation's channel)
        ke::Vector3 inertia{0.0f}, pos{0.0f}, vel{0.0f}, angVel{0.0f}; // inertia: about the body's own axes
        std::string label;             // the name its channels go under («Куб» -> «Куб/height»)
    };
    void showBodyCard(int body);       // index into the frame's bodies
    void fillBodyCard(const LabBodyFacts& f);
    void labScrubStarted();                   // the timeline was taken: the scene pauses
    void labWentLive();                       // back to the newest frame: the scene runs on if it ran
    bool sceneRunning() const;
    void labPlot(const QString& channel);     // «Построить график»: the channel into the plots, the plots shown

    // The plots and what ties them to the window (MainWindowPlots.cpp): what the simulation measures
    // for them, the Laboratory's frame as their cursor, a click on them as a jump of the timeline.
    QWidget* buildPlots(); // the charts and the bar under them, for the results dock
    void connectPlots();
    void updateChannelRequests();             // the scene's channels while the plots or the Laboratory are open
    QString watchLabel(int object) const;     // the name a body's channels go under (by its entity id)
    void watchBody(int object);               // this body's channels measured from the next frame on
    void updatePlotObjects(const ke::Frame& snapshot);
    std::string plotObjectsScene_;
    void watchSelected();                     // the selected object's body, while the scene runs
    bool watchArm();                          // an IK goal selected: its arm's motors and miss in the plots
    bool armWatched_ = false;
    void forgetWatched();                     // editing again or another scene: no body watched
    void labSeekTime(double t);               // a click on a plot: the timeline to that moment
    void saveVisibleCsv();                    // «Сохранить CSV»: the lines on the charts
    void autoOpenPlots();                     // the first ▶: the plots open by themselves, compact
    void recordFrames(const std::vector<SimController::Reading>& readings); // every simulated frame
    std::string plotScene_;                   // the scene and the frame the plots recorded last
    uint64_t plotFrame_ = 0;
    std::vector<ke::ObjectRef> watched_;      // the bodies whose channels are measured (by their entities)
    bool plotsClosedByHand_ = false;          // the reader closed the plots: ▶ leaves them closed
    bool settingGraphs_ = false;              // setGraphsVisible at work (not the reader's hand)

    std::unique_ptr<SimController> ctrl_;
    Viewport* view_ = nullptr;
    QTimer* timer_ = nullptr;
    uint64_t lastFrame_ = 0;
    std::string lastSceneName_;

    QAction* graphsAct_ = nullptr;
    QAction* collidersAct_ = nullptr; // every object's collider as a green wireframe, not only the selected one's
    class QTabWidget* inspectorTabs_ = nullptr; // "Сцена" | "Объект"
    QAction* playAct_ = nullptr;
    QAction* recordAct_ = nullptr; // ▶ with a take: sim_record (MainWindowPlay.cpp)
    class ControllerKeys* controllerKeys_ = nullptr; // a person's keys to the scene's panels while it plays
    class PluginHost* plugins_ = nullptr;            // plugins as processes (MainWindowPlugins.cpp)
    void buildPlugins();
    class QTimer* recordBlink_ = nullptr;
    QAction* bottomWideAct_ = nullptr; // the bottom panel the window's whole width (MainWindowLayout.cpp)
    qint64 recordSince_ = 0;       // when the take started: a second press within 0.5 s is a double click's, not a stop
    void buildRecordAction();
    void syncRecord();             // the button as the take is: on, blinking, or off
    QAction* pauseAct_ = nullptr;
    QAction* stopAct_ = nullptr;
    QAction* stepAct_ = nullptr;
    QAction* localAct_ = nullptr;
    class QActionGroup* toolGroup_ = nullptr;
    PlotPanel* plots_ = nullptr;
    SceneBuilder* builder_ = nullptr;
    void buildOperators(); // the API's operations and the magnet, before the top bar
    void buildConsoleDock(); // the API console: every call as a Python line, and a line to type calls
    void buildSequencerDock(); // the Sequencer: a tab of the bottom panel (MainWindowPlots.cpp)
    void connectHistory();     // its playhead over the run's last frames (MainWindowPlots.cpp)
    class SequencerPanel* sequencer_ = nullptr;
    class EditorOps* ops_ = nullptr;
    class EditorServer* apiServer_ = nullptr; // the API, once started (apiPort)
    class MagnetController* magnet_ = nullptr;
    void showDrivenByApi();             // the plate "an agent drives the editor" while requests come in
    class QLabel* apiBanner_ = nullptr;
    class QTimer* apiIdle_ = nullptr;
    QString startScene_;
    class QToolBar* createBar_ = nullptr;
    std::vector<class QToolButton*> createButtons_; // shapes, model, lights, camera: first to lose captions
    std::vector<class QToolButton*> runButtons_;    // run / pause / stop / step, undo / redo
    int toolbarLevel_ = 0;                          // how much room the bar gave up (setToolbarLevel)
    class FirstStartHint* hint_ = nullptr;
    QAction* deleteAct_ = nullptr;
    QAction* duplicateAct_ = nullptr;
    class QActionGroup* schemeGroup_ = nullptr;
    QLabel* hintLabel_ = nullptr; // the status bar: what the mouse buttons and keys do now
    int hintStep_ = 0; // 0: «Запись», 1: the arm by its keys and ■, 2: the playhead, 3: what is next
    bool firstMinuteHeard_ = false; // the listener of the operations is there (once per window)
    bool forceFirstStart_ = false;
    class SimulationBar* simulationBar_ = nullptr;
    class CameraPicker* cameraPicker_ = nullptr;
    class CommandSearch* search_ = nullptr;
    QAction* keepAct_ = nullptr;   // K: keep the simulation's poses after Stop
    QAction* searchAct_ = nullptr; // Ctrl+K
    QAction* labAct_ = nullptr;    // F8: the Laboratory
    QAction* leftBarAct_ = nullptr, *bottomPanelAct_ = nullptr, *rightBarAct_ = nullptr; // Ctrl+B, Ctrl+J, Ctrl+Alt+B
    QAction* onlyViewAct_ = nullptr;                // F11: only the 3D view
    std::vector<QStringList> closedDocks_ = std::vector<QStringList>(9); // by dock area: what was open when it closed
    QStringList onlyViewDocks_;                     // what "only the 3D view" closed (docks, then "shelf", "status")
    class QDockWidget* labDock_ = nullptr;
    class LabPanel* lab_ = nullptr;
    class FrameHistory* history_ = nullptr; // the run's last frames: the Sequencer's playhead goes back over them
    bool labResume_ = false;       // the timeline paused a running scene: «Вживую» lets it run on
    bool labFresh_ = false;        // new frames were recorded since the view last drew one

    struct Automation {
        bool active = false;
        int frames = 0;
        int settle = 0;
        bool wholeWindow = false;
        bool editOnly = false;
        bool throughCamera = false;
        QString shot;
    } auto_;
};
