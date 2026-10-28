// VLCFlix Studio - Windows installer and launcher.
//
// This is the single file a user gets: one .exe that carries the whole application -
// the program, SDL2, FreeType, libpng, zlib, libjpeg, libVLC and all 29 of its plugin
// modules - appended to itself as a zip. Double-clicking it installs the app and
// starts it; from then on it behaves like any other Windows application: a shortcut on
// the Desktop and in the Start Menu, an entry in Apps & features, and an uninstall
// that removes all of it.
//
// Why a self-contained installer rather than a pile of DLLs beside an exe:
// libVLC loads its codecs at runtime from a plugins directory, so the application can
// never be one file. The honest way to get "one file to hand out" is to ship the
// directory inside the file and unpack it on first run - which is what this does.
//
// Everything it does is logged to %LOCALAPPDATA%\VLCFlixStudio\logs\setup.log, because
// an installer that fails silently is the worst kind.
//
// Command line:
//   (no arguments)      install if needed, then start the app
//   --install-only      install, do not start
//   --uninstall         remove the app, its shortcuts and its registry entry
//   --verify            report installation state and exit 0/1 (used by the tests)
//   --dir <path>        install somewhere other than %LOCALAPPDATA%\Programs\VLCFlixStudio
//   --quiet             no message boxes
#include <windows.h>

#include <shlobj.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "miniz.h"

namespace fs = std::filesystem;

namespace {

constexpr const char* kVersion = "2.0.0";
constexpr const wchar_t* kProductName = L"VLCFlix Studio";
constexpr const wchar_t* kExeName = L"VLCFlixStudio.exe";
constexpr const wchar_t* kSetupName = L"VLCFlixStudio-Setup.exe";
constexpr const wchar_t* kUninstallKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VLCFlixStudio";

constexpr const wchar_t* kProtectedDirName = L"VLCFlix";

// The zip is appended to this executable, followed by these 24 bytes.
constexpr char kFooterMagic[8] = {'V', 'F', 'S', 'U', 'Z', 'I', 'P', '1'};
constexpr std::size_t kFooterSize = 8 + 8 + 8;

std::wstring g_log_path;
bool g_quiet = false;

std::wstring home_dir() {
  wchar_t* buffer = nullptr;
  std::wstring result;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &buffer))) {
    result = buffer;
    CoTaskMemFree(buffer);
  }
  return result;
}

void log_line(const std::string& text) {
  // Opens, writes and closes every time: a crash in the middle of an install must
  // still leave everything up to that point on disk.
  if (g_log_path.empty()) return;
  std::error_code ec;
  fs::create_directories(fs::path(g_log_path).parent_path(), ec);
  std::FILE* file = _wfopen(g_log_path.c_str(), L"ab");
  if (!file) return;
  SYSTEMTIME now;
  GetLocalTime(&now);
  std::fprintf(file, "[%04d-%02d-%02d %02d:%02d:%02d] %s\r\n", now.wYear, now.wMonth,
               now.wDay, now.wHour, now.wMinute, now.wSecond, text.c_str());
  std::fclose(file);
}

void log_line(const std::wstring& text) {
  if (text.empty()) {
    log_line(std::string());
    return;
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string utf8(static_cast<std::size_t>(size > 0 ? size - 1 : 0), '\0');
  if (size > 1) {
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, utf8.data(), size, nullptr, nullptr);
  }
  log_line(utf8);
}

void say(const std::wstring& text) {
  log_line(text);
  if (g_quiet) return;
  std::wstring line = text + L"\n";
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD written = 0;
  if (out != INVALID_HANDLE_VALUE && out != nullptr) {
    WriteConsoleW(out, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr);
  }
}

void fail_box(const std::wstring& text) {
  log_line(L"FAILED: " + text);
  if (g_quiet) return;
  MessageBoxW(nullptr, text.c_str(), kProductName, MB_ICONERROR | MB_OK);
}

std::wstring self_path() {
  wchar_t buffer[MAX_PATH * 4];
  const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
  if (length == 0) return std::wstring();
  return std::wstring(buffer, length);
}

// The installer keeps a copy of itself inside the install directory so that
// "Uninstall" still works after the download has been deleted.
std::wstring default_install_dir() { return home_dir() + L"\\Programs\\VLCFlixStudio"; }

