// Plugins as processes (see PluginHost.h): the folders scanned, a process a plugin, its commands as the editor's
// operations and buttons, its calls held for it and answered, a saved file restarting it, a fall said on a banner.
#include "PluginHost.h"
#include "UiLanguage.h"

#include "RunHooks.h"

#include "PluginPanel.h"
#include "Resources.h"
#include "PythonRuntime.h"
#include "SceneBuilder.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QUrl>
#include <QDesktopServices>
#include <QFormLayout>
#include <QJsonArray>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QMainWindow>
#include <QMenu>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QFileDialog>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QToolBar>
#include <QUuid>

#include <cmath>

namespace {

// «cubes» + «cube_row» -> cubes_cube_row: an operation's name, as ops_list, the console and MCP spell it. Russian letters
// are spelt in Latin («Мой плагин» -> moy_plagin; it was «___»), anything else becomes one _, none at the ends.
QString slug(const QString& s) {
    static const QStringList latin{"a", "b", "v", "g", "d", "e", "zh", "z", "i", "y", "k", "l", "m", "n", "o", "p", "r", "s", "t", "u",
                                   "f", "kh", "ts", "ch", "sh", "shch", "", "y", "", "e", "yu", "ya"};
    QString out;
    for (const QChar c : s.toLower()) {
        if (c >= QChar(0x0430) && c <= QChar(0x044F)) out += latin[c.unicode() - 0x0430];
        else if (c == QChar(0x0451)) out += "e"; // ё
        else out += c;
    }
    out.replace(QRegularExpression("[^a-z0-9_]+"), "_");
    out.replace(QRegularExpression("_+"), "_");
    while (out.startsWith('_')) out.remove(0, 1);
    while (out.endsWith('_')) out.chop(1);
    if (!out.isEmpty() && out[0].isDigit()) out.prepend("p_");
    return out;
}

// The command's fields as JSON Schema 2020-12: what MCP gives an agent as the tool's inputSchema.
// A plugin's tool on the shelf: an orange rounded square with its label's first letter (C4D's colour of a plugin's things).
QIcon toolIcon(const QString& label) {
    QPixmap pix(56, 56);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 146, 40));
    p.drawRoundedRect(QRectF(6, 6, 44, 44), 10, 10);
    QFont f = p.font();
    f.setPixelSize(28);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(40, 24, 8));
    p.drawText(QRectF(6, 6, 44, 44), Qt::AlignCenter, label.left(1).toUpper());
    return QIcon(pix);
}

// A field that is an object of the scene (kinetica.plugin.field(ref="joint")) and has no default must be said.
bool mustBeSaid(const QJsonObject& f) { return f.contains("ref") && !f.contains("default"); }

QJsonObject jsonSchema(const QJsonObject& args) {
    QJsonObject properties;
    QJsonArray required;
    for (auto it = args.begin(); it != args.end(); ++it) {
        QJsonObject f = it.value().toObject(), p{{"type", f.value("type")}};
        if (f.value("type") == "vector") // a point, a direction: three numbers
            p = {{"type", "array"}, {"items", QJsonObject{{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}};
        if (f.contains("default")) p.insert("default", f.value("default"));
        QString about = f.value("label").toString();
        if (f.contains("unit")) about += ", " + f.value("unit").toString();
        if (f.contains("ref")) about += QString(": the id of a scene object of kind %1 (scene_list {kind: \"%1\"} lists them)").arg(f.value("ref").toString());
        if (f.contains("help")) about += ". " + f.value("help").toString();
        p.insert("description", about);
        for (const char* k : {"minimum", "maximum", "enum"})
            if (f.contains(k)) p.insert(k, f.value(k));
        properties.insert(it.key(), p);
        if (mustBeSaid(f)) required.append(it.key());
    }
    QJsonObject schema{{"type", "object"}, {"properties", properties}, {"additionalProperties", false}};
    if (!required.isEmpty()) schema.insert("required", required);
    return schema;
}

QString argsHelp(const QJsonObject& args, const QJsonArray& order) {
    QStringList parts;
    for (const QJsonValue& n : order) {
        const QJsonObject f = args.value(n.toString()).toObject();
        const QString type = f.contains("ref") ? "int (a " + f.value("ref").toString() + "'s id)" : f.value("type").toString();
        parts << n.toString() + (mustBeSaid(f) ? ": " : "?: ") + type + (f.contains("unit") ? " " + f.value("unit").toString() : QString());
    }
    return "{" + parts.join(", ") + "}";
}

} // namespace

PluginHost::PluginHost(EditorOps* ops, SceneBuilder* builder, QWidget* window, QMenu* menu, RunHooks* hooks,
                       std::function<quint16()> apiPort, std::function<void(PluginPanel*, bool)> place)
    : QObject(window), ops_(ops), builder_(builder), window_(window), menu_(menu), hooks_(hooks), apiPort_(std::move(apiPort)),
      place_(std::move(place)), watcher_(new QFileSystemWatcher(this)) {
    changeTimer_ = new QTimer(this);
    changeTimer_->setSingleShot(true);
    changeTimer_->setInterval(120);
    connect(changeTimer_, &QTimer::timeout, this, &PluginHost::componentsChanged);
    connect(builder_, &SceneBuilder::editSnapshot, changeTimer_, qOverload<>(&QTimer::start)); // every edit of the scene redraws it
    connect(hooks_, &RunHooks::drew, this, &PluginHost::hookDrew);
    connect(hooks_, &RunHooks::failed, this, &PluginHost::hookFailed);
    connect(hooks_, &RunHooks::showed, this, &PluginHost::hookShowed);
    ops_->setUnknownHint([this](const QString& op) -> QString { // a command of a plugin that is down: why, and what to do
        for (const auto& p : plugins_) {
            if (!op.startsWith(p->name + "_")) continue;
            if (p->state == "упал")
                return "«" + op + uiText("Kinetica", "» - команда плагина «") + p->name + uiText("Kinetica", "», а он упал: ") + (p->error.isEmpty() ? QString(uiText("Kinetica", "без сообщения")) : p->error) +
                       uiText("Kinetica", ". Исправьте файл и сохраните - плагин перезапустится сам");
            if (p->state == "запускается") return "плагин «" + p->name + "» ещё запускается: «" + op + "» появится через секунду";
            if (p->state == "остановлен") return "плагин «" + p->name + "» остановлен: plugin_reload {name: \"" + p->name + "\"} запустит его";
        }
        return QString();
    });
    commandsEnd_ = menu_->addSeparator();
    connect(watcher_, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
        QTimer::singleShot(150, this, [this, path] { // an editor writes in steps: the file as it stands a moment later
            if (!QFileInfo::exists(path)) return forget(path); // its folder deleted: it was «запускается» for ever
            watcher_->addPath(path); // a replaced file is a new one to watch
            for (auto& p : plugins_)
                if (p->file == path) {
                    p->reloaded = true;
                    p->since.start();
                    if (p->process) p->restarting = true, stop(*p);
                    else launch(*p);
                }
        });
    });
    addOps();
}

