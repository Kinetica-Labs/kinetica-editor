// The window's layout (see MainWindow.h): three buttons at the right end
// of the menu line - where they take no room from the
// top bar - close and open the left, the bottom and the right dock area together with whatever
// was open in them (Ctrl+B, Ctrl+J, Ctrl+Alt+B), and a fourth opens the layout
// menu - the three areas, the bottom one in the window's whole width, every panel on its own, the status line, the
// tool shelf and "only the 3D view" (F11). An area opened with nothing remembered shows its main panel:
// the Laboratory on the left, the graphs at the bottom, the inspector on the right. The buttons follow
// the panels however they open and close (their ✕, F8, F9).
#include "MainWindow.h"
#include "UiLanguage.h"

#include "SequencerPanel.h"

#include "Icons.h"

#include <QAction>
#include <QDockWidget>
#include <QSettings>
#include <QHBoxLayout>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

namespace {

const char* mainDock(Qt::DockWidgetArea area) {
    switch (area) {
    case Qt::LeftDockWidgetArea: return "labDock";
    case Qt::BottomDockWidgetArea: return "resultsDock";
    default: return "inspectorDock";
    }
}

// The panels in the order the layout menu lists them.
const char* const kDocks[] = {"inspectorDock", "labDock", "resultsDock"};

} // namespace

void MainWindow::addLayoutButtons() {
    struct Area { QAction** act; Qt::DockWidgetArea area; LayoutIcon icon; const char* name, *text, *key, *tip; };
    const Area areas[] = {
        {&leftBarAct_, Qt::LeftDockWidgetArea, LayoutIcon::LeftBar, "actionLeftBar", uiText("Kinetica", "Левая панель"), "Ctrl+B",
         uiText("Kinetica", "Скрыть или показать левую панель (Ctrl+B): Лаборатория")},
        {&bottomPanelAct_, Qt::BottomDockWidgetArea, LayoutIcon::BottomPanel, "actionBottomPanel", uiText("Kinetica", "Нижняя панель"), "Ctrl+J",
         uiText("Kinetica", "Скрыть или показать нижнюю панель (Ctrl+J): графики и показания")},
        {&rightBarAct_, Qt::RightDockWidgetArea, LayoutIcon::RightBar, "actionRightBar", uiText("Kinetica", "Правая панель"), "Ctrl+Alt+B",
         uiText("Kinetica", "Скрыть или показать правую панель (Ctrl+Alt+B): инспектор")}};
    QMenu* viewMenu = menuBar()->findChild<QMenu*>("viewMenu");
    viewMenu->addSeparator();
    for (const Area& a : areas) {
        auto* act = new QAction(layoutIcon(a.icon, 16), a.text, this);
        act->setObjectName(a.name);
        act->setCheckable(true);
        act->setShortcut(QKeySequence(a.key));
        act->setToolTip(a.tip);
        const Qt::DockWidgetArea area = a.area;
        connect(act, &QAction::triggered, this, [this, area](bool on) { setDockAreaOpen(area, on); });
        viewMenu->addAction(act);
        *a.act = act;
    }
    // The bottom panel under the inspector too, the window's whole width (the Sequencer's ⇔).
    bottomWideAct_ = new QAction(uiText("Kinetica", "Нижняя панель во всю ширину"), this);
    bottomWideAct_->setObjectName("actionBottomWide");
    bottomWideAct_->setCheckable(true);
    bottomWideAct_->setToolTip(uiText("Kinetica", "Нижняя панель — Секвенсор, графики, консоль — во всю ширину окна, и под инспектором"));
    connect(bottomWideAct_, &QAction::toggled, this, [this](bool on) {
        setCorner(Qt::BottomRightCorner, on ? Qt::BottomDockWidgetArea : Qt::RightDockWidgetArea);
        setCorner(Qt::BottomLeftCorner, on ? Qt::BottomDockWidgetArea : Qt::LeftDockWidgetArea);
        QSettings().setValue("layout/bottomWide", on);
    });
    bottomWideAct_->setChecked(QSettings().value("layout/bottomWide", false).toBool());
    viewMenu->addAction(bottomWideAct_);
    if (sequencer_) sequencer_->addBarAction(bottomWideAct_, "⇔"); // the Sequencer is built before
    // Small and flat: the filled area of the icon tells open from closed, so a checked
    // button gets no highlight of its own, and the menu button no arrow.
    // Small and flat: the filled area of the icon tells open from closed, so a checked
    // button gets no highlight of its own, and the menu button no arrow.
    auto* corner = new QWidget;
    corner->setObjectName("layoutButtons");
    corner->setStyleSheet("QToolButton { min-width: 0; padding: 2px; margin: 0px; border: none; border-radius: 4px; background: transparent; }"
                          "QToolButton:hover, QToolButton:checked:hover { background: #3a404b; }"
                          "QToolButton:checked { background: transparent; }"
                          "QToolButton::menu-indicator { image: none; width: 0px; }");
    auto* row = new QHBoxLayout(corner);
    row->setContentsMargins(0, 0, 6, 0);
    row->setSpacing(2);
    auto button = [row](QToolButton* b) {
        if (b->objectName().isEmpty()) b->setObjectName("layoutButton");
        b->setIconSize(QSize(16, 16));
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setAutoRaise(true);
        row->addWidget(b);
    };
    auto* left = new QToolButton;
    left->setDefaultAction(leftBarAct_);
    button(left);
    auto* menuButton = new QToolButton;
    menuButton->setObjectName("layoutMenuButton");
    menuButton->setIcon(layoutIcon(LayoutIcon::Customize, 16));
    menuButton->setToolTip(uiText("Kinetica", "Раскладка окна: какие панели видны"));
    menuButton->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(menuButton);
    menu->setObjectName("layoutMenu");
    buildLayoutMenu(menu);
    menuButton->setMenu(menu);
    button(menuButton);
    for (QAction* a : {bottomPanelAct_, rightBarAct_}) {
        auto* b = new QToolButton;
        b->setDefaultAction(a);
        button(b);
    }
    menuBar()->setCornerWidget(corner, Qt::TopRightCorner);
    for (QDockWidget* d : findChildren<QDockWidget*>()) {
        connect(d, &QDockWidget::visibilityChanged, this, &MainWindow::syncLayoutButtons);
        connect(d, &QDockWidget::dockLocationChanged, this, &MainWindow::syncLayoutButtons);
        connect(d, &QDockWidget::topLevelChanged, this, &MainWindow::syncLayoutButtons);
    }
    syncLayoutButtons();
}

