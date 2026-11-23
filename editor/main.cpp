// The editor's entry point: the dark palette, the text size and the style sheet of the whole window,
// the software OpenGL switch (it has to be chosen before the application exists), the command line
// (automation, tests, pictures, the API), and the counting operator new behind the probe's
// The window itself is MainWindow.h.
#include "Icons.h"
#include "MainWindow.h"
#include "EditorOps.h"
#include "AgentInstanceGuard.h"
#include "SelfTest.h"
#include "SceneBuilder.h"
#include "UiLanguage.h"

#include "core/Probe.h"

#include <QTimer>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QComboBox>
#include <QAbstractButton>
#include <QKeyEvent>
#include <QIcon>
#include <QDir>
#include <QAction>
#include <QCommandLineParser>

#include <cstdio>
#include <QPalette>
#include <QScreen>
#include <QStyleFactory>
#include <QSettings>
#include <QSurfaceFormat>

#include <cstring>
#include <vector>

static void applyDarkPalette(QApplication& app) {
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    const QColor bg(37, 40, 46), base(28, 30, 35), text(220, 223, 228), accent(64, 150, 255);
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, bg);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(48, 52, 60));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::PlaceholderText, QColor(130, 135, 145));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 124, 130));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 124, 130));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(120, 124, 130));
    app.setPalette(p);
}

// The frame of the window: group boxes, docks, the big toolbar, the inspector's tabs, the gallery.
// The first rule is one text size on every system: Windows gives Qt 9 pt, a GNOME desktop 11 pt,
// and the window is laid out for 9 pt (the top bar fits 1280 px). It has to be a rule of the sheet:
// under a style sheet the widgets take the platform theme's size, not QApplication::setFont's. Any
// more specific rule with its own size still wins; the family stays the system's own.
static QString windowStyle() {
    return "* { font-size: 9pt; }"
           "QGroupBox { border: 1px solid #3a3f48; border-radius: 6px; margin-top: 14px; padding-top: 6px; font-weight: 600; }"
           "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: #8fc1ff; }"
           "QDockWidget::title { background: #2c3038; padding: 5px; font-weight: 600; }"
           "QToolBar { spacing: 6px; padding: 4px; border: none; }"
           "QToolBar::separator:horizontal { width: 1px; margin: 12px 6px; background: #474d59; }" // a thin line between groups of buttons
           "QToolBar::separator:vertical { height: 1px; margin: 6px 8px; background: #474d59; }"
           "QToolButton { padding: 4px 8px; border-radius: 4px; }"
           "QToolButton:hover { background: #3a404b; }"
           "QToolButton:checked { background: #2d5c99; }"
           "QTableWidget { gridline-color: #333842; }"
           "QHeaderView::section { background: #2c3038; padding: 4px; border: none; }"
           // The big toolbar: airy, 8 px rhythm, readable contrast.
           "QToolBar#createBar { spacing: 4px; padding: 6px 8px; background: #2a2e36; border-bottom: 1px solid #3a3f48; }"
           "QToolBar#createBar[run=\"play\"], QStatusBar[run=\"play\"] { background: #1f3428; }" // the run's colour (MainWindow::syncRecord)
           "QToolBar#createBar[run=\"pause\"], QStatusBar[run=\"pause\"] { background: #3a3220; }"
           "QToolBar#createBar[run=\"record\"], QStatusBar[run=\"record\"] { background: #3b2224; }"
           "QToolBar#createBar QToolButton { padding: 4px 10px; border-radius: 8px; min-width: 64px; }"
           "QToolBar#createBar QToolButton:hover { background: #394150; }"
           "QToolBar#createBar QToolButton:pressed { background: #2d5c99; }"
           "QToolBar#createBar QToolButton::menu-indicator { image: none; width: 0; }"
           "QToolBar#createBar QToolButton::menu-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 18px;"
           " height: 22px; right: 8px; bottom: 2px; border: none; border-radius: 6px; background: transparent; }"
           "QToolBar#createBar QToolButton::menu-button:hover { background: #4b5566; }"
           "QToolBar#createBar QToolButton::menu-arrow { image: url(:/app/icons/menu-arrow.png); width: 8px; height: 5px; }"
           "QToolBar#createBar QToolButton#compactButton { min-width: 0; padding: 6px 12px; margin-left: 4px; border: 1px solid #3a3f48; }"
           "QToolBar#createBar QToolButton#compactButton:checked { background: #24466f; border: 1px solid #4096ff; }"
           "QToolBar#createBar QToolButton#compactButton::menu-indicator { image: none; }"
           "QTabWidget#inspectorTabs::pane { border: none; border-top: 1px solid #3a3f48; }"
           "QTabWidget#inspectorTabs QTabBar::tab { padding: 6px 18px; margin-right: 2px; border-top-left-radius: 6px;"
           " border-top-right-radius: 6px; background: #2c3038; color: #aab1bd; }"
           "QTabWidget#inspectorTabs QTabBar::tab:selected { background: #24466f; color: white; }"
           "QListWidget#galleryList { background: #1f2227; border: none; }"
           "QListWidget#galleryList::item { color: #dfe3e8; padding: 4px; border-radius: 8px; }"
           "QListWidget#galleryList::item:hover { background: #2f3540; }"
           "QLabel#galleryHint { color: #8b93a2; padding: 4px 2px 8px 2px; }";
}