bool read_payload(std::vector<unsigned char>* out) {
  const std::wstring self = self_path();
  if (self.empty()) return false;
  std::error_code ec;
  const std::uintmax_t size = fs::file_size(self, ec);
  if (ec || size < kFooterSize) return false;

  std::FILE* file = _wfopen(self.c_str(), L"rb");
  if (!file) return false;
  if (_fseeki64(file, 0, SEEK_END) != 0) {
    std::fclose(file);
    return false;
  }
  const long long total = _ftelli64(file);
  if (total < static_cast<long long>(kFooterSize)) {
    std::fclose(file);
    return false;
  }
  _fseeki64(file, total - static_cast<long long>(kFooterSize), SEEK_SET);
  unsigned char footer[kFooterSize];
  const bool footer_ok = std::fread(footer, 1, kFooterSize, file) == kFooterSize;
  std::fclose(file);
  if (!footer_ok || std::memcmp(footer, kFooterMagic, 8) != 0) return false;

  unsigned long long offset = 0;
  unsigned long long length = 0;
  std::memcpy(&offset, footer + 8, 8);
  std::memcpy(&length, footer + 16, 8);
  if (offset + length > static_cast<unsigned long long>(total)) return false;

  out->assign(static_cast<std::size_t>(length), 0);
  file = _wfopen(self.c_str(), L"rb");
  if (!file) return false;
  _fseeki64(file, static_cast<long long>(offset), SEEK_SET);
  const bool ok = std::fread(out->data(), 1, out->size(), file) == out->size();
  std::fclose(file);
  return ok;
}

bool write_bytes(const fs::path& path, const void* data, std::size_t size) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::FILE* file = _wfopen(path.c_str(), L"wb");
  if (!file) return false;
  const bool ok = size == 0 || std::fwrite(data, 1, size, file) == size;
  std::fclose(file);
  return ok;
}

// Unpacks the appended zip. Directory entries are created explicitly because zip files
// written by the build script contain them, and every file's parent is created too so
// the payload survives a zip without directory entries.
bool extract_payload(const std::wstring& install_dir, std::size_t* file_count) {
  std::vector<unsigned char> payload;
  if (!read_payload(&payload)) {
    fail_box(L"This installer is damaged: the application payload is missing from the end "
             L"of the file. Download it again.");
    return false;
  }
  mz_zip_archive zip{};
  if (!mz_zip_reader_init_mem(&zip, payload.data(), payload.size(), 0)) {
    fail_box(L"This installer is damaged: the payload could not be read.");
    return false;
  }
  const mz_uint count = mz_zip_reader_get_num_files(&zip);
  std::size_t written = 0;
  for (mz_uint i = 0; i < count; ++i) {
    mz_zip_archive_file_stat stat{};
    if (!mz_zip_reader_file_stat(&zip, i, &stat)) continue;
    const fs::path target = fs::path(install_dir) / fs::path(stat.m_filename);
    if (mz_zip_reader_is_file_a_directory(&zip, i)) {
      std::error_code ec;
      fs::create_directories(target, ec);
      continue;
    }
    std::size_t size = 0;
    void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
    if (!data) {
      mz_zip_reader_end(&zip);
      fail_box(std::wstring(L"Could not unpack ") + target.wstring());
      return false;
    }
    const bool ok = write_bytes(target, data, size);
    mz_free(data);
    if (!ok) {
      mz_zip_reader_end(&zip);
      fail_box(std::wstring(L"Could not write ") + target.wstring() +
               L" - is the disk full, or is the app still running?");
      return false;
    }
    ++written;
  }
  mz_zip_reader_end(&zip);
  if (file_count) *file_count = written;
  return true;
}

// A .lnk that points at the installed exe, with its icon and working directory.
bool create_shortcut(const fs::path& link_path, const fs::path& target,
                     const std::wstring& description) {
  IShellLinkW* link = nullptr;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                              reinterpret_cast<void**>(&link)))) {
    log_line(L"shortcut: CoCreateInstance failed");
    return false;
  }
  link->SetPath(target.c_str());
  link->SetWorkingDirectory(target.parent_path().c_str());
  link->SetDescription(description.c_str());
  link->SetIconLocation(target.c_str(), 0);
  IPersistFile* file = nullptr;
  bool ok = false;
  if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)))) {
    std::error_code ec;
    fs::create_directories(link_path.parent_path(), ec);
    ok = SUCCEEDED(file->Save(link_path.c_str(), TRUE));
    file->Release();
  }
  link->Release();
  log_line(std::wstring(ok ? L"shortcut: " : L"shortcut FAILED: ") + link_path.wstring());
  return ok;
}

