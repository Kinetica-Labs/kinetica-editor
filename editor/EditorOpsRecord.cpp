// sim_record - the Sequencer's take as an operation of the API (EditorOps.h). On: the scene
// plays and the simulation's thread records every free body a frame (RobotWorld::startRecording) from the world's
// record_from. At the run's end (closeTake) the take closes and waits, the world paused, ▶ going on without it. Off - or
// ■ Stop, the take running or waiting -: the scene stops and the take comes in as a new active sequence «Запись N», through
// scene_put - its block, keys and all, is the journal's line and one undo step, so a journal replays it without the run.
#include "EditorOps.h"
#include "SceneBuilder.h"
#include "SimController.h"
#include "UiLanguage.h"
#include "Viewport.h"
#include "scene/SceneGraph.h"
#include "sim/Simulation.h"

#include <QLocale>
#include <QMetaObject>

#include <utility>

namespace {

constexpr double kTakeLimit = 120; // s of a take with no play range: a frame of every free body, not without end

// «Запись N»: one more than the takes the scene holds.
QString takeName(const ke::SceneGraph& g) {
    int n = 1;
    for (const ke::SequenceObject& q : g.sequences)
        if (QString::fromStdString(q.name).startsWith("Запись ")) n = std::max(n, QString::fromStdString(q.name).mid(7).toInt() + 1);
    return QString("Запись %1").arg(n);
}

// The take as a block of the scene file, id 0: scene_put gives it a new one.
std::string blockOf(ke::SequenceObject take) {
    ke::SceneGraph one;
    take.id = 0;
    one.sequences.push_back(std::move(take));
    const std::string text = one.save();
    const size_t world = text.find("\nworld ");
    return text.substr(text.find('\n', world + 1) + 1);
}

} // namespace

void EditorOps::addRecordOp() {
    add("sim_record", "The Sequencer's take: on - the scene plays (from edit, as ▶) and every dynamic body the playing "
                      "sequence does not lead is recorded a frame, its pose and velocity, from the world's record_from on (now, if later): "
                      "before it the world runs and nothing is kept. At the run's end - the world's duration; none: two minutes of take - "
                      "the take closes and the world pauses (▶ goes on, not recording); it comes in with ■ Stop. Off - the scene stops and the "
                      "take comes in as a new sequence «Запись N», open, not played by ▶ (a scene_put: one undo step), each body a track of keys "
                      "at the world's time that give its run back within 0.5 mm and 0.2 degrees (Hermite between recorded keys); a robot's "
                      "links its joints and IK move are recorded too, bodies that stood still are not; the tracks of the sequence that played "
                      "come along - a camera's keys, a light's. ■ Stop while recording ends it the same way", "{on: bool}", [this](const QJsonObject& a, const Reply& reply) {
        const bool on = a.value("on").toBool(!recording_);
        if (on == recording_) return reply(error(on ? "already recording" : "not recording"));
        if (on) {
            if (!waitingTake_.empty()) return reply(error("the take closed at the run's end waits for ■ Stop to come into the scene: stop, then record"));
            const ke::WorldSettings w = builder_->graph().world;
            const double now = builder_->editing() || !view_->snapshot() ? 0 : double(view_->snapshot()->time);
            const double from = std::max(double(w.recordFrom), now);
            if (w.duration > 0 && from >= double(w.duration) - 1e-6)
                return reply(error(QString("the run is at %1 s, past the play range's end %2 s: ■ Stop, then record").arg(now, 0, 'f', 2).arg(double(w.duration))));
            if (builder_->mode() != SceneBuilder::Mode::Playing) builder_->play();
            builder_->setTakeEnd(w.duration > 0 ? 0 : from + kTakeLimit);
            takeFrom_ = from;
            setRecording(true);
            ctrl_->post([start = w.recordFrom](ke::Simulation& s) { s.startRecording(start); }, "record"); // after the load play posted
            if (refresh_) refresh_();
            return reply(ok({{"recording", true}}));
        }
        setRecording(false);
        builder_->setTakeEnd(0);
        if (!builder_->editing()) builder_->stop(); // the load of Stop keeps the take (Simulation::takeRecording)
        finishTake(reply);
    });
    connect(builder_, &SceneBuilder::modeChanged, this, [this] { // ■ Stop: the take that runs, or waits, comes in
        if (!builder_->editing()) return;
        if (recording_) {
            setRecording(false);
            builder_->setTakeEnd(0);
            return finishTake(nullptr);
        }
        if (!waitingTake_.empty()) putTake(std::exchange(waitingTake_, {}), waitingCounts_, nullptr);
    });
}

void EditorOps::closeTake() {
    const bool limited = builder_->graph().world.duration <= 0;
    const double from = takeFrom_, to = builder_->playEnd();
    setRecording(false);
    builder_->setTakeEnd(0);
    takeOut([this, limited, from, to](const std::string& block, const QJsonObject& counts) {
        const QString span = QString("%1–%2").arg(QLocale().toString(from, 'f', 2), QLocale().toString(to, 'f', 2));
        if (block.empty()) return builder_->say(uiText("Kinetica", "Дубль пуст: ни одно свободное тело не сдвинулось. ▶ — дальше, ■ Стоп вернёт сцену"));
        if (builder_->editing()) return putTake(block, counts, nullptr); // stopped in the meantime
        waitingTake_ = block, waitingCounts_ = counts;
        builder_->say(limited ? QString(uiText("Kinetica", "Запись ограничена 2 мин: дубль %1 с войдёт в секвенсор с ■ Стоп. Дольше — задайте «До» в секвенсоре; ▶ — дальше без записи")).arg(span)
                              : QString(uiText("Kinetica", "Дубль записан: %1 с — войдёт в секвенсор с ■ Стоп. ▶ — смотреть дальше без записи")).arg(span));
    });
}

void EditorOps::takeOut(std::function<void(const std::string&, const QJsonObject&)> done) {
    const QString name = takeName(builder_->graph());
    ctrl_->post([this, done, name](ke::Simulation& s) {
        ke::SequenceObject take;
        const bool got = s.takeRecording(name.toStdString(), take) && !take.tracks.empty();
        size_t keys = 0;
        for (const ke::SequenceTrack& t : take.tracks) keys += t.keys.size();
        const QJsonObject counts{{"tracks", int(take.tracks.size())}, {"keys", int(keys)}, {"length", double(take.length())}};
        const std::string block = got ? blockOf(std::move(take)) : std::string();
        QMetaObject::invokeMethod(this, [done, block, counts] { done(block, counts); }, Qt::QueuedConnection);
    }, "take");
}

void EditorOps::putTake(const std::string& block, const QJsonObject& counts, const Reply& reply) {
    if (block.empty()) {
        if (refresh_) refresh_();
        if (reply) reply(error("the take holds nothing: no free body moved"));
        return;
    }
    run("scene_put", QJsonObject{{"block", QString::fromStdString(block)}}, [reply, counts](const QJsonObject& r) {
        QJsonObject answer = r;
        for (auto it = counts.begin(); it != counts.end(); ++it) answer.insert(it.key(), it.value());
        if (reply) reply(answer);
    }, "ui");
}

void EditorOps::finishTake(const Reply& reply) {
    takeOut([this, reply](const std::string& block, const QJsonObject& counts) { putTake(block, counts, reply); });
}