// The scene builder's panels: the role tiles, the component cards, the collider buttons, the banner.
static QString builderStyle() {
    return "QWidget#builderPanel QLabel#sectionTitle { color: #8fc1ff; margin-top: 6px; }"
           "QWidget#builderPanel QLabel#headerName { font-size: 15px; font-weight: 600; }"
           "QWidget#builderPanel QLabel#emptyHint { color: #8b93a2; padding: 24px; }"
           "QWidget#builderPanel QLabel#roleRowLabel { color: #8b93a2; }"
           "QWidget#builderPanel QToolButton#roleButton { padding: 4px 2px; border: 1px solid #3a3f48; border-radius: 8px; background: #2c3038; color: #8b93a2; }"
           "QWidget#builderPanel QToolButton#roleButton:hover { background: #363c47; }"
           "QWidget#builderPanel QToolButton#roleButton:checked { background: #24466f; border: 1px solid #4096ff; color: white; font-weight: 600; }"
           "QWidget#builderPanel QPushButton#sectionHeader { border: none; text-align: left; padding: 5px 4px; font-weight: 600; }"
           "QWidget#builderPanel QPushButton#sectionHeader:hover { background: #333842; border-radius: 4px; }"
           "QWidget#builderPanel QTreeWidget { border: 1px solid #3a3f48; border-radius: 6px; padding: 2px; }"
           "QWidget#builderPanel QTreeWidget::item { padding: 3px 0; }"
           "QWidget#builderPanel QFrame#componentCard { border: 1px solid #3a3f48; border-radius: 8px; background: #2a2e35; }"
           "QWidget#builderPanel QToolButton#cardRemove { color: #9aa3b0; padding: 2px 6px; }"
           "QWidget#builderPanel QToolButton#cardRemove:hover { color: white; background: #7a2f35; }"
           "QWidget#builderPanel QPushButton#addComponent { padding: 9px; border: 1px dashed #4a5260; border-radius: 8px;"
           " background: #262a31; font-weight: 600; }"
           "QWidget#builderPanel QPushButton#addComponent:hover { background: #303642; border-color: #4096ff; }"
           "QWidget#builderPanel QToolButton#colliderButton { padding: 3px; border: 1px solid #3a3f48; border-radius: 6px; background: #22262c; }"
           "QWidget#builderPanel QToolButton#colliderButton:checked { background: #1f4a33; border: 1px solid #7cff9a; }"
           "QWidget#builderPanel QLabel#colliderKindName { color: #7cff9a; }"
           "QFrame#simulationBar { background: #25282e; border: none; border-top: 1px solid #3a3f48; }"
           "QFrame#simulationBar QLabel#simulationHint { color: #b8c4d0; }"
           "QFrame#simulationBar QPushButton#keepButton { background: #303642; border: 1px solid #4a5260;"
           " border-radius: 4px; padding: 3px 9px; color: #dcdfe4; }"
           "QFrame#simulationBar QPushButton#keepButton:hover { background: #3a4350; border-color: #4096ff; }"
           "QFrame#cameraPicker { background: rgba(18, 20, 24, 190); border: 1px solid rgba(64, 150, 255, 150); border-radius: 8px; }"
           "QFrame#cameraPicker QLabel { color: #b8c4d0; }"
           "QFrame#cameraPicker QComboBox { min-width: 150px; font-weight: 600; }"
           "QFrame#commandSearch { background: #22262c; border: 1px solid #4096ff; border-radius: 10px; }"
           "QFrame#commandSearch QLineEdit { padding: 8px; font-size: 11pt; border-radius: 6px; }"
           "QFrame#commandSearch QListWidget#commandList { border: none; background: #1d2025; }"
           "QFrame#commandSearch QListWidget#commandList::item { padding: 5px 6px; }"
           "QFrame#commandSearch QListWidget#commandList::item:selected { background: #24466f; }"
           "QFrame#commandSearch QLabel#commandTip { color: #8a93a3; }"
           "QFrame#inlineBanner { background: #4a3b16; border: 1px solid #c99a2e; border-radius: 8px; }"
           "QFrame#inlineBanner QLabel { color: #ffe3a3; }"
           "QFrame#inlineBanner QPushButton#bannerButton { background: #c99a2e; color: #1c1810; border: none; border-radius: 6px;"
           " padding: 5px 10px; font-weight: 600; }"
           "QFrame#inlineBanner QPushButton#bannerButton:hover { background: #e2b448; }";
}

