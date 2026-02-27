#define IOSB_EXPORTS
#include "ios_device_bridge.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct idevice_private;
struct lockdownd_client_private;
struct lockdownd_service_descriptor_private;
struct afc_client_private;

using idevice_t = idevice_private*;
using lockdownd_client_t = lockdownd_client_private*;
using lockdownd_service_descriptor_t = lockdownd_service_descriptor_private*;
using afc_client_t = afc_client_private*;

constexpr const char* kBackendVersion = "ios-device-bridge/0.2.0-libimobiledevice";
constexpr const char* kAfcServiceName = "com.apple.afc";
constexpr int64_t kAfcModeReadOnly = 1;
constexpr int64_t kAfcModeWriteOnly = 3;
constexpr uint32_t kChunkSize = 64 * 1024;
constexpr const char* kLibIdeviceCandidates[] = {
    "libimobiledevice-1.0.dll",
    "imobiledevice.dll"
};
constexpr const char* kKnownRuntimeDeps[] = {
    "libplist-2.0.dll",
    "libusbmuxd-2.0.dll",
    "libssl-3-x64.dll",
    "libcrypto-3-x64.dll",
    "zlib1.dll"
};

thread_local std::string g_last_error;
std::mutex g_mutex;
int g_next_handle = 1;

struct DeviceSession {
    std::string udid;
    idevice_t device = nullptr;
    afc_client_t afc = nullptr;
};

std::unordered_map<int, DeviceSession> g_open_handles;

int64_t now_unix() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

void set_error(const std::string& message) {
    g_last_error = message.empty() ? "Unknown error" : message;
}

void set_error(const char* message) {
    g_last_error = (message != nullptr && message[0] != '\0') ? message : "Unknown error";
}

bool copy_text(char* target, int target_size, const std::string& src) {
    if (target == nullptr || target_size <= 0) {
        return false;
    }
    const int size = static_cast<int>(src.size());
    if (size >= target_size) {
        std::memcpy(target, src.c_str(), target_size - 1);
        target[target_size - 1] = '\0';
        return false;
    }
    std::memcpy(target, src.c_str(), size);
    target[size] = '\0';
    return true;
}

std::string win32_error_message(DWORD code) {
    if (code == 0) {
        return "no error";
    }

    LPSTR buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD n = FormatMessageA(flags, nullptr, code, 0, reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    if (n == 0 || buffer == nullptr) {
        return "code " + std::to_string(static_cast<unsigned long long>(code));
    }

    std::string text(buffer, n);
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) {
        text.pop_back();
    }
    LocalFree(buffer);
    return text;
}

bool find_dll_on_search_path(const char* name, std::string* out_full_path) {
    char buffer[MAX_PATH] = {};
    const DWORD n = SearchPathA(nullptr, name, nullptr, MAX_PATH, buffer, nullptr);
    if (n == 0 || n >= MAX_PATH) {
        return false;
    }
    if (out_full_path != nullptr) {
        *out_full_path = buffer;
    }
    return true;
}

void append_line(std::string& s, const std::string& line) {
    s.append(line);
    s.push_back('\n');
}

std::string build_runtime_diagnostics() {
    std::string out;
    append_line(out, "libimobiledevice runtime diagnostics:");

    bool found_candidate = false;
    for (const char* candidate : kLibIdeviceCandidates) {
        std::string full_path;
        if (find_dll_on_search_path(candidate, &full_path)) {
            found_candidate = true;
            append_line(out, std::string("  FOUND  ") + candidate + " -> " + full_path);
        } else {
            append_line(out, std::string("  MISSING ") + candidate);
        }
    }

    for (const char* dep : kKnownRuntimeDeps) {
        std::string full_path;
        if (find_dll_on_search_path(dep, &full_path)) {
            append_line(out, std::string("  FOUND  ") + dep + " -> " + full_path);
        } else {
            append_line(out, std::string("  MISSING ") + dep);
        }
    }

    HMODULE m = nullptr;
    bool loaded_ok = false;
    DWORD last_err = 0;
    for (const char* candidate : kLibIdeviceCandidates) {
        SetLastError(0);
        m = LoadLibraryA(candidate);
        last_err = GetLastError();
        if (m != nullptr) {
            append_line(out, std::string("  LOAD OK ") + candidate);
            loaded_ok = true;
            FreeLibrary(m);
            break;
        }
    }

    if (!loaded_ok) {
        append_line(out, std::string("  LOAD FAILED: ") + win32_error_message(last_err) + " (code " + std::to_string(last_err) + ")");
    }

    if (!found_candidate) {
        append_line(out, "Hint: copy runtime DLLs next to ios_device_bridge.dll or add their folder to PATH.");
    }

    return out;
}

