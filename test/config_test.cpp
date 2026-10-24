#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <algorithm>
#include "config_defaults.h"
#include <string>
#include <vector>
#include "config.h"

namespace {
unsigned failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); ++failures; } } while (0)
std::wstring directory;
std::wstring path(const wchar_t* name) { return directory + L"\\" + name; }
std::vector<char> bytes(const std::wstring& file) {
    HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::vector<char> data(GetFileSize(h, nullptr));
    DWORD read = 0;
    CHECK(ReadFile(h, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) && read == data.size());
    CloseHandle(h);
    return data;
}
void write(const std::wstring& file, const wchar_t* text) {
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
    CHECK(h != INVALID_HANDLE_VALUE);
    if (h == INVALID_HANDLE_VALUE) return;
    const wchar_t bom = 0xfeff;
    DWORD written;
    CHECK(WriteFile(h, &bom, sizeof(bom), &written, nullptr));
    const DWORD size = static_cast<DWORD>(wcslen(text) * sizeof(wchar_t));
    CHECK(WriteFile(h, text, size, &written, nullptr) && written == size);
    CloseHandle(h);
}
std::wstring value(const std::wstring& file, const wchar_t* key, const wchar_t* section = L"Display") {
    wchar_t text[256];
    GetPrivateProfileStringW(section, key, L"<missing>", text, 256, file.c_str());
    return text;
}
void missingAndReadOnlyLoad() {
    auto file = path(L"missing.ini");
    auto c = dm::loadConfigFile(file.c_str(), false);
    CHECK(GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES);
    CHECK(c.fullscreen == 0 && c.borderlessFullscreen == 0 && c.dynamicUpload == 1 && c.vsync == 0);
    c = dm::loadConfigFile(file.c_str(), true);
    CHECK(bytes(file) == std::vector<char>(dm::kDefaultIni, dm::kDefaultIni + sizeof(dm::kDefaultIni) - 1));
    CHECK(value(file, L"DynamicUpload") == L"Auto" && value(file, L"VSync") == L"Auto");
    CHECK(value(file, L"FullscreenMode") == L"Exclusive" && value(file, L"Fullscreen") == L"0");
    CHECK(c.dynamicUpload == 1 && c.vsync == 0 && c.fullscreen == 0);
    const auto before = bytes(file);
    dm::loadConfigFile(file.c_str(), true);
    CHECK(bytes(file) == before);
}
void preserveOverridesAndUnicode() {
    const auto file = path(L"\u65e5\u672c\u8a9e.ini");
    write(file, L"; user comment\r\n[Display]\r\nFullscreenMode=Borderless\r\nVSync=1\r\nDynamicUpload=0\r\nFilter=Point\r\nCustomWidth=800\r\nUnknown=keep\r\n[Private]\r\nLabel=\u65e5\u672c\u8a9e\r\n");
    const auto c = dm::loadConfigFile(file.c_str(), true);
    CHECK(c.borderlessFullscreen == 1 && c.vsync == 1 && c.dynamicUpload == 0);
    CHECK(c.filter == 1 && c.customWidth == 800);
    CHECK(value(file, L"Unknown") == L"keep" && value(file, L"Label", L"Private") == L"\u65e5\u672c\u8a9e");
    const auto contents = bytes(file);
    const wchar_t comment[] = L"; user comment";
    const char* first = reinterpret_cast<const char*>(comment);
    CHECK(std::search(contents.begin(), contents.end(), first, first + sizeof(comment) - sizeof(wchar_t)) != contents.end());
    CHECK(value(file, L"TraceFrames") == L"0" && value(file, L"Enabled") == L"1");
    const auto before = bytes(file);
    dm::loadConfigFile(file.c_str(), true);
    CHECK(bytes(file) == before);
}
void malformedValues() {
    const auto file = path(L"invalid.ini");
    write(file, L"[Display]\r\nEnabled=maybe\r\nFullscreenMode=\r\nVSync=abc\r\nDynamicUpload=-9\r\nMode=bad\r\nFilter=bad\r\nCustomWidth=0\r\nSharpness=99\r\nTraceFrames=999999999999999999999\r\nBackgroundColor=1234567\r\nLogEvery=-1\r\n");
    const auto c = dm::loadConfigFile(file.c_str(), true);
    CHECK(c.enabled == 1 && c.borderlessFullscreen == 0 && c.vsync == 0 && c.dynamicUpload == 1);
    CHECK(c.mode == dm::ScaleMode::Fit && c.filter == 0 && c.customWidth == 640);
    CHECK(c.sharpness == 20 && c.traceFrames == 0 && c.logEvery == 0 && c.backgroundColor == 0xff000000);
    CHECK(value(file, L"VSync") == L"Auto" && value(file, L"Sharpness") == L"20");
}
void numericCompatibility() {
    const auto file = path(L"numbers.ini");
    write(file, L"[Display]\r\nVSync=-1\r\nCustomWidth=0x320\r\nSharpness=+25\r\nFullscreenMode=0\r\n");
    const auto c = dm::loadConfigFile(file.c_str(), true);
    CHECK(c.vsync == 0 && c.customWidth == 800 && c.sharpness == 25 && c.borderlessFullscreen == 0);
    CHECK(value(file, L"VSync") == L"-1" && value(file, L"CustomWidth") == L"0x320");
    const auto longFile = path(L"long.ini");
    const std::wstring longValue = L"[Display]\r\nSharpness=000000000000000000000000000000000000000000000000000000000000000251\r\n";
    write(longFile, longValue.c_str());
    CHECK(dm::loadConfigFile(longFile.c_str(), false).sharpness == 20);
    CHECK(dm::loadConfigFile(longFile.c_str(), true).sharpness == 20);
    CHECK(value(longFile, L"Sharpness") == L"20");
}
void protectedFile() {
    const auto file = path(L"protected.ini");
    write(file, L"[Display]\r\nVSync=1\r\nFilter=Point\r\n");
    const auto before = bytes(file);
    CHECK(SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_READONLY));
    const auto c = dm::loadConfigFile(file.c_str(), true);
    CHECK(c.vsync == 1 && c.filter == 1 && c.dynamicUpload == 1);
    CHECK(bytes(file) == before);
    CHECK(SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL));
}
void absentDirectory() {
    const auto file = path(L"not-created\\DisplayManager.ini");
    const auto c = dm::loadConfigFile(file.c_str(), true);
    CHECK(c.enabled == 1 && c.dynamicUpload == 1 && c.vsync == 0);
    CHECK(GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES);
}
}
namespace dm { namespace log { void write(const char*, ...) {} } }
int main() {
    wchar_t temp[MAX_PATH], unique[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, temp) || !GetTempFileNameW(temp, L"dmc", 0, unique)) return 2;
    // This path was just created by GetTempFileName for this test process.
    if (!DeleteFileW(unique) || !CreateDirectoryW(unique, nullptr)) return 2;
    directory = unique;
    missingAndReadOnlyLoad();
    preserveOverridesAndUnicode();
    malformedValues();
    numericCompatibility();
    protectedFile();
    absentDirectory();
    std::printf("config files: %u failure(s)\n", failures);
    return failures ? 1 : 0;
}