PluginHost::~PluginHost() {
    ops_->setUnknownHint(nullptr);
    for (auto& p : plugins_)
        if (p->process) p->process->disconnect(this), p->process->kill(), p->process->waitForFinished(500);
}

QString PluginHost::userFolder() const { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/plugins"; }

QStringList PluginHost::names() const {
    QStringList out;
    for (const auto& p : plugins_) out << p->name;
    return out;
}

PluginHost::Plugin* PluginHost::find(const QString& name) {
    for (auto& p : plugins_)
        if (p->name == name) return p.get();
    return nullptr;
}

PluginHost::Plugin* PluginHost::authorised(const QJsonObject& a, QJsonObject& refusal) {
    Plugin* p = find(a.value("name").toString());
    if (!p || p->token.isEmpty() || a.value("token").toString() != p->token)
        return refusal = EditorOps::error("a plugin speaks with the name and the token the editor started it with"), nullptr;
    return p;
}

// The folders plugins are looked for in, in this order: KE_PLUGINS; then, in a person's window, the editor's own (URDF),
// the added ones (a plugins' repository, kinetica-plugins: Плагины → Добавить папку…), the user's.
QStringList PluginHost::lookedThrough() const {
    QStringList out;
    if (const QString given = qEnvironmentVariable("KE_PLUGINS"); !given.isEmpty()) out << given;
    if (!onlyGiven_) out << resourceDir("plugins") << folders() << userFolder();
    return out;
}

void PluginHost::start(bool onlyGiven) {
    onlyGiven_ = onlyGiven;
    for (const QString& folder : lookedThrough()) scan(folder);
}

QStringList PluginHost::folders() const { return QSettings().value("plugins/folders").toStringList(); }

void PluginHost::addFolder(const QString& folder) {
    QStringList all = folders();
    if (!all.contains(folder)) all << folder, QSettings().setValue("plugins/folders", all);
    scan(folder);
}

void PluginHost::scan(const QString& folder) {
    for (const QFileInfo& d : QDir(folder).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString file = d.absoluteFilePath() + "/plugin.py", name = slug(d.fileName());
        if (!QFileInfo::exists(file) || find(name)) continue;
        auto p = std::make_unique<Plugin>();
        p->name = name, p->file = file;
        watcher_->addPath(file);
        plugins_.push_back(std::move(p));
        launch(*plugins_.back());
    }
}

QString PluginHost::create(const QString& name, QString& error, const QString& kind) {
    const QString folder = userFolder() + "/" + slug(name);
    if (slug(name).isEmpty()) return error = uiText("Kinetica", "имя плагина: буквы и цифры, например «my_tools»"), QString();
    if (QFileInfo::exists(folder)) return error = uiText("Kinetica", "плагин «") + slug(name) + uiText("Kinetica", "» уже есть: ") + folder, QString();
    if (kind != "command" && kind != "controller" && kind != "world") return error = "kind: command, controller or world", QString();
    QFile templ(pythonPackage() + "/kinetica/plugin_template" + (kind == "command" ? QString() : "_" + kind) + ".py");
    if (!templ.open(QIODevice::ReadOnly)) return error = uiText("Kinetica", "нет заготовки плагина: ") + templ.fileName(), QString();
    QDir().mkpath(folder);
    QFile out(folder + "/plugin.py");
    if (!out.open(QIODevice::WriteOnly)) return error = uiText("Kinetica", "не записать ") + out.fileName(), QString();
    out.write(QString::fromUtf8(templ.readAll()).replace("PLUGIN_NAME", slug(name)).toUtf8());
    out.close();
    scan(userFolder());
    return out.fileName();
}

void PluginHost::launch(Plugin& p) {
    const quint16 port = apiPort_();
    if (!port) {
        p.state = "нет API";
        return;
    }
    p.token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p.process = new QProcess(this);
    QProcessEnvironment env = pythonEnvironment(port);
    env.insert("KE_PLUGIN_NAME", p.name);
    env.insert("KE_PLUGIN_TOKEN", p.token);
    env.insert("KE_RUN_PORT", QString::number(hooks_->port())); // its on_play / on_tick / on_stop: their own wire (RunHooks.h)
    hooks_->expect(p.name, p.token);
    p.process->setProcessEnvironment(env);
    p.process->setWorkingDirectory(QFileInfo(p.file).absolutePath());
    p.process->setProcessChannelMode(QProcess::MergedChannels); // its prints and its traceback, one tail
    const QString name = p.name;
    connect(p.process, &QProcess::readyReadStandardOutput, this, [this, name] {
        Plugin* q = find(name);
        if (!q || !q->process) return;
        bool fell = false;
        q->unfinished += q->process->readAllStandardOutput();
        const qsizetype whole = q->unfinished.lastIndexOf('\n') + 1; // only the lines that have ended: the rest waits for its end
        const QByteArray ended = q->unfinished.left(whole);
        q->unfinished.remove(0, whole);
        for (QByteArray raw : ended.split('\n')) {
            while (raw.endsWith('\r')) raw.chop(1); // Windows' Python ends a line with \r\n (a \r the plugin wrote itself: \r\r\n)
            const QString line = QString::fromUtf8(raw).trimmed().isEmpty() ? QString() : QString::fromUtf8(raw);
            if (line.isEmpty()) continue;
            q->stderrTail << line;
            fell = fell || line.startsWith("Traceback");
            emit output(name, line); // print() and tracebacks in the API console as they come (they were seen only after a fall)
        }
        while (q->stderrTail.size() > 40) q->stderrTail.removeFirst();
        if (fell && q->state == "работает") { // a worker thread fell while the process lives on: said, not silent
            QTimer::singleShot(200, this, [this, name] { // the traceback's last line comes a moment later
                if (Plugin* r = find(name)) {
                    r->error = pythonError(r->stderrTail);
                    notice(name, uiText("Kinetica", "Плагин «") + name + uiText("Kinetica", "»: ошибка в фоне — ") + r->error);
                    emit changed();
                }
            });
        }
    });
    connect(p.process, &QProcess::errorOccurred, this, [this, name](QProcess::ProcessError e) {
        Plugin* q = find(name);
        if (!q || e != QProcess::FailedToStart) return;
        q->state = "не запустился", q->error = uiText("Kinetica", "нет Python: ") + pythonInterpreter();
        notice(name, uiText("Kinetica", "Плагин «") + name + uiText("Kinetica", "» не запустился: ") + q->error);
        emit changed();
    });
    connect(p.process, &QProcess::finished, this, [this, name](int code, QProcess::ExitStatus status) {
        Plugin* q = find(name);
        if (!q) return;
        if (const QString last = QString::fromUtf8(q->unfinished).trimmed(); !last.isEmpty()) q->stderrTail << last; // it died mid-line
        q->unfinished.clear();
        onExit(*q, status == QProcess::CrashExit ? -1 : code);
    });
    p.state = "запускается";
    p.stderrTail.clear();
    p.unfinished.clear();
    if (!p.since.isValid()) p.since.start();
    p.process->start(pythonInterpreter(), {"-u", "-m", "kinetica.plugin", p.file}); // the runner serves the file
}

void PluginHost::forget(const QString& file) {
    for (auto it = plugins_.begin(); it != plugins_.end(); ++it) {
        if ((*it)->file != file) continue;
        Plugin& p = **it;
        if (p.process) p.process->disconnect(this), stop(p);
        unregister(p);
        for (auto& [id, reply] : p.open) reply(EditorOps::error(uiText("Kinetica", "плагин «") + p.name + uiText("Kinetica", "» удалён")));
        if (noticeFor_ == p.name) notice(p.name, {});
        hooks_->expect(p.name, {});
        plugins_.erase(it);
        emit changed();
        return;
    }
}

void PluginHost::stop(Plugin& p) {
    if (!p.process) return;
    p.process->terminate();
    if (!p.process->waitForFinished(700)) p.process->kill();
}

// The process ended: its commands go, its calls fail; restarted when its file was saved, said on a banner when it fell.
void PluginHost::onExit(Plugin& p, int code) {
    p.process->readAll();
    p.process->deleteLater();
    p.process = nullptr;
    unregister(p);
    for (auto& [id, reply] : p.open) reply(EditorOps::error(uiText("Kinetica", "плагин «") + p.name + uiText("Kinetica", "» остановился")));
    p.open.clear(), p.queued.clear(), p.waiting = nullptr;
    if (p.restarting) return p.restarting = false, launch(p); // once: a plugin falling at its start falls, it does not loop
    hooks_->expect(p.name, {});
    p.state = code == 0 ? "остановлен" : "упал";
    p.error = code == 0 ? QString() : pythonError(p.stderrTail);
    if (code != 0) notice(p.name, uiText("Kinetica", "Плагин «") + p.name + uiText("Kinetica", "» упал: ") + (p.error.isEmpty() ? QString(uiText("Kinetica", "код %1")).arg(code) : p.error));
    emit changed();
}

void PluginHost::notice(const QString& plugin, const QString& text) {
    auto* w = qobject_cast<QMainWindow*>(window_);
    if (!w) return;
    if (!notice_) {
        notice_ = new QFrame;
        notice_->setObjectName("pluginNotice");
        notice_->setStyleSheet("QFrame#pluginNotice { background: #4a2323; border: 1px solid #ff6a5a; border-radius: 4px; }");
        auto* row = new QHBoxLayout(notice_);
        row->setContentsMargins(8, 1, 4, 1);
        noticeText_ = new QLabel;
        noticeText_->setObjectName("pluginNoticeText");
        auto* again = new QToolButton;
        again->setText(uiText("Kinetica", "Перезапустить"));
        auto* close = new QToolButton;
        close->setText("✕");
        row->addWidget(noticeText_), row->addWidget(again), row->addWidget(close);
        connect(again, &QToolButton::clicked, this, [this] {
            if (Plugin* q = find(noticeFor_); q && !q->process) q->since.start(), launch(*q);
        });
        connect(close, &QToolButton::clicked, notice_, &QFrame::hide);
        w->statusBar()->insertPermanentWidget(0, notice_, 1);
    }
    noticeFor_ = plugin;
    noticeText_->setText("⚠ " + text);
    notice_->setVisible(!text.isEmpty());
}

// The line on the tool shelf between the gizmo's own tools and the plugins' under them: there while a plugin's tool is.
static void lineShelf(QWidget* window) {
    auto* shelf = window->findChild<QToolBar*>("toolShelf");
    if (!shelf) return;
    QAction *line = nullptr, *first = nullptr;
    for (QAction* a : shelf->actions()) {
        if (a->objectName() == "pluginToolsLine") line = a;
        else if (!first && a->objectName().startsWith("plugin:")) first = a;
    }
    if (first && !line) {
        line = new QAction(shelf);
        line->setSeparator(true);
        line->setObjectName("pluginToolsLine");
        shelf->insertAction(first, line);
    } else if (!first) {
        delete line;
    }
}

void PluginHost::unregister(Plugin& p) {
    ops_->run("view_draw", {{"clear", true}, {"key", p.name + "/"}}, [](const QJsonObject&) {}, "ui"); // its pictures with it
    builder_->setPluginComponents(p.name, {}); // its components' cards say the plugin is not running; their words stay
    if (p.panel) { // its values kept for its return; the panel out of the window
        p.panelValues = p.panel->values();
        place_(p.panel, false);
        p.panel->deleteLater();
        p.panel = nullptr;
    }
    for (Command& c : p.commands) {
        ops_->removePluginOp(c.op);
        delete c.action;
    }
    p.commands.clear();
    lineShelf(window_);
    delete p.menu;
    p.menu = nullptr;
}

void PluginHost::call(Plugin& p, const QString& command, const QJsonObject& args, const EditorOps::Reply& reply) {
    const int id = ++nextCall_;
    builder_->beginUndoGroup(); // every edit the plugin makes for this call: one step of Ctrl+Z
    p.open[id] = [this, reply](QJsonObject r) { // a call that failed changes nothing: what it had built is taken back
        if (builder_->endUndoGroup(r.value("ok").toBool()))
            r.insert("error", r.value("error").toString() + uiText("Kinetica", ". Правки этого вызова отменены: сцена как до него"));
        reply(r);
    };
    QJsonObject given;
    for (auto it = args.begin(); it != args.end(); ++it)
        if (!it.key().startsWith('_')) given.insert(it.key(), it.value());
    p.queued.push_back({id, {command, given}});
    deliver(p);
    QTimer::singleShot(30000, this, [this, name = p.name, id] {
        Plugin* q = find(name);
        const auto it = q ? q->open.find(id) : std::map<int, EditorOps::Reply>::iterator();
        if (!q || it == q->open.end()) return;
        const EditorOps::Reply r = it->second;
        q->open.erase(it);
        r(EditorOps::error(uiText("Kinetica", "плагин «") + name + uiText("Kinetica", "» не ответил за 30 с")));
    });
}

bool PluginHost::ticking(bool world) const { return hooks_->ticking(world); }

// What a field of a scene object may be: the scene's objects of its kind, as scene_list lists them - the form offers
// these, a call is checked against these. `why`: the kind's word is not one scene_list knows.
QJsonArray PluginHost::refObjects(const QString& kind, QString* why) const {
    const QJsonObject listed = ops_->runNow("scene_list", {{"kind", kind}});
    if (why && !listed.value("ok").toBool()) *why = listed.value("error").toString();
    return listed.value("objects").toArray();
}

// A call's objects of the scene are of the kinds its fields ask for; else what is wrong, with the objects there are.
QString PluginHost::refProblem(const QJsonObject& fields, const QJsonObject& args) const {
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        const QString kind = it.value().toObject().value("ref").toString();
        if (kind.isEmpty() || !args.contains(it.key())) continue;
        QString why;
        const QJsonArray objects = refObjects(kind, &why);
        if (!why.isEmpty()) return it.key() + ": " + why;
        const int id = args.value(it.key()).toInt();
        QStringList there;
        bool found = false;
        for (const QJsonValue& o : objects) {
            found = found || o.toObject().value("id").toInt() == id;
            if (there.size() < 8) there << QString("%1 «%2»").arg(o.toObject().value("id").toInt()).arg(o.toObject().value("name").toString());
        }
        if (!found)
            return QString(uiText("Kinetica", "%1: объект %2 — не «%3»; в сцене: %4")).arg(it.key()).arg(id).arg(kind, objects.isEmpty() ? QString(uiText("Kinetica", "таких нет"))
                                                                    : there.join(", ") + (objects.size() > there.size() ? " …" : ""));
    }
    return QString();
}

