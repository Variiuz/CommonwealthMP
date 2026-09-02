#include "win_console.hpp"

#ifdef _WIN32

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <io.h>
#include <fcntl.h>

namespace {

constexpr UINT_PTR TIMER_ID = 1;

constexpr int ID_LOG_EDIT = 100;
constexpr int ID_INPUT_EDIT = 101;
constexpr int ID_SEND_BUTTON = 102;

constexpr std::size_t kMaxLogLines = 2000;

WNDPROC g_orig_input_proc = nullptr;

LRESULT CALLBACK input_edit_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_KEYDOWN && wparam == VK_RETURN) {
        auto* self = reinterpret_cast<WinConsole*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (self != nullptr) {
            self->submit_input();
        }
        return 0;
    }
    if (g_orig_input_proc != nullptr) {
        return CallWindowProc(g_orig_input_proc, hwnd, msg, wparam, lparam);
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

}  // namespace

WinConsole::WinConsole()
{
}

WinConsole::~WinConsole()
{
    stop();
}

bool WinConsole::start()
{
    if (running_.exchange(true)) {
        return true;
    }

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE read_handle = INVALID_HANDLE_VALUE;
    HANDLE write_handle = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&read_handle, &write_handle, &sa, 0)) {
        running_ = false;
        return false;
    }

    const int stdout_fd = _fileno(stdout);
    const int stderr_fd = _fileno(stderr);
    stdout_dup_ = _dup(stdout_fd);
    stderr_dup_ = _dup(stderr_fd);

    const int pipe_fd = _open_osfhandle(reinterpret_cast<intptr_t>(write_handle), _O_TEXT);
    if (pipe_fd == -1) {
        CloseHandle(read_handle);
        _close(stdout_dup_);
        _close(stderr_dup_);
        stdout_dup_ = -1;
        stderr_dup_ = -1;
        running_ = false;
        return false;
    }

    _dup2(pipe_fd, stdout_fd);
    _dup2(pipe_fd, stderr_fd);
    _close(pipe_fd);

    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    pipe_read_ = read_handle;

    if (HWND wnd = GetConsoleWindow(); wnd != nullptr) {
        ShowWindow(wnd, SW_HIDE);
    }
    FreeConsole();

    ui_thread_ = std::thread(&WinConsole::run_ui, this);
    reader_thread_ = std::thread(&WinConsole::run_reader, this);

    return true;
}

void WinConsole::stop()
{
    if (!running_.exchange(false)) {
        return;
    }

    if (stdout_dup_ != -1) {
        _dup2(stdout_dup_, _fileno(stdout));
        _close(stdout_dup_);
        stdout_dup_ = -1;
    }
    if (stderr_dup_ != -1) {
        _dup2(stderr_dup_, _fileno(stderr));
        _close(stderr_dup_);
        stderr_dup_ = -1;
    }

    if (pipe_read_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_read_);
        pipe_read_ = INVALID_HANDLE_VALUE;
    }

    if (hwnd_ != nullptr) {
        PostMessage(hwnd_, WM_CLOSE, 0, 0);
    }

    if (ui_thread_.joinable()) {
        ui_thread_.join();
    }
    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }
}

bool WinConsole::poll(std::string& out)
{
    std::lock_guard<std::mutex> lock(cmd_mutex_);
    if (cmd_queue_.empty()) {
        return false;
    }
    out = std::move(cmd_queue_.front());
    cmd_queue_.pop();
    return true;
}

void WinConsole::set_title(const std::string& title)
{
    if (hwnd_ == nullptr) {
        return;
    }
    std::wstring wide;
    wide.reserve(title.size());
    for (const char ch : title) {
        wide.push_back(static_cast<wchar_t>(ch));
    }
    SetWindowTextW(hwnd_, wide.c_str());
}

void WinConsole::set_shutdown_callback(std::function<void()> callback)
{
    shutdown_callback_ = std::move(callback);
}

void WinConsole::submit_input()
{
    if (input_edit_ == nullptr) {
        return;
    }
    char buffer[1024] = {};
    GetWindowTextA(input_edit_, buffer, sizeof(buffer) - 1);
    SetWindowTextA(input_edit_, "");
    std::string line(buffer);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
        line.pop_back();
    }
    if (line.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(cmd_mutex_);
    cmd_queue_.push(std::move(line));
}

