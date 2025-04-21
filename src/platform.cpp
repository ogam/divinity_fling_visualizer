#include "platform.h"
#define WIN32_LEAN_AND_MEAN
#include "windows.h"
#include <tlhelp32.h> // looking into process / module info
#include <psapi.h> // running process snapshot
#include <winver.h> // process version info
#include <string>

#define STREQU(A, B) (!strcmp(A, B))

static LARGE_INTEGER SYSTEM_FREQUENCY;
static LARGE_INTEGER START_TIME;
struct ProcessInfo
{
    HANDLE handle;
    HWND hwnd;
    s32 thread_id;
    u64 base_address;
    u64 module_size;
    char file_name[1024];
    char file_path[1024];
    char version[1024];
    char name[1024];
    
    u32 version_major;
    u32 version_minor;
    u32 version_build;
    u32 version_private;
    
    u32 process_id;
    
    b32 is_gog;
    b32 is_dx11;
    
    u32 pointer_size;
    
    u8 *signature_scanner_buffer;
};

HWND s_app_hwnd;
Arena s_arena;

static ProcessInfo process_info;

BOOL CALLBACK enum_windows_get_hwnd(HWND hwnd, LPARAM param)
{
    DWORD process_id;
    GetWindowThreadProcessId(hwnd, &process_id);
    BOOL should_continue_enumerating = process_id != param;
    if(!should_continue_enumerating)
    {
        process_info.hwnd = hwnd;
        GetWindowText(hwnd, process_info.name, sizeof(process_info.name));
    }
    return should_continue_enumerating;
}

void platform_init(Arena arena)
{
    QueryPerformanceFrequency(&SYSTEM_FREQUENCY);
    QueryPerformanceCounter(&START_TIME);
    s_app_hwnd = GetActiveWindow();
    s_arena = arena;
}

// https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate
b32 key_is_down(s32 key_code)
{
    return GetAsyncKeyState(key_code) & (1 << 15);
}

//  @todo:  covers majority of cases, some keys are still off
void platform_key_name(s32 virtual_key, String *string)
{
    s32 sc = get_scan_code(virtual_key);
    string->length = GetKeyNameTextA(sc << 16, string->str, sizeof(string->str));
};

V2i get_cursor_position()
{
    V2i cursor_position;
    POINT point;
    GetCursorPos((LPPOINT)&point);
    cursor_position.x = (s16)point.x;
    cursor_position.y = (s16)point.y;
    return cursor_position;
}

f64 get_time()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return ((f64)now.QuadPart - (f64)START_TIME.QuadPart) / (f64)SYSTEM_FREQUENCY.QuadPart;
}

u32 get_scan_code(u32 key_code)
{
    return MapVirtualKeyA(key_code, MAPVK_VK_TO_VSC);
}

void push_keyboard_event(u8 vk, u8 sc, s32 flags, u32 *extra_info)
{
    keybd_event(vk, sc, flags, (ULONG_PTR)extra_info);
}

void push_mouse_event(s32 flags, s32 dx, s32 dy, s32 data, u32* extra_info)
{
    mouse_event(flags, dx, dy, data, (ULONG_PTR)extra_info);
}

void push_mouse_move_absolute(V2i position)
{
    SetCursorPos(position.x, position.y);
}

void push_mouse_move_relative(V2i position)
{
    push_mouse_event(MOUSEEVENTF_MOVE, position.x, position.y, 0, 0);
}

void push_mouse_button(MouseButton button, b32 is_down)
{
    s32 flags = 0;
    if (button == MouseButton_Left)
    {
        if (is_down)
        {
            flags = MOUSEEVENTF_LEFTDOWN;
        }
        else
        {
            flags = MOUSEEVENTF_LEFTUP;
        }
    }
    else if (button == MouseButton_Right)
    {
        if (is_down)
        {
            flags = MOUSEEVENTF_RIGHTDOWN;
        }
        else
        {
            flags = MOUSEEVENTF_RIGHTUP;
        }
    }
    else if (button == MouseButton_Middle)
    {
        if (is_down)
        {
            flags = MOUSEEVENTF_MIDDLEDOWN;
        }
        else
        {
            flags = MOUSEEVENTF_MIDDLEUP;
        }
    }
    
    if (flags)
    {
        push_mouse_event(flags, 0, 0, 0, 0);
    }
}