void PluginHost::runStarted() {
    for (auto& p : plugins_) p->hookError.clear();
}

void PluginHost::hookDrew(const QString& name, const QString& event, const QJsonArray& pictures) {
    if (event == "tick" && builder_->editing()) return; // a late frame's pictures after Stop: none
    for (const QJsonValue& d : pictures)
        ops_->run("view_draw", d.toObject(), [this, name](const QJsonObject& a) {
            Plugin* q = find(name);
            if (a.value("ok").toBool() || !q || !q->hookError.isEmpty()) return;
            q->hookError = "draw: " + a.value("error").toString();
            emit output(name, q->hookError);
        }, "plugin:" + name);
}

// What a hook showed on its plugin's panel: with its frame's time, so a chart's lines take a point there.
void PluginHost::hookShowed(const QString& name, const QString& event, double time, const QJsonObject& values) {
    if (event == "tick" && builder_->editing()) return; // a late frame's values after Stop: none
    if (Plugin* p = find(name); p && p->panel) p->panel->setValues(values, time);
}

void PluginHost::hookFailed(const QString& name, const QString& event, const QString& error) {
    Plugin* q = find(name);
    if (!q || !q->hookError.isEmpty()) return; // said once a run, not sixty times a second
    q->hookError = error;
    emit output(name, "on_" + event + ": " + error);
    notice(name, uiText("Kinetica", "Плагин «") + name + uiText("Kinetica", "»: ошибка в on_") + event + " — " + error);
}