std::string hint_for_idevice_rc(int rc) {
    switch (rc) {
        case -3:
            return "No device found. Check USB cable and unlock the device.";
        case -5:
            return "SSL handshake failed. Re-trust this computer on the device.";
        case -6:
            return "Device not paired. Pair/trust the device from Windows/iTunes stack.";
        case -8:
            return "Connection timeout. Retry with the device unlocked.";
        case -9:
            return "Could not connect to lockdownd. Ensure Apple Mobile Device Support is installed.";
        default:
            return "Check Apple Mobile Device Support, usbmuxd stack, and trust pairing.";
    }
}

std::string join_path(const std::string& base, const std::string& name) {
    if (base.empty() || base == "/") {
        return std::string("/") + name;
    }
    if (base.back() == '/') {
        return base + name;
    }
    return base + "/" + name;
}

std::string normalize_path(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return "/";
    }
    std::string p(path);
    std::replace(p.begin(), p.end(), '\\', '/');
    if (p.empty() || p[0] != '/') {
        p.insert(p.begin(), '/');
    }
    while (p.size() > 1 && p.back() == '/') {
        p.pop_back();
    }
    return p;
}

const char* dict_value(char** dict, const char* key) {
    if (dict == nullptr || key == nullptr) {
        return nullptr;
    }
    for (int i = 0; dict[i] != nullptr && dict[i + 1] != nullptr; i += 2) {
        if (std::strcmp(dict[i], key) == 0) {
            return dict[i + 1];
        }
    }
    return nullptr;
}

uint64_t parse_u64(const char* value, uint64_t fallback = 0) {
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    char* end = nullptr;
    const unsigned long long out = std::strtoull(value, &end, 10);
    if (end == value) {
        return fallback;
    }
    return static_cast<uint64_t>(out);
}

int64_t parse_i64(const char* value, int64_t fallback = 0) {
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    char* end = nullptr;
    const long long out = std::strtoll(value, &end, 10);
    if (end == value) {
        return fallback;
    }
    return static_cast<int64_t>(out);
}

bool dict_is_directory(char** dict) {
    const char* ifmt = dict_value(dict, "st_ifmt");
    if (ifmt != nullptr && std::strcmp(ifmt, "S_IFDIR") == 0) {
        return true;
    }
    const char* st_size = dict_value(dict, "st_size");
    const char* st_blocks = dict_value(dict, "st_blocks");
    if (ifmt != nullptr && std::strcmp(ifmt, "S_IFREG") == 0) {
        return false;
    }
    return st_size != nullptr && std::strcmp(st_size, "0") == 0 && st_blocks != nullptr && std::strcmp(st_blocks, "0") == 0;
}

struct Entry {
    std::string path;
    std::string name;
    bool is_directory = false;
    uint64_t size_bytes = 0;
    int64_t modified_unix = 0;
};

class LibIdeviceApi {
public:
    using fn_idevice_get_device_list = int (*)(char***, int*);
    using fn_idevice_device_list_free = int (*)(char**);
    using fn_idevice_new = int (*)(idevice_t*, const char*);
    using fn_idevice_free = int (*)(idevice_t);

