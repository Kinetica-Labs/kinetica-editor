// The datasheets of the kinds of sensors and the tables of their models (SensorSpec.h).
#include "scene/SensorSpec.h"
#include "core/UiLabel.h"

#include "core/Format.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace ke {

namespace {

// A number of a datasheet: its key, unit (UCUM), range, step, decimals, its value with no model, its words.
Field num(const char* key, const char* unit, double min, double max, double step, int decimals, double value, const char* label) {
    Field f;
    f.key = key, f.unit = unit, f.min = min, f.max = max, f.step = step, f.decimals = decimals, f.value = value, f.label = label;
    return f;
}

// The fields' values with no model: a generic unit of the kind, nothing more precise than its kind usually is.
const std::vector<Field> kImu = {
    num("rate_hz", "Hz", 0.1, 1e5, 10, 1, 1000, KE_UI_LABEL("Kinetica", "Частота выдачи")),
    num("bandwidth_hz", "Hz", 0.1, 2e4, 10, 1, 260, KE_UI_LABEL("Kinetica", "Полоса фильтра")),
    num("accel_range_g", "[g]", 0.5, 400, 1, 1, 2, KE_UI_LABEL("Kinetica", "Диапазон акселерометра, ±")),
    num("gyro_range_dps", "deg/s", 1, 1e4, 125, 0, 250, KE_UI_LABEL("Kinetica", "Диапазон гироскопа, ±")),
    num("bits", "bit", 2, 32, 1, 0, 16, KE_UI_LABEL("Kinetica", "Разрядность")),
    num("accel_noise_ug_rthz", "u[g]/[rtHz]", 0, 1e5, 10, 1, 400, KE_UI_LABEL("Kinetica", "Шум акселерометра")),
    num("gyro_noise_dps_rthz", "deg/s/[rtHz]", 0, 10, 0.001, 5, 0.005, KE_UI_LABEL("Kinetica", "Шум гироскопа")),
    num("accel_offset_mg", "m[g]", 0, 1000, 5, 1, 50, KE_UI_LABEL("Kinetica", "Смещение нуля акселерометра, ±")),
    num("gyro_offset_dps", "deg/s", 0, 100, 0.5, 2, 20, KE_UI_LABEL("Kinetica", "Смещение нуля гироскопа, ±")),
    num("scale_error_pct", "%", 0, 50, 0.5, 2, 3, KE_UI_LABEL("Kinetica", "Ошибка чувствительности, ±")),
    num("mass_kg", "kg", 0, 100, 0.001, 4, 0, KE_UI_LABEL("Kinetica", "Масса"))};
const std::vector<Field> kSonar = {
    num("min_m", "m", 0, 100, 0.01, 3, 0.02, KE_UI_LABEL("Kinetica", "Ближе не видит")),
    num("max_m", "m", 0.01, 1000, 0.5, 2, 4, KE_UI_LABEL("Kinetica", "Дальше не видит")),
    num("beam_deg", "deg", 0.1, 180, 1, 1, 15, KE_UI_LABEL("Kinetica", "Угол луча")),
    num("accuracy_mm", "mm", 0, 1000, 1, 1, 3, KE_UI_LABEL("Kinetica", "Точность (3σ)")),
    num("cycle_ms", "ms", 0.1, 1e4, 10, 1, 60, KE_UI_LABEL("Kinetica", "Цикл измерения")),
    num("timeout_ms", "ms", 0.1, 1e4, 1, 1, 38, KE_UI_LABEL("Kinetica", "Нет эха: спад ECHO через")),
    num("mass_kg", "kg", 0, 100, 0.001, 4, 0, KE_UI_LABEL("Kinetica", "Масса"))};
const std::vector<Field> kWrench = {
    num("rate_hz", "Hz", 0.1, 1e5, 100, 1, 500, KE_UI_LABEL("Kinetica", "Частота")),
    num("force_range_n", "N", 0.1, 1e6, 10, 1, 50, KE_UI_LABEL("Kinetica", "Диапазон силы, ±")),
    num("torque_range_n_m", "N.m", 0.01, 1e5, 1, 2, 10, KE_UI_LABEL("Kinetica", "Диапазон момента, ±")),
    num("force_precision_n", "N", 0, 1e4, 0.1, 3, 3.5, KE_UI_LABEL("Kinetica", "Шум силы (3σ)")),
    num("torque_precision_n_m", "N.m", 0, 1e3, 0.01, 4, 0.2, KE_UI_LABEL("Kinetica", "Шум момента (3σ)")),
    num("force_accuracy_n", "N", 0, 1e4, 0.1, 3, 4, KE_UI_LABEL("Kinetica", "Смещение силы (3σ)")),
    num("torque_accuracy_n_m", "N.m", 0, 1e3, 0.01, 4, 0.3, KE_UI_LABEL("Kinetica", "Смещение момента (3σ)")),
    num("mass_kg", "kg", 0, 100, 0.01, 3, 0, KE_UI_LABEL("Kinetica", "Масса"))};
// A lidar's pattern is its numbers: a beam 1 x 1, a ring 360 rays over 360 degrees in one layer, a sphere layers over
// 180 degrees, a flashlight a grid in a narrow field.
const std::vector<Field> kLidar = {
    num("rays_h", "1", 1, 4096, 1, 0, 1, KE_UI_LABEL("Kinetica", "Лучей по горизонтали")),
    num("fov_h_deg", "deg", 0, 360, 5, 1, 0, KE_UI_LABEL("Kinetica", "Поле по горизонтали")),
    num("rays_v", "1", 1, 256, 1, 0, 1, KE_UI_LABEL("Kinetica", "Лучей по вертикали")),
    num("fov_v_deg", "deg", 0, 180, 5, 1, 0, KE_UI_LABEL("Kinetica", "Поле по вертикали")),
    num("min_m", "m", 0, 100, 0.01, 3, 0.05, KE_UI_LABEL("Kinetica", "Ближе не видит")),
    num("max_m", "m", 0.01, 1000, 1, 2, 12, KE_UI_LABEL("Kinetica", "Дальше не видит")),
    num("accuracy_mm", "mm", 0, 1000, 1, 1, 30, KE_UI_LABEL("Kinetica", "Точность (3σ)")),
    num("rate_hz", "Hz", 0.1, 1000, 1, 1, 10, KE_UI_LABEL("Kinetica", "Сканов в секунду")),
    num("mass_kg", "kg", 0, 100, 0.001, 4, 0, KE_UI_LABEL("Kinetica", "Масса"))};
const std::vector<Field> kNone;

const char* kKindKeys[] = {"encoder", "imu", "sonar", "touch", "wrench", "effort", "stereo", "lidar"}; // SensorKind

std::string trimmed(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// One line of a table: cells split at commas, a cell in double quotes may hold commas ("" is a quote).
std::vector<std::string> cells(const std::string& line) {
    std::vector<std::string> out(1);
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"' && quoted && i + 1 < line.size() && line[i + 1] == '"') out.back() += '"', ++i;
        else if (c == '"') quoted = !quoted;
        else if (c == ',' && !quoted) out.emplace_back();
        else out.back() += c;
    }
    for (std::string& c : out) c = trimmed(c);
    return out;
}

const Field* fieldOf(SensorKind kind, const std::string& key) {
    for (const Field& f : specFields(kind))
        if (key == f.key) return &f;
    return nullptr;
}

} // namespace

