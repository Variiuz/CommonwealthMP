#include "pch.h"
#include "crash.h"

#include "REX/W32/OLE32.h"
#include "REX/W32/SHELL32.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <DbgHelp.h>
#include <Psapi.h>
#ifdef ERROR
#undef ERROR
#endif
#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>

namespace {

constexpr int kNoteCount = 24;
constexpr int kNoteLen = 120;

alignas(64) char g_notes[kNoteCount][kNoteLen]{};
std::atomic<unsigned> g_noteSeq{ 0 };
std::atomic<LONG> g_writing{ 0 };
std::atomic<int> g_userQuit{ 0 };
wchar_t g_logPath[MAX_PATH]{};
wchar_t g_dmpPath[MAX_PATH]{};
LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter{ nullptr };
WNDPROC g_prevWndProc{ nullptr };
HWND g_hookedHwnd{ nullptr };
bool g_installed{ false };

struct SehPack {
	void (*fn)(void*);
	void* ctx;
	unsigned code;
	std::uintptr_t ip;
};

thread_local int g_sehDepth{ 0 };

const char* CodeName(unsigned code)
{
	switch (code) {
	case EXCEPTION_ACCESS_VIOLATION:
		return "ACCESS_VIOLATION";
	case EXCEPTION_IN_PAGE_ERROR:
		return "IN_PAGE_ERROR";
	case EXCEPTION_ILLEGAL_INSTRUCTION:
		return "ILLEGAL_INSTRUCTION";
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
		return "INT_DIVIDE_BY_ZERO";
	case EXCEPTION_STACK_OVERFLOW:
		return "STACK_OVERFLOW";
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		return "ARRAY_BOUNDS";
	case EXCEPTION_PRIV_INSTRUCTION:
		return "PRIV_INSTRUCTION";
	case EXCEPTION_NONCONTINUABLE_EXCEPTION:
		return "NONCONTINUABLE";
	case EXCEPTION_DATATYPE_MISALIGNMENT:
		return "MISALIGNMENT";
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:
		return "FLT_DIVIDE_BY_ZERO";
	case 0xC0000374:
		return "HEAP_CORRUPTION";
	case 0xC0000409:
		return "STACK_BUFFER_OVERRUN";
	case 0x40000015:
		return "FATAL_APP_EXIT";
	case 0xE06D7363:
		return "CPP_EXCEPTION";
	default:
		return "EXCEPTION";
	}
}

LRESULT CALLBACK QuitWndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_CLOSE || msg == WM_DESTROY || msg == WM_QUIT
		|| (msg == WM_SYSCOMMAND && (wp & 0xFFF0) == SC_CLOSE)) {
		g_userQuit.store(1, std::memory_order_release);
	}
	if (g_prevWndProc) {
		return CallWindowProcW(g_prevWndProc, wnd, msg, wp, lp);
	}
	return DefWindowProcW(wnd, msg, wp, lp);
}

bool IsQuitting()
{
	if (g_userQuit.load(std::memory_order_acquire)) {
		return true;
	}
	if (auto* main = RE::Main::GetSingleton()) {
		if (main->quitGame) {
			return true;
		}
	}
	return false;
}

bool IsFatal(unsigned code)
{
	switch (code) {
	case EXCEPTION_ACCESS_VIOLATION:
	case EXCEPTION_IN_PAGE_ERROR:
	case EXCEPTION_ILLEGAL_INSTRUCTION:
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
	case EXCEPTION_STACK_OVERFLOW:
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
	case EXCEPTION_PRIV_INSTRUCTION:
	case EXCEPTION_NONCONTINUABLE_EXCEPTION:
	case EXCEPTION_DATATYPE_MISALIGNMENT:
	case 0xC0000374:
	case 0xC0000409:
	case 0x40000015:
		return true;
	default:
		return false;
	}
}

void WcsCopy(wchar_t* dst, std::size_t cap, const wchar_t* src)
{
	if (!dst || cap == 0) {
		return;
	}
	if (!src) {
		dst[0] = 0;
		return;
	}
	std::size_t n = 0;
	while (n + 1 < cap && src[n]) {
		++n;
	}
	std::wmemcpy(dst, src, n);
	dst[n] = 0;
}