b32 process_is_active()
{
    b32 is_active = false;
    if (process_info.handle != INVALID_HANDLE_VALUE)
    {
        s32 exit_code;
        if (GetExitCodeProcess(process_info.handle, (LPDWORD)&exit_code))
        {
            is_active = exit_code == STILL_ACTIVE;
        }
    }
    
    return is_active;
}

void process_info_init()
{
    process_info.handle = INVALID_HANDLE_VALUE;
    process_info.base_address = 0;
    process_info.module_size = 0;
    process_info.thread_id = 0;
    process_info.is_dx11 = false;
    memset(process_info.version, 0, sizeof(process_info.version));
    process_info.is_gog = false;
    memset(process_info.name, 0, sizeof(process_info.name));
    process_info.process_id = 0;
    
    if (process_info.signature_scanner_buffer)
    {
        free(process_info.signature_scanner_buffer);
        process_info.signature_scanner_buffer = nullptr;
    }
}

// https://learn.microsoft.com/en-us/windows/win32/toolhelp/taking-a-snapshot-and-viewing-processes
b32 process_find(StringCollection *process_names)
{
    HANDLE handle_process_snapshot;
    HANDLE handle_process;
    PROCESSENTRY32 pe32;
    
    ASSERT(process_names);
    
    if (process_info.handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(process_info.handle);
    }
    process_info_init();
    
    
    handle_process_snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (handle_process_snapshot == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    
    // Set the size of the structure before using it.
    pe32.dwSize = sizeof(PROCESSENTRY32);
    
    if (!Process32First(handle_process_snapshot, &pe32))
    {
        CloseHandle(handle_process_snapshot);          // clean the snapshot object
        return false;
    }
    
    u8 buffer[1024];
    
    do
    {
        for (s32 process_name_index = 0; process_name_index < process_names->count; ++process_name_index)
        {
            const char *process_name = process_names->strings[process_name_index];
            if (STREQU(process_name, pe32.szExeFile))
            {
                //  @todo:  reduce access, we only read
                handle_process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pe32.th32ProcessID);
                BOOL is_emulating_x86 = false;
                IsWow64Process(handle_process, &is_emulating_x86);
                
                process_info.pointer_size = 8;
                if (is_emulating_x86)
                {
                    process_info.pointer_size = 4;
                }
                
                if (handle_process != NULL)
                {
                    HANDLE handle_module_snapshot = INVALID_HANDLE_VALUE;
                    MODULEENTRY32 me32;
                    handle_module_snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pe32.th32ProcessID);
                    if(handle_module_snapshot != INVALID_HANDLE_VALUE)
                    {
                        me32.dwSize = sizeof(MODULEENTRY32);
                        if(Module32First(handle_module_snapshot, &me32))
                        {
                            process_info.base_address = (u64)me32.modBaseAddr;
                            process_info.module_size = (u64)me32.modBaseSize;
                        }
                        
                        do
                        {
                            if (STREQU("Galaxy64.dll", me32.szModule))
                            {
                                process_info.is_gog = true;
                            }
                            if (STREQU("d3d11.dll", me32.szModule))
                            {
                                process_info.is_dx11 = true;
                            }
                            
                        }
                        while (Module32Next(handle_module_snapshot, &me32));
                        CloseHandle(handle_module_snapshot);
                    }
                    
                    snprintf(process_info.file_name, sizeof(process_info.file_name), pe32.szExeFile);
                    GetModuleFileNameExA(handle_process, NULL, process_info.file_path, sizeof(process_info.file_path));
                    
                    DWORD file_info_handle;
                    DWORD file_info_output;
                    file_info_output = GetFileVersionInfoSize(process_info.file_path, &file_info_handle);
                    file_info_output = GetFileVersionInfo(process_info.file_path, file_info_handle, sizeof(buffer), buffer);
                    
                    u32 buffer_size;
                    VS_FIXEDFILEINFO *fixed_file_info = NULL;
                    // using the following fails, but looks like the game uses file version as the product
                    // version, with an additional characters between major/minor/etc X.1.1.XXXXXXX 
                    //file_info_output = VerQueryValue(buffer, "\\StringFileInfo\\040904E4\\ProductVersion", (LPVOID*)&fixed_file_info, (PUINT)&buffer_size);
                    
                    file_info_output = VerQueryValue(buffer, "\\", (LPVOID*)&fixed_file_info, (PUINT)&buffer_size);
                    if (buffer_size)
                    {
                        if (fixed_file_info->dwSignature == 0xFEEF04BD)
                        {
#if 0
                            s32 v0 = (fixed_file_info->dwProductVersionMS >> 16) & 0xFFFF;
                            s32 v1 = (fixed_file_info->dwProductVersionMS >> 0) & 0xFFFF;
                            s32 v2 = (fixed_file_info->dwProductVersionLS >> 16) & 0xFFFF;
                            s32 v3 = (fixed_file_info->dwProductVersionLS >> 0) & 0xFFFF;
#endif
                            s32 vv0 = (fixed_file_info->dwFileVersionMS  >> 16) & 0xFFFF;
                            s32 vv1 = (fixed_file_info->dwFileVersionMS  >> 0) & 0xFFFF;
                            s32 vv2 = (fixed_file_info->dwFileVersionLS  >> 16) & 0xFFFF;
                            s32 vv3 = (fixed_file_info->dwFileVersionLS  >> 0) & 0xFFFF;
                            
                            snprintf(process_info.version, sizeof(process_info.version), "%d.1.1.%02d%02d%03d", vv0, vv1, vv2, vv3);
                            sscanf(process_info.version, "%u.%u.%u.%u", &process_info.version_major, &process_info.version_minor, &process_info.version_build, &process_info.version_private);
                        }
                    }
                    
                    EnumWindows(enum_windows_get_hwnd, pe32.th32ProcessID);
                    process_info.thread_id = GetWindowThreadProcessId(process_info.hwnd, &pe32.th32ProcessID);
                    
                    if (process_info.base_address)
                    {
                        process_info.handle = handle_process;
                        process_info.process_id = pe32.th32ProcessID;
                    }
                    break;
                }
            }
        }
        
    } 
    while(Process32Next(handle_process_snapshot, &pe32));
    
    CloseHandle(handle_process_snapshot);
    
    return process_info.handle != INVALID_HANDLE_VALUE;
}

