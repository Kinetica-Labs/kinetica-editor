// The play mode made unmistakable, and two safety nets (see MainWindow.h):
//   - the view gets an amber frame; mode and pose saving sit below it (SimulationBar);
//   - K keeps what the simulation did (SceneBuilder::keepSimulationPoses);
//   - Ctrl+K finds any command by name (CommandSearch);
//   - the scene's panel takes the keys while it plays (ControllerKeys: W A S D, R F, Z X, Space; T gives them back),
//     its words on the simulation bar;
//   - ● Запись beside ▶ (Ctrl+R): play and record the physics (sim_record) - again, or ■ Стоп, and the take is a new
//     sequence on the Sequencer's tab (one button, the run kept as an animation);
//   - a white window: if the GPU's first frames come back all one colour, the editor offers to start
//     again with the software renderer, and remembers the choice (Вид → Рендер на процессоре).
#include "MainWindow.h"
#include "UiLanguage.h"

#include "CommandSearch.h"
#include "ControllerKeys.h"
#include "Icons.h"
#include "EditorOps.h"
#include "SimulationBar.h"
#include "SceneBuilder.h"
#include "Viewport.h"

#include <QAction>
#include <QDateTime>
#include <QLabel>
#include <QJsonObject>
#include <QDockWidget>
#include <QCoreApplication>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

void MainWindow::buildPlayExtras() {
    keepAct_ = new QAction(uiText("Kinetica", "Сохранить положения из симуляции"), this);
    keepAct_->setObjectName("keepSimulation");
    keepAct_->setShortcut(Qt::Key_K);
    keepAct_->setToolTip(uiText("Kinetica", "Положения тел сейчас останутся после ■ Стоп (выбранных, или всех) — K"));
    connect(keepAct_, &QAction::triggered, builder_, &SceneBuilder::keepSimulationPoses);
    QMenu* sim = menuBar()->findChild<QMenu*>("simulationMenu");
    if (sim) sim->addAction(keepAct_);
    else addAction(keepAct_);
    simulationBar_ = new SimulationBar(centralWidget(), keepAct_);
    qobject_cast<QVBoxLayout*>(centralWidget()->layout())->addWidget(simulationBar_);

    search_ = new CommandSearch(this);
    searchAct_ = new QAction(uiText("Kinetica", "Найти команду…"), this);
    searchAct_->setObjectName("commandSearch");
    searchAct_->setShortcut(QKeySequence("Ctrl+K"));
    connect(searchAct_, &QAction::triggered, search_, &CommandSearch::open);
    QMenu* help = menuBar()->findChild<QMenu*>("helpMenu");
    if (help) help->insertAction(help->actions().value(0), searchAct_);
    else addAction(searchAct_);

    auto* cpu = new QAction(uiText("Kinetica", "Рендер на процессоре (после перезапуска)"), this);
    cpu->setObjectName("softwareRender");
    cpu->setCheckable(true);
    cpu->setChecked(QSettings().value("render/softwareGL", false).toBool());
    connect(cpu, &QAction::toggled, this, [](bool on) { QSettings().setValue("render/softwareGL", on); });
    menuBar()->findChild<QMenu*>("viewMenu")->addAction(cpu);
}

// The frame, status row and available actions follow every mode change.
void MainWindow::updateSimulationBar() {
    if (!simulationBar_) return;
    if (!controllerKeys_ && ops_) { // the keys of the scene's panels while it plays: their words on this bar
        controllerKeys_ = new ControllerKeys(ops_, builder_, this);
        connect(controllerKeys_, &ControllerKeys::changed, this, &MainWindow::updateSimulationBar);
    }
    if (controllerKeys_) controllerKeys_->sync(), simulationBar_->setNote(controllerKeys_->note());
    const SceneBuilder::Mode m = builder_->mode();
    const bool builderPlay = m != SceneBuilder::Mode::Edit;
    view_->setPlayFrame(builderPlay);
    simulationBar_->setState(m == SceneBuilder::Mode::Playing, m == SceneBuilder::Mode::Paused, builderPlay);
    keepAct_->setEnabled(builderPlay);
}

