// The editor's first minute and its examples (see MainWindow.h): the scene it opens with, the
// bubble that leads to the first falling cube, the "Примеры" gallery, and the window without panels
// that makes the gallery's pictures.
#include "MainWindow.h"

#include "EditorOps.h"
#include "ExamplesGallery.h"
#include "FirstStartHint.h"
#include "RoleBar.h"
#include "SceneBuilder.h"
#include "SimController.h"
#include "Viewport.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QDateTime>
#include <QToolBar>
#include <QToolButton>

using namespace ke;

namespace {
const char* kFirstMinuteDone = "hints/firstMinuteDone";
} // namespace

// Where the editor's own scenes are: examples/ next to the program when installed, else the source
// tree the build came from.
QString MainWindow::examplesDir() {
    const QString nextToExe = QCoreApplication::applicationDirPath() + "/examples";
    if (QDir(nextToExe).exists()) return nextToExe;
#ifdef KE_EXAMPLES
    return QStringLiteral(KE_EXAMPLES);
#else
    return QDir::currentPath();
#endif
}

// The builder's first scene: the file given on the command line; on the very first start (or with
// --first-start) the welcome scene with the first-minute bubble; else an empty floor.
// This window's session of the journal (EditorOps.h); the session before it, when it crashed with
// changes in it, offered back by a plate - its scene as at its last line.
void MainWindow::startJournal() {
    QJsonObject before;
    const QJsonArray sessions = ops_->sessions();
    if (!sessions.isEmpty()) before = sessions.last().toObject();
    ops_->startSession();
    if (!before.value("crashed").toBool() || before.value("edits").toInt() == 0) return;
    const QString when = QDateTime::fromString(before.value("start").toString(), Qt::ISODateWithMs).toLocalTime().toString("dd.MM HH:mm");
    builder_->showBanner(QString("Прошлая сессия (%1, изменений: %2) закрылась сбоем. Её сцену можно вернуть.").arg(when).arg(before.value("edits").toInt()),
                         "Восстановить", [this, session = before.value("session").toString()] {
        const QString why = ops_->restore(session, -1);
        statusBar()->showMessage(why.isEmpty() ? "Сцена восстановлена по журналу (Ctrl+Z вернёт прежнюю)" : "Не восстановлено: " + why, 8000);
    });
}

void MainWindow::startBuilder(bool interactive) {
    QString error;
    const bool firstMinute = interactive && startScene_.isEmpty() &&
                             (forceFirstStart_ || !QSettings().value(kFirstMinuteDone, false).toBool());
    const QString scene = firstMinute ? examplesDir() + "/welcome.kscene" : startScene_;
    if (!scene.isEmpty() && !builder_->openFile(scene, error)) QMessageBox::warning(this, "Открыть сцену", error);
    if (scene.isEmpty() || !error.isEmpty()) builder_->newScene();
    if (firstMinute) QTimer::singleShot(0, this, &MainWindow::startFirstMinute); // after the first layout
    if (interactive) QTimer::singleShot(2500, this, [this] { checkBlankView(); }); // a white window: offer the CPU
    if (interactive) startJournal();
    // No question before the first frame: the simple mouse scheme is the default, and the choice
    // (Blender, Maya/Unity) is one line in the last hint and in Вид → Управление.
}

void MainWindow::startFirstMinute() {
    hint_ = new FirstStartHint(this);
    connect(hint_, &FirstStartHint::closeClicked, this, &MainWindow::finishFirstMinute);
    // Evaluated after the action has run its course (▶ switches the mode after announcing itself).
    connect(builder_, &SceneBuilder::firstAction, hint_, [this] { QTimer::singleShot(0, this, &MainWindow::onFirstMinuteAction); });
    hintStep_ = 0;
    showFirstMinuteStep();
}

// The bubble at the button of the current step: "Куб", the tile "Твёрдое", ▶.
void MainWindow::showFirstMinuteStep() {
    if (!hint_) return;
    if (hintStep_ == 0) {
        hint_->pointAt(shapesButton_, "Нажмите «Фигура ▾» → «Куб» и рисуйте мышью: подсказка вверху вида ведёт по шагам. Затем плитка «Твёрдое» и ▶");
    } else if (hintStep_ == 1) {
        QToolButton* tile = nullptr;
        for (QToolButton* b : builder_->inspectorPanel()->findChildren<QToolButton*>("roleButton"))
            if (b->text() == roleName(RoleIcon::Rigid) && b->isVisible()) tile = b;
        hint_->pointAt(tile, "Теперь плитка «Твёрдое»: куб станет телом, которое падает");
    } else {
        hint_->pointAt(createBar_->widgetForAction(playAct_),
                       "И ▶ Пуск — смотрите!\nПривыкли к Blender или Maya? Вид → Управление.");
    }
}

// Each step is one expected action. Anything else first: the user found their own way, the bubble goes.
void MainWindow::onFirstMinuteAction() {
    if (!hint_) return;
    QSettings().setValue(kFirstMinuteDone, true); // the bubble never comes back after the first action
    const Entity* e = nullptr;
    for (const Entity& x : builder_->graph().entities)
        if (x.id == builder_->selectedId()) e = &x;
    const bool cube = e && e->shape == ShapeKind::Box && !e->locked;
    const bool expected = hintStep_ == 0   ? cube && entityIsGeometryOnly(*e)
                          : hintStep_ == 1 ? cube && e->rigid.enabled
                                           : builder_->mode() == SceneBuilder::Mode::Playing;
    if (!expected || ++hintStep_ == 3) return finishFirstMinute();
    showFirstMinuteStep();
}

void MainWindow::finishFirstMinute() {
    QSettings().setValue(kFirstMinuteDone, true);
    if (hint_) hint_->deleteLater();
    hint_ = nullptr;
}

QString MainWindow::firstMinuteText() const { return hint_ ? hint_->text() : QString(); }

// ---------------------------------------------------------------------------
// Примеры
// ---------------------------------------------------------------------------
void MainWindow::openGallery() {
    ExamplesGallery gallery(examplesDir(), this);
    if (gallery.exec() != QDialog::Accepted || !gallery.chosen()) return;
    QString error;
    if (!builder_->openFile(gallery.chosen()->sceneFile, error)) QMessageBox::warning(this, "Открыть сцену", error);
}

// The gallery with every picture made, saved as a PNG; then the program quits (--gallery-shot).
void MainWindow::runGalleryShot(const QString& file) {
    auto* gallery = new ExamplesGallery(examplesDir(), this);
    gallery->show();
    auto save = [gallery, file] {
        gallery->grab().save(file);
        qApp->quit();
    };
    if (gallery->pendingThumbnails() == 0) QTimer::singleShot(600, gallery, save);
    else connect(gallery, &ExamplesGallery::thumbnailsDone, gallery, [save] { QTimer::singleShot(600, save); });
    QTimer::singleShot(20 * 60 * 1000, gallery, save); // whatever is ready after 20 minutes
}

// The picture of a scene for the gallery: only the 3D view, and no window on the screen.
void MainWindow::setThumbnailMode() {
    setAttribute(Qt::WA_DontShowOnScreen);
    for (QDockWidget* d : findChildren<QDockWidget*>()) d->hide();
    for (QToolBar* t : findChildren<QToolBar*>()) t->hide();
    menuBar()->hide();
    statusBar()->hide();
    view_->setOverlayVisible(false);
}