void WcsCat(wchar_t* dst, std::size_t cap, const wchar_t* src)
{
	if (!dst || cap == 0) {
		return;
	}
	const auto have = std::wcslen(dst);
	if (have + 1 >= cap) {
		return;
	}
	WcsCopy(dst + have, cap - have, src);
}

void AppendPath(wchar_t* dst, const wchar_t* name)
{
	const auto n = std::wcslen(dst);
	if (n + 8 >= MAX_PATH) {
		return;
	}
	if (n && dst[n - 1] != L'\\' && dst[n - 1] != L'/') {
		dst[n] = L'\\';
		dst[n + 1] = 0;
	}
	WcsCat(dst, MAX_PATH, name);
}

void ResolvePaths()
{
	wchar_t* known = nullptr;
	const auto hr = REX::W32::SHGetKnownFolderPath(
		REX::W32::FOLDERID_Documents, REX::W32::KF_FLAG_DEFAULT, nullptr, &known);
	if (known && hr == 0) {
		WcsCopy(g_logPath, MAX_PATH, known);
		REX::W32::CoTaskMemFree(known);
		WcsCat(g_logPath, MAX_PATH, L"\\My Games\\");
		const auto folder = F4SE::GetSaveFolderName();
		if (!folder.empty()) {
			char utf8[64]{};
			const auto n = static_cast<int>(std::min(folder.size(), sizeof(utf8) - 1));
			std::memcpy(utf8, folder.data(), static_cast<std::size_t>(n));
			wchar_t wide[64]{};
			MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, 64);
			WcsCat(g_logPath, MAX_PATH, wide);
		} else {
			WcsCat(g_logPath, MAX_PATH, L"Fallout4");
		}
		WcsCat(g_logPath, MAX_PATH, L"\\F4SE");
		CreateDirectoryW(g_logPath, nullptr);
	} else {
		GetModuleFileNameW(reinterpret_cast<HMODULE>(REX::W32::GetCurrentModule()), g_logPath, MAX_PATH);
		auto* slash = std::wcsrchr(g_logPath, L'\\');
		if (slash) {
			slash[1] = 0;
		}
	}
	WcsCopy(g_dmpPath, MAX_PATH, g_logPath);
	AppendPath(g_logPath, L"CommonwealthMP.crash.txt");
	AppendPath(g_dmpPath, L"CommonwealthMP.dmp");
}

void WriteRaw(HANDLE file, const char* s, DWORD n)
{
	if (!file || file == INVALID_HANDLE_VALUE || !s || !n) {
		return;
	}
	DWORD written = 0;
	WriteFile(file, s, n, &written, nullptr);
}

void WriteStr(HANDLE file, const char* s)
{
	if (s) {
		WriteRaw(file, s, static_cast<DWORD>(std::strlen(s)));
	}
}

void WriteHex(HANDLE file, std::uint64_t v)
{
	char buf[32]{};
	const auto n = std::snprintf(buf, sizeof(buf), "%llX", static_cast<unsigned long long>(v));
	if (n > 0) {
		WriteRaw(file, buf, static_cast<DWORD>(n));
	}
}

void WriteDec(HANDLE file, unsigned v)
{
	char buf[16]{};
	const auto n = std::snprintf(buf, sizeof(buf), "%u", v);
	if (n > 0) {
		WriteRaw(file, buf, static_cast<DWORD>(n));
	}
}

void FormatModAddr(char* dst, std::size_t cap, std::uintptr_t addr)
{
	if (!dst || cap == 0) {
		return;
	}
	HMODULE mod = nullptr;
	if (!GetModuleHandleExA(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(addr),
			&mod)
		|| !mod) {
		std::snprintf(dst, cap, "%llX", static_cast<unsigned long long>(addr));
		return;
	}
	char name[MAX_PATH]{};
	GetModuleFileNameA(mod, name, MAX_PATH);
	const char* baseName = name;
	if (const auto* slash = std::strrchr(name, '\\')) {
		baseName = slash + 1;
	}
	MODULEINFO info{};
	GetModuleInformation(GetCurrentProcess(), mod, &info, sizeof(info));
	std::snprintf(
		dst,
		cap,
		"%s+%llX",
		baseName,
		static_cast<unsigned long long>(addr - reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll)));
}