// A frame that is all one colour: nothing was drawn (a white window). A grid of samples is enough.
bool MainWindow::looksBlank(const QImage& frame) {
    if (frame.isNull() || frame.width() < 8 || frame.height() < 8) return true;
    const QRgb first = frame.pixel(0, 0);
    int spread = 0;
    for (int j = 0; j < 24; ++j)
        for (int i = 0; i < 24; ++i) {
            const QRgb c = frame.pixel(i * (frame.width() - 1) / 23, j * (frame.height() - 1) / 23);
            spread = std::max({spread, std::abs(qRed(c) - qRed(first)), std::abs(qGreen(c) - qGreen(first)),
                               std::abs(qBlue(c) - qBlue(first))});
        }
    return spread < 4;
}

// On the GPU only (the software renderer is the cure): if the view shows nothing, one question.
// `pretend`: the self-test's way to see the question without a broken driver.
void MainWindow::checkBlankView(bool pretend) {
    if (!pretend && (view_->softwareRenderer() || !looksBlank(view_->grabFramebuffer()))) return;
    auto* box = new QMessageBox(QMessageBox::Warning, uiText("Kinetica", "Окно пустое?"),
                                uiText("Kinetica", "Похоже, видеокарта не нарисовала сцену: окно белое или одноцветное. Так бывает, "
                                "когда драйвер занят другой программой (например, видеоредактором).\n\n"
                                "Перезапустить редактор с рендером на процессоре? Выбор запомнится "
                                "(Вид → Рендер на процессоре)."),
                                QMessageBox::NoButton, this);
    box->setObjectName("blankViewDialog");
    box->setAttribute(Qt::WA_DeleteOnClose);
    QPushButton* restart = box->addButton(uiText("Kinetica", "Перезапустить на процессоре"), QMessageBox::AcceptRole);
    box->addButton(uiText("Kinetica", "Нет, всё видно"), QMessageBox::RejectRole);
    connect(restart, &QPushButton::clicked, this, [] {
        QSettings().setValue("render/softwareGL", true);
        QStringList args = QCoreApplication::arguments().mid(1);
        args << "--software-gl";
        QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
        qApp->quit();
    });
    box->open(); // not modal to the rest: the window keeps working
}

void MainWindow::buildRecordAction() {
    recordAct_ = new QAction(controlIcon(ControlIcon::Record, 48), uiText("Kinetica", "Запись"), this); // ▶●
    recordAct_->setObjectName("actionRecord");
    recordAct_->setCheckable(true);
    recordAct_->setShortcut(QKeySequence("Ctrl+R"));
    recordAct_->setToolTip(uiText("Kinetica", "Пуск и запись (Ctrl+R): физика идёт и пишется. Ещё раз или ■ Стоп — запись становится секвенцией "
                           "«Запись N» во вкладке «Секвенсор»: смотреть бегунком и ▶ Просмотром; «▶ в Пуске» — повторять её как анимацию"));
    connect(recordAct_, &QAction::triggered, this, [this](bool on) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (!on && now - recordSince_ < 500) return recordAct_->setChecked(true); // the second click of a double click
        if (on) recordSince_ = now;
        ops_->run("sim_record", QJsonObject{{"on", on}}, [this](const QJsonObject& r) {
            syncRecord();
            if (!r["ok"].toBool()) return statusBar()->showMessage(r["error"].toString(), 8000);
            if (!r.contains("tracks")) return;
            statusBar()->showMessage(QString(uiText("Kinetica", "Записано: тел %1, ключей %2, %3 с — во вкладке «Секвенсор»: бегунок, ▶ Просмотр; «▶ в Пуске» повторит как анимацию"))
                                         .arg(r["tracks"].toInt()).arg(r["keys"].toInt()).arg(r["length"].toDouble(), 0, 'f', 2), 12000);
            if (auto* dock = findChild<QDockWidget*>("sequencerDock")) dock->show(), dock->raise(); // the take to look at
        }, "ui");
        syncRecord();
    });
    recordBlink_ = new QTimer(this);
    recordBlink_->setInterval(500);
    connect(recordBlink_, &QTimer::timeout, this, [this] {
        static bool lit = false;
        lit = !lit;
        recordAct_->setIcon(controlIcon(lit ? ControlIcon::Recording : ControlIcon::Record, 48));
    });
}

void MainWindow::syncRecord() {
    const bool on = ops_ && ops_->recording();
    if (on == recordBlink_->isActive()) return;
    recordAct_->setChecked(on);
    recordAct_->setText(on ? uiText("Kinetica", "Пишется") : uiText("Kinetica", "Запись"));
    if (on) recordBlink_->start();
    else recordBlink_->stop(), recordAct_->setIcon(controlIcon(ControlIcon::Record, 48));
}
