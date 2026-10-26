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
#include <cstring>
#include <cstdarg>
#include <cstdio>

#include "WebView2.h"
#include "resource.h"

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

static HINSTANCE g_instance = nullptr;
static HWND      g_window = nullptr;
static HWND      g_loading = nullptr;     // "Starting VLCFlix..." label
static HANDLE    g_server = nullptr;      // server process
static HANDLE    g_serverJob = nullptr;   // job object: guarantees cleanup
static std::wstring g_baseUrl;
static ICoreWebView2Controller* g_controller = nullptr;
static ICoreWebView2* g_webview = nullptr;

#define WM_APP_SERVER_READY  (WM_APP + 1)
#define WM_APP_SERVER_FAILED (WM_APP + 2)

// ---------------------------------------------------------------------------
// COM plumbing (hand-rolled: mingw has no WRL Callback / __uuidof)
// ---------------------------------------------------------------------------

// Emits the three IUnknown methods for a handler that implements exactly one
// WebView2 interface. Each handler is reference-counted and deletes itself.
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

static void FailAndClose(const wchar_t* message) {
  MessageBoxW(g_window, message, L"VLCFlix", MB_ICONERROR | MB_OK);
  if (g_window) PostMessageW(g_window, WM_CLOSE, 0, 0);
}

// --- environment created ----------------------------------------------------

class EnvironmentHandler final
    : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
  COM_HANDLER_BOILERPLATE(EnvironmentHandler,
                          ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
                          IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)
  HRESULT STDMETHODCALLTYPE Invoke(HRESULT result,
                                   ICoreWebView2Environment* environment) override;
};

// --- controller created -----------------------------------------------------

class ControllerHandler final
    : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
  COM_HANDLER_BOILERPLATE(ControllerHandler,
                          ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
                          IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)
  HRESULT STDMETHODCALLTYPE Invoke(HRESULT result,
                                   ICoreWebView2Controller* controller) override;
};

// --- navigation guard -------------------------------------------------------

class NavigationHandler final : public ICoreWebView2NavigationStartingEventHandler {
  COM_HANDLER_BOILERPLATE(NavigationHandler, ICoreWebView2NavigationStartingEventHandler,
                          IID_ICoreWebView2NavigationStartingEventHandler)
  HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,
                                   ICoreWebView2NavigationStartingEventArgs* args) override;
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

/** Ask the OS for a free loopback port by binding one and reading it back. */
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

