// The editor's first minute and its examples (see MainWindow.h): the scene it opens with, the
// bubble that leads to the first falling cube, the "Примеры" gallery, and the window without panels
// that makes the gallery's pictures.
#include "MainWindow.h"
#include "UiLanguage.h"
#include "Resources.h"

#include "EditorOps.h"
#include "EditorServer.h"
#include "ExamplesGallery.h"
#include "FirstStartHint.h"
#include "RoleBar.h"
#include "SceneBuilder.h"
#include "scene/SceneActions.h"
#include "SimController.h"
#include "Viewport.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QTemporaryFile>
#include <QDateTime>
#include <QToolBar>
#include <QToolButton>

using namespace ke;

namespace {
const char* kFirstMinuteDone = "hints/firstMinuteDone";
const char* kRestartPrefix = "kinetica-language-";

QStringList restartArguments(const QStringList& arguments, const QString& snapshot) {
    const QStringList replaced{"--language", "--resume-editor", "--scene", "--focus-object"};
    QStringList result;
    for (qsizetype i = 1; i < arguments.size(); ++i) {
        const QString arg = arguments.at(i);
        const QString option = arg.section('=', 0, 0);
        if (replaced.contains(option)) {
            if (arg == option) ++i;
        } else if (option != "--play" && option != "--first-start" && option != "--physics") {
            result << arg;
        }
    }
    return result << "--resume-editor" << snapshot;
}
} // namespace

QString MainWindow::examplesDir() { return resourceDir("examples"); }

void MainWindow::changeLanguage(const QString& code) {
    auto* menu = findChild<QMenu*>("languageMenu");
    if (!menu || !menu->isEnabled() || !qApp->property("keInteractive").toBool()) return;
    if (auto* action = menu->findChild<QAction*>("language_" + UiLanguage::active())) action->setChecked(true);
    if (!builder_->editing() &&
        QMessageBox::question(this, uiText("Kinetica", "Язык"),
            uiText("Kinetica", "Сменить язык и остановить текущую симуляцию? Редактор откроет сцену в режиме правки."))
            != QMessageBox::Yes) return;

    menu->setEnabled(false);
    if (ops_->recording()) {
        ops_->run("sim_record", {{"on", false}}, [this, code](const QJsonObject&) {
            restartForLanguage(code);
        }, "ui");
    } else {
        builder_->stop();
        restartForLanguage(code);
    }
}

void MainWindow::restartForLanguage(const QString& code) {
    auto failed = [this](const QString& error) {
        if (auto* menu = findChild<QMenu*>("languageMenu")) menu->setEnabled(true);
        QMessageBox::warning(this, uiText("Kinetica", "Язык"), error);
    };
    QTemporaryFile snapshot(QDir::temp().filePath(QString(kRestartPrefix) + "XXXXXX.json"));
    QJsonObject saved = builder_->restartSnapshot();
    saved.insert("kinetica_restart", 1);
    saved.insert("geometry", QString::fromLatin1(saveGeometry().toBase64()));
    const QByteArray bytes = QJsonDocument(saved).toJson(QJsonDocument::Compact);
    if (!snapshot.open() || snapshot.write(bytes) != bytes.size() || !snapshot.flush())
        return failed(uiText("Kinetica", "Не удалось сохранить текущую сцену для перезапуска."));
    snapshot.close();

    const QString previous = UiLanguage::preference();
    if (!UiLanguage::setPreference(code))
        return failed(uiText("Kinetica", "Не удалось сохранить выбор языка."));
    const quint16 port = apiServer_ ? apiServer_->port() : 0;
    delete apiServer_; // the new process can immediately reuse an explicitly requested port
    apiServer_ = nullptr;
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                 restartArguments(QCoreApplication::arguments(), snapshot.fileName()))) {
        UiLanguage::setPreference(previous);
        if (port) startApi(port);
        return failed(uiText("Kinetica", "Не удалось перезапустить редактор."));
    }
    snapshot.setAutoRemove(false);
    qApp->quit();
}

