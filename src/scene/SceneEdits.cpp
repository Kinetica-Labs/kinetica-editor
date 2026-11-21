// The core edits of a SceneGraph (SceneEdits.h): one object in and out as its block of the scene file.
#include "scene/Animation.h"
#include "scene/SceneEdits.h"

#include "scene/Assembly.h"

#include <algorithm>
#include <sstream>

namespace ke {

namespace {

template <class T>
bool copyOut(const std::vector<T>& from, uint32_t id, std::vector<T>& to) {
    for (const T& o : from)
        if (o.id == id) return to.push_back(o), true;
    return false;
}

template <class T>
bool hasId(const std::vector<T>& v, uint32_t id) {
    return id != 0 && std::any_of(v.begin(), v.end(), [id](const T& o) { return o.id == id; });
}

// Replaces the object of the same id in `into`, or appends it. False when another kind has that id.
template <class T>
uint32_t place(SceneGraph& g, std::vector<T>& into, T object, std::string& error) {
    const bool taken = findObject(g, object.id) || hasId(g.joints, object.id) || hasId(g.batteries, object.id) || hasId(g.drivers, object.id) ||
                       hasId(g.controllers, object.id);
    for (T& o : into)
        if (o.id == object.id && object.id != 0) return o = std::move(object), o.id;
    if (taken) {
        error = "id " + std::to_string(object.id) + " belongs to an object of another kind";
        return 0;
    }
    if (object.id == 0) object.id = nextId(g);
    into.push_back(std::move(object));
    return into.back().id;
}

template <class T>
bool eraseId(std::vector<T>& v, uint32_t id) {
    const size_t before = v.size();
    v.erase(std::remove_if(v.begin(), v.end(), [id](const T& o) { return o.id == id; }), v.end());
    return v.size() != before;
}

// A word of a line and where it stands in the line, its quotes included.
struct Word {
    std::string text;
    size_t begin, end;
    bool quoted;
};

std::vector<Word> spans(const std::string& line) {
    std::vector<Word> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
        if (i >= line.size()) break;
        const size_t start = i;
        if (line[i] == '"') {
            const size_t close = line.find('"', i + 1);
            i = close == std::string::npos ? line.size() : close + 1;
            out.push_back({line.substr(start + 1, i - start - 2), start, i, true});
        } else {
            while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') ++i;
            out.push_back({line.substr(start, i - start), start, i, false});
        }
    }
    return out;
}

std::string quoteIfNeeded(const std::string& v, bool quoted) {
    return quoted || v.empty() || v.find(' ') != std::string::npos ? "\"" + v + "\"" : v;
}

// Whether a line begins with the words of `head` ("rigid", or "drive rx": a line's first words).
bool startsWith(const std::vector<Word>& w, const std::vector<Word>& head) {
    if (head.empty() || w.size() < head.size()) return false;
    for (size_t i = 0; i < head.size(); ++i)
        if (w[i].text != head[i].text) return false;
    return true;
}

// The word after which a key's values stand: the key itself, found past the line's first `head` words (the line's
// size: it has none); the last of those words for an empty key. A key is looked for as it is named, then under its
// present name: the block is the scene as it is saved now, the caller may be a script written when the key was called
// otherwise (and a plugin's component spells its keys itself - one that looks like an earlier name is found as it is).
size_t keyAt(const std::vector<Word>& w, size_t head, const std::string& key) {
    if (key.empty()) return head - 1;
    for (const std::string& name : {key, presentKey(key)})
        for (size_t i = head; i < w.size(); ++i)
            if (!w[i].quoted && w[i].text == name) return i;
    return w.size();
}

// The name a key new to its line is written under: the format's present one; a plugin's component keeps its own.
std::string writtenKey(const std::vector<Word>& head, const std::string& key) {
    return !head.empty() && head[0].text == "component" ? key : presentKey(key);
}

// The line with as many words after word `at` as there are values replaced by them (appended past its end).
std::string replaceWords(const std::string& line, const std::vector<Word>& w, size_t at, const std::vector<std::string>& values) {
    if (values.empty()) return line;
    std::string text;
    for (size_t k = 0; k < values.size(); ++k) {
        const size_t i = at + 1 + k;
        text += (k ? " " : "") + quoteIfNeeded(values[k], i < w.size() && w[i].quoted);
    }
    const size_t first = at + 1, last = std::min(w.size(), at + 1 + values.size());
    if (first >= w.size()) return line + (values.empty() ? "" : " " + text);
    return line.substr(0, w[first].begin) + text + line.substr(w[last - 1].end);
}

// The values after `key` on the first of the lines that starts with the words of `line` replaced (the key appended
// when the line has none). False: no line starts so.
bool replaceField(std::vector<std::string>& lines, const std::string& line, const std::string& key, const std::vector<std::string>& values) {
    const std::vector<Word> head = spans(line);
    for (std::string& l : lines) {
        const std::vector<Word> w = spans(l);
        if (!startsWith(w, head)) continue;
        const size_t at = keyAt(w, head.size(), key);
        if (at == w.size()) l += " " + writtenKey(head, key); // a key the line has not got: appended
        l = replaceWords(l, w, at, values);
        return true;
    }
    return false;
}

} // namespace