    using fn_lockdownd_client_new_with_handshake = int (*)(idevice_t, lockdownd_client_t*, const char*);
    using fn_lockdownd_client_free = int (*)(lockdownd_client_t);
    using fn_lockdownd_get_device_name = int (*)(lockdownd_client_t, char**);
    using fn_lockdownd_start_service = int (*)(lockdownd_client_t, const char*, lockdownd_service_descriptor_t*);
    using fn_lockdownd_service_descriptor_free = int (*)(lockdownd_service_descriptor_t);

    using fn_afc_client_new = int (*)(idevice_t, lockdownd_service_descriptor_t, afc_client_t*);
    using fn_afc_client_free = int (*)(afc_client_t);
    using fn_afc_read_directory = int (*)(afc_client_t, const char*, char***);
    using fn_afc_dictionary_free = int (*)(char**);
    using fn_afc_get_file_info = int (*)(afc_client_t, const char*, char***);
    using fn_afc_file_open = int (*)(afc_client_t, const char*, uint64_t, uint64_t*);
    using fn_afc_file_close = int (*)(afc_client_t, uint64_t);
    using fn_afc_file_read = int (*)(afc_client_t, uint64_t, char*, uint32_t, uint32_t*);
    using fn_afc_file_write = int (*)(afc_client_t, uint64_t, const char*, uint32_t, uint32_t*);

    bool ensure_loaded() {
        if (loaded_) {
            return true;
        }

        for (const char* dll_name : kLibIdeviceCandidates) {
            module_ = LoadLibraryA(dll_name);
            if (module_ != nullptr) {
                break;
            }
        }

        if (module_ == nullptr) {
            set_error(build_runtime_diagnostics());
            return false;
        }

        if (!load_all_symbols()) {
            FreeLibrary(module_);
            module_ = nullptr;
            return false;
        }

        loaded_ = true;
        return true;
    }

    fn_idevice_get_device_list idevice_get_device_list = nullptr;
    fn_idevice_device_list_free idevice_device_list_free = nullptr;
    fn_idevice_new idevice_new = nullptr;
    fn_idevice_free idevice_free = nullptr;

    fn_lockdownd_client_new_with_handshake lockdownd_client_new_with_handshake = nullptr;
    fn_lockdownd_client_free lockdownd_client_free = nullptr;
    fn_lockdownd_get_device_name lockdownd_get_device_name = nullptr;
    fn_lockdownd_start_service lockdownd_start_service = nullptr;
    fn_lockdownd_service_descriptor_free lockdownd_service_descriptor_free = nullptr;

    fn_afc_client_new afc_client_new = nullptr;
    fn_afc_client_free afc_client_free = nullptr;
    fn_afc_read_directory afc_read_directory = nullptr;
    fn_afc_dictionary_free afc_dictionary_free = nullptr;
    fn_afc_get_file_info afc_get_file_info = nullptr;
    fn_afc_file_open afc_file_open = nullptr;
    fn_afc_file_close afc_file_close = nullptr;
    fn_afc_file_read afc_file_read = nullptr;
    fn_afc_file_write afc_file_write = nullptr;

private:
    template <typename T>
    bool load_symbol(T& fn, const char* symbol) {
        fn = reinterpret_cast<T>(GetProcAddress(module_, symbol));
        if (fn == nullptr) {
            set_error(std::string("Missing symbol in libimobiledevice runtime: ") + symbol);
            return false;
        }
        return true;
    }

    bool load_all_symbols() {
        return load_symbol(idevice_get_device_list, "idevice_get_device_list") &&
               load_symbol(idevice_device_list_free, "idevice_device_list_free") &&
               load_symbol(idevice_new, "idevice_new") &&
               load_symbol(idevice_free, "idevice_free") &&
               load_symbol(lockdownd_client_new_with_handshake, "lockdownd_client_new_with_handshake") &&
               load_symbol(lockdownd_client_free, "lockdownd_client_free") &&
               load_symbol(lockdownd_get_device_name, "lockdownd_get_device_name") &&
               load_symbol(lockdownd_start_service, "lockdownd_start_service") &&
               load_symbol(lockdownd_service_descriptor_free, "lockdownd_service_descriptor_free") &&
               load_symbol(afc_client_new, "afc_client_new") &&
               load_symbol(afc_client_free, "afc_client_free") &&
               load_symbol(afc_read_directory, "afc_read_directory") &&
               load_symbol(afc_dictionary_free, "afc_dictionary_free") &&
               load_symbol(afc_get_file_info, "afc_get_file_info") &&
               load_symbol(afc_file_open, "afc_file_open") &&
               load_symbol(afc_file_close, "afc_file_close") &&
               load_symbol(afc_file_read, "afc_file_read") &&
               load_symbol(afc_file_write, "afc_file_write");
    }