// The Laboratory (LabPanel, LabCard): the same dark ground, one accent blue, quiet captions.
static QString labStyle() {
    return "QWidget#labPanel QLabel#labIntro { color: #8b93a2; }"
           "QWidget#labPanel QLabel#labSection { color: #8fc1ff; font-weight: 600; margin-top: 4px; }"
           "QWidget#labPanel QLabel#labGroup { color: #aab1bd; font-weight: 600; margin-top: 8px; padding-left: 2px; }"
           "QWidget#labPanel QPushButton#labPreset { padding: 6px 8px; border: 1px solid #3a3f48; border-radius: 8px; background: #2c3038; }"
           "QWidget#labPanel QPushButton#labPreset:hover { background: #363c47; }"
           "QWidget#labPanel QPushButton#labPreset:checked { background: #24466f; border: 1px solid #4096ff; }"
           "QWidget#labPanel QCheckBox#labLayer { padding: 2px 0; }"
           "QScrollArea#labLayerScroll { background: transparent; }"
           "QWidget#labLayers { background: transparent; }"
           "QFrame#labCard { border: 1px solid #3a3f48; border-radius: 8px; background: #2a2e35; }"
           "QFrame#labCard QLabel#labCardTitle { font-size: 14px; font-weight: 600; }"
           "QFrame#labCard QLabel#labCardSubtitle { color: #8b93a2; }"
           "QFrame#labCard QLabel#labCardName { color: #aab1bd; }"
           "QFrame#labCard QLabel#labCardValue { font-family: Consolas, 'Cascadia Mono', monospace; }"
           "QFrame#labCard QPushButton#labCardButton { padding: 6px 10px; border: 1px solid #4096ff; border-radius: 6px; background: #24466f; }"
           "QFrame#labCard QPushButton#labCardButton:hover { background: #2d5c99; }";
}

// Software rendering for PCs without a usable GPU / OpenGL driver: Qt's Mesa llvmpipe
// (opengl32sw.dll, an OpenGL 3.0 context) runs the same shaders on the CPU. Chosen by --software-gl or the
// environment variable KE_SOFTWARE_GL=1; it must be decided before the application object exists.
static bool wantsSoftwareGL(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (QString::fromLocal8Bit(argv[i]) == "--software-gl") return true;
    if (qEnvironmentVariableIntValue("KE_SOFTWARE_GL") != 0) return true;
    // Chosen once after a white window (MainWindow::checkBlankView) or in Вид → Рендер на процессоре.
    return QSettings("Kinetica", "Kinetica").value("render/softwareGL", false).toBool();
}

// While a popup (a menu, a combo box's list) is open, Qt on X11 grabs the pointer and the keyboard of the
// whole desktop for it. In a run nobody watches that was the person's input: their clicks and keys anywhere on
// the screen went to the hidden window, closed the popup under the agent and landed in the 3D view (a second X
// client's grab answers AlreadyGrabbed with a list open). Released after the fact, the grab closes the popup. So such a run asks the X11 plugin never to grab ("--nograb", the option Qt
// takes under a debugger): popups work inside the application, and the rig's hand sends its events to the
// widgets directly. The plugin removes the argument; elsewhere the parser takes it as a hidden option.
static std::vector<char*> withoutDesktopGrab(int argc, char** argv, bool unattended) {
    static char noGrab[] = "--nograb";
    std::vector<char*> args(argv, argv + argc);
    if (unattended) args.push_back(noGrab);
    args.push_back(nullptr);
    return args;
}