std::string getObject(const SceneGraph& g, uint32_t id) {
    if (id == 0) return std::string();
    SceneGraph one;
    if (!copyOut(g.entities, id, one.entities) && !copyOut(g.groups, id, one.groups) && !copyOut(g.arrays, id, one.arrays) &&
        !copyOut(g.lights, id, one.lights) && !copyOut(g.cameras, id, one.cameras) && !copyOut(g.joints, id, one.joints) &&
        !copyOut(g.batteries, id, one.batteries) && !copyOut(g.drivers, id, one.drivers) && !copyOut(g.sensors, id, one.sensors) &&
        !copyOut(g.wheels, id, one.wheels) && !copyOut(g.controllers, id, one.controllers) && !copyOut(g.sequences, id, one.sequences))
        return std::string();
    const std::string text = one.save();
    const size_t world = text.find("\nworld ");
    const size_t body = text.find('\n', world + 1);
    return text.substr(body + 1); // past the header and the world line
}

uint32_t putObject(SceneGraph& g, const std::string& block, std::string& error) {
    SceneGraph one;
    if (!one.load(block, error)) return 0;
    const size_t n = one.entities.size() + one.groups.size() + one.arrays.size() + one.lights.size() + one.cameras.size() + one.joints.size() +
                     one.batteries.size() + one.drivers.size() + one.sensors.size() + one.wheels.size() + one.controllers.size() + one.sequences.size();
    if (n != 1) {
        error = "the text holds " + std::to_string(n) + " objects, not one";
        return 0;
    }
    if (!one.entities.empty()) return place(g, g.entities, one.entities[0], error);
    if (!one.groups.empty()) return place(g, g.groups, one.groups[0], error);
    if (!one.arrays.empty()) return place(g, g.arrays, one.arrays[0], error);
    if (!one.lights.empty()) return place(g, g.lights, one.lights[0], error);
    if (!one.cameras.empty()) return place(g, g.cameras, one.cameras[0], error);
    if (!one.joints.empty()) return place(g, g.joints, one.joints[0], error);
    if (!one.batteries.empty()) return place(g, g.batteries, one.batteries[0], error);
    if (!one.drivers.empty()) return place(g, g.drivers, one.drivers[0], error);
    if (!one.wheels.empty()) return place(g, g.wheels, one.wheels[0], error);
    if (!one.controllers.empty()) return place(g, g.controllers, one.controllers[0], error);
    if (!one.sequences.empty()) { // an active one plays alone
        const uint32_t id = place(g, g.sequences, one.sequences[0], error);
        if (id && one.sequences[0].active) makeActive(g, id);
        return id;
    }
    return place(g, g.sensors, one.sensors[0], error);
}

uint32_t setField(SceneGraph& g, uint32_t id, const std::string& line, const std::string& key,
                  const std::vector<std::string>& values, std::string& error) {
    std::istringstream in(getObject(g, id));
    if (in.str().empty()) {
        error = "no object " + std::to_string(id);
        return 0;
    }
    std::vector<std::string> lines;
    for (std::string l; std::getline(in, l);) lines.push_back(l);
    if (!replaceField(lines, line, key, values)) {
        std::string added = "  " + line + (key.empty() ? "" : " " + writtenKey(spans(line), key));
        for (const std::string& v : values) added += " " + quoteIfNeeded(v, false);
        lines.insert(lines.end() - 1, added); // before "end"
    }
    std::string block;
    for (const std::string& l : lines) block += l + "\n";
    return putObject(g, block, error);
}

std::vector<std::vector<BlockWord>> blockLines(const std::string& block) {
    std::vector<std::vector<BlockWord>> out;
    std::istringstream in(block);
    for (std::string l; std::getline(in, l);) {
        std::vector<BlockWord> words;
        for (const Word& w : spans(l)) words.push_back({w.text, w.quoted});
        if (!words.empty() && !(words.size() == 1 && words[0].text == "end")) out.push_back(std::move(words));
    }
    return out;
}

