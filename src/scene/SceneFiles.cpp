// A scene's files and assemblies (SceneFiles.h): a scene read and written whole; a part of a scene cut out as a scene of
// its own, and such a scene's text added to another.
#include "scene/SceneFiles.h"
#include "scene/Describe.h"

#include "core/Json.h"
#include "scene/SceneActions.h"
#include "scene/SceneOps.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <type_traits>

namespace ke {

namespace fs = std::filesystem;

namespace {

bool refuse(std::string& error, const std::string& why) {
    error = why;
    return false;
}

// A word of a line and where it stands - quotes and all, as the reader's words() splits it.
struct Word {
    size_t at = 0, length = 0;
    std::string text;
};

std::vector<Word> wordsAt(const std::string& line) {
    std::vector<Word> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
        if (i >= line.size()) break;
        const size_t start = i;
        if (line[i] == '"') {
            const size_t end = line.find('"', i + 1);
            i = end == std::string::npos ? line.size() : end + 1;
            out.push_back({start, i - start, line.substr(start + 1, (end == std::string::npos ? line.size() : end) - start - 1)});
        } else {
            while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') ++i;
            out.push_back({start, i - start, line.substr(start, i - start)});
        }
    }
    return out;
}

// Per kind of block and its line (part): the keys whose values are ids, and the word a line leads with before an id.
struct IdPlaces {
    std::set<std::string> keys;
    std::string lead;
};

const std::map<std::string, std::map<std::string, IdPlaces>>& idPlaces() {
    static const std::map<std::string, std::map<std::string, IdPlaces>> places = [] {
        std::map<std::string, std::map<std::string, IdPlaces>> out;
        for (const SchemaKind& kind : sceneSchema())
            for (const SchemaPart& part : kind.parts) {
                IdPlaces p;
                for (const SchemaKey& key : part.keys)
                    if (key.form.compare(0, 2, "id") == 0) p.keys.insert(key.word);
                const std::vector<Word> lead = wordsAt(part.lead);
                if (lead.size() > 1 && lead[1].text.compare(0, 4, "<id>") == 0) p.lead = lead[0].text; // "camera <id>: ..."
                if (!p.keys.empty() || !p.lead.empty()) out[kind.kind][part.word] = p;
            }
        return out;
    }();
    return places;
}

bool isWhole(const std::string& s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }); }

template <class T>
constexpr bool kPosed = std::is_base_of_v<SceneObject, T>;

} // namespace

bool readSceneFile(const std::string& path, SceneGraph& out, std::string& error) {
    if (path.empty()) return refuse(error, "не назван файл");
    const fs::path p = fs::u8path(path);
    std::error_code ec;
    const fs::path whole = fs::absolute(p, ec).lexically_normal();
    if (!fs::exists(p, ec)) return refuse(error, "нет файла «" + path + "»" + (p.is_relative() ? " (искали: " + whole.generic_u8string() + ")" : ""));
    if (fs::is_directory(p, ec)) return refuse(error, "«" + path + "» - каталог, а не файл");
    std::ifstream in(p, std::ios::binary);
    if (!in) return refuse(error, "файл «" + path + "» не читается");
    std::ostringstream text;
    text << in.rdbuf();
    SceneGraph g;
    if (!g.load(validUtf8(text.str()), error)) return false;
    g.baseDirectory = whole.parent_path().generic_u8string(); // model files next to the scene
    out = std::move(g);
    return true;
}

bool writeSceneFile(const SceneGraph& g, const std::string& path, std::string& savedAs, std::string& error) {
    if (path.empty()) return refuse(error, "не назван файл");
    std::error_code ec;
    const fs::path whole = fs::absolute(fs::u8path(path), ec).lexically_normal();
    if (!fs::is_directory(whole.parent_path(), ec)) return refuse(error, "нет каталога «" + whole.parent_path().generic_u8string() + "» для файла «" + path + "»");
    SceneGraph saved = g;
    rebaseFiles(saved, whole.parent_path().generic_u8string()); // into another folder, the models' and textures' paths are written from there
    fs::path part = whole;
    part += ".part";
    {
        std::ofstream out(part, std::ios::binary);
        out << saved.save();
        if (!out.flush()) return fs::remove(part, ec), refuse(error, "файл «" + path + "» не записывается");
    }
    fs::rename(part, whole, ec);
    if (ec) return fs::remove(part, ec), refuse(error, "файл «" + path + "» не записывается");
    savedAs = whole.generic_u8string(); // forward slashes on every system, as the window answers
    return true;
}

