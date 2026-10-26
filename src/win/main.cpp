// ---------------------------------------------------------------------------
// VLCFlix native shell for Windows
//
// A single-file Win32 application that:
//   1. starts the bundled Node server (resources\app\server\index.js) on a free
//      loopback port,
//   2. waits until /api/health answers,
//   3. shows the UI in WebView2 -- the Chromium-based engine that already ships
//      with Windows 10/11 -- inside a normal desktop window,
//   4. stops the server when the window closes (a job object guarantees it).
//
// There is no bundled browser. WebView2 is an OS component, which is what keeps
// this an ordinary ~1 MB executable instead of a 200 MB Electron bundle.
//
// Built with mingw-w64, which means no <wrl/implements.h> and no __uuidof.
// The handful of COM callbacks WebView2 needs are written out by hand below.
//
// Failure policy: a windowed app with no console is undebuggable, so every step
// that can fail writes a line with its HRESULT to %LOCALAPPDATA%\VLCFlix\
// launcher.log, and a browser failure never kills the window -- it shows a
// native panel with the URL so the app remains usable.
// ---------------------------------------------------------------------------

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>

#include <string>
#include <vector>
#include <cstdio>
#include <cstdarg>
#include <cstring>

#include "WebView2.h"
#include "resource.h"

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

static HINSTANCE g_instance = nullptr;
static HWND      g_window = nullptr;
static HWND      g_loading = nullptr;      // "Starting VLCFlix..." label
static HANDLE    g_server = nullptr;       // server process
static HANDLE    g_serverJob = nullptr;    // job object: guarantees cleanup
static std::wstring g_baseUrl;
static ICoreWebView2Controller* g_controller = nullptr;
static ICoreWebView2* g_webview = nullptr;
static bool g_webviewAttached = false;
static int  g_attempt = 0;         // which browser attempt is live
static bool g_pageLoaded = false;  // the page really rendered
static UINT_PTR g_watchdog = 0;
static HRESULT g_envHr = S_OK;     // last environment creation result
static bool g_gotController = false;  // did any attempt reach a controller?
static const wchar_t* g_closeReason = L"none";

#define WM_APP_SERVER_READY  (WM_APP + 1)
#define WM_APP_SERVER_FAILED (WM_APP + 2)

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

static std::wstring g_logPath;
static SRWLOCK g_logLock = SRWLOCK_INIT;

static void Log(const wchar_t* format, ...) {
  if (g_logPath.empty()) return;
  AcquireSRWLockExclusive(&g_logLock);
  FILE* f = nullptr;
  if (_wfopen_s(&f, g_logPath.c_str(), L"a, ccs=UTF-8") == 0 && f) {
    va_list args;
    va_start(args, format);
    vfwprintf(f, format, args);
    va_end(args);
    fputws(L"\n", f);
    fclose(f);
  }
  ReleaseSRWLockExclusive(&g_logLock);
}

/** Log an HRESULT in hex plus a human hint for the common WebView2 failures. */
static void LogHr(const wchar_t* step, HRESULT hr) {
  const wchar_t* hint = L"";
  switch (static_cast<unsigned long>(hr)) {
    case 0x80070002: hint = L" (ERROR_FILE_NOT_FOUND)"; break;
    case 0x80070003: hint = L" (ERROR_PATH_NOT_FOUND - user data folder?)"; break;
    case 0x80070005: hint = L" (ERROR_ACCESS_DENIED)"; break;
    case 0x80070057: hint = L" (E_INVALIDARG)"; break;
    case 0x80040154: hint = L" (REGDB_E_CLASSNOTREG - WebView2 runtime missing?)"; break;
    case 0x80070490: hint = L" (ERROR_NOT_FOUND - WebView2 runtime missing?)"; break;
    default: break;
  }
  Log(L"[webview2] %ls -> 0x%08lX%ls", step, static_cast<unsigned long>(hr), hint);
}

// ---------------------------------------------------------------------------
// COM plumbing (hand-rolled: mingw has no WRL Callback / __uuidof)
// ---------------------------------------------------------------------------

#define COM_HANDLER_BOILERPLATE(CLASS, IFACE, IID_NAME)                        \
 public:                                                                       \
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { \
    if (!ppv) return E_POINTER;                                                \
    if (riid == IID_IUnknown || IsEqualIID(riid, IID_NAME)) {                   \
      *ppv = static_cast<IFACE*>(this);                                        \
      AddRef();                                                                \
      return S_OK;                                                             \
    }                                                                          \
    *ppv = nullptr;                                                            \
    return E_NOINTERFACE;                                                      \
  }                                                                            \
  ULONG STDMETHODCALLTYPE AddRef() override {                                  \
    return static_cast<ULONG>(InterlockedIncrement(&refs_));                   \
  }                                                                            \
  ULONG STDMETHODCALLTYPE Release() override {                                 \
    LONG n = InterlockedDecrement(&refs_);                                     \
    if (n == 0) delete this;                                                   \
    return static_cast<ULONG>(n);                                              \
  }                                                                            \
                                                                               \
 private:                                                                      \
  LONG refs_ = 1;                                                              \
  ~CLASS() = default;                                                          \
                                                                               \
 public:

static void ShowFallbackPanel(const std::wstring& reason);

class EnvironmentHandler final
    : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
 public:
  explicit EnvironmentHandler(int attempt) : attempt_(attempt) {}
  COM_HANDLER_BOILERPLATE(EnvironmentHandler,
                          ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
                          IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)
  HRESULT STDMETHODCALLTYPE Invoke(HRESULT result,
                                   ICoreWebView2Environment* environment) override;

 private:
  int attempt_;
};

class ControllerHandler final
    : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
 public:
  explicit ControllerHandler(int attempt) : attempt_(attempt) {}
  COM_HANDLER_BOILERPLATE(ControllerHandler,
                          ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
                          IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)
  HRESULT STDMETHODCALLTYPE Invoke(HRESULT result,
                                   ICoreWebView2Controller* controller) override;

 private:
  int attempt_;
};

class NavigationStartingHandler final : public ICoreWebView2NavigationStartingEventHandler {
 public:
  explicit NavigationStartingHandler(int attempt) : attempt_(attempt) {}
  COM_HANDLER_BOILERPLATE(NavigationStartingHandler,
                          ICoreWebView2NavigationStartingEventHandler,
                          IID_ICoreWebView2NavigationStartingEventHandler)
  HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,
                                   ICoreWebView2NavigationStartingEventArgs* args) override;

 private:
  int attempt_;
};

class NavigationCompletedHandler final
    : public ICoreWebView2NavigationCompletedEventHandler {
 public:
  explicit NavigationCompletedHandler(int attempt) : attempt_(attempt) {}
  COM_HANDLER_BOILERPLATE(NavigationCompletedHandler,
                          ICoreWebView2NavigationCompletedEventHandler,
                          IID_ICoreWebView2NavigationCompletedEventHandler)
  HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,
                                   ICoreWebView2NavigationCompletedEventArgs* args) override;

 private:
  int attempt_;
};

class ProcessFailedHandler final : public ICoreWebView2ProcessFailedEventHandler {
 public:
  explicit ProcessFailedHandler(int attempt) : attempt_(attempt) {}
  COM_HANDLER_BOILERPLATE(ProcessFailedHandler, ICoreWebView2ProcessFailedEventHandler,
                          IID_ICoreWebView2ProcessFailedEventHandler)
  HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,
                                   ICoreWebView2ProcessFailedEventArgs* args) override;

 private:
  int attempt_;
};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static std::wstring ExePath() {
  wchar_t buffer[MAX_PATH * 4];
  DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH * 4);
  return std::wstring(buffer, n);
}

static std::wstring DirName(const std::wstring& path) {
  size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? path : path.substr(0, slash);
}

static std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (a.back() == L'\\' || a.back() == L'/') return a + b;
  return a + L"\\" + b;
}

static bool FileExists(const std::wstring& path) {
  DWORD attrs = GetFileAttributesW(path.c_str());
  return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring Quote(const std::wstring& s) { return L"\"" + s + L"\""; }

static int FindFreePort() {
  int port = 0;
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s != INVALID_SOCKET) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
      sockaddr_in bound{};
      int len = sizeof(bound);
      if (getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len) == 0) {
        port = ntohs(bound.sin_port);
      }
    }
    closesocket(s);
  }
  return port;
}

/** GET http://127.0.0.1:<port>/api/health and report whether it returned 200. */
static bool ServerIsUp(int port, DWORD timeoutMs) {
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  bool ok = false;
  if (s != INVALID_SOCKET) {
    DWORD tv = timeoutMs;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
      char req[256];
      int n = snprintf(req, sizeof(req),
                       "GET /api/health HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n"
                       "Connection: close\r\n\r\n",
                       port);
      if (n > 0 && send(s, req, n, 0) > 0) {
        char buf[256] = {0};
        int got = recv(s, buf, sizeof(buf) - 1, 0);
        ok = got > 12 && strncmp(buf, "HTTP/1.", 7) == 0 && strstr(buf, " 200 ") != nullptr;
      }
    }
    closesocket(s);
  }
  return ok;
}

