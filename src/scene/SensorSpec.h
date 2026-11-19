#pragma once
// A sensor's datasheet in the scene, and the tables of models it is copied from. A kind of sensor (an IMU, a sonar, a
// force-torque sensor) has one device model (src/devices) that behaves as any unit of the kind given the numbers of its
// datasheet: its fields (SpecField) say which - each a column of the kind's table and a key of the scene file, its unit
// in its name as the datasheet states it. The tables (catalog/sensors/<kind>.csv: a row per model, a column per field,
// `model` and `source` - the datasheet it was read from) are data: a model is added by a row, not by code. A sensor
// keeps its own copy of the numbers (SensorObject::spec), so a scene reads the same with no table at hand.
#include "devices/ForceTorque.h"
#include "devices/Imu.h"
#include "devices/Sonar.h"
#include "scene/SceneGraph.h"

#include <map>
#include <string>
#include <vector>

namespace ke {

// One number of a kind's datasheet: its key, its words and unit for a person, its range and step, and the value a
// sensor has when no model gave one.
struct SpecField {
    const char* key;
    const char* label;
    const char* unit;
    double min, max, step;
    int decimals;
    double value;
};
const std::vector<SpecField>& specFields(SensorKind kind); // empty: the kind has no datasheet (yet)
const char* sensorKindKey(SensorKind kind);                // "imu", "sonar", "wrench"...: its table's name, its file word
bool sensorKindOf(const std::string& key, SensorKind& kind);

Real specValue(const SensorObject& s, const std::string& key); // the sensor's number, else its field's value
ImuSpec imuSpec(const SensorObject& s);
SonarSpec sonarSpec(const SensorObject& s);
ForceTorqueSpec wrenchSpec(const SensorObject& s);
Real sensorMass(const SensorObject& s); // kg

// A model as its table has it: the numbers its datasheet states (a cell left empty: not stated, the field's value).
struct SensorModel {
    SensorKind kind = SensorKind::Imu;
    std::string name, source;
    std::map<std::string, Real> values;
};

// The tables of models found in some directories: <directory>/<kind>.csv each. A later directory's row of a model
// replaces an earlier's - the person's own tables over the program's.
class SensorCatalog {
public:
    std::string load(const std::vector<std::string>& directories); // all again; the problems found, one per line
    void clear() { models_.clear(), files_.clear(); }
    std::string read(SensorKind kind, const std::string& text, const std::string& file); // one table's text, after the earlier
    std::vector<const SensorModel*> models(SensorKind kind) const;
    const SensorModel* find(SensorKind kind, const std::string& name) const;
    const std::vector<std::string>& files() const { return files_; }

private:
    std::vector<SensorModel> models_;
    std::vector<std::string> files_;
};
SensorCatalog& sensorCatalog(); // the process's: the host loads it (the editor at start and when a table changes)

// The model's numbers into the sensor, the fields it does not state at their values; its name kept as where they
// came from. The default: the first model of the kind's table, else the fields' values and no name.
void applyModel(SensorObject& s, const SensorModel& m);
void applyDefaultModel(SensorObject& s);
bool specMatches(const SensorObject& s, const SensorModel& m); // its numbers are still the model's

} // namespace ke
