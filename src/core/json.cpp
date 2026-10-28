#include "core/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace vf {

Json Json::array() {
  Json j;
  j.type_ = Type::Array;
  return j;
}

Json Json::object() {
  Json j;
  j.type_ = Type::Object;
  return j;
}

long long Json::asInt(long long def) const {
  if (type_ != Type::Number) return def;
  return static_cast<long long>(std::llround(num_));
}

std::size_t Json::size() const {
  if (type_ == Type::Array) return items_.size();
  if (type_ == Type::Object) return members_.size();
  return 0;
}

Json& Json::push(Json v) {
  if (type_ != Type::Array) {
    type_ = Type::Array;
    items_.clear();
  }
  items_.push_back(std::move(v));
  return items_.back();
}

const Json& Json::at(std::size_t i) const {
  static const Json kNull;
  if (type_ != Type::Array || i >= items_.size()) return kNull;
  return items_[i];
}

const Json* Json::find(const std::string& key) const {
  if (type_ != Type::Object) return nullptr;
  for (const auto& kv : members_) {
    if (kv.first == key) return &kv.second;
  }
  return nullptr;
}

Json& Json::set(const std::string& key, Json v) {
  if (type_ != Type::Object) {
    type_ = Type::Object;
    members_.clear();
  }
  for (auto& kv : members_) {
    if (kv.first == key) {
      kv.second = std::move(v);
      return kv.second;
    }
  }
  members_.emplace_back(key, std::move(v));
  return members_.back().second;
}

void Json::erase(const std::string& key) {
  for (std::size_t i = 0; i < members_.size(); ++i) {
    if (members_[i].first == key) {
      members_.erase(members_.begin() + static_cast<long>(i));
      return;
    }
  }
}

std::string Json::str(const std::string& key, const std::string& def) const {
  const Json* v = find(key);
  return v ? v->asString(def) : def;
}

double Json::num(const std::string& key, double def) const {
  const Json* v = find(key);
  return v ? v->asNumber(def) : def;
}

long long Json::integer(const std::string& key, long long def) const {
  const Json* v = find(key);
  return v ? v->asInt(def) : def;
}

bool Json::flag(const std::string& key, bool def) const {
  const Json* v = find(key);
  return v ? v->asBool(def) : def;
}

/* ------------------------------- writing -------------------------------- */

static void escapeInto(std::string& out, const std::string& s) {
  out.push_back('"');
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  out.push_back('"');
}

static void numberInto(std::string& out, double v) {
  if (!std::isfinite(v)) {
    out += "0";  // JSON has no Infinity/NaN; never write invalid JSON
    return;
  }
  if (v == static_cast<double>(static_cast<long long>(v)) && std::fabs(v) < 9.0e15) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(v));
    out += buf;
    return;
  }
  char buf[40];
  std::snprintf(buf, sizeof buf, "%.17g", v);
  out += buf;
}

void Json::dumpTo(std::string& out, int indent, int depth) const {
  const bool pretty = indent > 0;
  auto newlineIndent = [&](int d) {
    if (!pretty) return;
    out.push_back('\n');
    out.append(static_cast<std::size_t>(indent * d), ' ');
  };

  switch (type_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += bool_ ? "true" : "false"; break;
    case Type::Number: numberInto(out, num_); break;
    case Type::String: escapeInto(out, str_); break;
    case Type::Array:
      if (items_.empty()) {
        out += "[]";
        break;
      }
      out.push_back('[');
      for (std::size_t i = 0; i < items_.size(); ++i) {
        if (i) out.push_back(',');
        newlineIndent(depth + 1);
        items_[i].dumpTo(out, indent, depth + 1);
      }
      newlineIndent(depth);
      out.push_back(']');
      break;
    case Type::Object:
      if (members_.empty()) {
        out += "{}";
        break;
      }
      out.push_back('{');
      for (std::size_t i = 0; i < members_.size(); ++i) {
        if (i) out.push_back(',');
        newlineIndent(depth + 1);
        escapeInto(out, members_[i].first);
        out += pretty ? ": " : ":";
        members_[i].second.dumpTo(out, indent, depth + 1);
      }
      newlineIndent(depth);
      out.push_back('}');
      break;
  }
}

std::string Json::dump(int indent) const {
  std::string out;
  out.reserve(256);
  dumpTo(out, indent, 0);
  return out;
}

