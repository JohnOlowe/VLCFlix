#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/brand.h"
#include "core/catalog.h"
#include "core/json.h"
#include "core/log.h"
#include "core/model.h"
#include "core/naming.h"
#include "core/paths.h"
#include "core/store.h"
#include "core/sync.h"
#include "library/artwork.h"
#include "library/probe.h"
#include "library/scan.h"
#include "library/walk.h"

using namespace vf;

namespace {

struct Args {
  std::string command = "help";
  std::string log_path;
  std::string data_dir;
  std::string folders;  // comma-separated
  std::string key_value;
  bool console = false;
  bool no_art = false;
  bool json = false;
};

Args parse_args(int argc, char** argv) {
  Args args;
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto value = [&](const char* name) -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "%s needs a value\n", name);
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--log") args.log_path = value("--log");
    else if (a == "--data") args.data_dir = value("--data");
    else if (a == "--folders") args.folders = value("--folders");
    else if (a == "--set") args.key_value = value("--set");
    else if (a == "--console") args.console = true;
    else if (a == "--no-art") args.no_art = true;
    else if (a == "--json") args.json = true;
    else if (a.rfind("--", 0) == 0) positional.push_back(a.substr(2));
    else positional.push_back(a);
  }
  if (!positional.empty()) args.command = positional.front();
  return args;
}

std::string human_duration(double seconds) {
  if (!(seconds > 0)) return "--:--";
  const int total = static_cast<int>(seconds + 0.5);
  char buf[32];
  std::snprintf(buf, sizeof buf, "%d:%02d", total / 60, total % 60);
  return buf;
}