bool MainWindow::resumeAfterRestart(QString& error) {
    const QFileInfo info(restartFile_);
    if (info.absolutePath() != QDir::temp().absolutePath() || info.isSymLink() ||
        !info.fileName().startsWith(kRestartPrefix) || info.suffix() != "json") {
        error = uiText("Kinetica", "Некорректный файл восстановления сцены.");
        return false;
    }
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return false;
    }
    const QJsonObject saved = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    if (saved.value("kinetica_restart").toInt() != 1 || !saved.value("scene").isString()) {
        error = uiText("Kinetica", "Некорректный файл восстановления сцены.");
        return false;
    }
    if (!builder_->restoreRestartSnapshot(saved, error)) return false;
    restoreGeometry(QByteArray::fromBase64(saved.value("geometry").toString().toLatin1()));
    file.remove();
    return true;
}

// The builder's first scene: the file given on the command line; on the very first start (or with
// --first-start) the UR5e with the first-minutes bubble; else an empty floor.
// This window's session of the journal (EditorOps.h); the session before it, when it crashed with
// changes in it, offered back by a plate - its scene as at its last line.
void MainWindow::startJournal() {
    QJsonObject before;
    const QJsonArray sessions = ops_->sessions();
    if (!sessions.isEmpty()) before = sessions.last().toObject();
    ops_->startSession();
    if (!before.value("crashed").toBool() || before.value("edits").toInt() == 0) return;
    const QString when = QDateTime::fromString(before.value("start").toString(), Qt::ISODateWithMs).toLocalTime().toString("dd.MM HH:mm");
    builder_->showBanner(QString(uiText("Kinetica", "Прошлая сессия (%1, изменений: %2) закрылась сбоем. Её сцену можно вернуть.")).arg(when).arg(before.value("edits").toInt()),
                         uiText("Kinetica", "Восстановить"), [this, session = before.value("session").toString()] {
        const QString why = ops_->restore(session, -1);
        statusBar()->showMessage(why.isEmpty() ? uiText("Kinetica", "Сцена восстановлена по журналу (Ctrl+Z вернёт прежнюю)") : uiText("Kinetica", "Не восстановлено: ") + why, 8000);
    });
}

void MainWindow::startBuilder(bool interactive) {
    QString error;
    const bool resumed = !restartFile_.isEmpty() && resumeAfterRestart(error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, uiText("Kinetica", "Не удалось восстановить сцену"),
                             error + "\n" + QDir::toNativeSeparators(restartFile_));
        error.clear();
    }
    const bool firstMinute = interactive && restartFile_.isEmpty() && startScene_.isEmpty() &&
                             (forceFirstStart_ || !QSettings().value(kFirstMinuteDone, false).toBool());
    const QString scene = firstMinute ? examplesDir() + "/ur5e.kscene" : startScene_; // a robotics editor opens with a robot
    if (!resumed) {
        if (!scene.isEmpty() && !builder_->openFile(scene, error)) QMessageBox::warning(this, uiText("Kinetica", "Открыть сцену"), error);
        if (scene.isEmpty() || !error.isEmpty()) builder_->newScene();
    }
    if (firstMinute) QTimer::singleShot(0, this, [this] { // after the first layout: the robot close, the bubble at «Запись»
        for (const ke::JointObject& j : builder_->graph().joints) { // a robot's root: the group its links hang under
            const std::vector<uint32_t> chain = ke::ancestry(builder_->graph(), j.a);
            if (chain.size() < 2) continue;
            builder_->select(chain.back()); // the chain runs from the link up to its root
            builder_->frameSelected();
            builder_->select(0);
            break;
        }
        startFirstMinute();
    });
    if (interactive) QTimer::singleShot(2500, this, [this] { checkBlankView(); }); // a white window: offer the CPU
    if (interactive) startJournal();
    // No question before the first frame: the simple mouse scheme is the default, and the choice
    // (the other two schemes) is one line in the last hint and in Вид → Управление.
}

