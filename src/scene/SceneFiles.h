#pragma once
// A scene's files: reading and writing one whole, and assemblies (сборки) - a part of a scene kept as a scene file of its own (a car, a gripper, a stand) and added to another
// scene in one step, as a game engine's prefab. A file's ids go up past the scene's by the format's own schema - every
// key whose form is an id, every <id> a line leads with (sceneSchema) - so whatever refers to whatever inside it keeps
// doing so. The operations on files (scene_open, scene_save, assembly_list, assembly_add, assembly_save) are the hosts':
// src/api/Scene.cpp and the window's EditorOps.
#include "scene/SceneGraph.h"

#include "core/Value.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ke {

// A scene file read whole (its models' folder its own) or nothing: false and `error` in words.
bool readSceneFile(const std::string& path, SceneGraph& out, std::string& error);
// Written whole or not at all - a failed save leaves the old file as it was -, the models' paths from its folder.
// `savedAs`: the absolute path, with forward slashes.
bool writeSceneFile(const SceneGraph& g, const std::string& path, std::string& savedAs, std::string& error);
// The text of a file (an assembly's), or false and `error`.
bool readTextFile(const std::string& path, std::string& text, std::string& error);
// The user's own folder of the program's data, as Qt's AppDataLocation has it for Kinetica: %APPDATA%/Kinetica/Kinetica,
// ~/Library/Application Support/Kinetica/Kinetica, $XDG_DATA_HOME or ~/.local/share/Kinetica/Kinetica.
std::string userDataFolder();

// A scene file's text with every object id in it moved by `by` (0 stays 0: none, the world).
std::string shiftedIds(const std::string& text, int64_t by);

// The objects `ids` with all under them and what belongs to them - the joints between them (or to the world), the
// sensors and drivers on those joints, the drivers' batteries, the controllers that drive only them - as a scene of
// their own, standing about the origin with their lowest point on the floor (y = 0).
SceneGraph assemblyOf(const SceneGraph& g, const std::vector<uint32_t>& ids);

// An assembly's text (a scene file's; its world lines are not taken) added to `g` under a new group named `name`
// standing at `at`: its models' paths read from `folder`. Answers the group's id; 0 and `error` when it does not read.
// `given`: each id of the text with the one it got.
uint32_t insertAssembly(SceneGraph& g, const std::string& text, const std::string& folder, const Vector3& at,
                        const std::string& name, std::string& error, std::map<uint32_t, uint32_t>* given = nullptr);

// The assemblies in these folders, in order (a name met again is the first one's): *.kscene, the file's stem its name.
struct AssemblyFile {
    std::string name, file;
};
std::vector<AssemblyFile> assemblyFiles(const std::vector<std::string>& folders);

// assembly_list, assembly_add, assembly_describe and assembly_save for either host, as their table has them (hostOps): the program's own
// assemblies in <dataDir>/assemblies, the user's in <userDataFolder()>/assemblies; `edit` makes a change of the graph
// one undo step. False and `error` when refused.
using AssemblyEdit = std::function<bool(const std::function<bool(SceneGraph&, std::string&)>&, std::string&)>;
bool assemblyCall(const std::string& op, const Value& args, const SceneGraph& g, const std::string& dataDir, const AssemblyEdit& edit,
                  Value& answer, std::string& error);

} // namespace ke