void DescribeAddr(HANDLE file, std::uintptr_t addr)
{
	WriteHex(file, addr);
	char loc[MAX_PATH + 32]{};
	FormatModAddr(loc, sizeof(loc), addr);
	WriteStr(file, " ");
	WriteStr(file, loc);
	WriteStr(file, "\n");
}

const char* LastNote()
{
	const unsigned seq = g_noteSeq.load(std::memory_order_acquire);
	if (seq == 0) {
		return "-";
	}
	return g_notes[(seq - 1) % kNoteCount];
}

void WhyText(char* dst, std::size_t cap, EXCEPTION_POINTERS* ep)
{
	if (!dst || cap == 0) {
		return;
	}
	if (!ep || !ep->ExceptionRecord) {
		std::snprintf(dst, cap, "Fallout 4 stopped with no exception record.");
		return;
	}
	const auto* rec = ep->ExceptionRecord;
	const auto code = rec->ExceptionCode;
	switch (code) {
	case EXCEPTION_ACCESS_VIOLATION: {
		const auto op = rec->NumberParameters >= 1 ? rec->ExceptionInformation[0] : 0;
		const auto target = rec->NumberParameters >= 2 ? rec->ExceptionInformation[1] : 0;
		if (target == 0) {
			std::snprintf(
				dst,
				cap,
				"Null pointer. The game %s address 0 (nothing was there).",
				op == 1 ? "wrote to" : (op == 8 ? "tried to run code at" : "read"));
		} else {
			std::snprintf(
				dst,
				cap,
				"Bad memory %s at %llX. That address is not valid.",
				op == 1 ? "write" : (op == 8 ? "execute" : "read"),
				static_cast<unsigned long long>(target));
		}
		break;
	}
	case EXCEPTION_IN_PAGE_ERROR:
		std::snprintf(dst, cap, "A memory page failed to load from disk (paged-out or missing file).");
		break;
	case EXCEPTION_ILLEGAL_INSTRUCTION:
		std::snprintf(dst, cap, "Illegal instruction. Execution jumped into damaged or non-code memory.");
		break;
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
		std::snprintf(dst, cap, "Integer divide by zero.");
		break;
	case EXCEPTION_STACK_OVERFLOW:
		std::snprintf(dst, cap, "Stack overflow. Likely infinite recursion or a huge local buffer.");
		break;
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		std::snprintf(dst, cap, "Array bounds exceeded.");
		break;
	case 0xC0000374:
		std::snprintf(dst, cap, "Heap corruption. Memory was overwritten before this crash.");
		break;
	case 0xC0000409:
		std::snprintf(dst, cap, "Stack buffer overrun (fast fail / security cookie).");
		break;
	case 0x40000015:
		std::snprintf(dst, cap, "The process aborted (Address Library mismatch or REX::FAIL).");
		break;
	case 0xE06D7363:
		std::snprintf(dst, cap, "Uncaught C++ exception.");
		break;
	default:
		std::snprintf(dst, cap, "Fatal exception %s (%08X).", CodeName(code), code);
		break;
	}
}

