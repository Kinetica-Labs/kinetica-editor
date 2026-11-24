// The resource lease of an agent's session (core/RuntimePolicy.h, hosted by EditorRuntime): its status,
// its limits, an explicit renewal and stop, and app_quit, which closes the program by its API. A stop
// pauses the scene on the simulation's thread and keeps its state; nothing here moves the robot.
#include "EditorOps.h"
#include "EditorRuntime.h"
#include "SceneBuilder.h"
#include "SimController.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QPointer>
#include <QTimer>
#include <algorithm>

namespace {
ke::Value policyArgs(const QJsonObject& args) {
    ke::Value out = ke::Value::map();
    for (auto it = args.begin(); it != args.end(); ++it) {
        if (it.value().isDouble()) out.set(it.key().toStdString(), it.value().toDouble());
        else out.set(it.key().toStdString(), "invalid numeric argument");
    }
    return out;
}
}

// Nothing of this stage runs beside the simulation (no camera stream, no arm planner): the usage is empty, and the
// lease only ends an agent's hidden editor that nobody calls any more.
ke::RuntimePolicy::Usage EditorOps::runtimeUsage() const { return {}; }

void EditorOps::addRuntimeOps() {
    runtime_ = new EditorRuntime(window_, this);
    runtime_->setUsage([this] { return runtimeUsage(); });
    runtime_->setStop([this](const QString& why, EditorRuntime::Done done) { stopRuntime(why, std::move(done)); });
    addSync("runtime_status", ke::kRuntimeStatusHelp, "{}", [this](const QJsonObject& args) {
        if (!args.isEmpty()) return error("runtime_status takes no arguments");
        auto out = coreAnswerJson(runtime_->policy().status(runtimeUsage()));
        out.insert("hidden", runtime_->hidden());
        out.insert("hidden_idle_ms", runtime_->externalIdleMs());
        out.insert("hidden_lease_remaining_ms", runtime_->hidden() ? std::max(0.0, runtime_->policy().limits().idleTimeoutMs - runtime_->externalIdleMs()) : -1);
        out.insert("stopping", runtime_->stopping());
        out.insert("hidden_instance_limit", 2);
        return out;
    });
    addSync("runtime_configure", ke::kRuntimeConfigureHelp, ke::kRuntimeConfigureArgs, [this](const QJsonObject& args) {
        std::string why;
        if (!runtime_->policy().configure(policyArgs(args), runtimeUsage(), why)) return error(QString::fromStdString(why));
        return coreAnswerJson(runtime_->policy().status(runtimeUsage()));
    });
    addSync("runtime_keepalive", "Explicitly renew the resource lease; status polling does not renew it", "{}", [this](const QJsonObject& args) {
        if (!args.isEmpty()) return error("runtime_keepalive takes no arguments");
        if (runtime_->stopping()) return error("runtime is stopping");
        runtime_->policy().activity();
        return coreAnswerJson(runtime_->policy().status(runtimeUsage()));
    });
    add("runtime_stop", ke::kRuntimeStopHelp, "{}", [this](const QJsonObject& args, const Reply& reply) {
        if (!args.isEmpty()) return reply(error("runtime_stop takes no arguments"));
        runtime_->stop("explicit_stop", [this, reply] { reply(coreAnswerJson(runtime_->policy().status(runtimeUsage()))); });
    });
    // The program closed by its API: the session stopped first, as runtime_stop does, then the event loop left after the
    // answer is out - the caller gets its answer before the connection goes.
    add("app_quit", "Close the editor: stop the session (the scene paused), answer, then quit", "{}", [this](const QJsonObject& args, const Reply& reply) {
        if (!args.isEmpty()) return reply(error("app_quit takes no arguments"));
        runtime_->stop("app_quit", [reply] {
            reply(QJsonObject{{"ok", true}, {"quitting", true}});
            QTimer::singleShot(200, qApp, &QCoreApplication::quit);
        });
    });
}

// A stop: the scene paused - its simulation and its measured state kept -, answered once the simulation's thread has
// passed the pause (a resume asked for while stopping cannot outrun it).
void EditorOps::stopRuntime(const QString&, std::function<void()> done) {
    builder_->pause();
    QPointer<EditorOps> self(this);
    ctrl_->post([self, done = std::move(done)](ke::Simulation&) {
        if (self) QMetaObject::invokeMethod(self, [self, done] {
            if (!self) return;
            self->builder_->pause();
            if (done) done();
        }, Qt::QueuedConnection);
    }, "runtime_stop");
}
