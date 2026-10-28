// JSON-file persistence, same files and same defaults as server/store.js so an
// existing library keeps its progress and My List. Nothing is read from or written
// to the 1.2.0 data directory: data_dir() is namespaced to this product.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "core/json.h"
#include "core/model.h"

namespace vf {

class Store {
 public:
  explicit Store(std::string data_dir);
  explicit Store() : Store(std::string()) {}

  Settings load_settings() const;
  // Applies a patch object and writes it. Unknown keys are ignored; a key with the
  // wrong type is rejected with a reason (bug A6 from the audit: vlcPassword used
  // to accept any JSON type).
  bool patch_settings(const Json& patch, Settings* out, std::string* error);

  Catalog load_catalog() const;
  bool save_catalog(const Catalog& catalog) const;

  std::map<std::string, Progress> load_progress() const;
  bool save_progress(const std::map<std::string, Progress>& progress) const;
  // Records a resume point. Below 2% is "not started" and above 92% is "finished";
  // either way the entry is removed, so a finished film leaves Continue Watching
  // (bug A4 in the audit: the JS kept it forever).
  bool set_progress(const std::string& id, double position, double duration);

  std::vector<std::string> load_my_list() const;
  bool save_my_list(const std::vector<std::string>& list) const;
  bool toggle_my_list(const std::string& id, std::vector<std::string>* out) const;

  std::map<std::string, std::string> load_subtitle_choices() const;
  bool set_subtitle_choice(const std::string& id, const std::string& name) const;  // "" clears

  const std::string& dir() const { return dir_; }
  std::string file_for(const std::string& name) const;

 private:
  std::string dir_;
};

}  // namespace vf
