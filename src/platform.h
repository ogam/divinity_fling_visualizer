#pragma once
#include "common.h"

enum KeyEvent
{
    KeyEvent_Extended = 0x0001,
    KeyEvent_KeyUp = 0x0002
};

enum MouseButton
{
    MouseButton_Left,
    MouseButton_Right,
    MouseButton_Middle,
};

void platform_init(Arena arena);
//  using winapi for keyboard and mouse events since application is checking for global hostkeys instead
//  of using only local ones with raylib
b32 key_is_down(s32 key_code);
void platform_key_name(s32 virtual_key, String *string);

//  these push events are desktop wide, so make sure to focus on the game first before sending these out
//  sending inputs directly to the game doesn't seem to do anything (SendInput, SendMessage)
//  keybd_event, mouse_event (button clicks), GetCursorPos and SetCursorPos (mouse position) seems to
//  actually affect the game

V2i get_cursor_position();
f64 get_time();
u32 get_scan_code(u32 key_code);

void push_keyboard_event(u8 vk, u8 sc, s32 flags, u32 *extra_info);
void push_mouse_event(s32 flags, s32 dx, s32 dy, s32 data, u32* extra_info);
void push_mouse_move_absolute(V2i position);
void push_mouse_move_relative(V2i position);
void push_mouse_button(MouseButton button, b32 is_down);

void process_info_init();
b32 process_is_active();
b32 process_find(StringCollection *process_names);
void process_focus();

void process_signature_scanner_begin();
void process_signature_scanner_end();
u64 process_signature_scan(Signature *signature);

u64 process_scan_memory(u64 address, void *out_buffer, u64 out_buffer_size);
u64 process_scan_memory_ex(u64 *address_offsets, s32 count, void *out_buffer, u64 out_buffer_size);
Aabbi process_window_size();

//  currently on windows 10 and 11 when you capture another window you fail to get a screen capture, so for now resort to capturing it from
//  the desktop space rather than window space. might be either a permission issue for not owning the child process or need additional
//  permissions when grabbing the process hwnd
b32 process_screenshot(void**buffer, Aabbi region, b32 include_window_frame);
b32 process_is_gog();
b32 process_is_dx11();
const char *process_get_window_title();
const char *process_get_version(u32 *version_major, u32 *version_minor, u32 *version_build, u32 *version_private);
const char *process_get_file_name();

ClipboardImage clipboard_get_image();