namespace {

// The plugin's components in the scene, in one text: whose they are and what they hold.
QByteArray componentsOf(const ke::SceneGraph& g, const QString& plugin) {
    const std::string prefix = plugin.toStdString() + ".";
    std::string text;
    ke::eachObjectList(g, [&](const auto& objects) {
        for (const auto& o : objects)
            for (const ke::PluginComponent& c : o.components) {
                if (c.type.rfind(prefix, 0) != 0) continue;
                text += std::to_string(o.id) + " " + c.type;
                for (const auto& [key, word] : c.values) text += " " + key + "=" + word;
                text += "\n";
            }
    });
    return QByteArray::fromStdString(text);
}

} // namespace

// @on_change (kinetica.plugin): a plugin that asked is told when its components in the scene are no longer what it was
// last told of - one put on an object, changed or taken off, by a hand, a script, an undo, a scene opened - and once
// when it starts. Told as a call of its own, «@change», that makes no undo step. Not while the scene plays: what a run
// changed is told after Stop, if it stayed.
void PluginHost::componentsChanged() {
    if (!builder_->editing()) return;
    for (const auto& p : plugins_) {
        if (!p->watches || !p->process || p->changeOpen) continue;
        const QByteArray now = componentsOf(builder_->graph(), p->name);
        if (now == p->componentsSeen) continue;
        p->componentsSeen = now;
        p->changeOpen = true;
        const int id = ++nextCall_;
        const auto answered = [this, name = p->name](const QJsonObject& r) {
            Plugin* q = find(name);
            if (!q) return;
            q->changeOpen = false;
            if (!r.value("ok").toBool()) emit output(name, "on_change: " + r.value("error").toString());
            changeTimer_->start(); // what changed meanwhile
        };
        p->open[id] = answered;
        p->queued.push_back({id, {"@change", {}}});
        deliver(*p);
        QTimer::singleShot(30000, this, [this, name = p->name, id, answered] { // it never answered: the next change is told all the same
            Plugin* q = find(name);
            if (q && q->open.erase(id)) answered(EditorOps::error(uiText("Kinetica", "плагин не ответил за 30 с")));
        });
    }
}