bool readTextFile(const std::string& path, std::string& text, std::string& error) {
    std::ifstream in(fs::u8path(path), std::ios::binary);
    if (!in) return refuse(error, "файл «" + path + "» не читается");
    std::ostringstream all;
    all << in.rdbuf();
    text = validUtf8(all.str());
    return true;
}

std::string userDataFolder() {
    auto env = [](const char* name) { const char* v = std::getenv(name); return std::string(v ? v : ""); };
    std::error_code ec;
    if (!env("APPDATA").empty()) return (fs::u8path(env("APPDATA")) / "Kinetica" / "Kinetica").generic_u8string();
    const fs::path home = fs::u8path(env("HOME"));
    if (fs::is_directory(home / "Library" / "Application Support", ec)) return (home / "Library" / "Application Support" / "Kinetica" / "Kinetica").generic_u8string();
    const fs::path xdg = env("XDG_DATA_HOME").empty() ? home / ".local" / "share" : fs::u8path(env("XDG_DATA_HOME"));
    return (xdg / "Kinetica" / "Kinetica").generic_u8string();
}

std::string shiftedIds(const std::string& text, int64_t by) {
    std::string out, kind;
    size_t from = 0;
    while (from <= text.size()) {
        const size_t end = std::min(text.find('\n', from), text.size());
        std::string line = text.substr(from, end - from);
        const std::vector<Word> w = wordsAt(line);
        if (!w.empty() && line[0] != ' ' && line[0] != '\t') kind = w[0].text == "end" ? std::string() : w[0].text; // a block's head
        else if (!w.empty() && !kind.empty()) {
            const auto k = idPlaces().find(kind);
            const auto p = k != idPlaces().end() ? k->second.find(w[0].text) : std::map<std::string, IdPlaces>::const_iterator();
            if (k != idPlaces().end() && p != k->second.end()) {
                std::vector<size_t> ids; // the words that are ids, by their place
                for (size_t i = 1; i + 1 < w.size(); ++i)
                    if (p->second.keys.count(w[i].text) || (i == 1 && w[1].text == p->second.lead)) ids.push_back(i + 1);
                for (auto i = ids.rbegin(); i != ids.rend(); ++i) // from the end: the places before stay where they were
                    if (isWhole(w[*i].text) && w[*i].text != "0")
                        line.replace(w[*i].at, w[*i].length, std::to_string(std::stoll(w[*i].text) + by));
            }
        }
        out += line;
        if (end < text.size()) out += '\n';
        from = end + 1;
    }
    return out;
}

SceneGraph assemblyOf(const SceneGraph& g, const std::vector<uint32_t>& ids) {
    SceneGraph part = g;
    std::set<uint32_t> keep;
    auto within = [&](uint32_t id) {
        const std::vector<uint32_t> chain = ancestry(part, id);
        return std::any_of(chain.begin(), chain.end(), [&](uint32_t x) { return std::count(ids.begin(), ids.end(), x) > 0; });
    };
    eachObjectList(part, [&](auto& list) {
        using T = typename std::decay_t<decltype(list)>::value_type;
        if constexpr (kPosed<T>)
            for (auto& o : list)
                if (within(o.id)) keep.insert(o.id);
    });
    for (const JointObject& j : part.joints)
        if (keep.count(j.a) && (j.b == 0 || keep.count(j.b))) keep.insert(j.id);
    for (const SensorObject& s : part.sensors)
        if (keep.count(s.parent)) keep.insert(s.id); // on a kept joint
    for (const DriverObject& d : part.drivers)
        if (keep.count(d.joint)) keep.insert(d.id), keep.insert(d.battery);
    for (const ControllerObject& c : part.controllers)
        if (!c.actions.empty() && std::all_of(c.actions.begin(), c.actions.end(), [&](const ControllerAction& a) { return keep.count(a.target); }))
            keep.insert(c.id);
    // What is kept and stood under what goes: it stands in the world, where it was.
    eachObjectList(part, [&](auto& list) {
        using T = typename std::decay_t<decltype(list)>::value_type;
        if constexpr (kPosed<T>)
            for (auto& o : list)
                if (keep.count(o.id) && o.parent && !keep.count(o.parent)) {
                    Vector3 p;
                    Quaternion q;
                    worldPose(part, o, p, q);
                    o.parent = 0;
                    setWorldPose(part, o, p, q);
                }
    });
    std::vector<uint32_t> gone;
    eachObjectList(part, [&](auto& list) {
        for (const auto& o : list)
            if (!keep.count(o.id)) gone.push_back(o.id);
    });
    deleteObjects(part, gone);
    AABB box;
    eachObjectList(part, [&](auto& list) {
        using T = typename std::decay_t<decltype(list)>::value_type;
        if constexpr (kPosed<T>)
            for (const auto& o : list)
                if (const AABB b = objectBounds(part, o.id); !o.parent && b.valid()) box.expand(b);
    });
    const Vector3 origin = box.valid() ? Vector3(box.center().x, box.lo.y, box.center().z) : Vector3(0.0_r);
    eachObjectList(part, [&](auto& list) {
        using T = typename std::decay_t<decltype(list)>::value_type;
        if constexpr (kPosed<T>)
            for (auto& o : list)
                if (!o.parent) o.position = o.position - origin;
    });
    for (JointObject& j : part.joints)
        if (!j.b) j.anchorB = j.anchorB - origin; // held to the world: its world point goes along
    return part;
}

