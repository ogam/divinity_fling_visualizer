#ifndef PLATFORM_H
#define PLATFORM_H

typedef struct Process_Handle
{
    u64 id;
} Process_Handle;

typedef struct Window_Handle
{
    u64 id;
} Window_Handle;

typedef struct Module_Info
{
    str8 name;
    u64 base_address;
    u64 size;
} Module_Info;

typedef struct Process_Info
{
    Process_Handle handle;
    Window_Handle window_handle;
    CF_ARRAY(Module_Info) modules;
    
    // incase system is x86 (4) or x64 (8)
    s32 pointer_size;
    s32 thread_id;
    u32 process_id;
    
    str8 name;
    str8 file_name;
    str8 file_path;
    str8 version;
    
    u32 version_major;
    u32 version_minor;
    u32 version_build;
    u32 version_private;
    
    CF_Arena* arena;
} Process_Info;

typedef struct Signature_Scanner
{
    u8* memory;
    u64 size;
} Signature_Scanner;

typedef struct Signature_Query
{
    u8* signature;
    s32 signature_length;
    s32 search_start;
    s32 search_end;
} Signature_Query;

typedef struct Process_Screenshot
{
    CF_Pixel* pixels;
    s32 w;
    s32 h;
} Process_Screenshot;

//  @todo:  process pause / resume
//          https://stackoverflow.com/questions/11010165/how-to-suspend-resume-a-process-in-windows
//          NtSuspendProcess / NtResumeProcess (not documented on microsoft..)
//          https://ntopcode.wordpress.com/2018/01/16/anatomy-of-the-thread-suspension-mechanism-in-windows-windows-internals/
//          SuspendThread / ResumeThread (not ideal since this is time sensitive and we can't wait for OS to pause each individual thread)
//          https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-suspendthread
//          DebugActiveProcess (no idea about this one yet, requires process to be created with DEBUG_ONLY_THIS_PROCESS which most likely not available)
//          https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-debugactiveprocess?redirectedfrom=MSDN

void process_focus(Process_Info process_info);
void close_process_hook(Process_Info process_info);
Process_Info find_process(const char* process_name, CF_Arena* arena);
b32 is_process_alive(Process_Info process_info);
Signature_Scanner signature_scan_begin(Process_Info process_info, Module_Info module);
void signature_scan_end(Signature_Scanner* scanner);
u64 signature_scan(Process_Info process_info, Signature_Scanner scanner, Signature_Query query);
u64 process_deref(Process_Info process_info, CF_ARRAY(u64) offsets, void* out_ptr, u64 ptr_size);

b32 async_get_key_down(CF_KeyButton key);
b32 async_get_mouse_down(CF_MouseButton button);
void desktop_mouse_position(s32* out_x, s32* out_y);

void send_keyboard_down(CF_KeyButton key);
void send_keyboard_up(CF_KeyButton key);
void send_mouse_position_absolute(s32 x, s32 y);
void send_mouse_position_relative(s32 dx, s32 dy);
void send_mouse_button_down(CF_MouseButton button);
void send_mouse_button_up(CF_MouseButton button);

CF_Rect process_window_rect(Process_Info process);
Process_Screenshot process_screenshot(Process_Info process, b32 include_window_frame);
void destroy_process_screenshot(Process_Screenshot* screenshot);

#endif //PLATFORM_H
