#ifndef PLATFORM_WINDOWS_H
#define PLATFORM_WINDOWS_H

#if _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h> // looking into process / module info
#include <psapi.h> // running process snapshot
#include <winver.h> // process version info

typedef struct Enum_Windows_Param
{
    DWORD th32ProcessID;
    HWND* hwnd;
} Enum_Windows_Param;

BOOL CALLBACK enum_windows_get_hwnd(HWND hwnd, LPARAM param)
{
    Enum_Windows_Param* data = (Enum_Windows_Param*)param;
    
    DWORD process_id;
    GetWindowThreadProcessId(hwnd, &process_id);
    BOOL should_continue_enumerating = process_id != data->th32ProcessID;
    if(!should_continue_enumerating)
    {
        *data->hwnd = hwnd;
    }
    return should_continue_enumerating;
}

void process_focus(Process_Info process_info)
{
    SetForegroundWindow((HWND)process_info.window_handle.id);
}

void close_process_hook(Process_Info process_info)
{
    HANDLE handle = *(HANDLE*)&process_info.handle;
    if (handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(handle);
    }
}

Process_Info find_process(const char* process_name, CF_Arena* arena)
{
    Process_Info info = { 0 };
    info.arena = arena;
    
    info.name = make_arena_string(arena, 260);
    info.file_name = make_arena_string(arena, 260);
    info.file_path = make_arena_string(arena, 260);
    info.version = make_arena_string(arena, 260);
    arena_array_fit(arena, info.modules, 512);
    
    if (!process_name)
    {
        return info;
    }
    
    HANDLE handle_process_snapshot;
    HANDLE handle_process;
    HWND window_handle;
    PROCESSENTRY32 pe32;
    
    handle_process_snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (handle_process_snapshot == INVALID_HANDLE_VALUE)
    {
        return info;
    }
    
    pe32.dwSize = sizeof(PROCESSENTRY32);
    if (!Process32First(handle_process_snapshot, &pe32))
    {
        goto FIND_PROCESS_CLEANUP;
    }
    
    char buffer[1024];
    
    do
    {
        if (cf_string_equ(process_name, pe32.szExeFile))
        {
            handle_process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pe32.th32ProcessID);
            BOOL is_emulating_x86 = false;
            IsWow64Process(handle_process, &is_emulating_x86);
            
            info.pointer_size = 8;
            if (is_emulating_x86)
            {
                info.pointer_size = 4;
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
                        do
                        {
                            Module_Info module_info = { 0 };
                            arena_array_fit(arena, module_info.name, 260);
                            cf_string_fmt(module_info.name, "%s", me32.szModule);
                            
                            module_info.base_address = (u64)me32.modBaseAddr;
                            module_info.size = (u64)me32.modBaseSize;
                            cf_array_push(info.modules, module_info);
                        }
                        while (Module32Next(handle_module_snapshot, &me32));
                    }
                    
                    CloseHandle(handle_module_snapshot);
                }
                
                GetModuleFileNameExA(handle_process, NULL, buffer, sizeof(buffer));
                cf_string_fmt(info.file_name, "%s", pe32.szExeFile);
                cf_string_fmt(info.file_path, "%s", buffer);
                
                Enum_Windows_Param param;
                param.th32ProcessID = pe32.th32ProcessID;
                param.hwnd = &window_handle;
                
                EnumWindows(enum_windows_get_hwnd, (LPARAM)&param);
                GetWindowText(window_handle, buffer, sizeof(buffer));
                cf_string_fmt(info.name, "%s", buffer);
                
                info.thread_id = GetWindowThreadProcessId(window_handle, &pe32.th32ProcessID);
                
                // file version
                {
                    DWORD file_info_handle;
                    DWORD file_info_output;
                    file_info_output = GetFileVersionInfoSize(info.file_path, &file_info_handle);
                    file_info_output = GetFileVersionInfo(info.file_path, file_info_handle, sizeof(buffer), buffer);
                    
                    u32 buffer_size;
                    VS_FIXEDFILEINFO *fixed_file_info = NULL;
                    
                    file_info_output = VerQueryValue(buffer, "\\", (LPVOID*)&fixed_file_info, (PUINT)&buffer_size);
                    if (buffer_size)
                    {
                        if (fixed_file_info->dwSignature == 0xFEEF04BD)
                        {
                            s32 vv0 = (fixed_file_info->dwFileVersionMS  >> 16) & 0xFFFF;
                            s32 vv1 = (fixed_file_info->dwFileVersionMS  >> 0) & 0xFFFF;
                            s32 vv2 = (fixed_file_info->dwFileVersionLS  >> 16) & 0xFFFF;
                            s32 vv3 = (fixed_file_info->dwFileVersionLS  >> 0) & 0xFFFF;
                            
                            //  @note:  this is wrong way to generate a version string
                            //          in dos1 / dos2 it's major.minor.build.private (correct way)
                            //          but bg3 it's major.1.1.(minor|build|private) (weird way)
                            //  @todo:  on next game release double check this and if it's back to
                            //          correct way then have a case only for bg3.exe and bg3_dx11.exe
                            cf_string_fmt(info.version, "%d.1.1.%02d%02d%03d", vv0, vv1, vv2, vv3);
                            sscanf(info.version, "%u.%u.%u.%u", &info.version_major, &info.version_minor, &info.version_build, &info.version_private);
                        }
                    }
                }
                
                if (cf_array_count(info.modules) > 0)
                {
                    info.handle = *(Process_Handle*)&handle_process;
                    info.window_handle = *(Window_Handle*)&window_handle;
                    info.process_id = pe32.th32ProcessID;
                }
                break;
            }
        }
    } 
    while(Process32Next(handle_process_snapshot, &pe32));
    
    FIND_PROCESS_CLEANUP:
    CloseHandle(handle_process_snapshot);          // clean the snapshot object
    
    return info;
}