void process_focus()
{
    SetForegroundWindow(process_info.hwnd);
}

//  @todo:  actually search this
void process_signature_scanner_begin()
{
    ASSERT(process_info.signature_scanner_buffer == nullptr);
    u64 allocation_size = process_info.module_size;
    process_info.signature_scanner_buffer = (u8*)malloc(allocation_size);
    
    memset(process_info.signature_scanner_buffer, 0, allocation_size);
    
    u64 bytes_read;
    ReadProcessMemory(process_info.handle, (LPCVOID)process_info.base_address, (LPVOID)process_info.signature_scanner_buffer, process_info.module_size, &bytes_read);
    
    ASSERT(bytes_read == process_info.module_size);
}

void process_signature_scanner_end()
{
    if (process_info.signature_scanner_buffer)
    {
        free(process_info.signature_scanner_buffer);
        process_info.signature_scanner_buffer = nullptr;
    }
}

u64 process_signature_scan(Signature *signature)
{
    if (!process_info.signature_scanner_buffer || !signature)
    {
        return 0;
    }
    
    u64 result = 0;
    u8 *signature_end = signature->signature + signature->byte_count;
    u8 *block_0 = nullptr;
    u8 *block_1 = nullptr;
    u64 size_0 = 0;
    u64 size_1 = 0;
    
    u8 *walker = process_info.signature_scanner_buffer;
    u8 *walker_end = process_info.signature_scanner_buffer + process_info.module_size;
    
    if (signature->search_start == 0)
    {
        block_0 = signature->signature + signature->search_length;
        size_0 = signature->byte_count - signature->search_length;
    }
    else if (signature->search_start + signature->search_length == signature->byte_count)
    {
        block_0 = signature->signature;
        size_0 = signature->byte_count - signature->search_start;
    }
    else
    {
        block_0 = signature->signature;
        size_0 = signature->search_start;
        block_1 = signature->signature + signature->search_start + signature->search_length;
        size_1 = signature_end - block_1;
        ASSERT(size_0 + size_1 + signature->search_length == signature->byte_count);
    }
    
    u8 *found = nullptr;
    
    if (block_0 && block_1)
    {
        u64 block_delta = block_1 - block_0;
        while (walker < walker_end)
        {
            if (!memcmp(block_0, walker, size_0) && !memcmp(block_1, walker + block_delta, size_1))
            {
                found = walker;
                break;
            }
            walker++;
        }
        
        if (found)
        {
            found += signature->search_start;
        }
    }
    else if (block_0)
    {
        u8 *found = nullptr;
        
        while (walker < walker_end)
        {
            if (!memcmp(block_0, walker, size_0))
            {
                found = walker;
                break;
            }
            walker++;
        }
        
        if (found)
        {
            if (signature->search_start == 0)
            {
                found -= signature->search_length;
            }
            else
            {
                found += size_0;
            }
        }
    }
    
    if (found)
    {
        u64 address = 0;
        memcpy(&address, found, MIN(signature->search_length, sizeof(u64)));
        result = address + (found - process_info.signature_scanner_buffer);
        // realign to pointer size
        result = result + (result % process_info.pointer_size);
    }
    
    return result;
}