bool LaunchGuiReporter(const char* origin)
{
	wchar_t exePath[MAX_PATH]{};
	if (!GetModuleFileNameW(reinterpret_cast<HMODULE>(REX::W32::GetCurrentModule()), exePath, MAX_PATH)) {
		return false;
	}
	auto* slash = std::wcsrchr(exePath, L'\\');
	if (!slash) {
		return false;
	}
	*(slash + 1) = 0;
	WcsCat(exePath, MAX_PATH, L"cmp-reporter.exe");
	if (GetFileAttributesW(exePath) == INVALID_FILE_ATTRIBUTES) {
		return false;
	}

	wchar_t originWide[64]{};
	if (origin && origin[0]) {
		MultiByteToWideChar(CP_UTF8, 0, origin, -1, originWide, 64);
	} else {
		WcsCopy(originWide, 64, L"unknown");
	}

	wchar_t cmd[MAX_PATH * 6]{};
	std::swprintf(
		cmd,
		MAX_PATH * 6,
		L"\"%s\" --crash-txt \"%s\" --crash-dmp \"%s\" --origin %s",
		exePath,
		g_logPath,
		g_dmpPath,
		originWide);

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	const BOOL ok = CreateProcessW(
		nullptr,
		cmd,
		nullptr,
		nullptr,
		FALSE,
		CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS,
		nullptr,
		nullptr,
		&si,
		&pi);
	if (!ok) {
		return false;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return true;
}

void ShowReporter(EXCEPTION_POINTERS* ep, const char* origin)
{
	if (LaunchGuiReporter(origin)) {
		return;
	}

	char why[320]{};
	WhyText(why, sizeof(why), ep);
	char where[MAX_PATH + 32]{};
	std::uintptr_t ip = 0;
	if (ep && ep->ExceptionRecord) {
		ip = reinterpret_cast<std::uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
	} else if (ep && ep->ContextRecord) {
		ip = ep->ContextRecord->Rip;
	}
	FormatModAddr(where, sizeof(where), ip);

	char logUtf8[MAX_PATH * 3]{};
	WideCharToMultiByte(CP_UTF8, 0, g_logPath, -1, logUtf8, static_cast<int>(sizeof(logUtf8)), nullptr, nullptr);

	char body[1400]{};
	std::snprintf(
		body,
		sizeof(body),
		"CommonwealthMP stopped Fallout 4. (or atleast i hope I was the reason!)\n"
		"\n"
		"Why:\n%s\n"
		"\n"
		"Where: %s\n"
		"Doing: %s\n"
		"Origin: %s\n"
		"\n"
		"Full log:\n%s\n"
		"\n"
		"cmp-reporter.exe was not found next to the plugin. Install it or attach the log above.\n",
		why,
		where[0] ? where : "-",
		LastNote(),
		origin ? origin : "-",
		logUtf8[0] ? logUtf8 : "-");

	wchar_t wide[1400]{};
	MultiByteToWideChar(CP_UTF8, 0, body, -1, wide, 1400);
	MessageBoxW(
		nullptr,
		wide,
		L"CommonwealthMP Crash Reporter",
		MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST | MB_TASKMODAL);
}

void WriteNotes(HANDLE file)
{
	WriteStr(file, "notes (newest last):\n");
	const unsigned seq = g_noteSeq.load(std::memory_order_acquire);
	const unsigned n = seq < kNoteCount ? seq : kNoteCount;
	for (unsigned i = 0; i < n; ++i) {
		const unsigned idx = (seq - n + i) % kNoteCount;
		WriteStr(file, "  ");
		WriteStr(file, g_notes[idx]);
		WriteStr(file, "\n");
	}
}

void WriteModules(HANDLE file)
{
	HMODULE mods[384]{};
	DWORD needed = 0;
	if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
		return;
	}
	const auto count = std::min<DWORD>(needed / sizeof(HMODULE), 384);
	WriteStr(file, "modules:\n");
	for (DWORD i = 0; i < count; ++i) {
		char name[MAX_PATH]{};
		GetModuleFileNameA(mods[i], name, MAX_PATH);
		const char* baseName = name;
		if (const auto* slash = std::strrchr(name, '\\')) {
			baseName = slash + 1;
		}
		MODULEINFO info{};
		GetModuleInformation(GetCurrentProcess(), mods[i], &info, sizeof(info));
		WriteStr(file, "  ");
		WriteStr(file, baseName);
		WriteStr(file, " base=");
		WriteHex(file, reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll));
		WriteStr(file, " size=");
		WriteHex(file, info.SizeOfImage);
		WriteStr(file, "\n");
	}
}

