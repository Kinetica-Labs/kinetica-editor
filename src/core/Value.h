#pragma once
// A value shaped as JSON - nothing, a truth, a number, a text, a list, a map with its keys in the order
// they came - for the arguments and answers of the API's operations (scene/SceneOps.h). It is only the
// tree: each client turns its own values into it and back (the editor's QJsonValue, Python's objects),
// so the core needs no JSON library. A number remembers whether it was a whole one, so an id goes back
// to Python as an int.
#include <cstdint>
#include <string>
#include <vector>

namespace ke {

class Value {
public:
    enum class Type { Null, Bool, Number, Text, List, Map };

    Value() = default;
    Value(bool b) : type_(Type::Bool), truth_(b) {}
    Value(int n) : type_(Type::Number), number_(double(n)), whole_(true) {}
    Value(uint32_t n) : type_(Type::Number), number_(double(n)), whole_(true) {}
    Value(int64_t n) : type_(Type::Number), number_(double(n)), whole_(true) {}
    Value(double n) : type_(Type::Number), number_(n) {}
    Value(const char* s) : type_(Type::Text), text_(s) {}
    Value(std::string s) : type_(Type::Text), text_(std::move(s)) {}
    static Value list() { Value v; v.type_ = Type::List; return v; }
    static Value map() { Value v; v.type_ = Type::Map; return v; }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool whole() const { return whole_; } // a number that came as an integer

    // The value as one kind, or `otherwise` when it is not that kind.
    bool truth(bool otherwise = false) const { return type_ == Type::Bool ? truth_ : otherwise; }
    double number(double otherwise = 0.0) const { return type_ == Type::Number ? number_ : otherwise; }
    int integer(int otherwise = 0) const { return type_ == Type::Number ? int(number_) : otherwise; }
    const std::string& text() const { return text_; }
    std::string text(const std::string& otherwise) const { return type_ == Type::Text ? text_ : otherwise; }

    // A list: its items; push makes the value a list.
    const std::vector<Value>& items() const { return items_; }
    Value& push(Value v);
    size_t size() const { return type_ == Type::Map ? keys_.size() : items_.size(); }

    // A map: a key present (and not null), its value (null when missing), set or replace one.
    bool has(const std::string& key) const;
    const Value& operator[](const std::string& key) const;
    Value& set(const std::string& key, Value v);
    const std::vector<std::string>& keys() const { return keys_; }
    const Value& at(size_t k) const { return items_[k]; } // a map's k-th value, in key order

private:
    Type type_ = Type::Null;
    bool truth_ = false;
    double number_ = 0.0;
    bool whole_ = false;
    std::string text_;
    std::vector<Value> items_;      // a list's items, or a map's values
    std::vector<std::string> keys_; // a map's keys, in the order they were set
};

// A value as the scene file's word: a number as the file writes it (numberText), a truth as 1 or 0, a
// text as it is - what `scene_set`'s values become.
std::string fileWord(const Value& v);

} // namespace ke
