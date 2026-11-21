// serve(): the line framer of the windowless host. The envelope is in Wire.h; the operations are Scene's.
#include "api/Wire.h"

#include "core/Json.h"

#include <istream>
#include <ostream>

namespace ke {

namespace {

Value notJson(const std::string& why) {
    Value answer = Value::map();
    answer.set("ok", false);
    answer.set("error", why);
    Value reply = Value::map();
    reply.set("result", answer);
    return reply;
}

Value reply(const std::string& line, Scene& scene) {
    Value request;
    std::string why;
    if (!parseJson(line, request, why)) return notJson(why);
    const bool list = request.type() == Value::Type::List;
    if (!list && request.type() != Value::Type::Map) return notJson("не JSON: ожидался объект или список (знак 1)");
    Value args = request["args"];
    if (args.type() != Value::Type::Map) args = Value::map();
    Value out = Value::map();
    if (request.contains("id")) out.set("id", request["id"]);
    out.set("result", scene.call(list ? "" : request["op"].text(""), args));
    return out;
}

} // namespace

void serve(std::istream& in, std::ostream& out, Scene& scene) {
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        out << toJson(reply(line, scene)) << '\n' << std::flush;
    }
}

} // namespace ke