std::vector<std::string> split_commas(const std::string& text) {
  std::vector<std::string> out;
  std::string current;
  for (char c : text) {
    if (c == ',' || c == ';') {
      if (!current.empty()) out.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) out.push_back(current);
  return out;
}

int command_scan(const Args& args) {
  Store store;
  Settings settings = store.load_settings();
  std::vector<std::string> folders =
      args.folders.empty() ? settings.library_folders : split_commas(args.folders);
  if (folders.empty()) {
    LOG_ERROR("no library folders configured; pass --folders <dir> or set them first");
    std::fprintf(stderr, "no library folders configured — pass --folders <dir>\n");
    return 3;
  }

  const std::string media = media_dir();
  ensure_dir(media);
  const int swept = sweep_temp_art(media);
  if (swept) LOG_INFO("cleared %d half-written artwork file(s) from an interrupted run", swept);

  Catalog previous = store.load_catalog();
  ScanOptions options;
  options.folders = folders;
  options.media_dir = media;
  options.previous = previous;
  options.render_artwork = !args.no_art;
  options.on_progress = [](const ScanProgress& p) {
    LOG_INFO("scan %s %d/%d %s", p.phase.c_str(), p.done, p.total, p.current.c_str());
  };
  options.on_catalog = [&](const Catalog& catalog) { store.save_catalog(catalog); };

  const Catalog catalog = scan_library(options);
  store.save_catalog(catalog);

  std::printf("\n%-28s %6s %8s %11s  %s\n", "TITLE", "YEAR", "LENGTH", "RESOLUTION", "ARTWORK");
  std::printf("%s\n", std::string(74, '-').c_str());
  for (const auto& item : catalog.items) {
    char resolution[32] = "-";
    if (item.width && item.height) {
      std::snprintf(resolution, sizeof resolution, "%dx%d", item.width, item.height);
    }
    const std::string art = (item.poster.empty() ? "-" : "poster") + std::string(" ") +
                            (item.backdrop.empty() ? "-" : "backdrop") + " " +
                            (item.preview.empty() ? "-" : "preview");
    std::printf("%-28s %6s %8s %11s  %s\n", item.title.c_str(),
                item.year ? std::to_string(item.year).c_str() : "-", human_duration(item.duration).c_str(),
                resolution, art.c_str());
  }
  std::printf("%s\n%zu item(s), %zu error(s), catalog at %s\n", std::string(74, '-').c_str(),
              catalog.items.size(), catalog.errors.size(),
              store.file_for("catalog").c_str());
  return 0;
}

int command_status(const Args& args) {
  Store store;
  const Settings settings = store.load_settings();
  const Catalog catalog = store.load_catalog();
  const auto progress = store.load_progress();
  const auto my_list = store.load_my_list();

  if (args.json) {
    Json root = Json::object();
    root.set("dataDir", store.dir());
    root.set("mediaDir", media_dir());
    root.set("logFile", log::path());
    root.set("items", static_cast<long long>(catalog.items.size()));
    root.set("scannedAt", static_cast<long long>(catalog.scanned_at));
    Json folders = Json::array();
    for (const auto& f : settings.library_folders) folders.push(Json(f));
    root.set("libraryFolders", folders);
    Json watching = Json::array();
    for (const auto& kv : progress) watching.push(Json(kv.first));
    root.set("inProgress", watching);
    root.set("myList", static_cast<long long>(my_list.size()));
    root.set("previousRunCrashed", log::previous_run_crashed());
    std::printf("%s\n", root.dump(2).c_str());
    return 0;
  }

  std::printf("data dir      %s\n", store.dir().c_str());
  std::printf("media dir     %s\n", media_dir().c_str());
  std::printf("log file      %s\n", log::path().c_str());
  std::printf("items         %zu\n", catalog.items.size());
  std::printf("library       %zu folder(s)\n", settings.library_folders.size());
  for (const auto& folder : settings.library_folders) std::printf("              %s\n", folder.c_str());
  std::printf("in progress   %zu\n", progress.size());
  std::printf("my list       %zu\n", my_list.size());
  std::printf("last scan     %s\n", catalog.scanned_at ? "yes" : "never");

  const auto rails = build_rails(catalog.items, progress, my_list);
  std::printf("rails         %zu:", rails.size());
  for (const auto& rail : rails) std::printf(" %s(%zu)", rail.title.c_str(), rail.items.size());
  std::printf("\n");
  return 0;
}

int command_settings(const Args& args) {
  Store store;
  if (args.key_value.empty()) {
    const Settings settings = store.load_settings();
    Json out = Json::object();
    Json folders = Json::array();
    for (const auto& f : settings.library_folders) folders.push(Json(f));
    out.set("libraryFolders", folders);
    out.set("vlcPath", settings.vlc_path.empty() ? Json() : Json(settings.vlc_path));
    out.set("vlcHttpPort", settings.vlc_http_port);
    out.set("vlcPasswordSet", !settings.vlc_password.empty());
    out.set("playMode", settings.play_mode);
    out.set("vlcFullscreen", settings.vlc_fullscreen);
    out.set("autoLatency", settings.auto_latency);
    out.set("hwDecode", settings.hw_decode);
    std::printf("%s\n", out.dump(2).c_str());
    return 0;
  }

  const std::size_t eq = args.key_value.find('=');
  if (eq == std::string::npos) {
    std::fprintf(stderr, "--set expects key=<json value>\n");
    return 2;
  }
  const std::string key = args.key_value.substr(0, eq);
  const std::string raw = args.key_value.substr(eq + 1);
  Json value;
  std::string error;
  if (!Json::parse(raw, value, &error)) {
    // Bare words are accepted as strings so the CLI stays usable.
    value = Json(raw);
  }
  Json patch = Json::object();
  patch.set(key, value);
  Settings updated;
  if (!store.patch_settings(patch, &updated, &error)) {
    std::fprintf(stderr, "rejected: %s\n", error.c_str());
    return 4;
  }
  std::printf("saved %s\n", key.c_str());
  return 0;
}

int command_naming(int argc, char** argv) {
  // `--naming a.mkv b.mkv ...` prints what the parser makes of each name. Used by
  // the tests and by anyone checking a rename before committing to it.
  int printed = 0;
  for (int i = 0; i < argc; ++i) {
    const std::string a = argv[i];
    if (a.rfind("--", 0) == 0) continue;
    if (a == "naming") continue;
    const ParsedName parsed = parse_name(a);
    std::printf("%s\n  title: %s\n  year:  %s\n", a.c_str(), parsed.title.c_str(),
                parsed.year ? std::to_string(parsed.year).c_str() : "(none)");
    ++printed;
  }
  return printed ? 0 : 2;
}

int command_crash() {
  // Deliberate crash, used by the test suite to prove that a crash still leaves a
  // log with a stack trace behind. Refuses to run without an explicit --log or a
  // data directory, so it cannot hurt a real installation by accident.
  LOG_WARN("crash test requested: raising SIGSEGV through the crash handler");
  // A real null dereference is optimised away at -O2 (it is undefined behaviour), so
  // the signal is raised explicitly: that exercises exactly the handler chain a real
  // crash uses, which is what this test is for.
#ifdef _WIN32
  RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
#else
  std::raise(SIGSEGV);
#endif
  return 0;  // unreachable when the handler is installed
}

void print_help() {
  std::printf(
      "%s %s\n\n"
      "usage: vlcflix [command] [options]\n\n"
      "commands\n"
      "  scan                 walk the library, probe films, draw artwork\n"
      "  status               what is stored right now (--json for machines)\n"
      "  settings [--set k=v] read or patch settings, with validation\n"
      "  naming <file>...     show how filenames are turned into titles\n"
      "  doctor               environment report: paths, tools, log location\n"
      "  crash                deliberately crash (proves the crash log works)\n"
      "  version | help\n\n"
      "options\n"
      "  --data <dir>         use this data directory\n"
      "  --folders <a,b,...>  library folders for this scan\n"
      "  --no-art             scan metadata only, draw nothing\n"
      "  --log <file>         explicit log file\n"
      "  --console            also mirror the log to stderr\n"
      "  --json               machine-readable output where supported\n",
      kProductName, kVersion);
}

int command_doctor(const Args& args) {
  const Store store;
  const auto catalog = store.load_catalog();
  std::printf("%s %s (built %s)\n\n", kProductName, kVersion, kBuildDate);
  std::printf("log file        %s\n", log::path().empty() ? "(stderr only)" : log::path().c_str());
  std::printf("log candidates  ");
  for (const auto& dir : log_dir_candidates()) std::printf("%s%s", dir.c_str(), " ");
  std::printf("\n");
  std::printf("data dir        %s\n", store.dir().c_str());
  std::printf("media dir       %s\n", media_dir().c_str());
  std::printf("exe dir         %s\n", exe_dir().c_str());
  const ToolProbe tools = probe_tools();
  std::printf("ffmpeg          %s\n", tools.ffmpeg ? "found" : "MISSING (no artwork)");
  std::printf("ffprobe         %s\n", tools.ffprobe ? "found" : "MISSING (no durations)");
  std::printf("catalog         %zu item(s)%s\n", catalog.items.size(),
              catalog.scanned_at ? "" : " (never scanned)");
  std::printf("previous run    %s\n",
              log::previous_run_crashed() ? "ENDED UNEXPECTEDLY" : "clean or first run");
  if (log::previous_run_crashed()) {
    std::printf("\nlast lines of that run:\n%s", log::previous_run_tail(15).c_str());
  }
  (void)args;
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  // ---- FIRST STATEMENT ------------------------------------------------------
  // Open the log and install the crash handlers. Nothing above this line may
  // allocate, parse anything or touch a device: if the app is going to die before
  // opening a window, this is the code that must already be working.
  // The two flags that can move the log are read first because they decide where
  // the log goes; the full parse happens afterwards.
  log::Options log_options;
  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    if (std::strcmp(a, "--log") == 0 && i + 1 < argc) log_options.path = argv[++i];
    else if (std::strcmp(a, "--console") == 0) log_options.console = true;
  }
  log::init(log_options);
  // ---- everything below is allowed to fail, and will be logged ---------------

  struct ExitGuard {
    int code = 0;
    ~ExitGuard() {
      LOG_INFO("exit code %d", code);
      log::shutdown();
    }
  } guard;

  const Args args = parse_args(argc, argv);
  if (!args.data_dir.empty()) {
    set_data_dir(args.data_dir);
    LOG_INFO("data directory overridden: %s", args.data_dir.c_str());
  }
  LOG_INFO("command: %s", args.command.c_str());

  std::printf("%s %s\n", kProductName, kVersion);
  std::printf("log: %s\n", log::path().empty() ? "(stderr only)" : log::path().c_str());
  if (log::previous_run_crashed()) {
    std::printf("NOTE: the previous run ended unexpectedly; see the lines above the last run-start\n");
  }

  int code = 0;
  if (args.command == "scan") code = command_scan(args);
  else if (args.command == "status") code = command_status(args);
  else if (args.command == "settings") code = command_settings(args);
  else if (args.command == "naming") code = command_naming(argc, argv);
  else if (args.command == "doctor") code = command_doctor(args);
  else if (args.command == "crash") code = command_crash();
  else if (args.command == "version") std::printf("%s\n", kVersion);
  else if (args.command == "help" || args.command == "--help") print_help();
  else {
    std::fprintf(stderr, "unknown command '%s' (try: help)\n", args.command.c_str());
    LOG_ERROR("unknown command: %s", args.command.c_str());
    code = 2;
  }
  guard.code = code;
  return code;
}