// WebView2Loader.dll is resolved at runtime rather than linked, because the
// shipped .lib is an MSVC import library and this binary is built by mingw.
typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateEnvWithOptions)(
    PCWSTR browserExecutableFolder, PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions* options,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* handler);

static std::wstring AppDataDir() {
  wchar_t* base = nullptr;
  std::wstring dir;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) {
    dir = JoinPath(base, L"VLCFlix");
    CoTaskMemFree(base);
  }
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

static PFN_CreateEnvWithOptions LoadWebView2Loader() {
  std::wstring exeDir = DirName(ExePath());
  std::wstring candidates[] = {JoinPath(exeDir, L"resources\\WebView2Loader.dll"),
                               L"WebView2Loader.dll"};
  for (const std::wstring& full : candidates) {
    HMODULE mod = LoadLibraryW(full.c_str());
    if (!mod) {
      Log(L"[webview2] LoadLibrary failed for %ls (err %lu)", full.c_str(), GetLastError());
      continue;
    }
    auto fn = reinterpret_cast<PFN_CreateEnvWithOptions>(
        GetProcAddress(mod, "CreateCoreWebView2EnvironmentWithOptions"));
    if (!fn) {
      // Loaded, but the export is missing -- a different problem from a missing
      // DLL, and it must be reported as such.
      Log(L"[webview2] %ls loaded but has no CreateCoreWebView2EnvironmentWithOptions "
          L"(wrong or truncated DLL)", full.c_str());
      continue;
    }
    Log(L"[webview2] loader = %ls", full.c_str());
    return fn;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Server lifecycle
// ---------------------------------------------------------------------------

struct ServerThreadArgs { int port; };

static DWORD WINAPI ServerWaiter(LPVOID param) {
  ServerThreadArgs* args = static_cast<ServerThreadArgs*>(param);
  HWND window = g_window;

  for (int i = 0; i < 125; ++i) {
    if (ServerIsUp(args->port, 700)) {
      Log(L"[health] server answered on port %d after %d ms", args->port, i * 200);
      PostMessageW(window, WM_APP_SERVER_READY, 0, 0);
      delete args;
      return 0;
    }
    if (g_server && WaitForSingleObject(g_server, 0) == WAIT_OBJECT_0) {
      Log(L"[health] node.exe exited before answering");
      break;
    }
    Sleep(200);
  }
  Log(L"[health] server did NOT answer on port %d", args->port);
  PostMessageW(window, WM_APP_SERVER_FAILED, 0, 0);
  delete args;
  return 0;
}

static bool StartServer(int port, std::wstring* error) {
  std::wstring exeDir = DirName(ExePath());
  std::wstring root = JoinPath(exeDir, L"resources");
  std::wstring app = JoinPath(root, L"app");
  std::wstring node = JoinPath(root, L"node.exe");
  std::wstring script = JoinPath(app, L"server\\index.js");

  if (!FileExists(node)) { *error = L"Missing runtime: " + node; return false; }
  if (!FileExists(script)) { *error = L"Missing application: " + script; return false; }

  std::wstring dataDir = AppDataDir();
  std::wstring cmd = Quote(node) + L" " + Quote(script);

  SetEnvironmentVariableW(L"PORT", std::to_wstring(port).c_str());
  SetEnvironmentVariableW(L"HOST", L"127.0.0.1");  // loopback only
  SetEnvironmentVariableW(L"NODE_ENV", L"production");
  SetEnvironmentVariableW(L"VLCFLIX_DATA_DIR", dataDir.c_str());
  SetEnvironmentVariableW(L"NODE_NO_WARNINGS", L"1");

  std::wstring serverLog = JoinPath(dataDir, L"server.log");
  SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE logHandle = CreateFileW(serverLog.c_str(), FILE_APPEND_DATA,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                                 OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
  si.wShowWindow = SW_HIDE;
  si.hStdOutput = logHandle;
  si.hStdError = logHandle;
  si.hStdInput = nullptr;
  PROCESS_INFORMATION pi{};

  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\0');

  Log(L"[launch] node    = %ls", node.c_str());
  Log(L"[launch] script  = %ls", script.c_str());
  Log(L"[launch] dataDir = %ls", dataDir.c_str());
  Log(L"[launch] port    = %d", port);

  BOOL started = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr,
                                logHandle ? TRUE : FALSE, CREATE_NO_WINDOW, nullptr,
                                app.c_str(), &si, &pi);
  if (logHandle) CloseHandle(logHandle);
  if (!started) {
    DWORD code = GetLastError();
    *error = L"Could not start node.exe (error " + std::to_wstring(code) + L")";
    Log(L"[launch] CreateProcess failed, GetLastError=%lu", code);
    return false;
  }
  Log(L"[launch] node.exe started, pid=%lu", pi.dwProcessId);

  g_serverJob = CreateJobObjectW(nullptr, nullptr);
  if (g_serverJob) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(g_serverJob, JobObjectExtendedLimitInformation, &info, sizeof(info));
    AssignProcessToJobObject(g_serverJob, pi.hProcess);
  }

  g_server = pi.hProcess;
  CloseHandle(pi.hThread);

  ServerThreadArgs* args = new ServerThreadArgs{port};
  CloseHandle(CreateThread(nullptr, 0, ServerWaiter, args, 0, nullptr));
  return true;
}

static void StopServer() {
  if (g_server) {
    TerminateProcess(g_server, 0);
    WaitForSingleObject(g_server, 3000);
    CloseHandle(g_server);
    g_server = nullptr;
  }
  if (g_serverJob) {
    CloseHandle(g_serverJob);
    g_serverJob = nullptr;
  }
}

// ---------------------------------------------------------------------------
// Fallback panel: a plain Win32 surface used when the browser cannot start.
// It keeps the window alive and gives the user the URL, instead of dying.
// ---------------------------------------------------------------------------

static bool g_fallbackShown = false;
static HBRUSH g_panelBrush = nullptr;

// All fallback controls are parented to the main window so that plain client
// coordinates work. (An earlier version parented them to the window but looked
// them up under a container, so nothing ever moved.)
static HWND MakeFallbackChild(const wchar_t* cls, const wchar_t* text, DWORD style,
                              int id) {
  HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10,
                           g_window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           g_instance, nullptr);
  if (h) {
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
                 TRUE);
  }
  return h;
}