static PFN_CreateEnvWithOptions LoadWebView2Loader(std::wstring* where) {
  const wchar_t* candidates[] = {L"resources\\WebView2Loader.dll", L"WebView2Loader.dll"};
  std::wstring exeDir = DirName(ExePath());
  for (const wchar_t* rel : candidates) {
    std::wstring full = (rel[0] == L'r') ? JoinPath(exeDir, rel) : std::wstring(rel);
    HMODULE mod = LoadLibraryW(full.c_str());
    if (mod) {
      *where = full;
      return reinterpret_cast<PFN_CreateEnvWithOptions>(
          GetProcAddress(mod, "CreateCoreWebView2EnvironmentWithOptions"));
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Diagnostics
//
// A windowed app with no console is impossible to debug from the outside, so
// everything that can go wrong writes a line to
//   %LOCALAPPDATA%\VLCFlix\launcher.log
// and the server's own stdout/stderr are redirected to server.log beside it.
// ---------------------------------------------------------------------------

static std::wstring g_logPath;

static void Log(const wchar_t* format, ...) {
  if (g_logPath.empty()) return;
  FILE* f = nullptr;
  if (_wfopen_s(&f, g_logPath.c_str(), L"a, ccs=UTF-8") != 0 || !f) return;
  va_list args;
  va_start(args, format);
  vfwprintf(f, format, args);
  va_end(args);
  fputws(L"\n", f);
  fclose(f);
}

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

// ---------------------------------------------------------------------------
// Server lifecycle
// ---------------------------------------------------------------------------

struct ServerThreadArgs { int port; };

static DWORD WINAPI ServerWaiter(LPVOID param) {
  ServerThreadArgs* args = static_cast<ServerThreadArgs*>(param);
  HWND window = g_window;

  // Give the server up to 25 seconds to answer. First launch is slower because
  // Node has to load modules from a cold cache.
  for (int i = 0; i < 125; ++i) {
    if (ServerIsUp(args->port, 700)) {
      Log(L"[health] server answered on port %d after %d ms", args->port, i * 200);
      PostMessageW(window, WM_APP_SERVER_READY, 0, 0);
      delete args;
      return 0;
    }
    if (g_server && WaitForSingleObject(g_server, 0) == WAIT_OBJECT_0) break;
    Sleep(200);
  }
  Log(L"[health] server did NOT answer on port %d", args->port);
  PostMessageW(window, WM_APP_SERVER_FAILED, 0, 0);
  delete args;
  return 0;
}

static bool StartServer(int port, std::wstring* error) {
  std::wstring exeDir = DirName(ExePath());
  // Layout: <install>\VLCFlix.exe, <install>\resources\app\...
  std::wstring root = JoinPath(exeDir, L"resources");
  std::wstring app = JoinPath(root, L"app");
  std::wstring node = JoinPath(root, L"node.exe");
  std::wstring script = JoinPath(app, L"server\\index.js");

  if (!FileExists(node)) { *error = L"Missing runtime: " + node; return false; }
  if (!FileExists(script)) { *error = L"Missing application: " + script; return false; }

  std::wstring dataDir = AppDataDir();

  // The command line goes straight to node.exe (no shell), so quoting only has
  // to survive the Windows argument parser.
  std::wstring cmd = Quote(node) + L" " + Quote(script);

  SetEnvironmentVariableW(L"PORT", std::to_wstring(port).c_str());
  // Loopback only: the API has no authentication and is for this desktop user.
  SetEnvironmentVariableW(L"HOST", L"127.0.0.1");
  SetEnvironmentVariableW(L"NODE_ENV", L"production");
  SetEnvironmentVariableW(L"VLCFLIX_DATA_DIR", dataDir.c_str());
  SetEnvironmentVariableW(L"NODE_NO_WARNINGS", L"1");

  // Send the server's stdout and stderr to a file. Without this, a crash on the
  // user's machine leaves nothing behind to look at.
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
                                logHandle ? TRUE : FALSE, CREATE_NO_WINDOW,
                                nullptr, app.c_str(), &si, &pi);
  if (logHandle) CloseHandle(logHandle);
  if (!started) {
    DWORD code = GetLastError();
    *error = L"Could not start node.exe (error " + std::to_wstring(code) + L")";
    Log(L"[launch] CreateProcess failed, GetLastError=%lu", code);
    return false;
  }
  Log(L"[launch] node.exe started, pid=%lu", pi.dwProcessId);

  // KILL_ON_JOB_CLOSE: the server can never outlive this window, even if the
  // app is killed from Task Manager.
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
    CloseHandle(g_serverJob);  // KILL_ON_JOB_CLOSE cleans up anything left
    g_serverJob = nullptr;
  }
}

// ---------------------------------------------------------------------------
// WebView2
// ---------------------------------------------------------------------------

static void ResizeWebView() {
  if (!g_controller) return;
  RECT bounds;
  GetClientRect(g_window, &bounds);
  g_controller->put_Bounds(bounds);
}

HRESULT STDMETHODCALLTYPE NavigationHandler::Invoke(
    ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) {
  LPWSTR uri = nullptr;
  args->get_Uri(&uri);
  bool internal = uri && (wcsncmp(uri, L"http://127.0.0.1:", 17) == 0 ||
                          wcsncmp(uri, L"http://localhost:", 17) == 0 ||
                          wcsncmp(uri, L"about:", 6) == 0 ||
                          wcsncmp(uri, L"data:", 5) == 0);
  if (!internal && uri) {
    // Anything else belongs in the user's browser, not in the app window.
    ShellExecuteW(nullptr, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL);
    args->put_Cancel(TRUE);
  }
  if (uri) CoTaskMemFree(uri);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE ControllerHandler::Invoke(
    HRESULT result, ICoreWebView2Controller* controller) {
  if (FAILED(result) || !controller) {
    FailAndClose(L"Could not create the WebView2 controller.");
    return S_OK;
  }
  g_controller = controller;
  if (FAILED(g_controller->get_CoreWebView2(&g_webview)) || !g_webview) {
    FailAndClose(L"WebView2 did not expose its browser object.");
    return S_OK;
  }
  ResizeWebView();

  // A desktop app should not get browser affordances.
  ICoreWebView2Settings* settings = nullptr;
  if (SUCCEEDED(g_webview->get_Settings(&settings)) && settings) {
    settings->put_AreDevToolsEnabled(FALSE);
    settings->put_AreDefaultContextMenusEnabled(FALSE);
    settings->put_IsStatusBarEnabled(FALSE);
    settings->put_IsZoomControlEnabled(FALSE);
    settings->Release();
  }

  EventRegistrationToken token;
  g_webview->add_NavigationStarting(new NavigationHandler(), &token);

  g_webview->Navigate(g_baseUrl.c_str());
  return S_OK;
}

HRESULT STDMETHODCALLTYPE EnvironmentHandler::Invoke(
    HRESULT result, ICoreWebView2Environment* environment) {
  if (FAILED(result) || !environment) {
    FailAndClose(
        L"The WebView2 runtime is not available.\n\n"
        L"VLCFlix uses the Edge WebView2 engine, which ships with Windows 10 and 11.\n"
        L"If it is missing, install the Evergreen WebView2 Runtime from Microsoft.");
    return S_OK;
  }
  environment->CreateCoreWebView2Controller(g_window, new ControllerHandler());
  return S_OK;
}

static void InitWebView2() {
  std::wstring loadedFrom;
  PFN_CreateEnvWithOptions create = LoadWebView2Loader(&loadedFrom);
  Log(L"[webview2] loader = %ls", create ? loadedFrom.c_str() : L"(not found)");
  if (!create) {
    FailAndClose(
        L"WebView2Loader.dll could not be loaded.\n\n"
        L"It ships inside resources\\ in the install folder; if antivirus removed it, "
        L"reinstall VLCFlix.");
    return;
  }
  std::wstring dataDir = JoinPath(AppDataDir(), L"webview");
  HRESULT hr = create(nullptr, dataDir.c_str(), nullptr, new EnvironmentHandler());
  if (FAILED(hr)) {
    FailAndClose(L"WebView2 could not be initialised. The Evergreen WebView2 Runtime "
                 L"may not be installed on this machine.");
  }
}

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

static LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_SIZE:
      ResizeWebView();
      return 0;

    case WM_APP_SERVER_READY:
      if (g_loading) ShowWindow(g_loading, SW_HIDE);
      InitWebView2();
      return 0;

    case WM_APP_SERVER_FAILED:
      MessageBoxW(window,
                  L"The VLCFlix background service did not start.\n\n"
                  L"This usually means node.exe was blocked, or a required file is "
                  L"missing from the install folder.\nTry reinstalling, and check whether "
                  L"antivirus quarantined files.",
                  L"VLCFlix could not start", MB_ICONERROR | MB_OK);
      DestroyWindow(window);
      return 0;

    case WM_CLOSE:
      if (g_webview) {
        // Let the page flush any in-flight progress save before teardown.
        g_webview->ExecuteScript(L"window.dispatchEvent(new Event('beforeunload'));",
                                 nullptr);
      }
      DestroyWindow(window);
      return 0;

    case WM_DESTROY:
      if (g_webview) { g_webview->Release(); g_webview = nullptr; }
      if (g_controller) { g_controller->Release(); g_controller = nullptr; }
      StopServer();
      PostQuitMessage(0);
      return 0;

    default:
      return DefWindowProcW(window, message, wParam, lParam);
  }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
  g_instance = instance;

  // Focus the existing window instead of starting a second server.
  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"VLCFlix.SingleInstance");
  if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
    HWND existing = FindWindowW(L"VLCFlixMainWindow", nullptr);
    if (existing) {
      ShowWindow(existing, SW_RESTORE);
      SetForegroundWindow(existing);
    }
    return 0;
  }

  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APPICON));
  wc.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APPICON));
  wc.hbrBackground = CreateSolidBrush(RGB(11, 11, 15));  // matches the app's --bg
  wc.lpszClassName = L"VLCFlixMainWindow";
  RegisterClassExW(&wc);

  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  int width = static_cast<int>((work.right - work.left) * 0.9);
  int height = static_cast<int>((work.bottom - work.top) * 0.9);
  int x = work.left + ((work.right - work.left) - width) / 2;
  int y = work.top + ((work.bottom - work.top) - height) / 2;

  g_window = CreateWindowExW(0, wc.lpszClassName, L"VLCFlix", WS_OVERLAPPEDWINDOW,
                             x, y, width, height, nullptr, nullptr, instance, nullptr);
  if (!g_window) {
    MessageBoxW(nullptr, L"Could not create the application window.", L"VLCFlix",
                MB_ICONERROR);
    return 1;
  }

  g_loading = CreateWindowExW(0, L"STATIC", L"Starting VLCFlix\u2026",
                              WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, width, 40,
                              g_window, nullptr, instance, nullptr);
  SendMessageW(g_loading, WM_SETFONT,
               reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
  RECT client;
  GetClientRect(g_window, &client);
  SetWindowPos(g_loading, nullptr, 0, (client.bottom - 40) / 2, client.right, 40,
               SWP_NOZORDER);

  ShowWindow(g_window, showCommand);
  UpdateWindow(g_window);

  g_logPath = JoinPath(AppDataDir(), L"launcher.log");
  Log(L"\n===== VLCFlix starting =====");
  Log(L"[launch] exe = %ls", ExePath().c_str());

  int port = FindFreePort();
  g_baseUrl = L"http://127.0.0.1:" + std::to_wstring(port) + L"/";
  std::wstring error;
  if (port == 0 || !StartServer(port, &error)) {
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