std::wstring known_folder(REFKNOWNFOLDERID id) {
  wchar_t* buffer = nullptr;
  std::wstring result;
  if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &buffer))) {
    result = buffer;
    CoTaskMemFree(buffer);
  }
  return result;
}

fs::path desktop_link() { return fs::path(known_folder(FOLDERID_Desktop)) / L"VLCFlix Studio.lnk"; }
fs::path start_menu_link() {
  return fs::path(known_folder(FOLDERID_Programs)) / L"VLCFlix Studio.lnk";
}
fs::path start_menu_uninstall() {
  return fs::path(known_folder(FOLDERID_Programs)) / L"Uninstall VLCFlix Studio.lnk";
}

bool write_registry(const std::wstring& install_dir) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kUninstallKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key,
                      nullptr) != ERROR_SUCCESS) {
    log_line(L"registry: could not create the uninstall key");
    return false;
  }
  const std::wstring exe = install_dir + L"\\" + kExeName;
  const std::wstring setup = install_dir + L"\\" + kSetupName;
  const std::wstring uninstall = L"\"" + setup + L"\" --uninstall";
  auto set_string = [&](const wchar_t* name, const std::wstring& value) {
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
  };
  auto set_dword = [&](const wchar_t* name, DWORD value) {
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof value);
  };
  const std::wstring wide_version(kVersion, kVersion + std::strlen(kVersion));
  set_string(L"DisplayName", kProductName);
  set_string(L"DisplayVersion", wide_version);
  set_string(L"Publisher", L"VLCFlix");
  set_string(L"DisplayIcon", exe);
  set_string(L"InstallLocation", install_dir);
  set_string(L"UninstallString", uninstall);
  set_string(L"QuietUninstallString", uninstall + L" --quiet");
  set_string(L"Comments", L"A Netflix-style front end that plays films through libVLC.");
  std::error_code ec;
  std::uintmax_t bytes = 0;
  for (const auto& entry : fs::recursive_directory_iterator(install_dir, ec)) {
    if (entry.is_regular_file(ec)) bytes += entry.file_size(ec);
  }
  set_dword(L"EstimatedSize", static_cast<DWORD>(bytes / 1024));
  set_dword(L"NoModify", 1);
  set_dword(L"NoRepair", 1);
  RegCloseKey(key);
  log_line(L"registry: uninstall entry written");
  return true;
}

bool read_registry_string(const wchar_t* name, std::wstring* out) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kUninstallKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
    return false;
  }
  wchar_t buffer[1024];
  DWORD size = sizeof buffer;
  DWORD type = 0;
  const LONG status = RegQueryValueExW(key, name, nullptr, &type,
                                       reinterpret_cast<BYTE*>(buffer), &size);
  RegCloseKey(key);
  if (status != ERROR_SUCCESS || type != REG_SZ) return false;
  *out = buffer;
  return true;
}

bool remove_registry() {
  const LONG status = RegDeleteTreeW(HKEY_CURRENT_USER, kUninstallKey);
  log_line(status == ERROR_SUCCESS ? L"registry: uninstall entry removed"
                                   : L"registry: nothing to remove");
  return true;
}

bool protected_folder_present() {
  std::error_code ec;
  const fs::path protected_path = fs::path(home_dir()) / L"Programs" / kProtectedDirName;
  return fs::exists(protected_path / L"VLCFlix.exe", ec) || fs::exists(protected_path, ec);
}

void note_protected_folder() {
  if (!protected_folder_present()) return;
  const std::wstring protected_path = (fs::path(home_dir()) / L"Programs" / kProtectedDirName).wstring();
  log_line(L"Protected folder detected at " + protected_path + L"; leaving it untouched.");
  say(L"Existing files at " + protected_path + L" will not be changed.");
}