std::vector<BlockWord> getField(const std::string& block, const std::string& line, const std::string& key, size_t count) {
    const std::vector<Word> head = spans(line);
    std::istringstream in(block);
    for (std::string l; std::getline(in, l);) {
        const std::vector<Word> w = spans(l);
        if (!startsWith(w, head)) continue;
        const size_t at = keyAt(w, head.size(), key);
        std::vector<BlockWord> out;
        for (size_t i = at + 1; i < w.size() && out.size() < count; ++i) out.push_back({w[i].text, w[i].quoted});
        return out.size() == count ? out : std::vector<BlockWord>();
    }
    return {};
}

// The sensors and point wheels riding on what is removed - an entity or a joint, the joints of an
// entity included, a wheel - go with it: they mean nothing without their parent (as a component
// without its actor); the sensors of a wheel that goes go too.
static void removeSensorsOn(SceneGraph& g, uint32_t id) {
    std::vector<uint32_t> parents{id};
    for (const JointObject& j : g.joints)
        if (j.a == id || (j.b == id && id != 0)) parents.push_back(j.id);
    for (const WheelObject& w : g.wheels)
        if (w.parent == id) parents.push_back(w.id);
    auto riding = [&](const SceneObject& s) { return std::find(parents.begin(), parents.end(), s.parent) != parents.end(); };
    g.sensors.erase(std::remove_if(g.sensors.begin(), g.sensors.end(), riding), g.sensors.end());
    g.wheels.erase(std::remove_if(g.wheels.begin(), g.wheels.end(), riding), g.wheels.end());
}

bool removeObject(SceneGraph& g, uint32_t id) {
    removeSensorsOn(g, id);
    const SceneObject* gone = findObject(g, id);
    if (!gone) return eraseId(g.joints, id) || eraseId(g.batteries, id) || eraseId(g.drivers, id) || eraseId(g.controllers, id);
    const uint32_t parent = gone->parent;
    struct Child { uint32_t id; Vector3 p; Quaternion q; };
    std::vector<Child> children;
    auto collect = [&](const auto& objects) {
        for (const auto& o : objects)
            if (o.parent == id) {
                Child c{o.id, Vector3(0.0_r), Quaternion()};
                worldPose(g, o, c.p, c.q);
                children.push_back(c);
            }
    };
    collect(g.entities), collect(g.groups), collect(g.arrays), collect(g.lights), collect(g.cameras);
    removeJointsOf(g, id);
    eraseId(g.entities, id) || eraseId(g.groups, id) || eraseId(g.arrays, id) || eraseId(g.lights, id) || eraseId(g.cameras, id) ||
        eraseId(g.sensors, id) || eraseId(g.wheels, id) || eraseId(g.sequences, id);
    for (const Child& c : children) {
        SceneObject* o = const_cast<SceneObject*>(findObject(g, c.id));
        o->parent = parent;
        setWorldPose(g, *o, c.p, c.q);
    }
    return true;
}

std::string getWorld(const SceneGraph& g) { return worldText(g.world); }

bool putWorld(SceneGraph& g, const std::string& text, std::string& error) {
    SceneGraph one;
    if (!one.load(text, error)) return false;
    if (!one.entities.empty() || !one.groups.empty() || !one.arrays.empty() || !one.lights.empty() || !one.cameras.empty() || !one.joints.empty() ||
        !one.batteries.empty() || !one.drivers.empty() || !one.sensors.empty() || !one.wheels.empty() || !one.controllers.empty()) {
        error = "the text holds objects, not only the world's lines";
        return false;
    }
    g.world = one.world;
    return true;
}

bool setWorldField(SceneGraph& g, const std::string& line, const std::string& key, const std::vector<std::string>& values,
                   std::string& error) {
    std::istringstream in(getWorld(g));
    std::vector<std::string> lines;
    for (std::string l; std::getline(in, l);) lines.push_back(l);
    if (!replaceField(lines, line, key, values)) {
        error = "the world has no line '" + line + "' (world, physics)";
        return false;
    }
    std::string text;
    for (const std::string& l : lines) text += l + "\n";
    return putWorld(g, text, error);
}