    HMODULE module_ = nullptr;
    bool loaded_ = false;
};

LibIdeviceApi& api() {
    static LibIdeviceApi instance;
    return instance;
}

void close_session(DeviceSession& session) {
    auto& a = api();
    if (session.afc != nullptr) {
        a.afc_client_free(session.afc);
        session.afc = nullptr;
    }
    if (session.device != nullptr) {
        a.idevice_free(session.device);
        session.device = nullptr;
    }
}

bool create_afc_session(const char* udid, DeviceSession& out) {
    auto& a = api();
    if (!a.ensure_loaded()) {
        return false;
    }

    idevice_t device = nullptr;
    if (a.idevice_new(&device, udid) != 0 || device == nullptr) {
        set_error("Failed to connect to iOS device. Verify the device is connected and trusted.");
        return false;
    }

    lockdownd_client_t lockdown = nullptr;
    if (a.lockdownd_client_new_with_handshake(device, &lockdown, "ios-browser") != 0 || lockdown == nullptr) {
        a.idevice_free(device);
        set_error("Failed to start lockdownd handshake. Unlock and trust this PC on the device.");
        return false;
    }

    lockdownd_service_descriptor_t service = nullptr;
    if (a.lockdownd_start_service(lockdown, kAfcServiceName, &service) != 0 || service == nullptr) {
        a.lockdownd_client_free(lockdown);
        a.idevice_free(device);
        set_error("Failed to start AFC service on device.");
        return false;
    }

    afc_client_t afc = nullptr;
    if (a.afc_client_new(device, service, &afc) != 0 || afc == nullptr) {
        a.lockdownd_service_descriptor_free(service);
        a.lockdownd_client_free(lockdown);
        a.idevice_free(device);
        set_error("Failed to initialize AFC client.");
        return false;
    }

    a.lockdownd_service_descriptor_free(service);
    a.lockdownd_client_free(lockdown);

    out.udid = udid != nullptr ? udid : "";
    out.device = device;
    out.afc = afc;
    return true;
}

std::string resolve_device_name(const char* udid) {
    // Avoid lockdownd_get_device_name allocation/free ownership issues on Windows.
    // Use UDID as display name for stability.
    return std::string(udid != nullptr ? udid : "Unknown iOS Device");
}

bool read_remote_file_to_local(afc_client_t afc, const char* remote_path, const char* local_path) {
    auto& a = api();
    uint64_t handle = 0;
    if (a.afc_file_open(afc, remote_path, kAfcModeReadOnly, &handle) != 0) {
        set_error("Failed to open remote file for reading.");
        return false;
    }

    std::ofstream out(local_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        a.afc_file_close(afc, handle);
        set_error("Failed to open local output file.");
        return false;
    }

    std::vector<char> buffer(kChunkSize);
    while (true) {
        uint32_t bytes_read = 0;
        const int rc = a.afc_file_read(afc, handle, buffer.data(), static_cast<uint32_t>(buffer.size()), &bytes_read);
        if (rc != 0) {
            a.afc_file_close(afc, handle);
            set_error("Failed while reading remote file.");
            return false;
        }
        if (bytes_read == 0) {
            break;
        }
        out.write(buffer.data(), bytes_read);
        if (!out) {
            a.afc_file_close(afc, handle);
            set_error("Failed while writing local file.");
            return false;
        }
    }

    a.afc_file_close(afc, handle);
    return true;
}