// A run nobody watches, known before the application object exists: the options of unattended() below, also as
// --option=value.
static bool unattendedArgs(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        for (const char* option : {"--agent-eval", "--self-test", "--gizmo-shots", "--screenshot", "--gallery-shot", "--thumbnail"})
            if (const size_t n = std::strlen(option); std::strncmp(argv[i], option, n) == 0 && (!argv[i][n] || argv[i][n] == '=')) return true;
    return false;
}

static void setSurfaceFormat(bool software, bool watched) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(software ? 0 : 4); // multisampling is costly on the CPU
    // A swap waits for the screen only in a window a person watches. A run nobody watches is not held to a screen: one
    // asleep or locked (DPMS off, the person away - just when an agent works) sends no frame, and every paint of the
    // hidden window then waited up to a second - an API edit took 49 ms instead of 1.2, timed checks of the self-test
    // failed (measured, GNOME on X11, Mesa). The interval asks any driver; vblank_mode asks Mesa, whose presentation
    // waits for the compositor whatever the interval says. KE_NO_VSYNC=1: the same for a watched window.
    const bool waits = watched && qEnvironmentVariableIntValue("KE_NO_VSYNC") == 0;
    if (!waits && qEnvironmentVariableIsEmpty("vblank_mode")) qputenv("vblank_mode", "0");
    fmt.setSwapInterval(waits ? 1 : 0);
    QSurfaceFormat::setDefaultFormat(fmt);
}

// The command line: automation (a scene, frames, a screenshot), the tests, the pictures, the API.
struct Options {
    QCommandLineOption frames{"frames", "Frames to simulate before the screenshot.", "n", "120"};
    QCommandLineOption shot{"screenshot", "Save a PNG of the viewport and quit.", "file"};
    QCommandLineOption size{"size", "Window size WxH.", "size", "1600x950"};
    QCommandLineOption software{"software-gl", "Render on the CPU (Mesa llvmpipe) - for PCs without a usable GPU."};
    QCommandLineOption window{"window", "The screenshot shows the whole window, panels too."};
    QCommandLineOption scene{"scene", "Open a scene file (*.kscene) in the scene builder.", "file"};
    QCommandLineOption play{"play", "Start the opened builder scene in simulation mode."};
    QCommandLineOption focusObject{"focus-object", "Frame a named group in the opened scene.", "name"};
    QCommandLineOption selfTest{"self-test", "Press the scene builder's buttons, check the results, quit (exit code = failures)."};
    QCommandLineOption shots{"shots", "With --self-test: also save screenshots (gizmo, edit and play) into the directory.", "dir"};
    QCommandLineOption gizmoShots{"gizmo-shots", "Save screenshots of the gizmo (move, rotate, scale) into the directory and quit.", "dir"};
    QCommandLineOption edit{"edit", "The builder's screenshot in edit mode: the scene as authored, nothing simulated."};
    QCommandLineOption physics{"physics", "Open the world physics settings tab."};
    QCommandLineOption camera{"camera", "The builder's screenshot looks through the scene's active camera (else its first)."};
    QCommandLineOption firstStart{"first-start", "Start as on the very first run: the UR5e arm and the first-minute hint."};
    QCommandLineOption thumbnail{"thumbnail", "Only the 3D view, no window on the screen (the gallery's pictures)."};
    QCommandLineOption galleryShot{"gallery-shot", "Make every picture of the examples gallery, save the gallery as a PNG, quit.", "file"};
    QCommandLineOption agentEval{"agent-eval", "Serve an isolated agent evaluation without showing windows or taking desktop input; use with --api."};
    QCommandLineOption api{"api", "Serve the editor's API on 127.0.0.1:<port> (JSON lines; tools/ke_editor.py).", "port"};
    QCommandLineOption language{"language", "Override the editor language for this run (ru or en).", "code"};
    QCommandLineOption script{"script", "Run a Python script that builds or changes the scene, once the window is up (script_run).", "file"};
    QCommandLineOption resumeEditor{"resume-editor", "Restore the editor's scene after a restart.", "file"};

