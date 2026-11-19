// sim_record - the Sequencer's take (Unreal's Take Recorder) as an operation of the API (EditorOps.h). On: the scene
// plays and the simulation's thread records every free body a frame (RobotWorld::startRecording). Off - or ■ Stop
// pressed while it records -: the scene stops and the take comes in as a new active sequence «Запись N», through
// scene_put - its block, keys and all, is the journal's line and one undo step, so a journal replays it without the run.
#include "EditorOps.h"
#include "SceneBuilder.h"
#include "SimController.h"
#include "scene/SceneGraph.h"
#include "sim/Simulation.h"

#include <QMetaObject>

namespace {

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
    add("sim_record", "The Sequencer's take: on - the scene plays (from edit, as ▶) and every free body (dynamic, of no robot, no joint, no "
                      "sequence) is recorded a frame, its pose and velocity; off - the scene stops and the take comes in as a new active "
                      "sequence «Запись N» (a scene_put: one undo step), each body a track of keys that give its run back within 0.5 mm and "
                      "0.2 degrees (Hermite between recorded keys). ■ Stop while recording ends it the same way. A robot's links are not "
                      "recorded yet (a joint track is a kind still to come)", "{on: bool}", [this](const QJsonObject& a, const Reply& reply) {
        const bool on = a.value("on").toBool(!recording_);
        if (on == recording_) return reply(error(on ? "already recording" : "not recording"));
        if (on) {
            if (builder_->mode() != SceneBuilder::Mode::Playing) builder_->play();
            recording_ = true;
            ctrl_->post([](ke::Simulation& s) { s.startRecording(); }, "record"); // after the load play posted
            if (refresh_) refresh_();
            return reply(ok({{"recording", true}}));
        }
        recording_ = false;
        if (!builder_->editing()) builder_->stop(); // the load of Stop keeps the take (Simulation::takeRecording)
        finishTake(reply);
    });
    connect(builder_, &SceneBuilder::modeChanged, this, [this] { // ■ Stop pressed while recording
        if (!recording_ || !builder_->editing()) return;
        recording_ = false;
        finishTake(nullptr);
    });
}

void EditorOps::finishTake(const Reply& reply) {
    const QString name = takeName(builder_->graph());
    ctrl_->post([this, reply, name](ke::Simulation& s) {
        ke::SequenceObject take;
        const bool got = s.takeRecording(name.toStdString(), take) && !take.tracks.empty();
        size_t keys = 0;
        for (const ke::SequenceTrack& t : take.tracks) keys += t.keys.size();
        const QJsonObject counts{{"tracks", int(take.tracks.size())}, {"keys", int(keys)}, {"length", double(take.length())}};
        const std::string block = got ? blockOf(std::move(take)) : std::string();
        QMetaObject::invokeMethod(this, [this, reply, got, block, counts] {
            if (!got) {
                if (refresh_) refresh_();
                if (reply) reply(error("the take holds nothing: no free body moved"));
                return;
            }
            run("scene_put", QJsonObject{{"block", QString::fromStdString(block)}}, [reply, counts](const QJsonObject& r) {
                QJsonObject answer = r;
                for (auto it = counts.begin(); it != counts.end(); ++it) answer.insert(it.key(), it.value());
                if (reply) reply(answer);
            }, "ui");
        }, Qt::QueuedConnection);
    }, "take");
}