bool write_local_file_to_remote(afc_client_t afc, const char* local_path, const char* remote_path) {
    auto& a = api();
    std::ifstream in(local_path, std::ios::binary);
    if (!in) {
        set_error("Failed to open local input file.");
        return false;
    }

    uint64_t handle = 0;
    if (a.afc_file_open(afc, remote_path, kAfcModeWriteOnly, &handle) != 0) {
        set_error("Failed to open remote file for writing.");
        return false;
    }

    std::vector<char> buffer(kChunkSize);
    while (in) {
        in.read(buffer.data(), buffer.size());
        const std::streamsize got = in.gcount();
        if (got <= 0) {
            break;
        }

        uint32_t bytes_written = 0;
        const int rc = a.afc_file_write(
            afc,
            handle,
            buffer.data(),
            static_cast<uint32_t>(got),
            &bytes_written);
        if (rc != 0 || bytes_written != static_cast<uint32_t>(got)) {
            a.afc_file_close(afc, handle);
            set_error("Failed while writing remote file.");
            return false;
        }
    }

    a.afc_file_close(afc, handle);
    return true;
}

std::vector<Entry> list_entries(afc_client_t afc, const std::string& path, bool* ok) {
    *ok = false;
    std::vector<Entry> entries;

    auto& a = api();
    char** names = nullptr;
    if (a.afc_read_directory(afc, path.c_str(), &names) != 0 || names == nullptr) {
        set_error("Failed to list remote directory.");
        return entries;
    }

    for (int i = 0; names[i] != nullptr; ++i) {
        const std::string name = names[i];
        if (name == "." || name == "..") {
            continue;
        }

        Entry entry;
        entry.name = name;
        entry.path = join_path(path, name);
        entry.modified_unix = now_unix();

        char** info = nullptr;
        if (a.afc_get_file_info(afc, entry.path.c_str(), &info) == 0 && info != nullptr) {
            entry.is_directory = dict_is_directory(info);
            entry.size_bytes = parse_u64(dict_value(info, "st_size"), 0);
            entry.modified_unix = parse_i64(dict_value(info, "st_mtime"), entry.modified_unix);
            a.afc_dictionary_free(info);
        }

        entries.push_back(std::move(entry));
    }

    a.afc_dictionary_free(names);
    *ok = true;
    return entries;
}

}  // namespace

extern "C" {

int iosb_get_version(char* buffer, int buffer_size) {
    if (!copy_text(buffer, buffer_size, kBackendVersion)) {
        set_error("Version buffer too small");
        return 0;
    }
    return 1;
}

int iosb_get_last_error(char* buffer, int buffer_size) {
    if (!copy_text(buffer, buffer_size, g_last_error)) {
        return 0;
    }
    return 1;
}

int iosb_get_runtime_diagnostics(char* buffer, int buffer_size) {
    const std::string details = build_runtime_diagnostics();
    if (!copy_text(buffer, buffer_size, details)) {
        set_error("Diagnostics buffer too small");
        return 0;
    }
    return 1;
}

int iosb_enumerate_devices(iosb_device_info* out_devices, int max_devices) {
    if (max_devices < 0) {
        set_error("max_devices must be >= 0");
        return -1;
    }

    auto& a = api();
    if (!a.ensure_loaded()) {
        return -1;
    }

    char** device_udids = nullptr;
    int raw_count = 0;
    const int rc = a.idevice_get_device_list(&device_udids, &raw_count);
    if (rc != 0 || raw_count < 0) {
        set_error(
            "Failed to enumerate iOS devices (idevice_get_device_list rc=" +
            std::to_string(rc) + "). " + hint_for_idevice_rc(rc));
        return -1;
    }

    std::vector<std::string> udids;
    udids.reserve(static_cast<size_t>(raw_count));
    for (int i = 0; i < raw_count; ++i) {
        if (device_udids[i] != nullptr && device_udids[i][0] != '\0') {
            udids.emplace_back(device_udids[i]);
        }
    }
    a.idevice_device_list_free(device_udids);

    if (out_devices == nullptr) {
        return static_cast<int>(udids.size());
    }

    const int n = (std::min)(static_cast<int>(udids.size()), max_devices);
    for (int i = 0; i < n; ++i) {
        std::memset(&out_devices[i], 0, sizeof(iosb_device_info));
        copy_text(out_devices[i].udid, IOSB_MAX_UDID, udids[i]);
        copy_text(out_devices[i].name, IOSB_MAX_NAME, resolve_device_name(udids[i].c_str()));
    }
    return n;
}

int iosb_open_device(const char* udid, int* out_handle) {
    if (out_handle == nullptr) {
        set_error("out_handle is null");
        return 0;
    }

    auto& a = api();
    if (!a.ensure_loaded()) {
        return 0;
    }

    std::string wanted = (udid != nullptr) ? std::string(udid) : std::string();
    if (wanted.empty()) {
        char** device_udids = nullptr;
        int raw_count = 0;
        const int rc = a.idevice_get_device_list(&device_udids, &raw_count);
        if (rc != 0 || raw_count <= 0 || device_udids == nullptr || device_udids[0] == nullptr) {
            if (device_udids != nullptr) {
                a.idevice_device_list_free(device_udids);
            }
            set_error("No iOS devices found.");
            return 0;
        }
        wanted = device_udids[0];
        a.idevice_device_list_free(device_udids);
    }

    DeviceSession session;
    if (!create_afc_session(wanted.c_str(), session)) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    const int handle = g_next_handle++;
    g_open_handles.emplace(handle, std::move(session));
    *out_handle = handle;
    return 1;
}

int iosb_close_device(int handle) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_open_handles.find(handle);
    if (it == g_open_handles.end()) {
        set_error("Invalid device handle");
        return 0;
    }

    close_session(it->second);
    g_open_handles.erase(it);
    return 1;
}

