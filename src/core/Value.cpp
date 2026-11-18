// The JSON-shaped value of the API's arguments and answers (Value.h): a list's items, a map's keys in
// the order they were set, and a value as the word the scene file writes for it.
#include "core/Value.h"

#include "core/Format.h"
#include "math/Real.h"

#include <algorithm>

namespace ke {

Value& Value::push(Value v) {
    type_ = Type::List;
    items_.push_back(std::move(v));
    return items_.back();
}

bool Value::has(const std::string& key) const {
    const auto it = std::find(keys_.begin(), keys_.end(), key);
    return type_ == Type::Map && it != keys_.end() && !items_[size_t(it - keys_.begin())].isNull();
}

const Value& Value::operator[](const std::string& key) const {
    static const Value none;
    const auto it = std::find(keys_.begin(), keys_.end(), key);
    return type_ == Type::Map && it != keys_.end() ? items_[size_t(it - keys_.begin())] : none;
}

Value& Value::set(const std::string& key, Value v) {
    type_ = Type::Map;
    const auto it = std::find(keys_.begin(), keys_.end(), key);
    if (it != keys_.end()) return items_[size_t(it - keys_.begin())] = std::move(v);
    keys_.push_back(key);
    items_.push_back(std::move(v));
    return items_.back();
}

std::string fileWord(const Value& v) {
    switch (v.type()) {
    case Value::Type::Bool: return v.truth() ? "1" : "0";
    case Value::Type::Number: return numberText(Real(v.number()));
    case Value::Type::Text: return v.text();
    default: return std::string();
    }
}

} // namespace ke