void PluginHost::deliver(Plugin& p) {
    if (!p.waiting || p.queued.empty()) return;
    const auto [id, what] = p.queued.front();
    p.queued.erase(p.queued.begin());
    const EditorOps::Reply waiting = std::move(p.waiting);
    p.waiting = nullptr;
    waiting(EditorOps::ok({{"call", id}, {"command", what.first}, {"args", what.second}}));
}

// A button: its fields as a form (each starts from the last value given, else its default), then the call.
void PluginHost::invoke(const QString& plugin, const Command& c) {
    static std::map<QString, QJsonObject> last;
    QJsonObject args = last[c.op];
    const auto send = [this, op = c.op](const QJsonObject& given) {
        ops_->run(op, given, [this](const QJsonObject& r) {
            if (!r.value("ok").toBool())
                if (auto* w = qobject_cast<QMainWindow*>(window_)) w->statusBar()->showMessage(r.value("error").toString(), 10000);
        }, "ui");
    };
    if (c.args.isEmpty()) return send({});
    QDialog form(window_);
    form.setWindowTitle(c.label);
    auto* rows = new QFormLayout(&form);
    std::vector<std::function<void()>> read;
    QStringList fields;
    for (const QJsonValue& n : c.order) fields << n.toString();
    for (auto it = c.args.begin(); it != c.args.end(); ++it)
        if (!fields.contains(it.key())) fields << it.key();
    for (const QString& name : fields) {
        const QJsonObject f = c.args.value(name).toObject();
        const QJsonValue start = args.contains(name) ? args.value(name) : f.value("default");
        const QString type = f.value("type").toString(), unit = f.value("unit").toString();
        QWidget* w = nullptr;
        if (f.contains("ref")) { // an object of the scene: chosen by its name, sent as its id; the selected one first
            auto* box = new QComboBox;
            for (const QJsonValue& o : refObjects(f.value("ref").toString()))
                box->addItem(o.toObject().value("name").toString(), o.toObject().value("id").toInt());
            const int selected = box->findData(int(builder_->selectedId()));
            box->setCurrentIndex(std::max(0, selected >= 0 ? selected : box->findData(start.toInt())));
            if (box->count() == 0) box->addItem(uiText("Kinetica", "в сцене нет: ") + f.value("ref").toString()), box->setEnabled(false);
            read.push_back([&args, name, box] {
                if (box->isEnabled()) args.insert(name, box->currentData().toInt());
                else args.remove(name);
            });
            w = box;
        } else if (f.contains("enum")) {
            auto* box = new QComboBox;
            for (const QJsonValue& e : f.value("enum").toArray()) box->addItem(e.toVariant().toString(), e.toVariant());
            box->setCurrentIndex(std::max(0, box->findData(start.toVariant())));
            read.push_back([&args, name, box] { args.insert(name, QJsonValue::fromVariant(box->currentData())); });
            w = box;
        } else if (type == "boolean") {
            auto* box = new QCheckBox;
            box->setChecked(start.toBool());
            read.push_back([&args, name, box] { args.insert(name, box->isChecked()); });
            w = box;
        } else if (type == "integer") {
            auto* box = new QSpinBox;
            box->setRange(f.contains("minimum") ? f.value("minimum").toInt() : -1000000, f.contains("maximum") ? f.value("maximum").toInt() : 1000000);
            box->setValue(start.toInt());
            if (!unit.isEmpty()) box->setSuffix(" " + unit);
            read.push_back([&args, name, box] { args.insert(name, box->value()); });
            w = box;
        } else if (type == "string") {
            auto* line = new QLineEdit(start.toString());
            read.push_back([&args, name, line] { args.insert(name, line->text()); });
            w = line;
        } else {
            auto* box = new QDoubleSpinBox;
            box->setDecimals(3);
            box->setRange(f.contains("minimum") ? f.value("minimum").toDouble() : -1e6, f.contains("maximum") ? f.value("maximum").toDouble() : 1e6);
            box->setSingleStep(std::max(0.001, (box->maximum() - box->minimum()) / 100));
            box->setValue(start.toDouble());
            if (!unit.isEmpty()) box->setSuffix(" " + unit);
            read.push_back([&args, name, box] { args.insert(name, box->value()); });
            w = box;
        }
        w->setObjectName("pluginField_" + name);
        if (f.contains("help")) w->setToolTip(f.value("help").toString());
        rows->addRow(f.value("label").toString(name), w);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(uiText("Kinetica", "Выполнить"));
    buttons->button(QDialogButtonBox::Cancel)->setText(uiText("Kinetica", "Отмена")); // Qt's own word is English: one language in one dialog
    rows->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &form, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &form, &QDialog::reject);
    form.setObjectName("pluginForm");
    if (form.exec() != QDialog::Accepted) return;
    for (const auto& r : read) r();
    last[c.op] = args;
    Q_UNUSED(plugin);
    send(args);
}