uint32_t insertAssembly(SceneGraph& g, const std::string& text, const std::string& folder, const Vector3& at, const std::string& name,
                        std::string& error, std::map<uint32_t, uint32_t>* given) {
    SceneGraph probe;
    if (!probe.load(text, error)) return 0;
    uint32_t lowest = 0;
    eachObjectList(probe, [&](const auto& list) {
        for (const auto& o : list)
            if (o.id && (!lowest || o.id < lowest)) lowest = o.id;
    });
    const int64_t by = lowest ? int64_t(nextId(g)) - int64_t(lowest) : 0;
    SceneGraph part;
    if (!part.load(shiftedIds(text, by), error)) return 0;
    if (given)
        eachObjectList(probe, [&](const auto& list) {
            for (const auto& o : list) (*given)[o.id] = uint32_t(int64_t(o.id) + by);
        });
    part.baseDirectory = folder;
    rebaseFiles(part, g.baseDirectory); // its models' paths as the scene reads them
    Group group;
    group.id = std::max(nextId(g), nextId(part));
    group.name = name;
    group.position = at;
    eachObjectList(part, [&](auto& list) {
        using T = typename std::decay_t<decltype(list)>::value_type;
        if constexpr (kPosed<T>)
            for (auto& o : list)
                if (!o.parent) o.parent = group.id;
    });
    for (JointObject& j : part.joints)
        if (!j.b) j.anchorB = j.anchorB + at;
    g.groups.push_back(group);
    auto add = [](auto& to, auto& from) { to.insert(to.end(), from.begin(), from.end()); };
    add(g.entities, part.entities), add(g.groups, part.groups), add(g.arrays, part.arrays), add(g.lights, part.lights), add(g.cameras, part.cameras);
    add(g.joints, part.joints), add(g.batteries, part.batteries), add(g.drivers, part.drivers), add(g.sensors, part.sensors);
    add(g.forces, part.forces), add(g.wheels, part.wheels), add(g.controllers, part.controllers), add(g.sequences, part.sequences);
    return group.id;
}

std::vector<AssemblyFile> assemblyFiles(const std::vector<std::string>& folders) {
    std::vector<AssemblyFile> out;
    for (const std::string& folder : folders) {
        std::error_code ec;
        std::vector<fs::path> files;
        for (const auto& f : fs::directory_iterator(fs::u8path(folder), ec))
            if (f.path().extension() == ".kscene") files.push_back(f.path());
        std::sort(files.begin(), files.end());
        for (const fs::path& f : files) {
            const std::string name = f.stem().u8string();
            if (std::none_of(out.begin(), out.end(), [&](const AssemblyFile& a) { return a.name == name; }))
                out.push_back({name, f.generic_u8string()});
        }
    }
    return out;
}

