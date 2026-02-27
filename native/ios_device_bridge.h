#pragma once

#include <stdint.h>

#ifdef IOSB_EXPORTS
#define IOSB_API __declspec(dllexport)
#else
#define IOSB_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define IOSB_MAX_UDID 64
#define IOSB_MAX_NAME 128
#define IOSB_MAX_PATH 512

typedef struct iosb_device_info {
    char udid[IOSB_MAX_UDID];
    char name[IOSB_MAX_NAME];
} iosb_device_info;

typedef struct iosb_file_entry {
    char path[IOSB_MAX_PATH];
    char name[IOSB_MAX_NAME];
    int is_directory;
    uint64_t size_bytes;
    int64_t modified_unix;
} iosb_file_entry;

IOSB_API int iosb_get_version(char* buffer, int buffer_size);
IOSB_API int iosb_get_last_error(char* buffer, int buffer_size);
IOSB_API int iosb_get_runtime_diagnostics(char* buffer, int buffer_size);

IOSB_API int iosb_enumerate_devices(iosb_device_info* out_devices, int max_devices);
IOSB_API int iosb_open_device(const char* udid, int* out_handle);
IOSB_API int iosb_close_device(int handle);

IOSB_API int iosb_list_directory(
    int handle,
    const char* path,
    iosb_file_entry* out_entries,
    int max_entries);

IOSB_API int iosb_pull_file(int handle, const char* remote_path, const char* local_path);
IOSB_API int iosb_push_file(int handle, const char* local_path, const char* remote_path);

#ifdef __cplusplus
}
#endif