// ---------------------------------------------------------------------------
// A change as edits (diffEdits)
// ---------------------------------------------------------------------------
namespace {

// Every object's id, kind by kind in the graph's own order.
std::vector<uint32_t> objectIds(const SceneGraph& g) {
    std::vector<uint32_t> ids;
    auto add = [&ids](const auto& objects) {
        for (const auto& o : objects) ids.push_back(o.id);
    };
    eachObjectList(g, add);
    return ids;
}

bool isNumber(const Word& w) {
    if (w.quoted || w.text.empty()) return false;
    bool digit = false;
    for (char c : w.text) {
        if (c >= '0' && c <= '9') digit = true;
        else if (c != '.' && c != '-' && c != '+' && c != 'e' && c != 'E') return false;
    }
    return digit;
}

// The first words that name the line among the block's lines: one, or two when another line starts with
// the same word (a joint's "drive rx" and "drive tz").
size_t headOf(const std::vector<std::vector<Word>>& lines, size_t at) {
    const std::vector<Word>& w = lines[at];
    for (size_t k = 0; k < lines.size(); ++k)
        if (k != at && !lines[k].empty() && lines[k][0].text == w[0].text) return std::min<size_t>(2, w.size());
    return 1;
}

// One changed line as a Set: the words from the first that differs to the last, after the nearest key
// before them that the line holds once (none: right after the line's own words). False: the line's
// shape changed - its count of words, its quotes or its own words - and only a Put says it.
bool lineEdit(uint32_t id, const std::vector<std::vector<Word>>& lines, size_t at, const std::vector<Word>& now, GraphEdit& out) {
    const std::vector<Word>& was = lines[at];
    if (was.size() != now.size()) return false;
    size_t first = was.size(), last = 0;
    for (size_t i = 0; i < was.size(); ++i)
        if (was[i].text != now[i].text || was[i].quoted != now[i].quoted) first = std::min(first, i), last = i;
    const size_t head = headOf(lines, at);
    if (first < head) return false;
    size_t key = head - 1;
    for (size_t k = first; k-- > head;) {
        if (was[k].quoted || isNumber(was[k])) continue;
        size_t seen = 0;
        for (size_t i = head; i < was.size(); ++i) seen += !was[i].quoted && was[i].text == was[k].text;
        if (seen == 1) key = k;
        break;
    }
    out = GraphEdit{GraphEdit::Kind::Set, id, std::string(), std::string(), std::string(), {}};
    for (size_t i = 0; i < head; ++i) out.line += (i ? " " : "") + was[i].text;
    if (key >= head) out.key = was[key].text;
    for (size_t i = key + 1; i <= last; ++i) out.values.push_back(now[i].text);
    return true;
}

std::vector<std::vector<Word>> blockLines(const std::string& block) {
    std::vector<std::vector<Word>> out;
    std::istringstream in(block);
    for (std::string l; std::getline(in, l);) out.push_back(spans(l));
    return out;
}

// One object from block `was` to block `now`: a Set per changed line, or one Put.
void objectEdits(uint32_t id, const std::string& was, const std::string& now, std::vector<GraphEdit>& out) {
    const std::vector<std::vector<Word>> a = blockLines(was), b = blockLines(now);
    std::vector<GraphEdit> sets;
    bool whole = a.size() != b.size();
    for (size_t i = 0; i < a.size() && !whole; ++i) {
        if (a[i].empty() || b[i].empty() || a[i][0].text != b[i][0].text) whole = true;
        else if (a[i].size() == b[i].size() && std::equal(a[i].begin(), a[i].end(), b[i].begin(), [](const Word& x, const Word& y) {
                     return x.text == y.text && x.quoted == y.quoted;
                 }))
            continue;
        else if (GraphEdit e; lineEdit(id, a, i, b[i], e)) sets.push_back(e);
        else whole = true;
    }
    if (whole) out.push_back(GraphEdit{GraphEdit::Kind::Put, id, now, std::string(), std::string(), {}});
    else out.insert(out.end(), sets.begin(), sets.end());
}

} // namespace

std::vector<GraphEdit> diffEdits(const SceneGraph& before, const SceneGraph& after) {
    std::vector<GraphEdit> out;
    if (getWorld(before) != getWorld(after)) out.push_back(GraphEdit{GraphEdit::Kind::World, 0, getWorld(after), std::string(), std::string(), {}});
    SceneGraph now = before; // what is gone goes first, with what goes with it; the rest is compared to that
    for (uint32_t id : objectIds(before))
        if (getObject(after, id).empty() && !getObject(now, id).empty()) {
            removeObject(now, id);
            out.push_back(GraphEdit{GraphEdit::Kind::Remove, id, std::string(), std::string(), std::string(), {}});
        }
    for (uint32_t id : objectIds(after)) {
        const std::string was = getObject(now, id), is = getObject(after, id);
        if (was.empty()) out.push_back(GraphEdit{GraphEdit::Kind::Put, id, is, std::string(), std::string(), {}});
        else if (was != is) objectEdits(id, was, is, out);
    }
    return out;
}

} // namespace ke