/* ------------------------------- reading -------------------------------- */

namespace {

struct Parser {
  const std::string& s;
  std::size_t i = 0;
  std::string error;

  explicit Parser(const std::string& text) : s(text) {}

  void skipWs() {
    while (i < s.size()) {
      const char c = s[i];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++i;
      } else {
        break;
      }
    }
  }

  bool fail(const char* what) {
    if (error.empty()) error = std::string(what) + " at byte " + std::to_string(i);
    return false;
  }

  bool parseValue(Json& out) {
    skipWs();
    if (i >= s.size()) return fail("unexpected end of input");
    const char c = s[i];
    if (c == '{') return parseObject(out);
    if (c == '[') return parseArray(out);
    if (c == '"') {
      std::string v;
      if (!parseString(v)) return false;
      out = Json(std::move(v));
      return true;
    }
    if (c == 't') return literal("true", Json(true), out);
    if (c == 'f') return literal("false", Json(false), out);
    if (c == 'n') return literal("null", Json(), out);
    if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(out);
    return fail("unexpected character");
  }

  bool literal(const char* text, Json value, Json& out) {
    const std::size_t n = std::char_traits<char>::length(text);
    if (s.compare(i, n, text) != 0) return fail("bad literal");
    i += n;
    out = std::move(value);
    return true;
  }

  bool parseNumber(Json& out) {
    const std::size_t start = i;
    if (i < s.size() && s[i] == '-') ++i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
    if (i < s.size() && s[i] == '.') {
      ++i;
      while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
    }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
      ++i;
      if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
      while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
    }
    if (i == start) return fail("bad number");
    out = Json(std::strtod(s.substr(start, i - start).c_str(), nullptr));
    return true;
  }

  bool parseString(std::string& out) {
    if (i >= s.size() || s[i] != '"') return fail("expected string");
    ++i;
    out.clear();
    while (i < s.size()) {
      const char c = s[i++];
      if (c == '"') return true;
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (i >= s.size()) return fail("bad escape");
      const char e = s[i++];
      switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          if (i + 4 > s.size()) return fail("bad \\u escape");
          unsigned code = 0;
          for (int k = 0; k < 4; ++k) {
            const char h = s[i++];
            code <<= 4;
            if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
            else return fail("bad hex digit");
          }
          if (code < 0x80) {
            out.push_back(static_cast<char>(code));
          } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          }
          break;
        }
        default: return fail("unknown escape");
      }
    }
    return fail("unterminated string");
  }

  bool parseArray(Json& out) {
    ++i;
    out = Json::array();
    skipWs();
    if (i < s.size() && s[i] == ']') {
      ++i;
      return true;
    }
    for (;;) {
      Json v;
      if (!parseValue(v)) return false;
      out.push(std::move(v));
      skipWs();
      if (i >= s.size()) return fail("unterminated array");
      if (s[i] == ',') {
        ++i;
        continue;
      }
      if (s[i] == ']') {
        ++i;
        return true;
      }
      return fail("expected , or ]");
    }
  }

  bool parseObject(Json& out) {
    ++i;
    out = Json::object();
    skipWs();
    if (i < s.size() && s[i] == '}') {
      ++i;
      return true;
    }
    for (;;) {
      skipWs();
      std::string key;
      if (!parseString(key)) return false;
      skipWs();
      if (i >= s.size() || s[i] != ':') return fail("expected :");
      ++i;
      Json v;
      if (!parseValue(v)) return false;
      out.set(key, std::move(v));
      skipWs();
      if (i >= s.size()) return fail("unterminated object");
      if (s[i] == ',') {
        ++i;
        continue;
      }
      if (s[i] == '}') {
        ++i;
        return true;
      }
      return fail("expected , or }");
    }
  }
};

}  // namespace

bool Json::parse(const std::string& text, Json& out, std::string* error) {
  Parser p(text);
  if (!p.parseValue(out)) {
    if (error) *error = p.error;
    return false;
  }
  p.skipWs();
  if (p.i != text.size()) {
    if (error) *error = "trailing data at byte " + std::to_string(p.i);
    return false;
  }
  return true;
}

Json Json::parseOr(const std::string& text, Json fallback) {
  Json out;
  if (!parse(text, out, nullptr)) return fallback;
  return out;
}

}  // namespace vf
