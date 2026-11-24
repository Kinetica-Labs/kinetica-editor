// kinetica-core: the windowless host. It speaks the wire of api/Wire.h on stdin and stdout - one JSON request a line,
// one answer a line - over a ke::Scene, so a program with no window (Python's ke.Scene(), a script, a pipe) runs the
// editor's operations. stdout carries the wire and nothing else; every line on stderr begins "kinetica-core: ".
// Exit codes: 0 end of input, --version, --help; 1 an exception that escaped; 2 usage, or no data folder.
#include "api/Wire.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

const char* usage = "использование: kinetica-core [--data-dir DIR]   строки JSON на stdin, ответы на stdout\n"
                    "       kinetica-core --version | --help\n"
                    "в DIR лежит sensors/ (без него ошибка); без --data-dir программа ищет рядом с собой, затем в дереве исходников\n";

bool hasSensors(const fs::path& dir) {
    std::error_code ec;
    return fs::is_directory(dir / "sensors", ec);
}

} // namespace

int main(int argc, char** argv) {
    std::string dataDir;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--version") return std::cout << "kinetica-core " << KE_VERSION << "\n", 0;
        if (arg == "--help") return std::cout << usage, 0;
        if (arg == "--data-dir" && i + 1 < argc) dataDir = argv[++i];
        else return std::cerr << "kinetica-core: " << (arg == "--data-dir" ? "--data-dir без значения" : "неизвестный ключ " + arg) << "\n" << usage, 2;
    }

    if (!dataDir.empty() && !hasSensors(fs::u8path(dataDir)))
        return std::cerr << "kinetica-core: нет каталога sensors в «" << dataDir << "» (--data-dir)\n", 2;
    std::vector<fs::path> places;
    if (!dataDir.empty()) places.push_back(fs::u8path(dataDir));
    // the real file of the program: argv[0] may be a link or a bare name found through PATH
    std::error_code ec;
    fs::path self = fs::canonical("/proc/self/exe", ec);
    if (ec) self = fs::canonical(fs::u8path(argv[0]), ec);
    if (!ec) places.push_back(self.parent_path() / "data"), places.push_back(self.parent_path().parent_path() / "data");
    places.push_back((fs::u8path(KE_SOURCE_DIR) / "data").lexically_normal());
    std::string searched;
    dataDir.clear();
    for (const fs::path& place : places) {
        if (hasSensors(place)) {
            dataDir = place.u8string();
            break;
        }
        searched += (searched.empty() ? "" : ", ") + place.u8string();
    }
    if (dataDir.empty())
        return std::cerr << "kinetica-core: нет каталога data/sensors (искали: " << searched << "); укажите --data-dir\n", 2;

    try {
        ke::Scene scene(dataDir);
        std::ios::sync_with_stdio(false);
        ke::serve(std::cin, std::cout, scene);
    } catch (const std::exception& e) {
        return std::cerr << "kinetica-core: " << e.what() << "\n", 1;
    }
    return 0;
}
