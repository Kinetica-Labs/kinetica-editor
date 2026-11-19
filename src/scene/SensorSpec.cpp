// The datasheets of the kinds of sensors and the tables of their models (SensorSpec.h).
#include "scene/SensorSpec.h"

#include "core/Format.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace ke {

namespace {

// The fields' values with no model: a generic unit of the kind, nothing more precise than its kind usually is.
const std::vector<SpecField> kImu = {
    {"rate_hz", "Частота выдачи", "Гц", 0.1, 1e5, 10, 1, 1000},
    {"bandwidth_hz", "Полоса фильтра", "Гц", 0.1, 2e4, 10, 1, 260},
    {"accel_range_g", "Диапазон акселерометра", "±g", 0.5, 400, 1, 1, 2},
    {"gyro_range_dps", "Диапазон гироскопа", "±°/с", 1, 1e4, 125, 0, 250},
    {"bits", "Разрядность", "бит", 2, 32, 1, 0, 16},
    {"accel_noise_ug", "Шум акселерометра", "мкg/√Гц", 0, 1e5, 10, 1, 400},
    {"gyro_noise_dps", "Шум гироскопа", "°/с/√Гц", 0, 10, 0.001, 5, 0.005},
    {"accel_offset_mg", "Смещение нуля акселерометра", "±мg", 0, 1000, 5, 1, 50},
    {"gyro_offset_dps", "Смещение нуля гироскопа", "±°/с", 0, 100, 0.5, 2, 20},
    {"scale_error_pct", "Ошибка чувствительности", "±%", 0, 50, 0.5, 2, 3},
    {"mass_g", "Масса", "г", 0, 1e5, 1, 2, 0}};
const std::vector<SpecField> kSonar = {
    {"min_m", "Ближе не видит", "м", 0, 100, 0.01, 3, 0.02},
    {"max_m", "Дальше не видит", "м", 0.01, 1000, 0.5, 2, 4},
    {"beam_deg", "Угол луча", "°", 0.1, 180, 1, 1, 15},
    {"accuracy_mm", "Точность (3σ)", "мм", 0, 1000, 1, 1, 3},
    {"cycle_ms", "Цикл измерения", "мс", 0.1, 1e4, 10, 1, 60},
    {"timeout_ms", "Нет эха: спад ECHO через", "мс", 0.1, 1e4, 1, 1, 38},
    {"mass_g", "Масса", "г", 0, 1e5, 1, 2, 0}};
const std::vector<SpecField> kWrench = {
    {"rate_hz", "Частота", "Гц", 0.1, 1e5, 100, 1, 500},
    {"force_range_n", "Диапазон силы", "±Н", 0.1, 1e6, 10, 1, 50},
    {"torque_range_nm", "Диапазон момента", "±Н·м", 0.01, 1e5, 1, 2, 10},
    {"force_precision_n", "Шум силы (3σ)", "Н", 0, 1e4, 0.1, 3, 3.5},
    {"torque_precision_nm", "Шум момента (3σ)", "Н·м", 0, 1e3, 0.01, 4, 0.2},
    {"force_accuracy_n", "Смещение силы (3σ)", "Н", 0, 1e4, 0.1, 3, 4},
    {"torque_accuracy_nm", "Смещение момента (3σ)", "Н·м", 0, 1e3, 0.01, 4, 0.3},
    {"mass_g", "Масса", "г", 0, 1e5, 10, 1, 0}};
const std::vector<SpecField> kNone;

const char* kKindKeys[] = {"encoder", "imu", "sonar", "touch", "wrench", "effort", "stereo"}; // SensorKind

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

const SpecField* fieldOf(SensorKind kind, const std::string& key) {
    for (const SpecField& f : specFields(kind))
        if (key == f.key) return &f;
    return nullptr;
}

} // namespace

const std::vector<SpecField>& specFields(SensorKind kind) {
    return kind == SensorKind::Imu ? kImu : kind == SensorKind::Sonar ? kSonar : kind == SensorKind::Wrench ? kWrench : kNone;
}

const char* sensorKindKey(SensorKind kind) { return kKindKeys[std::clamp(int(kind), 0, 6)]; }

bool sensorKindOf(const std::string& key, SensorKind& kind) {
    for (int k = 0; k < 7; ++k)
        if (key == kKindKeys[k]) return kind = SensorKind(k), true;
    return false;
}

Real specValue(const SensorObject& s, const std::string& key) {
    const auto at = s.spec.find(key);
    if (at != s.spec.end()) return at->second;
    const SpecField* f = fieldOf(s.kind, key);
    return f ? Real(f->value) : 0.0_r;
}

ImuSpec imuSpec(const SensorObject& s) {
    const auto v = [&s](const char* key) { return double(specValue(s, key)); };
    return {v("rate_hz"), v("bandwidth_hz"), v("accel_range_g"), v("gyro_range_dps"), int(v("bits")), v("accel_noise_ug"),
            v("gyro_noise_dps"), v("accel_offset_mg"), v("gyro_offset_dps"), v("scale_error_pct")};
}

SonarSpec sonarSpec(const SensorObject& s) {
    const auto v = [&s](const char* key) { return double(specValue(s, key)); };
    return {v("min_m"), v("max_m"), v("beam_deg"), v("accuracy_mm"), v("cycle_ms"), v("timeout_ms")};
}

ForceTorqueSpec wrenchSpec(const SensorObject& s) {
    const auto v = [&s](const char* key) { return double(specValue(s, key)); };
    return {v("rate_hz"), v("force_range_n"), v("torque_range_nm"), v("force_precision_n"), v("torque_precision_nm"), v("force_accuracy_n"),
            v("torque_accuracy_nm")};
}

Real sensorMass(const SensorObject& s) { return fieldOf(s.kind, "mass_g") ? specValue(s, "mass_g") / 1000 : 0.0_r; }

std::string SensorCatalog::load(const std::vector<std::string>& directories) {
    clear();
    std::string problems;
    for (const std::string& dir : directories)
        for (int k = 0; k < 7; ++k) {
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
            else if (parseNumber(row[i], v)) m.values[columns[i]] = v;
            else problems += where + columns[i] + " «" + row[i] + "» is not a number\n";
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
    for (const SpecField& f : specFields(s.kind)) {
        const auto at = m.values.find(f.key);
        s.spec[f.key] = at != m.values.end() ? at->second : Real(f.value);
    }
}

void applyDefaultModel(SensorObject& s) {
    const std::vector<const SensorModel*> models = sensorCatalog().models(s.kind);
    if (!models.empty()) return applyModel(s, *models.front());
    s.model.clear();
    s.spec.clear();
    for (const SpecField& f : specFields(s.kind)) s.spec[f.key] = Real(f.value);
}

bool specMatches(const SensorObject& s, const SensorModel& m) {
    for (const SpecField& f : specFields(s.kind)) {
        const auto at = m.values.find(f.key);
        if (specValue(s, f.key) != (at != m.values.end() ? at->second : Real(f.value))) return false;
    }
    return true;
}

} // namespace ke
