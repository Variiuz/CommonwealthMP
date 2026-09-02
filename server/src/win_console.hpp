#pragma once

#ifdef _WIN32

#include <atomic>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <Windows.h>

class WinConsole {
public:
    WinConsole();
    ~WinConsole();

    bool start();
    void stop();
    bool poll(std::string& out);
    void set_title(const std::string& title);
    void set_shutdown_callback(std::function<void()> callback);

    void submit_input();

private:
    void run_ui();
    void run_reader();
    void append_log_line(const std::string& line);
    void trim_log_buffer();
    void resize_controls();
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    std::atomic<bool> running_{ false };
    std::function<void()> shutdown_callback_;

    HWND hwnd_ = nullptr;
    HWND log_edit_ = nullptr;
    HWND input_edit_ = nullptr;

    HANDLE pipe_read_ = INVALID_HANDLE_VALUE;
    int stdout_dup_ = -1;
    int stderr_dup_ = -1;

    std::thread ui_thread_;
    std::thread reader_thread_;

    std::mutex cmd_mutex_;
    std::queue<std::string> cmd_queue_;

    std::mutex log_mutex_;
    std::vector<std::string> log_queue_;
    std::string pending_line_;
};

#endif