void WriteMiniDump(EXCEPTION_POINTERS* ep)
{
	const auto file = CreateFileW(
		g_dmpPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return;
	}
	MINIDUMP_EXCEPTION_INFORMATION info{};
	info.ThreadId = GetCurrentThreadId();
	info.ExceptionPointers = ep;
	info.ClientPointers = FALSE;
	MiniDumpWriteDump(
		GetCurrentProcess(),
		GetCurrentProcessId(),
		file,
		static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo),
		ep ? &info : nullptr,
		nullptr,
		nullptr);
	CloseHandle(file);
}

void WriteCrash(EXCEPTION_POINTERS* ep, const char* origin)
{
	if (IsQuitting()) {
		return;
	}
	if (g_writing.exchange(1, std::memory_order_acq_rel) != 0) {
		return;
	}

	const auto file = CreateFileW(
		g_logPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file != INVALID_HANDLE_VALUE) {
		SYSTEMTIME st{};
		GetLocalTime(&st);
		char header[160]{};
		std::snprintf(
			header,
			sizeof(header),
			"CommonwealthMP crash origin=%s %04u-%02u-%02u %02u:%02u:%02u tid=%lu\n",
			origin ? origin : "-",
			st.wYear,
			st.wMonth,
			st.wDay,
			st.wHour,
			st.wMinute,
			st.wSecond,
			GetCurrentThreadId());
		WriteStr(file, header);
		WriteStr(file, "plugin ");
		WriteStr(file, F4SE::GetPluginVersion().string().c_str());
		WriteStr(file, " f4se ");
		WriteStr(file, F4SE::GetF4SEVersion().string().c_str());
		WriteStr(file, "\n");

		if (ep && ep->ExceptionRecord) {
			const auto code = ep->ExceptionRecord->ExceptionCode;
			WriteStr(file, "code ");
			WriteStr(file, CodeName(code));
			WriteStr(file, " ");
			WriteHex(file, code);
			WriteStr(file, "\naddr ");
			DescribeAddr(file, reinterpret_cast<std::uintptr_t>(ep->ExceptionRecord->ExceptionAddress));
			if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
				WriteStr(file, ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "write " : "read ");
				WriteHex(file, ep->ExceptionRecord->ExceptionInformation[1]);
				WriteStr(file, "\n");
			}
			char why[320]{};
			WhyText(why, sizeof(why), ep);
			WriteStr(file, "why ");
			WriteStr(file, why);
			WriteStr(file, "\n");
		}
		if (ep && ep->ContextRecord) {
			const auto* c = ep->ContextRecord;
			WriteStr(file, "rip ");
			DescribeAddr(file, c->Rip);
			WriteStr(file, "rsp ");
			WriteHex(file, c->Rsp);
			WriteStr(file, " rbp ");
			WriteHex(file, c->Rbp);
			WriteStr(file, " rax ");
			WriteHex(file, c->Rax);
			WriteStr(file, " rcx ");
			WriteHex(file, c->Rcx);
			WriteStr(file, " rdx ");
			WriteHex(file, c->Rdx);
			WriteStr(file, "\n");
		}

		WriteNotes(file);
		WriteStr(file, "stack:\n");
		void* frames[48]{};
		const auto n = CaptureStackBackTrace(0, 48, frames, nullptr);
		for (USHORT i = 0; i < n; ++i) {
			WriteStr(file, "  ");
			DescribeAddr(file, reinterpret_cast<std::uintptr_t>(frames[i]));
		}
		WriteModules(file);
		WriteStr(file, "minidump ");
		char dmpUtf8[MAX_PATH * 3]{};
		WideCharToMultiByte(CP_UTF8, 0, g_dmpPath, -1, dmpUtf8, static_cast<int>(sizeof(dmpUtf8)), nullptr, nullptr);
		WriteStr(file, dmpUtf8);
		WriteStr(file, "\n");
		FlushFileBuffers(file);
		CloseHandle(file);
	}

	WriteMiniDump(ep);
	ShowReporter(ep, origin);
}