const std::vector<Field>& specFields(SensorKind kind) {
    return kind == SensorKind::Imu ? kImu : kind == SensorKind::Sonar ? kSonar : kind == SensorKind::Wrench ? kWrench : kind == SensorKind::Lidar ? kLidar : kNone;
}

const char* sensorKindKey(SensorKind kind) { return kKindKeys[std::clamp(int(kind), 0, int(kLastSensorKind))]; }

Real specValue(const SensorObject& s, const std::string& key) {
    const auto at = s.spec.find(key);
    if (at != s.spec.end()) return at->second;
    const Field* f = fieldOf(s.kind, key);
    return f ? Real(f->value) : 0.0_r;
}

ImuSpec imuSpec(const SensorObject& s) {
    const auto v = [&s](const char* key) { return double(specValue(s, key)); };
    return {v("rate_hz"), v("bandwidth_hz"), v("accel_range_g"), v("gyro_range_dps"), int(v("bits")), v("accel_noise_ug_rthz"),
            v("gyro_noise_dps_rthz"), v("accel_offset_mg"), v("gyro_offset_dps"), v("scale_error_pct")};
}

SonarSpec sonarSpec(const SensorObject& s) {
    const auto v = [&s](const char* key) { return double(specValue(s, key)); };
    return {v("min_m"), v("max_m"), v("beam_deg"), v("accuracy_mm"), v("cycle_ms"), v("timeout_ms")};
}

LidarSpec lidarSpec(const SensorObject& s) {
    const auto v = [&s](const char* key) { return double(specValue(s, key)); };
    return {int(v("rays_h")), int(v("rays_v")), v("fov_h_deg"), v("fov_v_deg"), v("min_m"), v("max_m"), v("accuracy_mm"), v("rate_hz")};
}

