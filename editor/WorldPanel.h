// The "Мир" tab: the scene's global physics, separate from object properties - gravity, the physics
// engine's settings (ke::PhysicsSettings: substeps, how stiff contacts and joints are, sleep,
// continuous collision, damping) and the size of the world. Edits are scene data, with undo and file
// persistence; the builder owns applying them. English engine names are search aliases.
#pragma once
#include "scene/SceneGraph.h"
#include <QWidget>
#include <functional>
#include <map>
#include <vector>
class CollapsibleSection;
class QLabel;
class QVBoxLayout;

class WorldPanel : public QWidget {
    Q_OBJECT
public:
    explicit WorldPanel(QWidget* parent = nullptr);
    void setWorld(const ke::WorldSettings& world);
    void setEditable(bool editable);
    ke::WorldSettings settings() const;
signals:
    void settingsEdited(const ke::WorldSettings& world);
private:
    struct Field { QWidget* row; QWidget* input; CollapsibleSection* section; QString search; };
    using Read = std::function<void(const ke::WorldSettings&)>;
    using Write = std::function<void(ke::WorldSettings&)>;
    CollapsibleSection* section(const QString& title, bool open);
    QWidget* row(CollapsibleSection* section, const QString& label, QWidget* input, const QString& tip, const QString& aliases);
    void number(CollapsibleSection* section, const QString& label, const QString& name, double low, double high,
                double scale, const QString& unit, const QString& tip, ke::Real ke::PhysicsSettings::* member);
    void count(CollapsibleSection* section, const QString& label, const QString& name, int low, int high,
               const QString& tip, int ke::PhysicsSettings::* member);
    void flag(CollapsibleSection* section, const QString& label, const QString& name, const QString& tip,
              bool ke::PhysicsSettings::* member);
    void buildGravity();
    void buildContacts();
    void buildJoints();
    void buildAdvanced();
    void buildEnvironment();
    void filter(const QString& query);
    void changed();
    ke::WorldSettings world_;
    QWidget* controls_;
    QVBoxLayout* body_;
    QLabel* status_;
    QLabel* empty_;
    std::vector<Read> readers_;
    std::vector<std::pair<QWidget*, Write>> writers_;
    std::vector<Field> fields_;
    std::map<CollapsibleSection*, bool> folded_;
};