    void addTo(QCommandLineParser& cli) const {
        cli.addOptions({frames, shot, size, software, window, scene, selfTest, shots, gizmoShots, edit, camera,
                        firstStart, thumbnail, galleryShot, physics, play, focusObject, api, agentEval, language, script});
        auto resume = resumeEditor;
        resume.setFlags(QCommandLineOption::HiddenFromHelp);
        cli.addOption(resume);
        QCommandLineOption noGrab{"nograb", "X11: never grab the desktop's pointer and keyboard (added to unattended runs)."};
        noGrab.setFlags(QCommandLineOption::HiddenFromHelp); // the X11 plugin removes it; other platforms leave it here
        cli.addOption(noGrab);
    }
};

// The number fields as a hand expects them (a newcomer's trials):
//   - a number typed with either decimal mark. Qt's fields take only the locale's - ',' on a Russian desktop - and a '.'
//     vanished without a word: "0.1" became 1. The other mark becomes the field's own on its way in;
//   - a number typed is applied when the hand goes elsewhere. Qt applies it on Enter or when the field loses the keyboard,
//     and a press on what takes no keyboard (Пуск, the toolbar, the 3D view in play) or a shortcut (Ctrl+S) left it
//     shown in the field and not applied - the field lied.
class NumberFields : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* to, QEvent* e) override {
        if (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::Shortcut) applyTyped(qobject_cast<QWidget*>(to));
        if (e->type() != QEvent::KeyPress) return false;
        auto* box = qobject_cast<QDoubleSpinBox*>(to);
        if (!box && to->parent()) box = qobject_cast<QDoubleSpinBox*>(to->parent()); // its line edit
        if (!box) return false;
        const auto* key = static_cast<QKeyEvent*>(e);
        const QString mark = box->locale().decimalPoint(), other = mark == "," ? "." : ",";
        if (key->text() != other) return false;
        QKeyEvent own(QEvent::KeyPress, mark == "," ? Qt::Key_Comma : Qt::Key_Period, key->modifiers(), mark);
        QCoreApplication::sendEvent(to, &own);
        return true;
    }

private:
    // The field with the keyboard - the application's, and the pressed window's own (an inactive window keeps its own) -
    // takes what was typed in it, unless the press is on that field.
    static void applyTyped(QWidget* pressed) {
        for (QWidget* focus : {QApplication::focusWidget(), pressed ? pressed->window()->focusWidget() : nullptr})
            if (auto* box = qobject_cast<QAbstractSpinBox*>(focus); box && pressed != box && !box->isAncestorOf(pressed))
                box->interpretText();
    }
};

// The keys stay where they were when a button, a check box or a list of the window is clicked: Qt gives
// such a widget the keyboard on a click, and then Space pressed the button just clicked instead of «Пуск», letters
// leafed through the list «Привод» - the editor's keys seemed dead (a person's report). Tab still reaches them; a dialog's
// own buttons keep theirs (Enter, Space on the focused one).
class KeysStay : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* to, QEvent* e) override {
        if (e->type() != QEvent::Polish) return false;
        auto* w = qobject_cast<QWidget*>(to);
        const bool takes = qobject_cast<QAbstractButton*>(w) || qobject_cast<QComboBox*>(w);
        if (takes && (w->focusPolicy() & Qt::ClickFocus) && !qobject_cast<QDialog*>(w->window())) w->setFocusPolicy(Qt::TabFocus);
        return false;
    }
};

// A run nobody watches - the self-test, the pictures, automation - never shows a window, so that
// nobody takes it for their own editor, and never takes the keyboard from the program in front:
//   - the main window stays on the screen, because Windows paints only what is on a screen (the 3D
//     view draws in its paint, and some checks watch what the paint does), but it is fully
//     transparent, has no taskbar button, is never activated and lets every click through;
//   - any other window of it (a menu, a popup, a tooltip) that would land on a screen is moved off
//     it before it is shown - the Show event comes before the window is put on the screen - or as
//     soon as it moves onto one (a tooltip placed again, a popup that places itself);
//   - an open popup (a menu, a combo box's list) holds no grab of the desktop's pointer and keyboard:
//     see withoutDesktopGrab.
class OffscreenGuard : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* object, QEvent* event) override {
        const bool shown = event->type() == QEvent::Show, moved = event->type() == QEvent::Move;
        auto* w = qobject_cast<QWidget*>(object);
        if ((!shown && !moved) || !w || !w->isWindow() || (moved && !w->isVisible())) return false;
        if (w->windowOpacity() == 0.0) return false; // the invisible main window
        if (shown) w->setProperty("keSeenOrder", QVariant::fromValue(++order_));
        for (const QScreen* screen : QGuiApplication::screens())
            if (screen->geometry().intersects(w->frameGeometry())) {
                w->setProperty("keSeenOrigin", w->mapToGlobal(QPoint()));
                w->move(w->pos() + QPoint(-20000, -20000));
                break;
            }
        return false;
    }