static void LayoutFallback() {
  if (!g_fallbackShown) return;
  RECT client;
  GetClientRect(g_window, &client);
  int w = client.right - client.left;
  int h = client.bottom - client.top;
  int x = 48;
  int cw = w - 96;
  if (cw < 240) { x = 16; cw = w - 32; }

  int textH = (h > 500) ? 168 : 148;
  int y = (h > 420) ? 44 : 12;

  MoveWindow(GetDlgItem(g_window, IDC_FALLBACK_TITLE), x, y, cw, 30, TRUE);
  MoveWindow(GetDlgItem(g_window, IDC_FALLBACK_BODY), x, y + 40, cw, textH, TRUE);
  MoveWindow(GetDlgItem(g_window, IDC_FALLBACK_OPEN), x, y + textH + 40, 180, 34, TRUE);
  MoveWindow(GetDlgItem(g_window, IDC_FALLBACK_RETRY), x + 196, y + textH + 40, 180, 34,
             TRUE);
}

static void ShowFallbackPanel(const std::wstring& reason) {
  if (g_fallbackShown) { LayoutFallback(); return; }
  if (g_loading) ShowWindow(g_loading, SW_HIDE);

  Log(L"[fallback] showing native panel: %ls", reason.c_str());
  Log(L"[fallback] url to offer = %ls", g_baseUrl.c_str());
  g_fallbackShown = true;

  // The URL goes in the body text as well as the edit box: the box is there for
  // copy-paste, but the user must be able to read the address even if it fails
  // to render on some Wine/runtime combination.
  std::wstring body = reason +
      L"\r\n\r\nThe background service is running. Open this address in your browser:"
      L"\r\n" + g_baseUrl +
      L"\r\n\r\nOr install the Microsoft Edge WebView2 Runtime, then press Retry."
      L"\r\nDiagnostics: " + g_logPath;

  MakeFallbackChild(L"STATIC", L"VLCFlix could not open its window", SS_LEFT,
                    IDC_FALLBACK_TITLE);
  MakeFallbackChild(L"STATIC", body.c_str(), SS_LEFT, IDC_FALLBACK_BODY);
  MakeFallbackChild(L"BUTTON", L"Open in browser", BS_PUSHBUTTON, IDC_FALLBACK_OPEN);
  MakeFallbackChild(L"BUTTON", L"Retry", BS_PUSHBUTTON, IDC_FALLBACK_RETRY);

  LayoutFallback();
  InvalidateRect(g_window, nullptr, TRUE);
}

// ---------------------------------------------------------------------------
// WebView2
//
// The page is served by our own process on loopback, so the only thing that can
// realistically go wrong is the embedded browser itself. v1.0.0 assumed it would
// simply work, and when it did not the user got a black window with no clue.
//
// The sequence below never assumes. Every call is checked, a watchdog fires if
// the page does not finish loading, and a second attempt runs with GPU
// acceleration disabled -- the standard remedy for a WebView2 surface that
// creates successfully but never paints.
// ---------------------------------------------------------------------------