ForceTorqueSpec wrenchSpec(const SensorObject& s) {
    const auto v = [&s](const char* key) { return double(specValue(s, key)); };
    return {v("rate_hz"), v("force_range_n"), v("torque_range_n_m"), v("force_precision_n"), v("torque_precision_n_m"), v("force_accuracy_n"),
            v("torque_accuracy_n_m")};
}

Real sensorMass(const SensorObject& s) { return fieldOf(s.kind, "mass_kg") ? specValue(s, "mass_kg") : 0.0_r; }

std::string SensorCatalog::load(const std::vector<std::string>& directories) {
    clear();
    std::string problems;
    for (const std::string& dir : directories)
        for (int k = 0; k <= int(kLastSensorKind); ++k) {
            if (specFields(SensorKind(k)).empty()) continue;
            const std::string file = dir + "/" + kKindKeys[k] + ".csv";
            std::ifstream in(file, std::ios::binary);
            if (!in) continue;
            std::ostringstream text;
            text << in.rdbuf();
            problems += read(SensorKind(k), text.str(), file);
        }
    return problems;
}

// The first line not empty and not a comment (#) names the columns: `model`, `source`, the kind's fields.
std::string SensorCatalog::read(SensorKind kind, const std::string& text, const std::string& file) {
    files_.push_back(file);
    std::istringstream in(text);
    std::string line, problems;
    std::vector<std::string> columns;
    for (int number = 1; std::getline(in, line); ++number) {
        if (trimmed(line).empty() || trimmed(line)[0] == '#') continue;
        const std::vector<std::string> row = cells(line);
        const std::string where = file + ":" + std::to_string(number) + ": ";
        if (columns.empty()) {
            columns = row;
            if (std::find(columns.begin(), columns.end(), "model") == columns.end()) return problems + where + "no column «model»\n";
            for (const std::string& c : columns)
                if (c != "model" && c != "source" && !fieldOf(kind, c)) problems += where + "unknown column «" + c + "», ignored\n";
            continue;
        }
        SensorModel m;
        m.kind = kind;
        for (size_t i = 0; i < row.size() && i < columns.size(); ++i) {
            Real v = 0;
            if (columns[i] == "model") m.name = row[i];
            else if (columns[i] == "source") m.source = row[i];
            else if (row[i].empty() || !fieldOf(kind, columns[i])) continue;
            else if (!parseNumber(row[i], v)) problems += where + columns[i] + " «" + row[i] + "» is not a number\n";
            else if (std::string e; !checkField(*fieldOf(kind, columns[i]), Value(double(v)), e)) problems += where + m.name + ": " + e + "\n";
            else m.values[columns[i]] = v;
        }
        if (m.name.empty()) {
            problems += where + "a row with no model\n";
            continue;
        }
        const auto same = std::find_if(models_.begin(), models_.end(), [&](const SensorModel& x) { return x.kind == kind && x.name == m.name; });
        if (same != models_.end()) *same = m;
        else models_.push_back(m);
    }
    return problems;
}

std::vector<const SensorModel*> SensorCatalog::models(SensorKind kind) const {
    std::vector<const SensorModel*> out;
    for (const SensorModel& m : models_)
        if (m.kind == kind) out.push_back(&m);
    return out;
}

const SensorModel* SensorCatalog::find(SensorKind kind, const std::string& name) const {
    for (const SensorModel& m : models_)
        if (m.kind == kind && m.name == name) return &m;
    return nullptr;
}

SensorCatalog& sensorCatalog() {
    static SensorCatalog catalog;
    return catalog;
}

void applyModel(SensorObject& s, const SensorModel& m) {
    s.model = m.name;
    s.spec.clear();
    for (const Field& f : specFields(s.kind)) {
        const auto at = m.values.find(f.key);
        s.spec[f.key] = at != m.values.end() ? at->second : Real(f.value);
    }
}

void applyDefaultModel(SensorObject& s) {
    const std::vector<const SensorModel*> models = sensorCatalog().models(s.kind);
    if (!models.empty()) return applyModel(s, *models.front());
    s.model.clear();
    s.spec.clear();
    for (const Field& f : specFields(s.kind)) s.spec[f.key] = Real(f.value);
}

bool specMatches(const SensorObject& s, const SensorModel& m) {
    for (const Field& f : specFields(s.kind)) {
        const auto at = m.values.find(f.key);
        if (specValue(s, f.key) != (at != m.values.end() ? at->second : Real(f.value))) return false;
    }
    return true;
}

} // namespace ke