b32 is_process_alive(Process_Info process_info)
{
    b32 is_active = false;
    HANDLE handle = *(HANDLE*)&process_info.handle;
    if (handle != INVALID_HANDLE_VALUE)
    {
        s32 exit_code;
        if (GetExitCodeProcess(handle, (LPDWORD)&exit_code))
        {
            is_active = exit_code == STILL_ACTIVE;
        }
    }
    
    return is_active;
}

Signature_Scanner signature_scan_begin(Process_Info process_info, Module_Info module)
{
    HANDLE handle = *(HANDLE*)&process_info.handle;
    Signature_Scanner scanner = { 0 };
    if (module.size && module.base_address)
    {
        scanner.memory = (u8*)cf_alloc(module.size);
        scanner.size = module.size;
        
        u64 bytes_read;
        ReadProcessMemory(handle, (LPCVOID)module.base_address, (LPVOID)scanner.memory, scanner.size, &bytes_read);
        
        if (bytes_read == 0)
        {
            signature_scan_end(&scanner);
        }
    }
    return scanner;
}

void signature_scan_end(Signature_Scanner* scanner)
{
    if (scanner->memory)
    {
        cf_free(scanner->memory);
        MEMZERO(scanner);
    }
}

u64 signature_scan(Process_Info process_info, Signature_Scanner scanner, Signature_Query query)
{
    if (!scanner.memory || !query.signature || query.signature_length == 0)
    {
        return 0;
    }
    
    u64 result = 0;
    u8* signature_end = query.signature + query.signature_length;
    u8* block_0 = NULL;
    u8* block_1 = NULL;
    s32 size_0 = 0;
    s32 size_1 = 0;
    
    u8* walker = scanner.memory;
    u8* walker_end = scanner.memory + scanner.size;
    
    s32 search_length = query.search_end - query.search_start;
    
    if (query.search_start == 0)
    {
        block_0 = query.signature + search_length;
        size_0 = query.signature_length - search_length;
    }
    else if (query.search_start + search_length == query.signature_length)
    {
        block_0 = query.signature;
        size_0 = query.signature_length - query.search_start;
    }
    else
    {
        block_0 = query.signature;
        size_0 = query.search_start;
        block_1 = query.signature + query.search_start + search_length;
        size_1 = (s32)(signature_end - block_1);
        CF_ASSERT(size_0 + size_1 + search_length == query.signature_length);
    }
    
    u8* found = NULL;
    
    if (block_0 && block_1)
    {
        u64 block_delta = block_1 - block_0;
        while (walker < walker_end)
        {
            if (!CF_MEMCMP(block_0, walker, size_0) && !CF_MEMCMP(block_1, walker + block_delta, size_1))
            {
                found = walker;
                break;
            }
            walker++;
        }
        
        if (found)
        {
            found += query.search_start;
        }
    }
    else if (block_0)
    {
        while (walker < walker_end)
        {
            if (!CF_MEMCMP(block_0, walker, size_0))
            {
                found = walker;
                break;
            }
            walker++;
        }
        
        if (found)
        {
            if (query.search_start == 0)
            {
                found -= search_length;
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
        CF_MEMCPY(&address, found, min(search_length, (s32)sizeof(u64)));
        result = address + (found - scanner.memory);
        // realign to pointer size
        result = result + (result % process_info.pointer_size);
    }
    
    return result;
}

u64 process_deref(Process_Info process_info, CF_ARRAY(u64) offsets, void* out_ptr, u64 ptr_size)
{
    HANDLE handle = *(HANDLE*)&process_info.handle;
    
    u8* buffer = (u8*)cf_arena_alloc(process_info.arena, 256);
    u64 bytes_read = 0;
    
    u64 next_address;
    *(u64*)buffer = process_info.modules[0].base_address;
    
    b32 read_successful = true;
    
    for (s32 index = 0; index < cf_array_count(offsets) - 1; ++index)
    {
        next_address = offsets[index] + *(u64*)buffer;
        if (ReadProcessMemory(handle, (LPCVOID)next_address, (LPVOID)buffer, sizeof(u64), &bytes_read))
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
        next_address = offsets[cf_array_count(offsets) - 1] + *(u64*)buffer;
        if (ReadProcessMemory(handle, (LPCVOID)next_address, (LPVOID)buffer, ptr_size, &bytes_read))
        {
            CF_MEMCPY(out_ptr, buffer, bytes_read);
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
    
    cf_arena_free(process_info.arena, 256);
    
    return bytes_read;
}

s32 get_vk_from_key(CF_KeyButton key)
{
    s32 vk = 0;
    
    switch (key)
    {
        case CF_KEY_BACKSPACE:    vk = 0x08; break;
        case CF_KEY_TAB:          vk = 0x09; break;
        case CF_KEY_CLEAR:        vk = 0x10; break;
        case CF_KEY_RETURN:       vk = 0x11; break;
        case CF_KEY_PAUSE:        vk = 0x13; break;
        case CF_KEY_CAPSLOCK:     vk = 0x14; break;
        case CF_KEY_ESCAPE:       vk = 0x1B; break;
        case CF_KEY_SPACE:        vk = 0x20; break;
        case CF_KEY_PAGEUP:       vk = 0x21; break;
        case CF_KEY_PAGEDOWN:     vk = 0x22; break;
        case CF_KEY_END:          vk = 0x23; break;
        case CF_KEY_HOME:         vk = 0x24; break;
        case CF_KEY_LEFT:         vk = 0x25; break;
        case CF_KEY_UP:           vk = 0x26; break;
        case CF_KEY_RIGHT:        vk = 0x27; break;
        case CF_KEY_DOWN:         vk = 0x28; break;
        case CF_KEY_SELECT:       vk = 0x29; break;
        case CF_KEY_PRINTSCREEN:  vk = 0x2C; break;
        case CF_KEY_INSERT:       vk = 0x2D; break;
        case CF_KEY_DELETE:       vk = 0x2E; break;
        case CF_KEY_HELP:         vk = 0x2F; break;
        case CF_KEY_0:            vk = 0x30; break;
        case CF_KEY_1:            vk = 0x31; break;
        case CF_KEY_2:            vk = 0x32; break;
        case CF_KEY_3:            vk = 0x33; break;
        case CF_KEY_4:            vk = 0x34; break;
        case CF_KEY_5:            vk = 0x35; break;
        case CF_KEY_6:            vk = 0x36; break;
        case CF_KEY_7:            vk = 0x37; break;
        case CF_KEY_8:            vk = 0x38; break;
        case CF_KEY_9:            vk = 0x39; break;
        case CF_KEY_A:            vk = 0x41; break;
        case CF_KEY_B:            vk = 0x42; break;
        case CF_KEY_C:            vk = 0x43; break;
        case CF_KEY_D:            vk = 0x44; break;
        case CF_KEY_E:            vk = 0x45; break;
        case CF_KEY_F:            vk = 0x46; break;
        case CF_KEY_G:            vk = 0x47; break;
        case CF_KEY_H:            vk = 0x48; break;
        case CF_KEY_I:            vk = 0x49; break;
        case CF_KEY_J:            vk = 0x4A; break;
        case CF_KEY_K:            vk = 0x4B; break;
        case CF_KEY_L:            vk = 0x4C; break;
        case CF_KEY_M:            vk = 0x4D; break;
        case CF_KEY_N:            vk = 0x4E; break;
        case CF_KEY_O:            vk = 0x4F; break;
        case CF_KEY_P:            vk = 0x50; break;
        case CF_KEY_Q:            vk = 0x51; break;
        case CF_KEY_R:            vk = 0x52; break;
        case CF_KEY_S:            vk = 0x53; break;
        case CF_KEY_T:            vk = 0x54; break;
        case CF_KEY_U:            vk = 0x55; break;
        case CF_KEY_V:            vk = 0x56; break;
        case CF_KEY_W:            vk = 0x57; break;
        case CF_KEY_X:            vk = 0x58; break;
        case CF_KEY_Y:            vk = 0x59; break;
        case CF_KEY_Z:            vk = 0x5A; break;
        case CF_KEY_KP_0:         vk = 0x60; break;
        case CF_KEY_KP_1:         vk = 0x61; break;
        case CF_KEY_KP_2:         vk = 0x62; break;
        case CF_KEY_KP_3:         vk = 0x63; break;
        case CF_KEY_KP_4:         vk = 0x64; break;
        case CF_KEY_KP_5:         vk = 0x65; break;
        case CF_KEY_KP_6:         vk = 0x66; break;
        case CF_KEY_KP_7:         vk = 0x67; break;
        case CF_KEY_KP_8:         vk = 0x68; break;
        case CF_KEY_KP_9:         vk = 0x69; break;
        case CF_KEY_KP_MULTIPLY:  vk = 0x6A; break;
        case CF_KEY_KP_PLUS:      vk = 0x6B; break;
        case CF_KEY_KP_MINUS:     vk = 0x6D; break;
        case CF_KEY_KP_PERIOD:    vk = 0x6E; break;
        case CF_KEY_KP_DIVIDE:    vk = 0x6F; break;
        case CF_KEY_F1:           vk = 0x70; break;
        case CF_KEY_F2:           vk = 0x71; break;
        case CF_KEY_F3:           vk = 0x72; break;
        case CF_KEY_F4:           vk = 0x73; break;
        case CF_KEY_F5:           vk = 0x74; break;
        case CF_KEY_F6:           vk = 0x75; break;
        case CF_KEY_F7:           vk = 0x76; break;
        case CF_KEY_F8:           vk = 0x77; break;
        case CF_KEY_F9:           vk = 0x78; break;
        case CF_KEY_F10:          vk = 0x79; break;
        case CF_KEY_F11:          vk = 0x7A; break;
        case CF_KEY_F12:          vk = 0x7B; break;
        case CF_KEY_F13:          vk = 0x7C; break;
        case CF_KEY_F14:          vk = 0x7D; break;
        case CF_KEY_F15:          vk = 0x7E; break;
        case CF_KEY_F16:          vk = 0x7F; break;
        case CF_KEY_F17:          vk = 0x80; break;
        case CF_KEY_F18:          vk = 0x81; break;
        case CF_KEY_F19:          vk = 0x82; break;
        case CF_KEY_F20:          vk = 0x83; break;
        case CF_KEY_F21:          vk = 0x84; break;
        case CF_KEY_F22:          vk = 0x85; break;
        case CF_KEY_F23:          vk = 0x86; break;
        case CF_KEY_F24:          vk = 0x87; break;
        case CF_KEY_NUMLOCKCLEAR: vk = 0x90; break;
        case CF_KEY_SCROLLLOCK:   vk = 0x91; break;
        case CF_KEY_LSHIFT:       vk = 0xA0; break;
        case CF_KEY_RSHIFT:       vk = 0xA1; break;
        case CF_KEY_LCTRL:        vk = 0xA2; break;
        case CF_KEY_RCTRL:        vk = 0xA3; break;
        case CF_KEY_LALT:         vk = 0xA4; break;
        case CF_KEY_RALT:         vk = 0xA5; break;
    }
    
    return vk;
}

b32 async_get_key_down(CF_KeyButton key)
{
    s32 vk = get_vk_from_key(key);
    b32 key_down = false;
    
    if (vk)
    {
        key_down = !!GetAsyncKeyState(vk);
    }
    
    return key_down;
}

b32 async_get_mouse_down(CF_MouseButton button)
{
    s32 vk = 0;
    b32 button_down = false;
    switch (button)
    {
        case CF_MOUSE_BUTTON_LEFT: vk = VK_LBUTTON; break;
        case CF_MOUSE_BUTTON_RIGHT: vk = VK_RBUTTON; break;
        case CF_MOUSE_BUTTON_MIDDLE: vk = VK_MBUTTON; break;
    }
    
    if (vk)
    {
        button_down = !!GetAsyncKeyState(vk);
    }
    
    return button_down;
}

void desktop_mouse_position(s32* out_x, s32* out_y)
{
    POINT p = { 0 };
    if (GetCursorPos(&p))
    {
        if (out_x)
        {
            *out_x = p.x;
        }
        if (out_y)
        {
            *out_y = p.y;
        }
    }
}

void send_keyboard_down(CF_KeyButton key)
{
    s32 vk = get_vk_from_key(key);
    s32 sc = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    keybd_event(vk, sc, 0, 0);
}

void send_keyboard_up(CF_KeyButton key)
{
    s32 vk = get_vk_from_key(key);
    s32 sc = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    keybd_event(vk, sc, KEYEVENTF_KEYUP, 0);
}

void send_mouse_position_absolute(s32 x, s32 y)
{
    SetCursorPos(x, y);
}

void send_mouse_position_relative(s32 dx, s32 dy)
{
    mouse_event(MOUSEEVENTF_MOVE, dx, dy, 0, 0);
}

void send_mouse_button_down(CF_MouseButton button)
{
    s32 flags = 0;
    switch (button)
    {
        case CF_MOUSE_BUTTON_LEFT: flags = MOUSEEVENTF_LEFTDOWN; break;
        case CF_MOUSE_BUTTON_RIGHT: flags = MOUSEEVENTF_RIGHTDOWN; break;
        case CF_MOUSE_BUTTON_MIDDLE: flags = MOUSEEVENTF_MIDDLEDOWN; break;
    }
    if (flags)
    {
        mouse_event(flags, 0, 0, 0, 0);
    }
}

void send_mouse_button_up(CF_MouseButton button)
{
    s32 flags = 0;
    switch (button)
    {
        case CF_MOUSE_BUTTON_LEFT: flags = MOUSEEVENTF_LEFTUP; break;
        case CF_MOUSE_BUTTON_RIGHT: flags = MOUSEEVENTF_RIGHTUP; break;
        case CF_MOUSE_BUTTON_MIDDLE: flags = MOUSEEVENTF_MIDDLEUP; break;
    }
    if (flags)
    {
        mouse_event(flags, 0, 0, 0, 0);
    }
}

CF_Rect process_window_rect(Process_Info process)
{
    RECT rect;
    GetWindowRect((HWND)process.window_handle.id, &rect);
    
    CF_Rect r = {
        .x = rect.left,
        .y = rect.top,
        .w = rect.right - rect.left,
        .h = rect.bottom - rect.top,
    };
    return r;
}

//  @todo:  switch over to using dxgi to capture
Process_Screenshot process_screenshot(Process_Info process, b32 include_window_frame)
{
    HDC handle_window_dc;
    b32 capture_success = true;
    
    process_focus(process);
    cf_sleep(30);
    HWND window_handle = (HWND)process.window_handle.id;
    
    CF_Rect region = process_window_rect(process);
    
    Process_Screenshot screenshot = { 0 };
    
    if (include_window_frame)
    {
        handle_window_dc = GetDC(NULL);
    }
    else
    {
        handle_window_dc = GetDC(window_handle);
    }
    HDC handle_memory_dc = CreateCompatibleDC(handle_window_dc);
    
    // get handle to window surface
    HBITMAP bitmap = CreateCompatibleBitmap(handle_window_dc, region.w, region.h);
    SelectObject(handle_memory_dc, bitmap);
    
    // try to copy sufrace to com object
    if (BitBlt(handle_memory_dc, 0, 0, region.w, region.h, handle_window_dc, region.x, region.y, SRCCOPY))
    {
        BITMAPINFO bitmap_info = { 0 };
        bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap_info.bmiHeader.biBitCount = 0;
        if (!GetDIBits(handle_memory_dc, bitmap, 0, 0, NULL, &bitmap_info, DIB_RGB_COLORS))
        {
            capture_success = false;
        }
        bitmap_info.bmiHeader.biHeight = -bitmap_info.bmiHeader.biHeight;
        bitmap_info.bmiHeader.biCompression = BI_RGB;
        
        screenshot.pixels = (CF_Pixel*)cf_calloc(sizeof(CF_Pixel), region.w * region.h);
        
        // blit com object to pixels
        if (!GetDIBits(handle_memory_dc, bitmap, 0, region.h, screenshot.pixels, &bitmap_info, DIB_RGB_COLORS))
        {
            capture_success = false;
        }
    }
    else
    {
        capture_success = false;
    }
    
    // clean up
    DeleteObject(bitmap);
    ReleaseDC(window_handle, handle_window_dc);
    DeleteDC(handle_memory_dc);
    
    if (capture_success)
    {
        // bgra -> rgba
        s32 count = region.w * region.h;
        u8 *pixels = (u8*)screenshot.pixels;
        for (s32 index = 0; index < count; ++index)
        {
            SWAP(screenshot.pixels[index].r, screenshot.pixels[index].b);
        }
        screenshot.w = region.w;
        screenshot.h = region.h;
    }
    else
    {
        destroy_process_screenshot(&screenshot);
    }
    
    return screenshot;
}

void destroy_process_screenshot(Process_Screenshot* screenshot)
{
    if (screenshot)
    {
        if (screenshot->pixels)
        {
            cf_free(screenshot->pixels);
        }
        MEMZERO(screenshot);
    }
}

#endif //_WIN32
#endif //PLATFORM_WINDOWS_H