static const int kAttempts = 2;
static const UINT_PTR TIMER_WATCHDOG = 1;
static const UINT kWatchdogMs = 8000;

static void ShowFallbackPanel(const std::wstring& reason);
static void StartAttempt();

static const wchar_t* AttemptArgs(int attempt) {
  return attempt == 1 ? L"--disable-gpu" : L"";
}

// A separate profile per attempt: the browser process for a user data folder is
// shared, so reusing the folder would let a wedged process answer the retry.
static std::wstring AttemptDataFolder(int attempt) {
  return JoinPath(AppDataDir(), attempt == 0 ? L"webview" : L"webview-sw");
}

static void ResizeWebView() {
  if (!g_controller) return;
  RECT bounds;
  GetClientRect(g_window, &bounds);
  HRESULT hr = g_controller->put_Bounds(bounds);
  if (FAILED(hr)) LogHr(L"put_Bounds", hr);
}

// Dumps what the browser actually thinks it is doing. Called when an attempt is
// abandoned, because "Navigate returned 0" plus "source is still about:blank"
// plus "bounds are 0x0" are three very different problems.
static void LogWebViewState() {
  if (!g_controller || !g_webview) {
    Log(L"[webview2] state: no controller/webview");
    return;
  }
  BOOL visible = FALSE;
  RECT bounds{};
  if (FAILED(g_controller->get_IsVisible(&visible))) visible = -1;
  if (FAILED(g_controller->get_Bounds(&bounds))) { bounds.right = bounds.left = -1; }
  LPWSTR source = nullptr;
  if (FAILED(g_webview->get_Source(&source))) source = nullptr;
  UINT32 browserPid = 0;
  if (FAILED(g_webview->get_BrowserProcessId(&browserPid))) browserPid = 0;
  Log(L"[webview2] state: visible=%d bounds=%ldx%ld source=%ls browserPid=%lu", visible,
      bounds.right - bounds.left, bounds.bottom - bounds.top,
      source ? source : L"(null)", browserPid);
  if (source) CoTaskMemFree(source);
}

static void TearDownWebView() {
  if (g_watchdog) { KillTimer(g_window, TIMER_WATCHDOG); g_watchdog = 0; }
  if (g_webview) { g_webview->Release(); g_webview = nullptr; }
  if (g_controller) { g_controller->Release(); g_controller = nullptr; }
  g_webviewAttached = false;
}

/** Give up on the current attempt and move to the next one, or to the panel. */
static void NextAttempt(const wchar_t* reason) {
  Log(L"[webview2] attempt %d abandoned: %ls", g_attempt, reason);
  LogWebViewState();
  TearDownWebView();
  ++g_attempt;
  if (g_attempt < kAttempts) {
    Log(L"[webview2] retrying with arguments '%ls'", AttemptArgs(g_attempt));
    StartAttempt();
    return;
  }
  // Report what actually went wrong, not a generic guess: "the runtime is not
  // installed" and "it started but never painted" call for different fixes.
  if (!g_gotController && FAILED(g_envHr)) {
    wchar_t msg[512];
    _snwprintf_s(msg, 512, _TRUNCATE,
                 L"The Microsoft Edge WebView2 Runtime could not be started "
                 L"(0x%08lX).\r\nIt is a standard Windows 10/11 component but is "
                 L"absent on some images and can be blocked by policy.\r\nInstall it, "
                 L"then press Retry.",
                 static_cast<unsigned long>(g_envHr));
    ShowFallbackPanel(msg);
  } else {
    ShowFallbackPanel(
        L"The built-in browser started but never drew the page.\r\n"
        L"Both a normal and a software-rendered attempt were tried.");
  }
}

/** The page is on screen: stand down the label and the watchdog. */
static void PageLoaded(const wchar_t* how) {
  if (g_pageLoaded) return;
  g_pageLoaded = true;
  if (g_watchdog) { KillTimer(g_window, TIMER_WATCHDOG); g_watchdog = 0; }
  if (g_loading) ShowWindow(g_loading, SW_HIDE);
  Log(L"[webview2] PAGE VISIBLE (%ls) on attempt %d", how, g_attempt);
}

