// Minimal JSON reader/writer. The data files are ours (settings, catalog,
// progress, My List, subtitle choices), so a dependency would buy nothing.
#pragma once
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace vf {

class Json {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Json() = default;
  Json(bool v) : type_(Type::Bool), bool_(v) {}
  Json(double v) : type_(Type::Number), num_(v) {}
  Json(int v) : type_(Type::Number), num_(static_cast<double>(v)) {}
  Json(long long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
  Json(const char* v) : type_(Type::String), str_(v ? v : "") {}
  Json(std::string v) : type_(Type::String), str_(std::move(v)) {}

  static Json array();
  static Json object();

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::Null; }
  bool isBool() const { return type_ == Type::Bool; }
  bool isNumber() const { return type_ == Type::Number; }
  bool isString() const { return type_ == Type::String; }
  bool isArray() const { return type_ == Type::Array; }
  bool isObject() const { return type_ == Type::Object; }

  bool asBool(bool def = false) const { return type_ == Type::Bool ? bool_ : def; }
  double asNumber(double def = 0) const { return type_ == Type::Number ? num_ : def; }
  long long asInt(long long def = 0) const;
  std::string asString(const std::string& def = "") const {
    return type_ == Type::String ? str_ : def;
  }

  std::size_t size() const;
  const std::vector<Json>& items() const { return items_; }
  Json& push(Json v);
  const Json& at(std::size_t i) const;

  const Json* find(const std::string& key) const;
  bool has(const std::string& key) const { return find(key) != nullptr; }
  Json& set(const std::string& key, Json v);
  void erase(const std::string& key);
  const std::vector<std::pair<std::string, Json>>& members() const { return members_; }
  std::vector<std::pair<std::string, Json>>& members() { return members_; }

  std::string str(const std::string& key, const std::string& def = "") const;
  double num(const std::string& key, double def = 0) const;
  long long integer(const std::string& key, long long def = 0) const;
  bool flag(const std::string& key, bool def = false) const;

  std::string dump(int indent = 2) const;
  static bool parse(const std::string& text, Json& out, std::string* error);
  static Json parseOr(const std::string& text, Json fallback);

 private:
  void dumpTo(std::string& out, int indent, int depth) const;

  Type type_ = Type::Null;
  bool bool_ = false;
  double num_ = 0;
  std::string str_;
  std::vector<Json> items_;
  std::vector<std::pair<std::string, Json>> members_;
};

}  // namespace vf
