#pragma once
// The Sequencer - a tab of the bottom panel, a simple Unreal Sequencer over the scene's sequences (scene/Animation.h):
// which sequence plays, the playhead and its time, the loop, ◆ a key of the selected objects where they stand (I), ⏺ a
// take of the physics (sim_record: played, recorded, stopped - a new sequence «Запись N»), and
// the timeline - a row a bound object, its keys as diamonds, a ruler of seconds. Dragging the playhead or ▶ in edit mode
// shows the poses (SceneBuilder's preview) and then makes one edit, sequence_seek; in play the playhead follows the
// world, which moves the sequence's bodies itself. Every change is an operation of the API (sequence_*), said in the
// console as a Python line.
#include <QElapsedTimer>
#include <QWidget>

#include <cstdint>

class EditorOps;
class SceneBuilder;
class QComboBox;
class QLabel;
class QTimer;
class QToolButton;
class SequencerTimeline;
class QJsonObject;
class QString;

class SequencerPanel : public QWidget {
    Q_OBJECT
public:
    SequencerPanel(EditorOps* ops, SceneBuilder* builder, QWidget* parent = nullptr);
    void sync();                    // the sequences as the scene has them now
    void setPlayTime(double time);  // in play: where the world is
    void keySelected();             // ◆: the selected objects keyed at the playhead
    double playhead() const;

private:
    friend class SequencerTimeline;
    void run(const QString& name, const QJsonObject& args);
    void seek(double time);         // the edit: sequence_seek
    void scrub(double time);        // the preview, no edit yet
    void endScrub();                // the preview ends: one sequence_seek
    void togglePreview(bool on);

    EditorOps* ops_;
    SceneBuilder* builder_;
    QComboBox* sequences_ = nullptr;
    QToolButton *add_ = nullptr, *start_ = nullptr, *play_ = nullptr, *loop_ = nullptr, *key_ = nullptr, *record_ = nullptr;
    QLabel *time_ = nullptr, *note_ = nullptr;
    SequencerTimeline* timeline_ = nullptr;
    QTimer* previewTimer_ = nullptr;
    QElapsedTimer previewClock_;
    double previewFrom_ = 0, scrubTime_ = 0, playTime_ = 0;
    bool scrubbing_ = false;
};