void PluginHost::useTool(const QString& op, const QJsonObject& click) {
    for (const auto& p : plugins_)
        for (const Command& c : p->commands) {
            if (c.op != op) continue;
            QJsonObject args;
            for (auto it = click.begin(); it != click.end(); ++it) // of what the view gives, what the command takes
                if (c.args.contains(it.key())) args.insert(it.key(), it.value());
            const QJsonObject panel = p->panel ? p->panel->values() : QJsonObject(); // the tool's options: its panel's values
            for (auto it = panel.begin(); it != panel.end(); ++it)
                if (c.args.contains(it.key()) && !args.contains(it.key())) args.insert(it.key(), it.value());
            return ops_->run(op, args, [this](const QJsonObject& r) {
                if (!r.value("ok").toBool())
                    if (auto* w = qobject_cast<QMainWindow*>(window_)) w->statusBar()->showMessage(r.value("error").toString(), 10000);
            }, "ui");
        }
}

QStringList PluginHost::formats() const {
    QStringList out;
    for (const auto& p : plugins_)
        for (const Command& c : p->commands) {
            if (c.opens.isEmpty()) continue;
            QStringList masks;
            for (const QString& ext : c.opens) masks << "*" + ext;
            out << c.label + " (" + masks.join(' ') + ")";
        }
    return out;
}

QString PluginHost::openerFor(const QString& file) const {
    const QString lower = file.toLower();
    for (const auto& p : plugins_)
        for (const Command& c : p->commands)
            for (const QString& ext : c.opens)
                if (lower.endsWith(ext)) return c.op;
    return {};
}

void PluginHost::openFile(const QString& op, const QString& path) {
    ops_->run(op, {{"path", path}}, [this, path](const QJsonObject& r) {
        auto* w = qobject_cast<QMainWindow*>(window_);
        if (!w) return;
        if (!r.value("ok").toBool()) return w->statusBar()->showMessage(r.value("error").toString(), 12000);
        const QString note = r.value("note").toString();
        w->statusBar()->showMessage(QFileInfo(path).fileName() + (note.isEmpty() ? QString(uiText("Kinetica", " открыт")) : ": " + note), 12000);
    }, "ui");
}

void PluginHost::setToolChecked(const QString& op) {
    for (const auto& p : plugins_)
        for (const Command& c : p->commands)
            if (c.tool && c.action) c.action->setChecked(c.op == op);
}