HRESULT STDMETHODCALLTYPE NavigationStartingHandler::Invoke(
    ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) {
  if (attempt_ != g_attempt) return S_OK;
  LPWSTR uri = nullptr;
  args->get_Uri(&uri);
  if (!uri) return S_OK;
  bool internal = wcsncmp(uri, L"http://127.0.0.1:", 17) == 0 ||
                  wcsncmp(uri, L"http://localhost:", 17) == 0 ||
                  wcsncmp(uri, L"about:", 6) == 0 || wcsncmp(uri, L"data:", 5) == 0;
  Log(L"[webview2] navigation starting: %ls%s", uri,
      internal ? L"" : L"  (cancelled, handed to the default browser)");
  if (!internal) {
    ShellExecuteW(nullptr, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL);
    args->put_Cancel(TRUE);
  }
  CoTaskMemFree(uri);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE NavigationCompletedHandler::Invoke(
    ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) {
  if (attempt_ != g_attempt) return S_OK;
  BOOL success = FALSE;
  args->get_IsSuccess(&success);
  COREWEBVIEW2_WEB_ERROR_STATUS webError = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
  args->get_WebErrorStatus(&webError);
  UINT status = 0;
  ICoreWebView2NavigationCompletedEventArgs2* args2 = nullptr;
  if (SUCCEEDED(args->QueryInterface(IID_ICoreWebView2NavigationCompletedEventArgs2,
                                     reinterpret_cast<void**>(&args2))) && args2) {
    int code = 0;
    args2->get_HttpStatusCode(&code);
    status = static_cast<UINT>(code);
    args2->Release();
  }
  Log(L"[webview2] navigation completed: success=%d http=%u webError=%d", success, status,
      static_cast<int>(webError));

  if (success) {
    PageLoaded(L"navigation completed");
    return S_OK;
  }
  // The controller exists and Navigate was accepted, yet the page did not load.
  // That is the black window; do not leave it on screen.
  NextAttempt(L"navigation reported failure");
  return S_OK;
}

HRESULT STDMETHODCALLTYPE ProcessFailedHandler::Invoke(
    ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) {
  if (attempt_ != g_attempt) return S_OK;
  COREWEBVIEW2_PROCESS_FAILED_KIND kind = COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;
  if (args) args->get_ProcessFailedKind(&kind);
  Log(L"[webview2] PROCESS FAILED, kind=%d", static_cast<int>(kind));
  NextAttempt(L"browser process failed");
  return S_OK;
}

HRESULT STDMETHODCALLTYPE ControllerHandler::Invoke(
    HRESULT result, ICoreWebView2Controller* controller) {
  LogHr(L"controller Invoke", result);
  if (attempt_ != g_attempt) { Log(L"[webview2] stale controller handler ignored"); return S_OK; }
  if (FAILED(result) || !controller) {
    NextAttempt(L"controller creation failed");
    return S_OK;
  }
  // The controller is BORROWED: the runtime drops its own reference when this
  // handler returns. Storing the raw pointer let it be destroyed immediately,
  // which closes the webview -- Navigate still "succeeded" because it ran inside
  // this call, but no navigation ever happened and nothing painted. Any later
  // use (resize, state dump) then crashed on freed memory.
  controller->AddRef();
  g_controller = controller;
  LogHr(L"put_IsVisible", g_controller->put_IsVisible(TRUE));

  HRESULT hr = g_controller->get_CoreWebView2(&g_webview);
  LogHr(L"get_CoreWebView2", hr);
  if (FAILED(hr) || !g_webview) {
    NextAttempt(L"get_CoreWebView2 failed");
    return S_OK;
  }
  g_webviewAttached = true;
  ResizeWebView();

  // Keep "Starting VLCFlix..." above the browser surface. The WebView2 child
  // window sits on top of our own children, so without this the user would be
  // looking at an unexplained black rectangle whenever the page does not paint.
  if (g_loading) {
    RECT client;
    GetClientRect(g_window, &client);
    SetWindowPos(g_loading, HWND_TOP, 0, (client.bottom - 40) / 2, client.right, 40,
                 SWP_NOACTIVATE);
    ShowWindow(g_loading, SW_SHOW);
  }

  ICoreWebView2Settings* settings = nullptr;
  hr = g_webview->get_Settings(&settings);
  LogHr(L"get_Settings", hr);
  if (SUCCEEDED(hr) && settings) {
    settings->put_AreDevToolsEnabled(FALSE);
    settings->put_AreDefaultContextMenusEnabled(FALSE);
    settings->put_IsStatusBarEnabled(FALSE);
    settings->Release();
  }

  EventRegistrationToken token;
  LogHr(L"add_NavigationStarting",
        g_webview->add_NavigationStarting(new NavigationStartingHandler(attempt_), &token));
  LogHr(L"add_NavigationCompleted",
        g_webview->add_NavigationCompleted(new NavigationCompletedHandler(attempt_), &token));
  LogHr(L"add_ProcessFailed",
        g_webview->add_ProcessFailed(new ProcessFailedHandler(attempt_), &token));
  hr = g_webview->Navigate(g_baseUrl.c_str());
  LogHr(L"Navigate", hr);
  if (FAILED(hr)) {
    NextAttempt(L"Navigate returned a failure");
    return S_OK;
  }

  g_watchdog = SetTimer(g_window, TIMER_WATCHDOG, kWatchdogMs, nullptr);
  Log(L"[webview2] watchdog armed for %u ms", kWatchdogMs);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE EnvironmentHandler::Invoke(
    HRESULT result, ICoreWebView2Environment* environment) {
  LogHr(L"environment Invoke", result);
  if (attempt_ != g_attempt) { Log(L"[webview2] stale environment handler ignored"); return S_OK; }
  if (FAILED(result) || !environment) {
    NextAttempt(L"environment creation failed");
    return S_OK;
  }
  g_gotController = true;
  LogHr(L"CreateCoreWebView2Controller",
        environment->CreateCoreWebView2Controller(g_window, new ControllerHandler(attempt_)));
  return S_OK;
}

static void StartAttempt() {
  Log(L"[webview2] attempt %d starting (args='%ls', profile='%ls')", g_attempt,
      AttemptArgs(g_attempt), AttemptDataFolder(g_attempt).c_str());

  PFN_CreateEnvWithOptions create = LoadWebView2Loader();
  if (!create) {
    ShowFallbackPanel(
        L"WebView2Loader.dll was not found next to the application, or it is the "
        L"wrong version.\r\nReinstalling VLCFlix may fix this.");
    return;
  }

  // Documented switch for pass-through browser flags. Set before the environment
  // is created, otherwise the browser process has already started without it.
  SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", AttemptArgs(g_attempt));

  std::wstring data = AttemptDataFolder(g_attempt);
  CreateDirectoryW(data.c_str(), nullptr);

  HRESULT hr = create(nullptr, data.c_str(), nullptr, new EnvironmentHandler(g_attempt));
  g_envHr = hr;
  LogHr(L"CreateCoreWebView2EnvironmentWithOptions", hr);
  if (FAILED(hr)) {
    NextAttempt(L"CreateCoreWebView2EnvironmentWithOptions failed");
  }
}

/** Called by the window when the watchdog fires. */
static void WatchdogFired() {
  g_watchdog = 0;
  Log(L"[webview2] WATCHDOG: no navigation completion after %u ms", kWatchdogMs);
  NextAttempt(L"the page never finished loading");
}

static void InitWebView2() {
  if (g_fallbackShown) return;
  g_attempt = 0;
  g_pageLoaded = false;
  g_gotController = false;
  g_envHr = S_OK;
  StartAttempt();
}

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

static LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_SIZE:
      ResizeWebView();
      LayoutFallback();
      return 0;

    case WM_TIMER:
      if (wParam == TIMER_WATCHDOG) { WatchdogFired(); return 0; }
      break;

    case WM_ERASEBKGND: {
      if (!g_fallbackShown) break;
      RECT rc;
      GetClientRect(window, &rc);
      FillRect(reinterpret_cast<HDC>(wParam), &rc,
               g_panelBrush ? g_panelBrush : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
      return 1;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
      HDC dc = reinterpret_cast<HDC>(wParam);
      SetTextColor(dc, RGB(236, 236, 240));
      SetBkColor(dc, RGB(11, 11, 15));
      return reinterpret_cast<LRESULT>(g_panelBrush);
    }

    case WM_COMMAND: {
      int id = LOWORD(wParam);
      if (id == IDC_FALLBACK_OPEN) {
        ShellExecuteW(nullptr, L"open", g_baseUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
      }
      if (id == IDC_FALLBACK_RETRY) {
        Log(L"[fallback] user pressed Retry");
        for (int id : {IDC_FALLBACK_TITLE, IDC_FALLBACK_BODY, IDC_FALLBACK_OPEN,
                       IDC_FALLBACK_RETRY}) {
          HWND child = GetDlgItem(g_window, id);
          if (child) DestroyWindow(child);
        }
        g_fallbackShown = false;
        TearDownWebView();
        if (g_loading) ShowWindow(g_loading, SW_SHOW);
        InitWebView2();
        return 0;
      }
      break;
    }

    case WM_APP_SERVER_READY:
      // The label stays up until the page has actually painted. Hiding it here
      // is what turned a browser failure into an unexplained black window.
      InitWebView2();
      return 0;

    case WM_APP_SERVER_FAILED:
      MessageBoxW(window,
                  L"The VLCFlix background service did not start.\r\n\r\n"
                  L"See server.log in %LOCALAPPDATA%\\VLCFlix for the reason.",
                  L"VLCFlix could not start", MB_ICONERROR | MB_OK);
      DestroyWindow(window);
      return 0;

    case WM_CLOSE:
      Log(L"[window] WM_CLOSE received (origin: %ls)", g_closeReason);
      if (g_webview) {
        g_webview->ExecuteScript(L"window.dispatchEvent(new Event('beforeunload'));", nullptr);
      }
      DestroyWindow(window);
      return 0;

    case WM_DESTROY:
      Log(L"[window] WM_DESTROY; webview attached was %d", g_webviewAttached ? 1 : 0);
      TearDownWebView();
      StopServer();
      PostQuitMessage(0);
      return 0;

    default:
      return DefWindowProcW(window, message, wParam, lParam);
  }
  return DefWindowProcW(window, message, wParam, lParam);
}

/** A windowed app that dies silently is undebuggable; record where it died. */
static LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* info) {
  if (info && info->ExceptionRecord) {
    wchar_t module[MAX_PATH] = L"?";
    HMODULE mod = nullptr;
    void* address = info->ExceptionRecord->ExceptionAddress;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(address), &mod)) {
      GetModuleFileNameW(mod, module, MAX_PATH);
    }
    Log(L"[crash] code=0x%08lX address=%p module=%ls",
        static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode), address, module);
  } else {
    Log(L"[crash] unhandled exception, no record");
  }
  return EXCEPTION_CONTINUE_SEARCH;  // let Windows report it as usual
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
  g_instance = instance;
  SetUnhandledExceptionFilter(OnUnhandledException);

  // Logging first, before anything at all can fail. A previous build initialised
  // the log after the window was created, so an early exit left no trace.
  g_logPath = JoinPath(AppDataDir(), L"launcher.log");
  Log(L"\n===== VLCFlix starting =====");
  Log(L"[launch] exe = %ls", ExePath().c_str());

  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"VLCFlix.SingleInstance");
  if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
    Log(L"[instance] already running; focusing the existing window");
    HWND existing = FindWindowW(L"VLCFlixMainWindow", nullptr);
    if (existing) {
      ShowWindow(existing, SW_RESTORE);
      SetForegroundWindow(existing);
    }
    return 0;
  }

  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  WSADATA wsa;
  int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsa);
  Log(L"[system] WSAStartup = %d", wsaResult);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APPICON));
  wc.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APPICON));
  wc.hbrBackground = CreateSolidBrush(RGB(11, 11, 15));
  wc.lpszClassName = L"VLCFlixMainWindow";
  if (!RegisterClassExW(&wc)) {
    Log(L"[system] RegisterClassEx failed, err %lu", GetLastError());
  }

  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  int width = static_cast<int>((work.right - work.left) * 0.9);
  int height = static_cast<int>((work.bottom - work.top) * 0.9);
  int x = work.left + ((work.right - work.left) - width) / 2;
  int y = work.top + ((work.bottom - work.top) - height) / 2;

  g_window = CreateWindowExW(0, wc.lpszClassName, L"VLCFlix", WS_OVERLAPPEDWINDOW,
                             x, y, width, height, nullptr, nullptr, instance, nullptr);
  if (!g_window) {
    Log(L"[system] CreateWindowEx failed, err %lu", GetLastError());
    MessageBoxW(nullptr, L"Could not create the application window.", L"VLCFlix", MB_ICONERROR);
    return 1;
  }

  g_loading = CreateWindowExW(0, L"STATIC", L"Starting VLCFlix\u2026",
                              WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, width, 40, g_window,
                              nullptr, instance, nullptr);
  SendMessageW(g_loading, WM_SETFONT,
               reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
  RECT client;
  GetClientRect(g_window, &client);
  SetWindowPos(g_loading, nullptr, 0, (client.bottom - 40) / 2, client.right, 40, SWP_NOZORDER);

  ShowWindow(g_window, showCommand);
  UpdateWindow(g_window);

  g_panelBrush = CreateSolidBrush(RGB(11, 11, 15));
  Log(L"[launch] display = %dx%d", width, height);

  int port = FindFreePort();
  Log(L"[launch] free port chosen = %d", port);
  g_baseUrl = L"http://127.0.0.1:" + std::to_wstring(port) + L"/";
  std::wstring error;
  if (port == 0 || !StartServer(port, &error)) {
    Log(L"[launch] failed: %ls", error.c_str());
    g_closeReason = L"server start failure";
    MessageBoxW(g_window,
                error.empty() ? L"Could not start the background service." : error.c_str(),
                L"VLCFlix could not start", MB_ICONERROR);
    return 1;
  }

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  WSACleanup();
  CoUninitialize();
  return 0;
}
