#pragma once
// A person's keys to the scene's panels while it plays (SceneGraph::controllers - the UR5e's «Пульт «Схват»»: W A S D
// along the floor, R F up and down, Z X a turn, Space the gripper). On while the scene plays with a panel that has
// keys, from the first ▶; T gives the keys back to the window and takes them again. Keys are read by their place on
// the board (KeyPlace.h: the Russian layout too); a key bound to a panel is the panel's while it is on - Space grips,
// it does not pause. A changed value is one call of controller_input (EditorOps): a script, an agent and a person
// move the arm the same way, and the console shows it. A text field keeps its keys.
#include <QObject>
#include <QString>

#include <map>
#include <set>
#include <string>

class EditorOps;
class SceneBuilder;

class ControllerKeys : public QObject {
    Q_OBJECT
public:
    ControllerKeys(EditorOps* ops, SceneBuilder* builder, QObject* parent = nullptr);
    void sync();                // the mode changed: on at ▶ with a panel, off at ■, every key let go
    bool active() const { return active_; }
    QString note() const;       // what the keys do, for the simulation bar (empty: off)

signals:
    void changed();             // on or off

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool hasKeys() const;       // a panel of the scene with a key bound
    void setActive(bool on);
    void send();                // every panel's values from the keys held, where they changed
    bool bound(const std::string& key) const;

    EditorOps* ops_;
    SceneBuilder* builder_;
    bool active_ = false, playing_ = false;
    std::set<std::string> held_;
    std::map<std::string, bool> toggled_;     // "panel/action": a toggle button's state
    std::map<std::string, std::string> sent_; // "panel/action": the value sent last, as text
};
