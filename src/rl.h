#pragma once
#include "common.h"

void set_frame_rate(s32 frame_rate);
void window_create(s32 width, s32 height, const char *title);
b32 window_should_close();
void window_close();

void renderer_begin(Color32 color);
void renderer_end();
AppTexture renderer_push_image(u8 *image_buffer, Aabbi bounds);
AppRenderTexture renderer_render_texture_make();
void renderer_set_clipboard_texture(AppTexture texture);
void renderer_unload_texture(AppTexture texture);
void renderer_unload_render_texture(AppRenderTexture texture);

s32 imgui_get_mouse_wheel(b32 invert);
struct ImVec2 imgui_get_mouse_drag();

void do_scanned_points_table(VisualizerCtx *ctx,  f32 x, f32 y, f32 width, f32 height, b32 is_filter_valid);
void do_world_filter(VisualizerCtx *ctx, b32 add_draw_region);
b32 do_poi_table(VisualizerCtx *ctx, PointOfInterest *interests, s32 *count, const char *name, f32 table_width, ImVec2 table_size);

b32 do_window(VisualizerCtx *ctx);
void do_fling_window(VisualizerCtx *ctx);
void do_map_window(VisualizerCtx *ctx);
b32 do_auto_map_window(VisualizerCtx *ctx);
b32 do_settings_window(VisualizerCtx *ctx);
b32 do_about_window(VisualizerCtx *ctx);

void directory_init();
const char *directory_get();
const char *directory_get_relative();
void directory_push(const char *name);
void directory_pop();
const char* path_get_ext(const char *path);
const char* path_get_file_name(const char *path);

void create_default_textures();
b32 load_assets(Arena *arena, VisualizerCtx *ctx);
b32 load_default_assets(Arena *arena, VisualizerCtx *ctx);
Aabbf calculate_max_world_region(Hashmap *world);

struct ImVec2;

void texture_get_zoom_uv(AppTexture *texture, ImVec2 size, ImVec2 focus_point, ImVec2 *out_uv0, ImVec2 *out_uv1);