// The menu behind the fourth button: the areas, the panels one by one, and the modes.
void MainWindow::buildLayoutMenu(QMenu* menu) {
    menu->addSection(uiText("Kinetica", "Области"));
    for (QAction* a : {leftBarAct_, bottomPanelAct_, rightBarAct_}) menu->addAction(a);
    menu->addAction(bottomWideAct_); // the bottom panel under the side panels too - the window's whole width
    auto* status = menu->addAction(uiText("Kinetica", "Строка состояния"));
    status->setCheckable(true);
    status->setChecked(true);
    connect(status, &QAction::toggled, statusBar(), &QWidget::setVisible);
    connect(menu, &QMenu::aboutToShow, status, [this, status] { status->setChecked(!statusBar()->isHidden()); });
    QAction* panels = menu->addSection(uiText("Kinetica", "Панели"));
    // The tool shelf is built after the top bar: its switch joins the menu when the menu first opens.
    connect(menu, &QMenu::aboutToShow, this, [this, menu, panels] {
        auto* shelf = findChild<QToolBar*>("toolShelf");
        if (!shelf || menu->actions().contains(shelf->toggleViewAction())) return;
        shelf->toggleViewAction()->setText(uiText("Kinetica", "Полка инструментов"));
        menu->insertAction(panels, shelf->toggleViewAction());
    });
    for (const char* name : kDocks) {
        auto* dock = findChild<QDockWidget*>(name);
        if (!dock) continue;
        // The graphs and the Laboratory have their own switches (F9, F8): they also start measuring.
        if (QString(name) == "resultsDock") menu->addAction(graphsAct_);
        else if (QString(name) == "labDock") menu->addAction(labAct_);
        else menu->addAction(dock->toggleViewAction());
    }
    menu->addSection(uiText("Kinetica", "Режим"));
    onlyViewAct_ = menu->addAction(uiText("Kinetica", "Только 3D-вид"));
    onlyViewAct_->setObjectName("actionOnlyViewport");
    onlyViewAct_->setCheckable(true);
    onlyViewAct_->setShortcut(QKeySequence("F11")); // Ctrl+K finds commands here
    onlyViewAct_->setToolTip(uiText("Kinetica", "Убрать все панели, полку инструментов и строку состояния (F11); ещё раз — вернуть"));
    addAction(onlyViewAct_); // the key works while the menu is closed
    connect(onlyViewAct_, &QAction::toggled, this, &MainWindow::setOnlyViewport);
}