int iosb_list_directory(int handle, const char* path, iosb_file_entry* out_entries, int max_entries) {
    if (max_entries < 0) {
        set_error("max_entries must be >= 0");
        return -1;
    }

    afc_client_t afc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto it = g_open_handles.find(handle);
        if (it == g_open_handles.end()) {
            set_error("Invalid or closed device handle");
            return -1;
        }
        afc = it->second.afc;
    }

    const std::string remote_path = normalize_path(path);
    bool ok = false;
    auto entries = list_entries(afc, remote_path, &ok);
    if (!ok) {
        return -1;
    }

    if (out_entries == nullptr) {
        return static_cast<int>(entries.size());
    }

    const int n = (std::min)(static_cast<int>(entries.size()), max_entries);
    for (int i = 0; i < n; ++i) {
        std::memset(&out_entries[i], 0, sizeof(iosb_file_entry));
        copy_text(out_entries[i].path, IOSB_MAX_PATH, entries[i].path);
        copy_text(out_entries[i].name, IOSB_MAX_NAME, entries[i].name);
        out_entries[i].is_directory = entries[i].is_directory ? 1 : 0;
        out_entries[i].size_bytes = entries[i].size_bytes;
        out_entries[i].modified_unix = entries[i].modified_unix;
    }
    return n;
}

int iosb_pull_file(int handle, const char* remote_path, const char* local_path) {
    if (remote_path == nullptr || local_path == nullptr) {
        set_error("remote_path/local_path cannot be null");
        return 0;
    }

    afc_client_t afc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto it = g_open_handles.find(handle);
        if (it == g_open_handles.end()) {
            set_error("Invalid or closed device handle");
            return 0;
        }
        afc = it->second.afc;
    }

    return read_remote_file_to_local(afc, normalize_path(remote_path).c_str(), local_path) ? 1 : 0;
}

int iosb_push_file(int handle, const char* local_path, const char* remote_path) {
    if (local_path == nullptr || remote_path == nullptr) {
        set_error("local_path/remote_path cannot be null");
        return 0;
    }

    afc_client_t afc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto it = g_open_handles.find(handle);
        if (it == g_open_handles.end()) {
            set_error("Invalid or closed device handle");
            return 0;
        }
        afc = it->second.afc;
    }

    return write_local_file_to_remote(afc, local_path, normalize_path(remote_path).c_str()) ? 1 : 0;
}

}  // extern "C"