LONG WINAPI Veh(EXCEPTION_POINTERS* ep)
{
	if (!ep || !ep->ExceptionRecord || !IsFatal(ep->ExceptionRecord->ExceptionCode)) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
		// SEH probes (e.g. steam_rich_probe) fault on purpose; let __try/__except handle them.
	if (g_sehDepth > 0) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	WriteCrash(ep, "veh");
	return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI Unhandled(EXCEPTION_POINTERS* ep)
{
	WriteCrash(ep, "unhandled");
	if (g_prevFilter) {
		return g_prevFilter(ep);
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

void OnTerminate()
{
	CONTEXT ctx{};
	RtlCaptureContext(&ctx);
	EXCEPTION_RECORD rec{};
	rec.ExceptionCode = 0xE06D7363;
	rec.ExceptionAddress = reinterpret_cast<PVOID>(ctx.Rip);
	EXCEPTION_POINTERS ep{ &rec, &ctx };
	WriteCrash(&ep, "terminate");
}

void OnAbort(int)
{
	CONTEXT ctx{};
	RtlCaptureContext(&ctx);
	EXCEPTION_RECORD rec{};
	rec.ExceptionCode = 0x40000015;
	rec.ExceptionAddress = reinterpret_cast<PVOID>(ctx.Rip);
	EXCEPTION_POINTERS ep{ &rec, &ctx };
	WriteCrash(&ep, "abort");
}

int SehFilter(EXCEPTION_POINTERS* ep, SehPack* pack)
{
	if (ep && ep->ExceptionRecord) {
		pack->code = ep->ExceptionRecord->ExceptionCode;
	}
	if (ep && ep->ContextRecord) {
		pack->ip = ep->ContextRecord->Rip;
	}
	return EXCEPTION_EXECUTE_HANDLER;
}

void SehRun(SehPack* pack)
{
	++g_sehDepth;
	__try {
		pack->fn(pack->ctx);
	} __except (SehFilter(GetExceptionInformation(), pack)) {
	}
	
}

}  // namespace

void CMP_CrashNote(const char* what)
{
	if (!what) {
		return;
	}
	const auto i = g_noteSeq.fetch_add(1, std::memory_order_relaxed) % kNoteCount;
	const auto n = std::min(std::strlen(what), static_cast<std::size_t>(kNoteLen - 1));
	std::memcpy(g_notes[i], what, n);
	g_notes[i][n] = 0;
}

bool CMP_SehCall(const char* what, void (*fn)(void*), void* ctx)
{
	if (!fn) {
		return false;
	}
	CMP_CrashNote(what);
	SehPack pack{ fn, ctx, 0, 0 };
	SehRun(&pack);
	if (pack.code) {
		char line[160]{};
		std::snprintf(
			line,
			sizeof(line),
			"SEH %s %s %08X ip=%llX",
			what ? what : "-",
			CodeName(pack.code),
			pack.code,
			static_cast<unsigned long long>(pack.ip));
		REX::ERROR("{}", line);
		CMP_CrashNote(line);
		return false;
	}
	return true;
}

void CMP_WatchQuit()
{
	if (g_hookedHwnd || g_userQuit.load(std::memory_order_acquire)) {
		return;
	}
	auto* main = RE::Main::GetSingleton();
	if (!main || !main->hwnd) {
		return;
	}
	g_hookedHwnd = reinterpret_cast<HWND>(main->hwnd);
	g_prevWndProc = reinterpret_cast<WNDPROC>(
		SetWindowLongPtrW(g_hookedHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(QuitWndProc)));
}

void CMP_InstallCrashHandler()
{
	if (g_installed) {
		return;
	}
	g_installed = true;
	ResolvePaths();
	AddVectoredExceptionHandler(1, Veh);
	g_prevFilter = SetUnhandledExceptionFilter(Unhandled);
	std::set_terminate(OnTerminate);
	std::signal(SIGABRT, OnAbort);
	char utf8[MAX_PATH * 3]{};
	WideCharToMultiByte(CP_UTF8, 0, g_logPath, -1, utf8, static_cast<int>(sizeof(utf8)), nullptr, nullptr);
	REX::INFO("crash handler {}", utf8);
}