bool assemblyCall(const std::string& op, const Value& args, const SceneGraph& g, const std::string& dataDir, const AssemblyEdit& edit,
                  Value& answer, std::string& error) {
    const std::string mine = userDataFolder() + "/assemblies";
    const std::vector<AssemblyFile> all = assemblyFiles({dataDir + "/assemblies", mine});
    if (op == "assembly_list") {
        Value list = Value::list();
        for (const AssemblyFile& a : all) {
            Value x = Value::map();
            x.set("name", a.name), x.set("file", a.file);
            list.push(x);
        }
        return answer.set("assemblies", list), true;
    }
    if (op == "assembly_save") {
        std::vector<uint32_t> ids;
        for (const Value& v : args["ids"].items()) ids.push_back(uint32_t(v.integer()));
        const std::string name = args["name"].text("");
        if (name.empty() || name.find_first_of("/\\:*?\"<>|") != std::string::npos) return refuse(error, "name: a word for the file, no / \\ : * ? \" < > |");
        const SceneGraph part = assemblyOf(g, ids);
        bool any = false;
        eachObjectList(part, [&](const auto& list) { any = any || !list.empty(); });
        if (!any) return refuse(error, "ids: none of them is an object of the scene");
        std::error_code ec;
        fs::create_directories(fs::u8path(mine), ec);
        std::string file;
        if (!writeSceneFile(part, mine + "/" + name + ".kscene", file, error)) return false;
        return answer.set("name", name), answer.set("file", file), true;
    }
    // assembly_add and assembly_describe: its text, a file, or by its name in the folders
    std::string file = args["file"].text(""), name = args["name"].text(""), text = args["text"].text("");
    if (!text.empty()) {
        if (text.find("\nend") == std::string::npos && text.compare(0, 3, "end") != 0) return refuse(error, "text: blocks of the scene file, each ending with 'end'");
        if (name.empty()) name = "Сборка";
    } else if (file.empty()) {
        const auto found = std::find_if(all.begin(), all.end(), [&](const AssemblyFile& a) { return a.name == name; });
        if (found == all.end()) {
            std::string names;
            for (const AssemblyFile& a : all) names += (names.empty() ? "" : ", ") + a.name;
            return refuse(error, "no assembly «" + name + "» (assembly_list: " + (names.empty() ? "none" : names) + ")");
        }
        file = found->file;
    }
    if (text.empty()) {
        if (name.empty()) name = fs::u8path(file).stem().u8string();
        if (!readTextFile(file, text, error)) return false;
    }
    if (op == "assembly_describe") { // an example studied before one's own is built: the device in words, the scene untouched
        SceneGraph example;
        if (!example.load(text, error)) return false;
        return answer.set("name", name), answer.set("text", describeScene(example)), true;
    }
    const std::vector<Value> p = args["position"].items();
    const Vector3 at = p.size() == 3 ? Vector3(Real(p[0].number()), Real(p[1].number()), Real(p[2].number())) : Vector3(0.0_r);
    std::error_code ec;
    const std::string folder = file.empty() ? g.baseDirectory : fs::absolute(fs::u8path(file), ec).parent_path().generic_u8string();
    uint32_t id = 0;
    std::map<uint32_t, uint32_t> given;
    const auto overridden = [&](SceneGraph& graph, std::string& why) { // each override a scene_set of the part of that name
        for (const Value& o : args["overrides"].items()) {
            const std::string part = o["name"].text("");
            uint32_t target = 0;
            eachObjectList(graph, [&](const auto& list) { // a part of this assembly, of any kind
                for (const auto& x : list)
                    if (!target && x.name == part && std::any_of(given.begin(), given.end(), [&](const auto& g) { return g.second == x.id; })) target = x.id;
            });
            if (!target) return refuse(why, "overrides: no part «" + part + "» in the assembly");
            Value set = Value::map(), answer = Value::map();
            set.set("id", int64_t(target)), set.set("line", o["line"]), set.set("key", o["key"]), set.set("values", o["values"]);
            if (!findSceneOp("scene_set")->edit(graph, set, answer, why)) return refuse(why, "overrides «" + part + "»: " + why);
        }
        return true;
    };
    if (!edit([&](SceneGraph& graph, std::string& why) { return (id = insertAssembly(graph, text, folder, at, name, why, &given)) != 0 && overridden(graph, why); }, error))
        return false;
    Value ids = Value::map();
    for (const auto& [was, now] : given) ids.set(std::to_string(was), int64_t(now));
    return answer.set("id", id), answer.set("name", name), answer.set("ids", ids), true;
}

} // namespace ke