bool install(const std::wstring& install_dir, std::size_t* file_count) {
  say(L"VLCFlix Studio " + std::wstring(kVersion, kVersion + std::strlen(kVersion)) +
      L" - installing to " + install_dir);
  note_protected_folder();
  if (fs::path(install_dir).filename() == kProtectedDirName) {
    fail_box(L"Refusing to install into " + install_dir +
             L": this protected folder cannot be used. Choose another folder.");
    return false;
  }
  std::error_code ec;
  fs::create_directories(install_dir, ec);
  if (ec) {
    fail_box(L"Could not create " + install_dir + L" (" +
             std::wstring(ec.message().begin(), ec.message().end()) + L").");
    return false;
  }

  // Remove the previous copy first, or an upgrade leaves files behind that the new
  // build no longer ships and libVLC happily loads.
  const fs::path exe = fs::path(install_dir) / kExeName;
  if (fs::exists(exe)) {
    say(L"Removing the previous installation");
  }
  for (const auto& entry : {"plugins", "logs"}) {
    const fs::path dir = fs::path(install_dir) / entry;
    if (entry == std::string("logs")) continue;  // keep the app's own log
    fs::remove_all(dir, ec);
  }
  if (!extract_payload(install_dir, file_count)) return false;
  say(L"Unpacked " + std::to_wstring(file_count ? *file_count : 0) + L" files");

  std::error_code copy_ec;
  fs::copy_file(self_path(), fs::path(install_dir) / kSetupName,
                fs::copy_options::overwrite_existing, copy_ec);
  log_line(copy_ec ? L"warning: could not keep a copy of the installer for uninstall"
                   : L"installer copy saved for uninstall");

  create_shortcut(desktop_link(), exe, L"VLCFlix Studio");
  create_shortcut(start_menu_link(), exe, L"VLCFlix Studio");
  create_shortcut(start_menu_uninstall(), fs::path(install_dir) / kSetupName,
                  L"Uninstall VLCFlix Studio");
  write_registry(install_dir);
  say(L"Installed. Start it from the Desktop or the Start Menu.");
  return true;
}

// Recursive delete that survives Windows' habit of failing on a file another process
// has not quite let go of yet. Returns true only when the directory is really gone.
bool remove_tree_retrying(const std::wstring& dir) {
  for (int attempt = 0; attempt < 20; ++attempt) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    if (!ec && !fs::exists(dir)) return true;
    // The usual culprits: the directory is this process's working directory, or an
    // antivirus/indexer still has a handle open.
    SetCurrentDirectoryW(L"C:\\");
    Sleep(250);
  }
  return false;
}

bool uninstall() {
  std::wstring install_dir;
  if (!read_registry_string(L"InstallLocation", &install_dir) || install_dir.empty()) {
    install_dir = default_install_dir();
  }
  say(L"VLCFlix Studio - uninstalling from " + install_dir);

  std::error_code ec;
  fs::remove(desktop_link(), ec);
  fs::remove(start_menu_link(), ec);
  fs::remove(start_menu_uninstall(), ec);
  log_line(L"shortcuts removed");

  // The running copy of the installer sits inside the directory being deleted, so it is
  // copied out and re-run from a temporary place rather than deleting itself.
  const std::wstring self = self_path();
  const std::wstring temp =
      home_dir() + L"\\Temp\\vlcflix-uninstall-" + std::to_wstring(GetCurrentProcessId()) + L".exe";
  std::error_code copy_ec;
  const bool inside = self.rfind(install_dir, 0) == 0;
  if (inside) {
    fs::copy_file(self, temp, fs::copy_options::overwrite_existing, copy_ec);
    if (!copy_ec) {
      // Re-launch from the temp copy with a flag that says the directory can go now.
      std::wstring command = L"\"" + temp + L"\" --finish-uninstall \"" + install_dir + L"\"";
      STARTUPINFOW si{};
      si.cb = sizeof si;
      PROCESS_INFORMATION pi{};
      std::vector<wchar_t> mutable_command(command.begin(), command.end());
      mutable_command.push_back(L'\0');
      if (CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE,
                         CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        say(L"Uninstalling in the background.");
        return true;
      }
    }
  }

  remove_registry();
  const bool removed = remove_tree_retrying(install_dir);
  say(removed ? L"VLCFlix Studio has been removed."
              : L"Some files could not be removed - restart and run the uninstaller again.");
  return removed;
}

bool finish_uninstall(const std::wstring& install_dir) {
  // Give the parent a moment to exit, and stop standing in the directory we are about
  // to delete - a process whose working directory is inside it keeps it alive.
  SetCurrentDirectoryW(L"C:\\");
  Sleep(700);
  remove_registry();
  const bool removed = remove_tree_retrying(install_dir);
  log_line(removed ? L"uninstall: files removed"
                   : L"uninstall: the directory could not be removed completely");
  std::error_code ec;
  fs::remove(self_path(), ec);
  return removed;
}