// The first minutes with the UR5e: the bubble leads through «Запись», the arm by its keys and ■, the playhead, and
// what is next. Each step waits for its operation (the window's buttons and keys are operations too: EditorOps' listener);
// anything else leaves the bubble where it is, ✕ closes it for good.
void MainWindow::startFirstMinute() {
    hint_ = new FirstStartHint(this);
    connect(hint_, &FirstStartHint::closeClicked, this, &MainWindow::finishFirstMinute);
    if (!firstMinuteHeard_) {
        firstMinuteHeard_ = true;
        ops_->addListener([this](const QString&, const QString& name, const QJsonObject& args, const QJsonObject& answer, bool query) {
            if (hint_ && !query) QTimer::singleShot(0, this, [this, name, args, answer] { onFirstMinuteOp(name, args, answer); });
        });
        connect(builder_, &SceneBuilder::modeChanged, this, [this] { // stopped with no take: nothing more to lead to
            if (hint_ && hintStep_ == 1 && builder_->editing())
                QTimer::singleShot(1500, this, [this] { if (hint_ && hintStep_ == 1) finishFirstMinute(); });
        });
    }
    hintStep_ = 0;
    showFirstMinuteStep();
}

// The bubble at the current step's button: «Запись», ■ Стоп with the keys, the Sequencer's timeline, «Датчик ▾».
void MainWindow::showFirstMinuteStep() {
    if (!hint_) return;
    if (hintStep_ == 0) {
        hint_->pointAt(createBar_->widgetForAction(recordAct_),
                       uiText("Kinetica", "Это рука UR5e. Нажмите «Запись»: сцена оживёт, и всё, что сделает рука, запишется"));
    } else if (hintStep_ == 1) {
        hint_->pointAt(createBar_->widgetForAction(stopAct_),
                       QString(uiText("Kinetica", "Ведите руку клавишами: W S A D — по полу, R F — вверх-вниз, Z X — поворот, пробел — схват. "
                               "Проще: щёлкните кубик и «Рука — к этому предмету» справа, потом F, пробел, R. %1"))
                           .arg(ops_->recording() ? uiText("Kinetica", "Хватит — «Стоп»") : uiText("Kinetica", "Записать — «Запись» рядом с «Пуском»")));
    } else if (hintStep_ == 2) {
        if (auto* dock = findChild<QDockWidget*>("sequencerDock")) dock->show(), dock->raise();
        hint_->pointAt(findChild<QWidget*>("sequencerTimeline"),
                       uiText("Kinetica", "Готово, это запись. Потяните бегунок по ленте — рука повторит. «▶ в Пуске» — Пуск сыграет её как анимацию"));
    } else {
        hint_->pointAt(findChild<QToolButton*>("sensorsButton"),
                       uiText("Kinetica", "Дальше: «Датчик ▾» — глаза и чувство силы руке; «Примеры» — другие сцены. Каждое действие — строкой Python: "
                       "Вид → Консоль API"));
        QTimer::singleShot(15000, this, [this] { if (hint_ && hintStep_ == 3) finishFirstMinute(); });
    }
}

// The step's operation came: the next step. A take is a scene_put of a sequence (sim_record's ■ or ■ Стоп).
void MainWindow::onFirstMinuteOp(const QString& name, const QJsonObject& args, const QJsonObject& answer) {
    if (!hint_ || !answer.value("ok").toBool(true)) return;
    QSettings().setValue(kFirstMinuteDone, true); // the bubble never comes back after the first step
    const bool take = name == "scene_put" && args.value("block").toString().startsWith("sequence");
    if (hintStep_ == 0 && (name == "sim_record" || name == "sim_play")) hintStep_ = 1;
    else if (hintStep_ == 1 && name == "sim_record" && ops_->recording()) {} // ▶ first, «Запись» after: the same step, its words anew
    else if (hintStep_ == 1 && take) hintStep_ = 2;
    else if (hintStep_ == 2 && name == "sequence_seek") hintStep_ = 3;
    else return;
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
    if (!builder_->openFile(gallery.chosen()->sceneFile, error)) QMessageBox::warning(this, uiText("Kinetica", "Открыть сцену"), error);
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