void WinConsole::run_ui()
{
    HINSTANCE instance = GetModuleHandle(nullptr);

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconA(instance, "IDI_ICON1");
    wc.hIconSm = LoadIconA(instance, "IDI_ICON1");
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = "CMPWinConsole";
    if (RegisterClassEx(&wc) == 0) {
        return;
    }

    hwnd_ = CreateWindowExA(
        0,
        "CMPWinConsole",
        "CommonwealthMP Server",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        900,
        600,
        nullptr,
        nullptr,
        instance,
        this);
    if (hwnd_ == nullptr) {
        return;
    }

    log_edit_ = CreateWindowExA(
        WS_EX_CLIENTEDGE,
        "EDIT",
        "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
        10, 10, 860, 480,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_LOG_EDIT)),
        instance,
        nullptr);

    input_edit_ = CreateWindowExA(
        WS_EX_CLIENTEDGE,
        "EDIT",
        "",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        10, 500, 760, 24,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_INPUT_EDIT)),
        instance,
        nullptr);

    CreateWindowA(
        "BUTTON",
        "Send",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        780, 500, 90, 24,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_SEND_BUTTON)),
        instance,
        nullptr);

    if (log_edit_ != nullptr) {
        HFONT font = static_cast<HFONT>(GetStockObject(ANSI_FIXED_FONT));
        SendMessage(log_edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    if (input_edit_ != nullptr) {
        HFONT font = static_cast<HFONT>(GetStockObject(ANSI_FIXED_FONT));
        SendMessage(input_edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SetWindowLongPtr(input_edit_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        g_orig_input_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(input_edit_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(input_edit_proc)));
    }

    resize_controls();
    SetTimer(hwnd_, TIMER_ID, 50, nullptr);
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    hwnd_ = nullptr;
    log_edit_ = nullptr;
    input_edit_ = nullptr;
    g_orig_input_proc = nullptr;
}

void WinConsole::run_reader()
{
    char buffer[4096];
    DWORD bytes_read = 0;

    while (running_) {
        BOOL ok = ReadFile(pipe_read_, buffer, sizeof(buffer) - 1, &bytes_read, nullptr);
        if (!ok || bytes_read == 0) {
            break;
        }
        buffer[bytes_read] = '\0';

        std::lock_guard<std::mutex> lock(log_mutex_);
        pending_line_ += buffer;

        std::size_t pos = 0;
        while (true) {
            const std::size_t nl = pending_line_.find('\n', pos);
            if (nl == std::string::npos) {
                break;
            }
            std::string line = pending_line_.substr(pos, nl - pos);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            log_queue_.push_back(std::move(line));
            pos = nl + 1;
        }
        if (pos < pending_line_.size()) {
            pending_line_ = pending_line_.substr(pos);
        } else {
            pending_line_.clear();
        }
    }
}

void WinConsole::append_log_line(const std::string& line)
{
    if (log_edit_ == nullptr) {
        return;
    }

    const std::size_t len = GetWindowTextLengthA(log_edit_);
    SendMessageA(log_edit_, EM_SETSEL, static_cast<WPARAM>(len), static_cast<LPARAM>(len));
    if (len > 0) {
        SendMessageA(log_edit_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>("\r\n"));
    }
    SendMessageA(log_edit_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
    trim_log_buffer();
    SendMessageA(log_edit_, EM_SCROLLCARET, 0, 0);
}

void WinConsole::trim_log_buffer()
{
    if (log_edit_ == nullptr) {
        return;
    }
    const std::size_t count = static_cast<std::size_t>(SendMessageA(log_edit_, EM_GETLINECOUNT, 0, 0));
    if (count <= kMaxLogLines) {
        return;
    }
    const std::size_t remove = count - kMaxLogLines;
    const LRESULT first_index = SendMessageA(log_edit_, EM_LINEINDEX, static_cast<WPARAM>(remove), 0);
    if (first_index >= 0) {
        SendMessageA(log_edit_, EM_SETSEL, 0, first_index + 1);
        SendMessageA(log_edit_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(""));
    }
}

void WinConsole::resize_controls()
{
    if (hwnd_ == nullptr) {
        return;
    }
    RECT client = {};
    GetClientRect(hwnd_, &client);
    const int w = client.right - client.left;
    const int h = client.bottom - client.top;

    if (log_edit_ != nullptr) {
        SetWindowPos(log_edit_, nullptr, 10, 10, w - 20, h - 60, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (input_edit_ != nullptr) {
        SetWindowPos(input_edit_, nullptr, 10, h - 40, w - 120, 24, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    HWND send_btn = GetDlgItem(hwnd_, ID_SEND_BUTTON);
    if (send_btn != nullptr) {
        SetWindowPos(send_btn, nullptr, w - 100, h - 40, 90, 24, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

LRESULT CALLBACK WinConsole::wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    WinConsole* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lparam);
        self = reinterpret_cast<WinConsole*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<WinConsole*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    switch (msg) {
    case WM_CREATE:
        return 0;
    case WM_SIZE:
        if (self != nullptr) {
            self->resize_controls();
        }
        return 0;
    case WM_TIMER:
        if (wparam == TIMER_ID && self != nullptr) {
            std::vector<std::string> lines;
            {
                std::lock_guard<std::mutex> lock(self->log_mutex_);
                lines.swap(self->log_queue_);
            }
            for (const auto& line : lines) {
                self->append_log_line(line);
            }
        }
        return 0;
    case WM_COMMAND:
        if (self != nullptr && HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) == ID_SEND_BUTTON) {
            self->submit_input();
            return 0;
        }
        return 0;
    case WM_CLOSE:
        if (self != nullptr && self->shutdown_callback_) {
            self->shutdown_callback_();
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hwnd, msg, wparam, lparam);
    }
}

#endif