bool verify(const std::wstring& install_dir) {
  std::wstring location;
  const bool registered = read_registry_string(L"InstallLocation", &location);
  const fs::path exe = fs::path(install_dir) / kExeName;
  const fs::path libvlc = fs::path(install_dir) / L"libvlc.dll";
  const fs::path plugins = fs::path(install_dir) / L"plugins";
  std::size_t plugin_count = 0;
  std::error_code ec;
  for (const auto& entry : fs::recursive_directory_iterator(plugins, ec)) {
    if (entry.is_regular_file(ec)) ++plugin_count;
  }
  const bool desktop = fs::exists(desktop_link());
  const bool start_menu = fs::exists(start_menu_link());

  say(std::wstring(L"install dir     ") + install_dir);
  say(std::wstring(L"registry key    ") + (registered ? L"present" : L"MISSING"));
  say(std::wstring(L"app exe         ") + (fs::exists(exe) ? L"present" : L"MISSING"));
  say(std::wstring(L"libvlc.dll      ") + (fs::exists(libvlc) ? L"present" : L"MISSING"));
  say(L"plugin files    " + std::to_wstring(plugin_count));
  say(std::wstring(L"desktop icon    ") + (desktop ? L"present" : L"MISSING"));
  say(std::wstring(L"start menu icon ") + (start_menu ? L"present" : L"MISSING"));
  const bool ok = registered && fs::exists(exe) && fs::exists(libvlc) && plugin_count > 20 &&
                  desktop && start_menu;
  say(ok ? L"OK: the app is installed and ready to launch" : L"INCOMPLETE");
  return ok;
}

bool launch(const std::wstring& install_dir) {
  const std::wstring exe = install_dir + L"\\" + kExeName;
  std::vector<wchar_t> command(exe.begin(), exe.end());
  command.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      install_dir.c_str(), &si, &pi)) {
    fail_box(L"The app was installed but could not be started (" +
             std::to_wstring(GetLastError()) + L"). Look in " + install_dir + L".");
    return false;
  }
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  log_line(L"launched " + exe);
  return true;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  // A GUI-subsystem binary started from cmd.exe should still print, so attach to the
  // parent console the way the app itself does.
  if (AttachConsole(ATTACH_PARENT_PROCESS)) {
    std::FILE* unused = nullptr;
    (void)unused;
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
  }
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  g_log_path = home_dir() + L"\\VLCFlixStudio\\logs\\setup.log";
  log_line(L"setup: started, " + self_path());

  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  bool uninstall_mode = false;
  bool install_only = false;
  bool verify_mode = false;
  std::wstring finish_dir;
  std::wstring install_dir = default_install_dir();
  for (int i = 1; i < argc; ++i) {
    const std::wstring arg = argv[i];
    if (arg == L"--uninstall") {
      uninstall_mode = true;
    } else if (arg == L"--install-only") {
      install_only = true;
    } else if (arg == L"--verify") {
      verify_mode = true;
    } else if (arg == L"--quiet" || arg == L"/S") {
      g_quiet = true;
    } else if (arg == L"--dir" && i + 1 < argc) {
      install_dir = argv[++i];
    } else if (arg == L"--finish-uninstall" && i + 1 < argc) {
      finish_dir = argv[++i];
    }
  }
  if (argv) LocalFree(argv);

  if (!finish_dir.empty()) {
    const bool ok = finish_uninstall(finish_dir);
    CoUninitialize();
    return ok ? 0 : 1;
  }
  if (uninstall_mode) {
    const bool ok = uninstall();
    CoUninitialize();
    return ok ? 0 : 1;
  }
  if (verify_mode) {
    std::wstring location;
    if (read_registry_string(L"InstallLocation", &location) && !location.empty()) {
      install_dir = location;
    }
    const bool ok = verify(install_dir);
    CoUninitialize();
    return ok ? 0 : 1;
  }

  std::size_t files = 0;
  if (!install(install_dir, &files)) {
    CoUninitialize();
    return 1;
  }
  if (!install_only) launch(install_dir);
  CoUninitialize();
  return 0;
}