void PluginHost::addOps() {
    ops_->addOp("plugin_register", "A plugin started by the editor: its commands made operations, buttons of «Плагины» and tools for agents "
                                   "(clients/python/kinetica/plugin.py sends it)",
                "{name: str, token: str, file?: str, commands: [{name, label, key, help, args: {field: {type, default, label, unit, minimum, "
                "maximum, enum}}, order: [field]}], panel?: {…} (its panel: kinetica.plugin.ui), components?: [{}], events?: [str] (change: it has an @on_change)}",
                [this](const QJsonObject& a, const EditorOps::Reply& reply) {
        QJsonObject refusal;
        Plugin* p = authorised(a, refusal);
        if (!p) return reply(refusal);
        unregister(*p);
        builder_->setPluginComponents(p->name, a.value("components").toArray()); // its components: cards, «+ Добавить компонент», points
        QJsonArray made;
        for (const QJsonValue& v : a.value("commands").toArray()) {
            const QJsonObject c = v.toObject();
            Command cmd;
            cmd.name = c.value("name").toString(), cmd.label = c.value("label").toString(cmd.name), cmd.key = c.value("key").toString();
            cmd.help = c.value("help").toString(), cmd.args = c.value("args").toObject(), cmd.order = c.value("order").toArray();
            cmd.tool = c.value("tool").toBool();
            for (const QJsonValue& x : c.value("opens").toArray()) cmd.opens << x.toString().toLower();
            cmd.op = p->name + "_" + slug(cmd.name);
            for (auto f = cmd.args.begin(); f != cmd.args.end(); ++f) // a field of a scene object whose kind is no kind: said at once
                if (const QString kind = f.value().toObject().value("ref").toString(); !kind.isEmpty()) {
                    QString why;
                    refObjects(kind, &why);
                    if (!why.isEmpty()) emit output(p->name, uiText("Kinetica", "поле «") + f.key() + uiText("Kinetica", "» команды «") + cmd.name + "»: " + why);
                }
            const QString plugin = p->name, command = cmd.name;
            ops_->addPluginOp(cmd.op, QString("«%1» - %2%3").arg(cmd.label, cmd.help.isEmpty() ? QString() : cmd.help + " ",
                                                                QString("(plugin %1)").arg(plugin)),
                              argsHelp(cmd.args, c.value("order").toArray()),
                              [this, plugin, command, fields = cmd.args](const QJsonObject& args, const EditorOps::Reply& r) {
                Plugin* q = find(plugin);
                if (!q || !q->process) return r(EditorOps::error(uiText("Kinetica", "плагин «") + plugin + uiText("Kinetica", "» не запущен")));
                if (const QString why = refProblem(fields, args); !why.isEmpty()) return r(EditorOps::error(why));
                call(*q, command, args, r);
            }, jsonSchema(cmd.args));
            cmd.action = new QAction(cmd.label + (cmd.args.isEmpty() || cmd.tool ? QString() : "…"), window_);
            if (!cmd.opens.isEmpty()) cmd.action->setText(cmd.label + "…");
            if (cmd.tool) { // on the shelf at the left of the view, under the gizmo's tools: orange, its first letter
                cmd.action->setCheckable(true);
                cmd.action->setIcon(toolIcon(cmd.label));
                if (auto* shelf = window_->findChild<QToolBar*>("toolShelf")) shelf->addAction(cmd.action);
            }
            cmd.action->setObjectName("plugin:" + cmd.op);
            if (cmd.tool) lineShelf(window_);
            cmd.action->setToolTip(cmd.help.isEmpty() ? cmd.op : cmd.help);
            if (!cmd.key.isEmpty()) { // a key the window or another plugin has already: left off, said (no two actions on one key)
                QString owner;
                for (QAction* other : window_->findChildren<QAction*>())
                    if (other != cmd.action && other->shortcuts().contains(QKeySequence(cmd.key))) owner = other->text().remove('&');
                if (owner.isEmpty()) cmd.action->setShortcut(QKeySequence(cmd.key));
                else emit output(p->name, uiText("Kinetica", "клавиша ") + cmd.key + uiText("Kinetica", " уже у «") + owner + uiText("Kinetica", "»: у «") + cmd.label + uiText("Kinetica", "» её нет, выберите другую"));
            }
            if (!p->menu) { // its own submenu: whose command it is, and its file a click away
                p->menu = new QMenu(p->name, window_);
                p->menu->setObjectName("pluginMenu:" + p->name);
                menu_->insertMenu(commandsEnd_, p->menu);
                p->menu->addSeparator();
                QAction* open = p->menu->addAction(uiText("Kinetica", "Открыть plugin.py"));
                open->setToolTip(p->file);
                connect(open, &QAction::triggered, this, [file = p->file] { QDesktopServices::openUrl(QUrl::fromLocalFile(file)); });
            }
            p->menu->insertAction(p->menu->actions().value(p->menu->actions().size() - 2), cmd.action); // before its separator
            connect(cmd.action, &QAction::triggered, this, [this, plugin, op = cmd.op] {
                if (Plugin* q = find(plugin))
                    for (const Command& x : q->commands) {
                        if (x.op != op) continue;
                        if (x.tool) return emit toolPicked(x.action->isChecked() ? op : QString(), x.label, x.args.contains("to")); // pressed again: left
                        if (!x.opens.isEmpty()) { // a format it opens: the file first
                            QStringList masks;
                            for (const QString& ext : x.opens) masks << "*" + ext;
                            const QString path = QFileDialog::getOpenFileName(window_, x.label, QString(), x.label + " (" + masks.join(' ') + ")");
                            return path.isEmpty() ? void() : openFile(op, path);
                        }
                        return invoke(plugin, x);
                    }
            });
            p->commands.push_back(cmd);
            made.append(cmd.op);
        }
        if (a.contains("panel")) { // its panel: a button calls its command with the panel's values the command has
            p->panel = new PluginPanel(a.value("panel").toObject(), [this, plugin = p->name](const QString& command, const QJsonObject& values) {
                Plugin* q = find(plugin);
                for (const Command& c : q ? q->commands : std::vector<Command>()) {
                    if (c.name != command) continue;
                    QJsonObject args;
                    for (auto it = values.begin(); it != values.end(); ++it)
                        if (c.args.contains(it.key())) args.insert(it.key(), it.value());
                    ops_->run(c.op, args, [this](const QJsonObject& r) {
                        if (!r.value("ok").toBool())
                            if (auto* w = qobject_cast<QMainWindow*>(window_)) w->statusBar()->showMessage(r.value("error").toString(), 10000);
                    }, "ui");
                }
            });
            p->panel->setValues(p->panelValues);
            place_(p->panel, true);
        }
        p->state = "работает", p->error.clear();
        p->watches = a.value("events").toArray().contains("change"), p->changeOpen = false, p->componentsSeen = "?"; // told once at its start
        changeTimer_->start();
        if (noticeFor_ == p->name) notice(p->name, {}); // back: its fall is no news any more
        if (p->reloaded) {
            p->reloaded = false;
            if (auto* w = qobject_cast<QMainWindow*>(window_))
                w->statusBar()->showMessage(QString(uiText("Kinetica", "Плагин «%1» перезапущен за %2 мс")).arg(p->name).arg(p->since.elapsed()), 5000);
        }
        emit changed();
        reply(EditorOps::ok({{"ops", made}}));
    });
    ops_->addOp("plugin_panel_set", "A plugin's panel shows these values: its fields' and its words' (a measured number, a state)",
                "{name: str, token: str, values: {name: value}}", [this](const QJsonObject& a, const EditorOps::Reply& reply) {
        QJsonObject refusal;
        Plugin* p = authorised(a, refusal);
        if (!p) return reply(refusal);
        if (!p->panel) return reply(EditorOps::error("the plugin has no panel (ui.panel(...) in its file)"));
        p->panel->setValues(a.value("values").toObject());
        reply(EditorOps::ok());
    });
    ops_->addOp("plugin_wait", "A plugin's next call, held until there is one (kinetica.plugin's loop)", "{name: str, token: str}",
                [this](const QJsonObject& a, const EditorOps::Reply& reply) {
        QJsonObject refusal;
        Plugin* p = authorised(a, refusal);
        if (!p) return reply(refusal);
        if (p->waiting) p->waiting(EditorOps::error("a newer plugin_wait took its place"));
        p->waiting = reply;
        deliver(*p);
    });
    ops_->addOp("plugin_reply", "A plugin's answer to a call: its result, or the error with its file and line", "{name: str, token: str, call: int, result?: {}, error?: str, event?: bool}",
                [this](const QJsonObject& a, const EditorOps::Reply& reply) {
        QJsonObject refusal;
        Plugin* p = authorised(a, refusal);
        if (!p) return reply(refusal);
        const auto it = p->open.find(a.value("call").toInt());
        if (it == p->open.end()) return reply(EditorOps::error("no such call (answered, or timed out)"));
        const EditorOps::Reply answer = it->second;
        p->open.erase(it);
        if (a.contains("error")) answer(EditorOps::error(a.value("event").toBool() ? a.value("error").toString()
                                                                                    : uiText("Kinetica", "плагин «") + p->name + "»: " + a.value("error").toString()));
        else answer(EditorOps::ok(a.value("result").toObject()));
        reply(EditorOps::ok());
    });
    ops_->addOp("plugin_list", "The plugins: name, file, state (работает, запускается, упал, остановлен), last error, commands; folder - "
                               "where plugin_new makes one, folders - where plugins are looked for (plugin_scan looks again)", "{}",
                [this](const QJsonObject&, const EditorOps::Reply& reply) {
        QJsonArray list;
        for (const auto& p : plugins_) {
            QJsonArray commands;
            for (const Command& c : p->commands) commands.append(QJsonObject{{"op", c.op}, {"label", c.label}, {"key", c.key}});
            list.append(QJsonObject{{"name", p->name}, {"file", p->file}, {"state", p->state}, {"error", p->error}, {"commands", commands}});
        }
        reply(EditorOps::ok({{"plugins", list}, {"folder", userFolder()}, {"folders", QJsonArray::fromStringList(lookedThrough())}}));
    });
    ops_->addOp("plugin_scan", "The plugins' folders looked through again: a folder with a plugin.py that came since they were is started (a "
                               "saved plugin.py restarts its plugin by itself, a deleted one is forgotten by itself). With folder - that "
                               "plugins' folder first added to them and remembered for the next starts: every folder in it with a plugin.py "
                               "is a plugin (a repository's checkout), as «Плагины → Добавить папку плагинов…»",
                "{folder?: str}", [this](const QJsonObject& a, const EditorOps::Reply& reply) {
        const QString folder = a.value("folder").toString();
        if (a.contains("folder") && !QFileInfo(folder).isDir()) return reply(EditorOps::error(uiText("Kinetica", "нет папки «") + folder + "»"));
        const QStringList before = names();
        if (a.contains("folder")) addFolder(QFileInfo(folder).absoluteFilePath());
        start(onlyGiven_);
        QJsonArray started;
        for (const QString& name : names())
            if (!before.contains(name)) started.append(name);
        reply(EditorOps::ok({{"started", started}, {"folders", QJsonArray::fromStringList(lookedThrough())}}));
    });
    ops_->addOp("plugin_new", "A new plugin from a template in the user's plugins' folder, started: kind command (a command with a field, "
                              "a button in «Плагины»), controller (a robot's on_tick: its sensors in, joint commands out) or world (an "
                              "extension of the physics: the bodies in, forces out - wind, a magnet, a thruster)",
                "{name: str, kind?: 'command'|'controller'|'world'}", [this](const QJsonObject& a, const EditorOps::Reply& reply) {
        QString error;
        const QString file = create(a.value("name").toString(), error, a.value("kind").toString("command"));
        reply(file.isEmpty() ? EditorOps::error(error) : EditorOps::ok({{"file", file}}));
    });
    ops_->addOp("plugin_reload", "A plugin started again (as a save of its file does)", "{name: str}", [this](const QJsonObject& a, const EditorOps::Reply& reply) {
        Plugin* p = find(a.value("name").toString());
        if (!p) return reply(EditorOps::error("no plugin " + a.value("name").toString()));
        p->reloaded = true;
        p->since.start();
        if (p->process) p->restarting = true, stop(*p);
        else launch(*p);
        reply(EditorOps::ok());
    });
}