private:
    quint64 order_ = 0;
};

static bool unattended(const QCommandLineParser& cli, const Options& o) {
    for (const QCommandLineOption* option : {&o.selfTest, &o.gizmoShots, &o.shot, &o.galleryShot, &o.thumbnail, &o.agentEval})
        if (cli.isSet(*option)) return true;
    return false;
}

// What the window does after it is shown: a test, pictures, automation, or the builder for a person.
static int run(QApplication& app, MainWindow& w, const QCommandLineParser& cli, const Options& o) {
    if (cli.isSet(o.selfTest)) return runBuilderSelfTest(w, cli.value(o.shots)); // its journal: KE_JOURNAL (main sets a temporary one, removed at the end)
    if (cli.isSet(o.gizmoShots)) return runGizmoShots(w, cli.value(o.gizmoShots));
    w.setScreenshotWholeWindow(cli.isSet(o.window));
    w.setEditScreenshot(cli.isSet(o.edit));
    w.setScreenshotThroughCamera(cli.isSet(o.camera));
    w.setForceFirstStart(cli.isSet(o.firstStart));
    if (cli.isSet(o.scene)) w.setStartScene(cli.value(o.scene));
    if (cli.isSet(o.resumeEditor)) w.setRestartFile(cli.value(o.resumeEditor));
    if (cli.isSet(o.galleryShot)) {
        w.startBuilder(false);
        w.runGalleryShot(cli.value(o.galleryShot));
    } else if (cli.isSet(o.shot)) {
        w.runAutomation(cli.value(o.frames).toInt(), cli.value(o.shot));
    } else {
        w.startBuilder(true); // a person: the first start opens the UR5e
        w.startPlugins();     // and their plugins (an unattended run starts none)
    }
    if (auto* builder = w.findChild<SceneBuilder*>()) {
        for (const auto& group : builder->graph().groups) if (QString::fromStdString(group.name) == cli.value(o.focusObject)) {
            builder->select(group.id); builder->frameSelected(); break;
        }
        if (cli.isSet(o.play)) builder->play();
    }
    if (cli.isSet(o.physics))
        if (auto* a = w.findChild<QAction*>("worldPhysics")) a->trigger();
    return app.exec();
}