u64 process_scan_memory(u64 address, void *out_buffer, u64 out_buffer_size)
{
    ASSERT(out_buffer);
    ASSERT(out_buffer_size);
    u8 buffer[256];
    memset(buffer, 0, sizeof(buffer));
    u64 bytes_read;
    
    if (ReadProcessMemory(process_info.handle, (LPCVOID)address, (LPVOID)buffer, out_buffer_size, &bytes_read))
    {
        memcpy(out_buffer, buffer, bytes_read);
    }
    
    return bytes_read;
}

u64 process_scan_memory_ex(u64 *address_offsets, s32 count, void *out_buffer, u64 out_buffer_size)
{
    ASSERT(address_offsets);
    ASSERT(out_buffer && out_buffer_size);
    
    u64 next_address;
    u8 *buffer[256];
    memset(buffer, 0, sizeof(buffer));
    *(u64*)buffer = process_info.base_address;
    
    u64 bytes_read;
    b32 read_successful = true;
    
    for (s32 index = 0; index < count - 1; ++index)
    {
        next_address = address_offsets[index] + *(u64*)buffer;
        if (ReadProcessMemory(process_info.handle, (LPCVOID)next_address, (LPVOID)buffer, sizeof(u64), &bytes_read))
        {
            if (bytes_read != sizeof(u64))
            {
                read_successful = false;
                break;
            }
        }
        else
        {
            read_successful = false;
            break;
        }
    }
    
    if (read_successful)
    {
        next_address = address_offsets[count - 1] + *(u64*)buffer;
        if (ReadProcessMemory(process_info.handle, (LPCVOID)next_address, (LPVOID)buffer, out_buffer_size, &bytes_read))
        {
            memcpy(out_buffer, buffer, bytes_read);
        }
        else
        {
            bytes_read = 0;
        }
    }
    else
    {
        bytes_read = 0;
    }
    
    return bytes_read;
}

Aabbi process_window_size()
{
    Aabbi aabb = {};
    RECT rect;
    GetWindowRect(process_info.hwnd, &rect);
    aabb.min.x = (s16)rect.left;
    aabb.min.y = (s16)rect.top;
    aabb.max.x = (s16)rect.right;
    aabb.max.y = (s16)rect.bottom;
    return aabb;
}

b32 process_screenshot(void **buffer, Aabbi region, b32 include_window_frame)
{
    ASSERT(buffer);
    V2i size = aabbi_size(region);
    ASSERT(size.x * size.y);
    HDC handle_window_dc;
    b32 capture_success = true;
    
    if (include_window_frame)
    {
        handle_window_dc = GetDC(NULL);
    }
    else
    {
        handle_window_dc = GetDC(process_info.hwnd);
    }
    HDC handle_memory_dc = CreateCompatibleDC(handle_window_dc);
    
    HBITMAP bitmap = CreateCompatibleBitmap(handle_window_dc, size.x, size.y);
    SelectObject(handle_memory_dc, bitmap);
    if (BitBlt(handle_memory_dc, 0, 0, size.x, size.y, handle_window_dc, region.min.x, region.min.y, SRCCOPY))
    {
        BITMAPINFO bitmap_info = {};
        bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap_info.bmiHeader.biBitCount = 0;
        if (!GetDIBits(handle_memory_dc, bitmap, 0, 0, NULL, &bitmap_info, DIB_RGB_COLORS))
        {
            capture_success = false;
        }
        bitmap_info.bmiHeader.biHeight = -bitmap_info.bmiHeader.biHeight;
        bitmap_info.bmiHeader.biCompression = BI_RGB;
        if (!GetDIBits(handle_memory_dc, bitmap, 0, size.y, *buffer, &bitmap_info, DIB_RGB_COLORS))
        {
            capture_success = false;
        }
    }
    else
    {
        capture_success = false;
    }
    DeleteObject(bitmap);
    ReleaseDC(process_info.hwnd, handle_window_dc);
    DeleteDC(handle_memory_dc);
    
    if (capture_success)
    {
        s32 bytes = size.x * size.y * 4;
        u8 *pixels = (u8*)*buffer;
        for (s32 index = 0; index < bytes; index += 4)
        {
            u8 b = pixels[index + 0];
            u8 g = pixels[index + 1];
            u8 r = pixels[index + 2];
            u8 a = pixels[index + 3];
            
            pixels[index + 0] = r;
            pixels[index + 1] = g;
            pixels[index + 2] = b;
            pixels[index + 3] = a;
        }
    }
    
    return capture_success;
}