bool MainWindow::dockAreaOpen(Qt::DockWidgetArea area) const {
    for (QDockWidget* d : findChildren<QDockWidget*>())
        if (!d->isFloating() && !d->isHidden() && dockWidgetArea(d) == area) return true;
    return false;
}

// Closing remembers the open panels of the area; opening brings them back (or the main panel).
void MainWindow::setDockAreaOpen(Qt::DockWidgetArea area, bool open) {
    QStringList& remembered = closedDocks_[size_t(area)];
    if (open != dockAreaOpen(area)) {
        if (!open) {
            remembered.clear();
            for (QDockWidget* d : findChildren<QDockWidget*>())
                if (!d->isFloating() && !d->isHidden() && dockWidgetArea(d) == area) remembered << d->objectName();
            for (const QString& name : remembered) findChild<QDockWidget*>(name)->hide();
        } else {
            if (remembered.isEmpty()) remembered << mainDock(area);
            for (const QString& name : remembered) showDock(name);
            remembered.clear();
        }
    }
    syncLayoutButtons();
}

void MainWindow::showDock(const QString& name) {
    if (name == "resultsDock") {
        if (!graphsAct_->isChecked()) graphsAct_->trigger(); // as F9: the plots start measuring
    } else if (name == "labDock") {
        if (!labAct_->isChecked()) labAct_->trigger(); // as F8: the debug layers come on
    } else if (auto* d = findChild<QDockWidget*>(name)) {
        d->show();
        d->raise();
    }
}

void MainWindow::syncLayoutButtons() {
    if (!leftBarAct_) return;
    leftBarAct_->setChecked(dockAreaOpen(Qt::LeftDockWidgetArea));
    bottomPanelAct_->setChecked(dockAreaOpen(Qt::BottomDockWidgetArea));
    rightBarAct_->setChecked(dockAreaOpen(Qt::RightDockWidgetArea));
}

// Every docked panel, the tool shelf and the status line away - and back as they were.
void MainWindow::setOnlyViewport(bool on) {
    auto* shelf = findChild<QToolBar*>("toolShelf");
    if (on) {
        onlyViewDocks_.clear();
        for (QDockWidget* d : findChildren<QDockWidget*>())
            if (!d->isFloating() && !d->isHidden()) onlyViewDocks_ << d->objectName();
        if (shelf && !shelf->isHidden()) onlyViewDocks_ << "shelf";
        if (!statusBar()->isHidden()) onlyViewDocks_ << "status";
        for (const QString& name : onlyViewDocks_)
            if (auto* d = findChild<QDockWidget*>(name)) d->hide();
        if (shelf) shelf->hide();
        statusBar()->hide();
    } else {
        for (const QString& name : onlyViewDocks_) {
            if (name == "shelf" && shelf) shelf->show();
            else if (name == "status") statusBar()->show();
            else showDock(name);
        }
        onlyViewDocks_.clear();
    }
    syncLayoutButtons();
}