int main(int argc, char** argv) {
    const bool software = wantsSoftwareGL(argc, argv);
    if (software) QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    const bool nobodyWatches = unattendedArgs(argc, argv);
    setSurfaceFormat(software, !nobodyWatches);

    std::vector<char*> args = withoutDesktopGrab(argc, argv, nobodyWatches);
    int count = int(args.size()) - 1; // QApplication keeps both: they live as long as main
    QApplication app(count, args.data());
    QApplication::setApplicationName("Kinetica");
    QApplication::setOrganizationName("Kinetica");
    QApplication::setDesktopFileName("kinetica"); // packaging/kinetica.desktop: Wayland ties the window to its icon by this name
    QIcon applicationIcon;
    for (int size : {16, 24, 32, 48, 64, 128, 256}) // the K of the logo (resources/logo/kinetica-k.png), every size drawn, not scaled by the desktop
        applicationIcon.addFile(QString(":/app/icons/kinetica-%1.png").arg(size), QSize(size, size));
    app.setWindowIcon(applicationIcon);
    applyDarkPalette(app);
    app.setStyleSheet(windowStyle() + builderStyle() + labStyle());

    QCommandLineParser cli;
    cli.setApplicationDescription("Kinetica");
    cli.addHelpOption();
    const Options o;
    o.addTo(cli);
    cli.process(app);
    const QString language = cli.isSet(o.language) ? cli.value(o.language)
        : cli.isSet(o.selfTest) ? QString("ru") : UiLanguage::preference();
    if (language != "ru" && language != "en") {
        std::fprintf(stderr, "--language requires ru or en\n");
        return 2;
    }
    const UiLanguage uiLanguage(app, language);
    if (cli.isSet(o.selfTest) && UiLanguage::active() != language) return 2;
    if (cli.isSet(o.agentEval) && !cli.isSet(o.api)) {
        std::fprintf(stderr, "--agent-eval requires --api <port>\n");
        return 2;
    }
    // A test's edits are not the person's history: their journal is a file of this run alone, removed when the run ends
    // (left, 68 of them - 33 MB - were found in the temporary folder). One the caller named in KE_JOURNAL is its own. Nor
    // are its settings the person's: it reads and writes its own, in a folder of this run alone (a self-test's press of
    // ⇔ was found left in the person's layout) - the language above is read from the person's still.
    struct RunFiles {
        QString journal, settings;
        ~RunFiles() {
            if (!journal.isEmpty()) QFile::remove(journal);
            if (!settings.isEmpty()) QDir(settings).removeRecursively();
        }
    } runFiles;
    if (unattended(cli, o)) {
        const QString pid = QString::number(QCoreApplication::applicationPid());
        if (qEnvironmentVariableIsEmpty("KE_JOURNAL"))
            runFiles.journal = QDir::tempPath() + "/kinetica-journal-" + pid + ".jsonl", qputenv("KE_JOURNAL", runFiles.journal.toUtf8());
        runFiles.settings = QDir::tempPath() + "/kinetica-settings-" + pid;
        for (const QSettings::Format format : {QSettings::NativeFormat, QSettings::IniFormat})
            QSettings::setPath(format, QSettings::UserScope, runFiles.settings);
    }

    app.setProperty("keAgentEval", cli.isSet(o.agentEval));
    app.setProperty("keInteractive", !unattended(cli, o));
    AgentInstanceGuard agentSlot;
    QString slotError;
    if (cli.isSet(o.agentEval) && !agentSlot.acquire(slotError)) {
        std::fprintf(stderr, "%s\n", qPrintable(slotError));
        return 3;
    }
    app.installEventFilter(new NumberFields(&app));
    app.installEventFilter(new KeysStay(&app));
    MainWindow w;
    const QStringList wh = cli.value(o.size).split('x');
    w.resize(wh.value(0).toInt() > 0 ? wh.value(0).toInt() : 1600, wh.value(1).toInt() > 0 ? wh.value(1).toInt() : 950);
    if (cli.isSet(o.thumbnail)) w.setThumbnailMode();
    if (unattended(cli, o)) {
        // Not a window of the desktop at all (X11: override-redirect): the window manager neither places it, nor lists
        // it, nor watches it. Watched, a run that kept this thread busy for a few seconds - a long check of the
        // self-test, an agent's long call - put the desktop's «Приложение "Kinetica" не отвечает» with «Принудительно
        // завершить» on the person's screen: an invisible program offered for killing (a person's report).
        w.setWindowFlags(w.windowFlags() | Qt::Tool | Qt::WindowTransparentForInput | Qt::BypassWindowManagerHint);
        w.setAttribute(Qt::WA_ShowWithoutActivating);
        w.setWindowOpacity(0.0);
        app.installEventFilter(new OffscreenGuard(&app));
        for (Qt::UIEffect e : {Qt::UI_AnimateTooltip, Qt::UI_FadeTooltip, Qt::UI_AnimateMenu, Qt::UI_FadeMenu, Qt::UI_AnimateCombo})
            QApplication::setEffectEnabled(e, false); // a rolling tooltip is a window of its own, placed by Qt
    }
    w.show();
    if (!unattended(cli, o)) { // a person's window takes the keyboard at once: started from a script or a terminal in the
        w.raise();             // background, the desktop left it without focus and Space, F, Ctrl+Z went to the window before
        w.activateWindow();    // it (a person's report: no click anywhere, no key worked)
    }
    if (cli.isSet(o.api) && !w.startApi(quint16(cli.value(o.api).toUInt())))
        std::fprintf(stderr, "the API port %s is taken\n", qPrintable(cli.value(o.api)));
    if (cli.isSet(o.script)) // once the window's loop runs: the scene is there, the API answers the script
        QTimer::singleShot(0, &w, [&w, file = QFileInfo(cli.value(o.script)).absoluteFilePath()] {
            w.operators()->run("script_run", {{"file", file}}, [](const QJsonObject& r) {
                if (!r.value("ok").toBool()) std::fprintf(stderr, "%s\n", qPrintable(r.value("error").toString()));
            }, "ui");
        });
    return run(app, w, cli, o);
}