b32 process_is_gog()
{
    return process_info.is_gog;
}

b32 process_is_dx11()
{
    return process_info.is_dx11;
}

const char *process_get_window_title()
{
    if (process_info.name == 0 && process_info.process_id)
    {
        EnumWindows(enum_windows_get_hwnd, process_info.process_id);
    }
    
    return process_info.name;
}

const char *process_get_version(u32 *version_major, u32 *version_minor, u32 *version_build, u32 *version_private)
{
    if (version_major)
    {
        *version_major = process_info.version_major;
    }
    if (version_minor)
    {
        *version_minor = process_info.version_minor;
    }
    if (version_build)
    {
        *version_build = process_info.version_build;
    }
    if (version_private)
    {
        *version_private = process_info.version_private;
    }
    return process_info.version;
}

// https://learn.microsoft.com/en-us/windows/win32/dataxchg/using-the-clipboard
ClipboardImage clipboard_get_image()
{
    PAINTSTRUCT paint_info;
    HDC paint_device_context = BeginPaint(s_app_hwnd, &paint_info);
    HDC paint_memory_device_context = CreateCompatibleDC(paint_device_context);
    //RECT rect;
    
    ClipboardImage clipboard_image = {};
    
    if (paint_memory_device_context)
    {
        if (OpenClipboard(s_app_hwnd))
        {
            arena_clear(&s_arena);
            
            HBITMAP bitmap_handle = (HBITMAP)GetClipboardData(CF_BITMAP);
            SelectObject(paint_memory_device_context, bitmap_handle); 
            //GetClientRect(s_app_hwnd, &rect); 
            
            BITMAPINFO bitmap_info = {};
            bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bitmap_info.bmiHeader.biBitCount = 0;
            GetDIBits(paint_memory_device_context, bitmap_handle, 0, 0, NULL, &bitmap_info, DIB_RGB_COLORS);
            
            bitmap_info.bmiHeader.biHeight = -bitmap_info.bmiHeader.biHeight;
            bitmap_info.bmiHeader.biCompression = BI_RGB;
            
            clipboard_image.image_size = bitmap_info.bmiHeader.biSizeImage;
            clipboard_image.bit_count = bitmap_info.bmiHeader.biBitCount;
            clipboard_image.width  = bitmap_info.bmiHeader.biWidth;
            clipboard_image.height  = bitmap_info.bmiHeader.biHeight;
            clipboard_image.image = (u8*)arena_alloc(&s_arena, clipboard_image.image_size);
            
            GetDIBits(paint_memory_device_context, bitmap_handle, 0, clipboard_image.height, clipboard_image.image, &bitmap_info, DIB_RGB_COLORS);
            
            //BitBlt(paint_device_context, 0, 0, rect.right, rect.bottom, paint_memory_device_context, 0, 0, SRCCOPY); 
            
            //GlobalLock(clipboard_handle);
            //GlobalUnlock(clipboard_handle);
            
            CloseClipboard();
            
            if (clipboard_image.image_size)
            {
                u8 *pixels = (u8*)clipboard_image.image;
                for (s32 index = 0; index < clipboard_image.image_size; index += 4)
                {
                    u8 b = pixels[index + 0];
                    u8 g = pixels[index + 1];
                    u8 r = pixels[index + 2];
                    u8 a = pixels[index + 3];
                    
                    pixels[index + 0] = r;
                    pixels[index + 1] = g;
                    pixels[index + 2] = b;
                    pixels[index + 3] = a;
                }
            }
        }
        
        DeleteDC(paint_memory_device_context);
    }
    EndPaint(s_app_hwnd, &paint_info);
    
    return clipboard_image;
}