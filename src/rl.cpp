#include "rl.h"
#include "raylib.h"

#pragma warning(disable : 4996)

#include "imgui.h"
#include "imgui_impl_raylib.h"
#define NO_FONT_AWESOME
#include "rlImGui.h"
#include "rlImGuiColors.h"

static String WORK_PATH;
static s32 WORK_PATH_DEPTH;
static Hashmap *CACHED_TEXTURES;
static const char *DEFAULT_TEXTURE_NAME = "__default";
static const char *CLIPBOARD_TEXTURE_NAME = "__clipboard";
static Camera2D camera;

void set_frame_rate(s32 frame_rate)
{
    SetTargetFPS(60);
}

void camera_init()
{
    camera = {};
    camera.zoom = 1.0f;
    camera.offset.x = GetScreenWidth() * 0.5f;
    camera.offset.y = GetScreenHeight() * 0.5f;
}

void window_create(s32 width, s32 height, const char *title)
{
    InitWindow(width, height, title);
    rlImGuiSetup(true);
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    ImGui::StyleColorsDark();
    ImGui_ImplRaylib_Init();
    io.Fonts->AddFontDefault();
    Imgui_ImplRaylib_BuildFontAtlas();
    
    io.IniFilename = nullptr;
    
    SetExitKey(0);
    directory_init();
    
    camera_init();
}

b32 window_should_close()
{
    return WindowShouldClose();
}

void window_close()
{
    ImGui_ImplRaylib_Shutdown();
    ImGui::DestroyContext();
    CloseWindow();
}

void renderer_begin(Color32 color)
{
    ImGui_ImplRaylib_ProcessEvents();
    ImGui_ImplRaylib_NewFrame();
    ImGui::NewFrame();
    BeginDrawing();
    ClearBackground(*(Color*)&color);
}

void renderer_end()
{
    ImGui::Render();
    ImGui_ImplRaylib_RenderDrawData(ImGui::GetDrawData());
    EndDrawing();
}

AppTexture renderer_push_image(u8 *image_buffer, Aabbi bounds)
{
    V2i size = aabbi_size(bounds);
    Image image = {};
    image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    image.width = size.x;
    image.height = size.y;
    image.mipmaps = 1;
    Texture texture = LoadTextureFromImage(image);
    
    UpdateTexture(texture, image_buffer);
    
    return *(AppTexture*)&texture;
}

AppRenderTexture renderer_render_texture_make()
{
    RenderTexture2D target = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
    return *(AppRenderTexture*)&target;
}

void renderer_set_clipboard_texture(AppTexture texture)
{
    if (texture.id)
    {
        if (AppTexture *cached_texture = (AppTexture*)hashmap_get(CACHED_TEXTURES, CLIPBOARD_TEXTURE_NAME))
        {
            UnloadTexture(*(Texture2D*)cached_texture);
        }
        hashmap_set(CACHED_TEXTURES, CLIPBOARD_TEXTURE_NAME, &texture);
    }
}

void renderer_unload_texture(AppTexture texture)
{
    UnloadTexture(*(Texture*)&texture);
}

void renderer_unload_render_texture(AppRenderTexture texture)
{
    UnloadRenderTexture(*(RenderTexture*)&texture);
}

ImVec2 imgui_get_mouse_drag()
{
    ImVec2 drag_delta = {};
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        // reset drag amount otherwise it'll be based off of where we initially clicked and panning will be very uncontrollable
        ImGui::ResetMouseDragDelta();
    }
    return drag_delta;
}

s32 imgui_get_mouse_wheel(b32 invert)
{
    f32 mouse_wheel_value = ImGui::GetIO().MouseWheel;
    s32 mouse_wheel = mouse_wheel_value > 0 ? 1 : mouse_wheel_value < 0 ? -1 : 0;
    if (invert) 
    {
        mouse_wheel = -mouse_wheel;
    }
    return mouse_wheel;
}

void do_scanned_points_table(VisualizerCtx *ctx,  f32 x, f32 y, f32 width, f32 height)
{
    // node points
    {
        String buffer;
        WorldFilter filter_type = ctx->filter_type;
        
        ImU32 highlight_color = 0xFF2F2FFF;
        ImGui::SetNextWindowPos({x, y}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({width, height}, ImGuiCond_Always);
        ImGui::BeginChild("##points");
        {
            ImGuiTableFlags table_flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders;
            ImGui::BeginTable("Coordinates", 1, table_flags, {width, height}, width - 2.0f);
            
            s32 collection_count = ctx->recorded_screen_positions.count;
            s32 selected_record_index = ctx->selected_record_index;
            s32 next_selected_record_index = selected_record_index;
            for (s32 index = 0; index < collection_count; ++index)
            {
                V2i screen_position = ctx->recorded_screen_positions.points[index];
                V3f world_position = ctx->recorded_world_positions.points[index];
                b32 is_selected = ctx->selected_record_index == index;
                
                if (!aabbf_filtered(ctx->filter_world_region, world_position, filter_type, is_selected))
                {
                    continue;
                }
                
                string_printf(&buffer, "%7.02f %7.02f %7.02f | %4d %4d", world_position.x, world_position.y, world_position.z, screen_position.x, screen_position.y);
                
                if (selected_record_index == index)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, highlight_color);
                }
                ImGui::TableNextRow(0, 15.0f);
                ImGui::TableNextColumn();
                if (ImGui::Button(buffer.str))
                {
                    next_selected_record_index = index;
                }
                
                if (selected_record_index == index)
                {
                    ImGui::PopStyleColor();
                }
            }
            ImGui::EndTable();
            if (next_selected_record_index != selected_record_index)
            {
                ctx->selected_record_index = next_selected_record_index;
            }
            
        }
        ImGui::EndChild();
    }
}

void do_world_filter(VisualizerCtx *ctx, b32 add_draw_region)
{
    // world
    ImVec2 region_text_size = ImGui::CalcTextSize("World Region Max");
    f32 desired_item_width = region_text_size.x + 40.0f;
    {
        ImGui::PushItemWidth(desired_item_width);
        ImGui::InputFloat3("Filter Region Min", (float*)&ctx->filter_world_region.min, "%.2f", ImGuiInputTextFlags_CharsDecimal);
        ImGui::InputFloat3("Filter Region Max", (float*)&ctx->filter_world_region.max, "%.2f", ImGuiInputTextFlags_CharsDecimal);
        
        if (add_draw_region)
        {
            if (ImGui::Button("Draw"))
            {
                ctx->current_drawing_aabbf = &ctx->filter_world_region;
            }
            ImGui::SameLine();
            if (ImGui::Button("Focus"))
            {
                if (ctx->selected_record_index < ctx->recorded_world_positions.count)
                {
                    V3f target = ctx->recorded_world_positions.points[ctx->selected_record_index];
                    camera.target.x = target.x;
                    camera.target.y = target.z;
                }
                else
                {
                    V3f size = aabbf_size(ctx->filter_world_region);
                    camera.target.x = ctx->filter_world_region.min.x + size.x * 0.5f;
                    camera.target.y = ctx->filter_world_region.min.z + size.z * 0.5f;
                }
            }
            ImGui::SameLine();
            ImGui::Checkbox("Draw Scan Samples", (bool*)&ctx->map_draw_scan_samples);
        }
        ImGui::PopItemWidth();
        
        ImGui::Text("Filter");
        ImGui::SameLine();
        if (ImGui::RadioButton("None", ctx->filter_type == WorldFilter_None)) ctx->filter_type = WorldFilter_None;
        ImGui::SetItemTooltip("Ignores filter");
        ImGui::SameLine();
        if (ImGui::RadioButton("In", ctx->filter_type == WorldFilter_In)) ctx->filter_type = WorldFilter_In;
        ImGui::SetItemTooltip("Filters to only show points inside of World Region Bounds");
        ImGui::SameLine();
        if (ImGui::RadioButton("Out", ctx->filter_type == WorldFilter_Out)) ctx->filter_type = WorldFilter_Out;
        ImGui::SetItemTooltip("Filters to only show points outside of World Region Bounds");
        ImGui::SameLine();
        if (ImGui::RadioButton("Select", ctx->filter_type == WorldFilter_Selected)) ctx->filter_type = WorldFilter_Selected;
        ImGui::SetItemTooltip("Filters to only selected");
        ImGui::Separator();
    }
}

//  @todo:  this is pretty shit, but it's a direct copy from the other stuff so redo this on next UI pass
b32 do_poi_table(VisualizerCtx *ctx, PointOfInterest *interests, s32 *count, const char *name, f32 table_width, ImVec2 table_size)
{
    String poi_buffer;
    String id_buffer;
    b32 poi_ui_result = PointOfInterestUIResult_None;
    
    ImGuiTableFlags table_flags = ImGuiTableFlags_ScrollY |  ImGuiTableFlags_Borders;
    ImGui::Text(name);
    {
        bool draw_regions = ctx->level_details_draw_type & LevelDrawType_Regions;
        ImGui::SameLine();
        string_printf(&id_buffer, "Draw##draw_%s", name);;
        if (ImGui::Checkbox(id_buffer.str, &draw_regions))
        {
            ctx->level_details_draw_type &= ~LevelDrawType_Regions;
            if (draw_regions)
            {
                ctx->level_details_draw_type |= LevelDrawType_Regions;
            }
        }
        ImGui::SameLine();
        string_printf(&id_buffer, "Add##level_%s_add", name);;
        if (ImGui::Button(id_buffer.str))
        {
            if (*count < POI_CAPACITY)
            {
                interests[*count].is_selected = true;
                ctx->current_drawing_aabbf = &interests[*count].region;
                (*count)++;
            }
        }
        
        ImGui::SameLine();
        string_printf(&id_buffer, "Auto Map##level_%s_auto_map", name);
        if (ImGui::Button(id_buffer.str))
        {
            poi_ui_result = PointOfInterestUIResult_Auto_Map;
        }
        ImGui::SetItemTooltip("Uses clipboard images or loaded images to rescale to world space");
        
        ImGui::SameLine();
        string_printf(&id_buffer, "Export##level_%s_export", name);
        if (ImGui::Button(id_buffer.str))
        {
            poi_ui_result = PointOfInterestUIResult_Export;
        }
        
        string_printf(&id_buffer, "%s##level_%s", name, name);;
        if (ImGui::BeginTable(id_buffer.str, 2, table_flags, {table_width, table_size.y}, table_width - 2.0f))
        {
            {
                ImGui::TableSetupColumn("##Interest", ImGuiTableColumnFlags_WidthFixed, table_width - 45);
                ImGui::TableSetupColumn("##Removal", ImGuiTableColumnFlags_WidthFixed, 10);
            }
            
            s32 removal_index = *count;
            for (s32 index = 0; index < *count; ++index)
            {
                PointOfInterest *interest = interests + index;
                ImGui::TableNextColumn();
                string_printf(&id_buffer, "%s##%s_poi_header_%d", interest->name.str, name, index);
                ImGui::SetNextItemOpen(interest->is_selected);
                if (ImGui::CollapsingHeader(id_buffer.str))
                {
                    interest->is_selected = true;
                    string_printf(&id_buffer, "Name##%s_poi_name_%d", name, index);
                    string_printf(&poi_buffer, interest->name.str);
                    if (ImGui::InputText(id_buffer.str, poi_buffer.str, sizeof(poi_buffer.str)))
                    {
                        string_printf(&interest->name, poi_buffer.str);
                    }
                    {
                        AppTexture *poi_texture = (AppTexture*)hashmap_get(CACHED_TEXTURES, interest->image_file.str);
                        s32 texture_count = CACHED_TEXTURES->count;
                        
                        const char **texture_names = (const char **)CACHED_TEXTURES->keys;
                        AppTexture *textures = (AppTexture*)*CACHED_TEXTURES->values;
                        
                        s32 texture_index = (s32)(poi_texture - textures);
                        
                        string_printf(&id_buffer, "Image##%s_poi_textures_%d", name, index);
                        if (ImGui::Combo(id_buffer.str, (int*)&texture_index, texture_names, texture_count))
                        {
                            interest->texture = textures[texture_index];
                            string_printf(&interest->image_file, texture_names[texture_index]);
                        }
                        ImGui::SameLine();
                        string_printf(&id_buffer, "##%s_poi_draw_%d", name, index);
                        ImGui::Checkbox(id_buffer.str, (bool*)&interest->draw);
                    }
                    
                    string_printf(&id_buffer, "Min##%s_min_%d", name, index);
                    ImGui::InputFloat3(id_buffer.str, (float*)&interest->region.min, "%.2f", ImGuiInputTextFlags_CharsDecimal);
                    string_printf(&id_buffer, "Max##%s_max_%d", name, index);
                    ImGui::InputFloat3(id_buffer.str, (float*)&interest->region.max, "%.2f", ImGuiInputTextFlags_CharsDecimal);
                    string_printf(&id_buffer, "Draw Bounds##%s_poi_draw_bounds_%d", name, index);
                    if (ImGui::Button(id_buffer.str))
                    {
                        ctx->current_drawing_aabbf = &interest->region;
                    }
                    ImGui::SameLine();
                    string_printf(&id_buffer, "Focus##%s_poi_focus_%d", name, index);
                    if (ImGui::Button(id_buffer.str))
                    {
                        V3f region_size = aabbf_size(interest->region);
                        camera.target.x = interest->region.min.x + region_size.x * 0.5f;
                        camera.target.y = interest->region.min.z + region_size.z * 0.5f;
                    }
                    ImGui::SetItemTooltip("Sets camera focus to center of region");
                    ImGui::SameLine();
                    string_printf(&id_buffer, "Set Filter##%s_set_world_region_%d", name, index);
                    if (ImGui::Button(id_buffer.str))
                    {
                        ctx->filter_world_region = interest->region;
                    }
                    ImGui::SetItemTooltip("Sets Filter to region");
                }
                else 
                {
                    interest->is_selected = false;
                }
                
                ImGui::TableNextColumn();
                string_printf(&id_buffer, "-##%s_removal_%d", name, index);
                if (ImGui::Button(id_buffer.str))
                {
                    removal_index = index;
                    if (ctx->current_drawing_aabbf == &interest->region)
                    {
                        ctx->current_drawing_aabbf = nullptr;
                    }
                }
            }
            if (removal_index < *count)
            {
                s32 copy_count = *count - removal_index - 1;
                memcpy(interests + removal_index, interests + removal_index + 1, sizeof(PointOfInterest) * copy_count);
                (*count)--;
            }
            
            ImGui::EndTable();
        }
    }
    
    return poi_ui_result;
}

b32 do_window(VisualizerCtx *ctx)
{
    b32 should_quit = false;
    
    if (!ctx->window_type)
    {
        ctx->window_type = WindowType_Node;
    }
    
    ImVec2 main_menu_size = {};
    if (ImGui::BeginMainMenuBar())
    {
        main_menu_size = ImGui::GetWindowSize();
        if (ImGui::MenuItem("Node"))
        {
            ctx->window_type &= ~WindowType_Map;
            ctx->window_type |= WindowType_Node;
            ctx->previous_tab_level = nullptr;
            ctx->current_tab_level = nullptr;
            ctx->auto_map_window_state = AutoMapWindow_None;
        }
        if (ImGui::MenuItem("Map"))
        {
            ctx->window_type &= ~WindowType_Node;
            ctx->window_type |= WindowType_Map;
            ctx->previous_tab_level = nullptr;
            ctx->current_tab_level = nullptr;
        }
        if (ImGui::MenuItem("Load Assets"))
        {
            if (Hashmap *world = load_assets(&ctx->arena))
            {
                ctx->world = world;
                ctx->max_world_region = calculate_max_world_region(world);
            }
            else
            {
                TraceLog(LOG_ERROR, "Failed to load assets!");
            }
        }
        if (ImGui::MenuItem("Force Rescan"))
        {
            ctx->force_rescan_memory = true;
            ImGui::SetItemTooltip("If level or world position becomes stale (no longer updating from game), forcefully rescan these addresses");
        }
        if (ImGui::MenuItem("Settings"))
        {
            ctx->window_type ^= WindowType_Settings;
        }
        if (ImGui::MenuItem("Exit"))
        {
            should_quit = true;
        }
        
        f32 next_cursor_position = main_menu_size.x - ImGui::CalcTextSize("About").x - 12;
        ImGui::SetCursorPosX(next_cursor_position);
        
        if (ImGui::MenuItem("About"))
        {
            ctx->window_type ^= WindowType_About;
        }
        ImGui::EndMainMenuBar();
    }
    
    bool show = true;
    ImGui::SetNextWindowPos({0.0f, main_menu_size.y}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({(f32)GetScreenWidth(), (f32)GetScreenHeight()}, ImGuiCond_Always);
    ImGui::Begin("##main_window", &show, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
    {
        if (ctx->window_type & WindowType_Node)
        {
            do_fling_window(ctx);
        }
        if (ctx->window_type & WindowType_Map)
        {
            do_map_window(ctx);
        }
    }
    ImGui::End();
    
    //  settings window should be spearate from the main one that doesn't have any borders
    if (ctx->window_type & WindowType_Settings)
    {
        if (!do_settings_window(ctx))
        {
            ctx->window_type &= ~WindowType_Settings;
        }
    }
    
    if (ctx->window_type & WindowType_About)
    {
        if (!do_about_window(ctx))
        {
            ctx->window_type &= ~WindowType_About;
        }
    }
    
    if (ctx->auto_map_window_state)
    {
        if (!do_auto_map_window(ctx))
        {
            ctx->auto_map_window_state = AutoMapWindow_None;
        }
    }
    else
    {
        ctx->current_auto_map_poi = nullptr;
    }
    
    return should_quit;
}

//  @todo:  remap function since this is done multiple areas in here
//          f32_remap(old_range_min, old_range_max, old_range_value, new_range_min, new_range_max)

void do_fling_window(VisualizerCtx *ctx)
{
    WorldFilter filter_type = ctx->filter_type;
    ImVec2 image_size = {960.0f, 540.0f};
    
    V2i game_screen_size = aabbi_size(ctx->game_screen_bounds);
    ImVec2 screen_size = {(f32)game_screen_size.x, (f32)game_screen_size.y};
    
    ImVec2 imgui_mouse_position = ImGui::GetMousePos();
    ImVec2 zoom_mouse_position = imgui_mouse_position;
    
    b32 is_mouse_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    b32 is_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    b32 is_mouse_release = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    s32 mouse_wheel = imgui_get_mouse_wheel(ctx->mouse_wheel_invert);
    s32 previous_zoom_level = ctx->zoom_level;
    
    {
        if (ctx->zoom_level == 0)
        {
            ctx->focus_point.x = (s16)(screen_size.x * 0.5f);
            ctx->focus_point.y = (s16)(screen_size.y * 0.5f);
        }
    }
    
    f32 zoom_levels[5] = {
        1.0f,
        0.9f,
        0.8f,
        0.7f,
        0.5f
    };
    
    f32 zoom = zoom_levels[ctx->zoom_level];
    ImVec2 zoom_size = {screen_size.x * zoom, screen_size.y * zoom};
    ImVec2 zoom_half_extents = {zoom_size.x * 0.5f, zoom_size.y * 0.5f};
    ImVec2 zoom_center = {(f32)ctx->focus_point.x, (f32)ctx->focus_point.y};
    ImVec2 zoom_min = {zoom_center.x - zoom_half_extents.x, zoom_center.y - zoom_half_extents.y};
    ImVec2 zoom_max = {zoom_center.x + zoom_half_extents.x, zoom_center.y + zoom_half_extents.y};
    ImVec2 delta = {};
    if (zoom_min.x < 0)
    {
        delta.x = -zoom_min.x;
    }
    if (zoom_min.y < 0)
    {
        delta.y = -zoom_min.y;
    }
    if (zoom_max.x > screen_size.x)
    {
        delta.x = screen_size.x - zoom_max.x;
    }
    if (zoom_max.y > screen_size.y)
    {
        delta.y = screen_size.y - zoom_max.y;
    }
    zoom_min.x += delta.x;
    zoom_min.y += delta.y;
    zoom_max.x += delta.x;
    zoom_max.y += delta.y;
    zoom_center.x += delta.x;
    zoom_center.y += delta.y;
    
    ImVec2 screenshot_window_position = {};
    ImVec2 screenshot_window_size = {};
    
    {
        ImVec2 image_uv0 = {};
        ImVec2 image_uv1 = {1.0f, 1.0f};
        
        f32 circle_radius = 3.0f;
        ImU32 circle_color = ctx->circle_color;
        ImU32 circle_highlight_color = ctx->selected_circle_color;
        ImGui::BeginChild("##screen_shot", image_size);
        {
            screenshot_window_position = ImGui::GetWindowPos();
            screenshot_window_size = ImGui::GetWindowSize();
            
            ImVec2 image_min = image_size;
            ImVec2 image_max = image_size;
            
            AppTexture *texture = &ctx->texture;
            if (!ctx->texture.id)
            {
                texture = (AppTexture*)hashmap_get(CACHED_TEXTURES, DEFAULT_TEXTURE_NAME);
            }
            {
                texture_get_zoom_uv(&ctx->texture, zoom_size, zoom_center, &image_uv0, &image_uv1);
                ImGui::Image((ImTextureID)texture, image_size, image_uv0, image_uv1);
                image_min = ImGui::GetItemRectMin();
                image_max = ImGui::GetItemRectMax();
                
                // zoom_mouse_position needs to be updated here for the image_min, otherwise everything will be off..
                zoom_mouse_position.x -= image_min.x;
                zoom_mouse_position.y -= image_min.y;
                zoom_mouse_position.x /= image_size.x;
                zoom_mouse_position.y /= image_size.y;
                zoom_mouse_position.x = zoom_mouse_position.x * zoom_size.x + zoom_min.x;
                zoom_mouse_position.y = zoom_mouse_position.y * zoom_size.y + zoom_min.y;
            }
            
            // update mouse panning and zoom
            if (ImGui::IsMouseHoveringRect(image_min, image_max) && !ctx->is_redrawing_region)
            {
                ImVec2 drag_delta = imgui_get_mouse_drag();
                ctx->focus_point.x -= (s16)drag_delta.x;
                ctx->focus_point.y -= (s16)drag_delta.y;
                
                if (mouse_wheel)
                {
                    ctx->zoom_level = CLAMP(ctx->zoom_level + mouse_wheel, 0, 4);
                    if (previous_zoom_level != ctx->zoom_level)
                    {
                        ImVec2 image_position = {imgui_mouse_position.x - image_min.x, imgui_mouse_position.y - image_min.y};
                        image_position.x /= image_size.x;
                        image_position.y /= image_size.y;
                        image_position.x *= zoom_size.x;
                        image_position.y *= zoom_size.y;
                        ImVec2 delta = {};
                        delta.x = (screen_size.x - zoom_size.x) * 0.5f;
                        delta.y = (screen_size.y - zoom_size.y) * 0.5f;
                        image_position.x += delta.x;
                        image_position.y += delta.y;
                        
                        // midpoint so zooming in/out isn't as jarring
                        // should probably be a smooth factor based on zoom level
                        ctx->focus_point.x += (s16)(image_position.x - ctx->focus_point.x) / 2;
                        ctx->focus_point.y += (s16)(image_position.y - ctx->focus_point.y) / 2;
                    }
                }
            }
            
            ImDrawList *draw_list = ImGui::GetWindowDrawList();
            
            s32 collection_count = ctx->recorded_screen_positions.count;
            for (s32 index = 0; index < collection_count; ++index)
            {
                V2i screen_position = ctx->recorded_screen_positions.points[index];
                V3f world_position = ctx->recorded_world_positions.points[index];
                f32 x = (f32)screen_position.x;
                f32 y = (f32)screen_position.y;
                
                ImU32 color = circle_color;
                b32 is_selected = ctx->selected_record_index == index;
                
                if (x < zoom_min.x || y < zoom_min.y || x > zoom_max.x || y > zoom_max.y)
                {
                    continue;
                }
                
                if (!aabbf_filtered(ctx->filter_world_region, world_position, filter_type, is_selected))
                {
                    continue;
                }
                
                if (is_selected)
                {
                    color = circle_highlight_color;
                }
                
                ImVec2 draw_circle_center = {};
                draw_circle_center.x = (x - zoom_min.x) * image_size.x / zoom_size.x;
                draw_circle_center.y = (y - zoom_min.y) * image_size.y / zoom_size.y;
                
                draw_circle_center.x += image_min.x;
                draw_circle_center.y += image_min.y;
                
                f32 mouse_delta_x = imgui_mouse_position.x - draw_circle_center.x;
                f32 mouse_delta_y = imgui_mouse_position.y - draw_circle_center.y;
                
                
                if (mouse_delta_x >= -circle_radius && mouse_delta_x <= circle_radius && 
                    mouse_delta_y >= -circle_radius && mouse_delta_y <= circle_radius)
                {
                    ImGui::BeginTooltip();
                    {
                        ImGui::BeginChild("##tooltip");
                        {
                            String *region_name = nullptr;
                            String *object_name = nullptr;
                            AppTexture *region_texture = nullptr;
                            AppTexture *object_texture = nullptr;
                            Aabbf region_aabb;
                            Aabbf object_aabb;
                            String texture_key;
                            
                            if (ctx->world)
                            {
                                if (Level *level = (Level*)hashmap_get(ctx->world, ctx->level_name.str))
                                {
                                    for (s32 region_index = 0; region_index < level->regions_count; ++region_index)
                                    {
                                        PointOfInterest *interest = level->regions + region_index;
                                        if (aabbf_contains(interest->region, world_position))
                                        {
                                            region_name = &interest->name;
                                            if (interest->image_file.length)
                                            {
                                                string_printf(&texture_key, "%s", interest->image_file.str);
                                                region_texture = (AppTexture*)hashmap_get(CACHED_TEXTURES, texture_key.str);
                                                region_aabb = interest->region;
                                            }
                                            break;
                                        }
                                    }
                                    for (s32 object_index = 0; object_index < level->objects_count; ++object_index)
                                    {
                                        PointOfInterest *interest = level->objects + object_index;
                                        if (aabbf_contains(interest->region, world_position))
                                        {
                                            object_name = &interest->name;
                                            if (interest->image_file.length)
                                            {
                                                string_printf(&texture_key, "%s", interest->image_file.str);
                                                object_texture = (AppTexture*)hashmap_get(CACHED_TEXTURES, texture_key.str);
                                                object_aabb = interest->region;
                                            }
                                            break;
                                        }
                                    }
                                }
                            }
                            
                            // haven't seen any maps larger than 5k min/max so giving this same amount of character spaces
                            ImVec2 min_tooltip_size = ImGui::CalcTextSize("-1000.00, -1000.00, -1000.00");
                            
                            if (region_name)
                            {
                                ImGui::Text(region_name->str);
                            }
                            if (region_texture)
                            {
                                ImVec2 tooltip_image_size = {};
                                tooltip_image_size.x = min_tooltip_size.x;
                                tooltip_image_size.y = min_tooltip_size.x * 9.0f/16.0f;
                                
                                V3f region_size = aabbf_size(region_aabb);
                                V3f region_local = v3f_sub(world_position, region_aabb.min);
                                
                                // only care about the XZ plane so it represents the map correctly, y axis should all ready be culled off
                                ImVec2 focus_point = {region_local.x / region_size.x, region_local.z / region_size.z};
                                focus_point = {region_texture->width * focus_point.x, region_texture->height * focus_point.y};
                                ImVec2 uv0 = {};
                                ImVec2 uv1 = {1.0f, 1.0f};
                                texture_get_zoom_uv(region_texture, tooltip_image_size, focus_point, &uv0, &uv1);
                                uv0.y = 1.0f - uv0.y;
                                uv1.y = 1.0f - uv1.y;
                                f32 min_y = MIN(uv0.y, uv1.y);
                                f32 max_y = MAX(uv0.y, uv1.y);
                                uv0.y = min_y;
                                uv1.y = max_y;
                                
                                ImGui::Image((ImTextureID)region_texture, tooltip_image_size, uv0, uv1);
                            }
                            if (object_name)
                            {
                                ImGui::Text(object_name->str);
                            }
                            if (object_texture)
                            {
                                ImVec2 tooltip_image_size = {};
                                tooltip_image_size.x = min_tooltip_size.x;
                                tooltip_image_size.y = min_tooltip_size.x * 9.0f/16.0f;
                                
                                V3f object_size = aabbf_size(object_aabb);
                                V3f object_local = v3f_sub(world_position, object_aabb.min);
                                
                                ImVec2 focus_point = {object_local.x / object_size.x, object_local.z / object_size.z};
                                focus_point = {object_texture->width * focus_point.x, object_texture->height * focus_point.y};
                                ImVec2 uv0 = {};
                                ImVec2 uv1 = {1.0f, 1.0f};
                                texture_get_zoom_uv(object_texture, tooltip_image_size, focus_point, &uv0, &uv1);
                                uv0.y = 1.0f - uv0.y;
                                uv1.y = 1.0f - uv1.y;
                                f32 min_y = MIN(uv0.y, uv1.y);
                                f32 max_y = MAX(uv0.y, uv1.y);
                                uv0.y = min_y;
                                uv1.y = max_y;
                                
                                ImGui::Image((ImTextureID)object_texture, tooltip_image_size, uv0, uv1);
                            }
                            ImGui::Text("%.2f, %.2f, %.2f", world_position.x, world_position.y, world_position.z);
                        }
                        ImGui::EndChild();
                    }
                    ImGui::EndTooltip();
                    color = circle_highlight_color;
                    
                    if (is_mouse_clicked)
                    {
                        ctx->selected_record_index = index;
                    }
                }
                
                draw_list->AddCircle(draw_circle_center, circle_radius, color);
            }
            
            // drawing region rect on the screenshot image
            if (ctx->is_redrawing_region || ctx->show_region_outline)
            {
                if (ctx->ui_region_selection < ctx->region_count)
                {
                    Aabbi *region =  ctx->regions + ctx->ui_region_selection;
                    ImVec2 rect_min = {(f32)region->min.x, (f32)region->min.y};
                    ImVec2 rect_max = {(f32)region->max.x, (f32)region->max.y};
                    
                    ImVec2 draw_dot = {};
                    if (ctx->is_redrawing_region)
                    {
                        if (is_mouse_down)
                        {
                            rect_max = zoom_mouse_position;
                        }
                        else
                        {
                            rect_min = zoom_mouse_position;
                            rect_max = zoom_mouse_position;
                        }
                    }
                    
                    rect_min.x = (rect_min.x - zoom_min.x) * image_size.x / zoom_size.x;
                    rect_min.y = (rect_min.y - zoom_min.y) * image_size.y / zoom_size.y;
                    rect_max.x = (rect_max.x - zoom_min.x) * image_size.x / zoom_size.x;
                    rect_max.y = (rect_max.y - zoom_min.y) * image_size.y / zoom_size.y;
                    
                    // offset by image_min otherwise rect will be slightly off
                    rect_min.x += image_min.x;
                    rect_min.y += image_min.y;
                    rect_max.x += image_min.x;
                    rect_max.y += image_min.y;
                    
                    draw_list->AddRect(rect_min, rect_max, 0xFF0F4FFF, 0.0f, ImDrawFlags_None, 1.0f);
                }
            }
        }
        ImGui::EndChild();
    }
    ImVec2 screenshot_rect_min = ImGui::GetItemRectMin();
    
    ImVec2 table_position = {image_size.x + 15.0f, screenshot_rect_min.y};
    ImVec2 table_size = {(f32)GetScreenWidth() - table_position.x - 15.0f, 170.0f};
    do_scanned_points_table(ctx, table_position.x, table_position.y, table_size.x, table_size.y);
    
    ImVec2 region_text_size = ImGui::CalcTextSize("World Region Max");
    ImGui::SetNextWindowPos({table_position.x, table_position.y + table_size.y}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({region_text_size.x + 50, 200.0f});
    ImGui::BeginChild("##settings");
    {
        f32 desired_item_width = region_text_size.x + 40.0f;
        do_world_filter(ctx, false);
        
        // actions
        {
            // don't show tooltip while action is happening this is to avoid drawing a tooltip when currently scanning and imgui is no longer
            // getting mouse input
            if (ImGui::Button("Set"))
            {
                ctx->next_action = Action_Set_World_Position;
            }
            if (!ctx->action_co) ImGui::SetItemTooltip("Sets node position in the world");
            ImGui::SameLine();
            if (ImGui::Button("Scan"))
            {
                ctx->next_action = Action_Scan_Regions;
            }
            if (!ctx->action_co) ImGui::SetItemTooltip("Scan will always take a screenshot prior to walking through regions to give you an updated view");
            ImGui::SameLine();
            if (ImGui::Button("Screenshot"))
            {
                ctx->next_action = Action_Screenshot;
            }
            if (ctx->action_co)
            {
                ImGui::SameLine();
                ImGui::Text("%c", "-\\|/"[(s32)(ImGui::GetFrameCount() / 4) % 3]);
            }
            ImGui::Separator();
        }
        
        // screen space regions
        {
            s32 previous_region_count = ctx->region_count;
            if (ImGui::Button("Add Region"))
            {
                if (ctx->region_count < ARRAY_SIZE(ctx->regions))
                {
                    ctx->ui_region_selection = ctx->region_count++;
                    ctx->is_redrawing_region = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove Region"))
            {
                s32 shift_count = MAX(ctx->region_count - ctx->ui_region_selection - 1, 0);
                memcpy(ctx->regions + ctx->ui_region_selection, ctx->regions + ctx->ui_region_selection + 1, sizeof(Aabbi) * shift_count);
                ctx->region_count = MAX(ctx->region_count - 1, 0);
                ctx->ui_region_selection = CLAMP(ctx->ui_region_selection - 1, 0, ctx->region_count - 1);
            }
            ImGui::SameLine();
            if (ImGui::Button("<<"))
            {
                ctx->ui_region_selection = CLAMP(ctx->ui_region_selection - 1, 0, ctx->region_count - 1);
            }
            ImGui::SameLine();
            if (ImGui::Button(">>"))
            {
                ctx->ui_region_selection = CLAMP(ctx->ui_region_selection + 1, 0, ctx->region_count - 1);
            }
            ImGui::SameLine();
            ImGui::Text("%d", ctx->region_count);
            ImGui::SetItemTooltip("Region Count");
            if (ctx->is_redrawing_region)
            {
                // skip current frame mouse up to when adding or removing a region
                if (previous_region_count == ctx->region_count)
                {
                    if (ctx->ui_region_selection < ctx->region_count)
                    {
                        Aabbi *region =  ctx->regions + ctx->ui_region_selection;
                        if (is_mouse_clicked)
                        {
                            V2i min = {(s16)zoom_mouse_position.x, (s16)zoom_mouse_position.y};
                            region->min = min;
                        }
                        
                        if (is_mouse_release)
                        {
                            V2i max = {(s16)zoom_mouse_position.x, (s16)zoom_mouse_position.y};
                            *region = aabbi_make(region->min, max);
                            ctx->is_redrawing_region = false;
                        }
                    }
                    else
                    {
                        ctx->is_redrawing_region = false;
                    }
                }
            }
            
            if (ctx->ui_region_selection < ctx->region_count)
            {
                
                Aabbi *region = ctx->regions + ctx->ui_region_selection;
                ImGui::Text("Screen Region: %d", ctx->ui_region_selection);
                ImGui::SetItemTooltip("Regions for scanner to walk through");
                s32 min[2] = {(s16)region->min.x, (s16)region->min.y};
                s32 max[2] = {(s16)region->max.x, (s16)region->max.y};
                ImGui::PushItemWidth(desired_item_width);
                ImGui::InputInt2("Screen Region Min", (int*)min, ImGuiInputTextFlags_CharsDecimal);
                ImGui::InputInt2("Screen Region Max", (int*)max, ImGuiInputTextFlags_CharsDecimal);
                ImGui::PopItemWidth();
                
                region->min.x = (s16)min[0];
                region->min.y = (s16)min[1];
                region->max.x = (s16)max[0];
                region->max.y = (s16)max[1];
                
                if (ImGui::Button("Draw Region"))
                {
                    ctx->is_redrawing_region = true;
                }
                ImGui::SetItemTooltip("Draw on the screenshot to determine your region bounds");
                ImGui::SameLine();
                if (ImGui::Button("Set Max Size"))
                {
                    region->min = {};
                    region->max = game_screen_size;
                }
                ImGui::SetItemTooltip("Max size is game window resolution");
                ImGui::SameLine();
                ImGui::Checkbox("Outline", (bool*)&ctx->show_region_outline);
            }
            ImGui::Separator();
        }
        // color options
        {
            ImGui::PushItemWidth(desired_item_width);
            ImVec4 circle_color_v4 = ImGui::ColorConvertU32ToFloat4((ImU32)ctx->circle_color);
            ImVec4 selected_circle_color_v4 = ImGui::ColorConvertU32ToFloat4((ImU32)ctx->selected_circle_color);
            ImGui::ColorEdit3("Point Color", &circle_color_v4.x);
            ImGui::ColorEdit3("Selected Point Color", &selected_circle_color_v4.x);
            ctx->circle_color = (u32)ImGui::ColorConvertFloat4ToU32(circle_color_v4);
            ctx->selected_circle_color = (u32)ImGui::ColorConvertFloat4ToU32(selected_circle_color_v4);
            ImGui::PopItemWidth();
            ImGui::Separator();
        }
        // inputs
        {
            //  @todo:  do a popupmodal to let the user know to press a button to bind
            String input_buffer;
            
            ImGui::Checkbox("Global Hotkeys Enabled", (bool*)&ctx->global_hotkeys_enabled);
            ImGui::SetItemTooltip("Disables global hotkeys, `Cancel` will always be checked to avoid being stuck in `Scanning` mode");
            ImGui::Separator();
            {
                ImGui::Text("Inventory Position");
                ImGui::SameLine();
                string_printf(&input_buffer, "%d %d##set_inventory_position", ctx->inventory_screen_position.x, ctx->inventory_screen_position.y);
                if (ImGui::Button(input_buffer.str)) ctx->rebind_type = RebindType_Inventory_Position;
                ImGui::SetItemTooltip("Inventory Position to start dragging to set nodes or scan");
                ImGui::Separator();
            }
        }
        // other configs
        {
            ImGui::PushItemWidth(desired_item_width);
            ImGui::SliderInt("Scan Step Rate", (int*)&ctx->scan_step_rate, 1, 20);
            ImGui::SetItemTooltip("How many pixels per scan update, ideally only set this to low values < 5 with small regions");
            ImGui::SliderInt("Set Wiggle Amount", (int*)&ctx->wiggle_amount, 1, 20);
            ImGui::SetItemTooltip("Wiggles your mouse after set your fling position, needs a little nudge to get the world position updated");
            ImGui::PopItemWidth();
            ImGui::Checkbox("Stop On first Filtered", (bool*)&ctx->stop_on_first_filtered);
            ImGui::Separator();
        }
    }
    ImGui::EndChild();
    
    ImGui::SetNextWindowPos({screenshot_window_position.x, screenshot_window_position.y + screenshot_window_size.y});
    ImGui::BeginChild("##level_info");
    {
        ImGui::Text("Level: %s", ctx->level_name.str);
        ImGui::Text("Resolution: %d x %d", game_screen_size.x, game_screen_size.y);
        ImGui::Text("World Position: %.2f, %.2f, %.2f", ctx->position.x, ctx->position.y, ctx->position.z);
        if (ImGui::IsItemHovered())
        {
            if (ImGui::IsKeyChordPressed(ImGuiModFlags_Ctrl | ImGuiKey_C))
            {
                String clipboard_text;
                string_printf(&clipboard_text, "%.2f, %.2f, %.2f", ctx->position.x, ctx->position.y, ctx->position.z);
                SetClipboardText(clipboard_text.str);
            }
        }
        ImGui::SetItemTooltip("Ctrl + C to copy coordinates to clipboard");
        ImGui::Text("Mouse Position:     %5d, %5d", ctx->game_screen_position.x, ctx->game_screen_position.y);
        ImGui::Text("Raw Mouse Position: %5d, %5d", ctx->raw_mouse_position.x, ctx->raw_mouse_position.y);
        ImGui::Separator();
    }
    ImGui::EndChild();
}

void do_map_window(VisualizerCtx *ctx)
{
    BeginTextureMode(*(RenderTexture2D*)&ctx->map_texture);
    ClearBackground(BLACK);
    EndTextureMode();
    
    Level *current_level = nullptr;
    ImVec2 image_size = {960.0f, 540.0f};
    f32 aspect_ratio = 16.0f / 9.0f;
    f32 inverse_aspect_ratio = 1.0f / aspect_ratio;
    
    s32 window_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    Aabbf *world_bounds = &ctx->max_world_region;
    V3f world_size = aabbf_size(*world_bounds);
    V3f world_min = ctx->max_world_region.min;
    V3f world_max = ctx->max_world_region.max;
    const char *current_level_name = nullptr;
    
    ImGui::BeginChild("##levels", {}, 0, window_flags);
    {
        if (ImGui::BeginTabBar("##level_selections", ImGuiTabBarFlags_Reorderable))
        {
            if (ctx->world)
            {
                for (s32 index = 0; index < ctx->world->count; ++index)
                {
                    const char *level_name = (const char*)ctx->world->keys[index];
                    if (ImGui::BeginTabItem(level_name))
                    {
                        current_level = (Level*)ctx->world->values[index];
                        current_level_name = level_name;
                        if (current_level != ctx->previous_tab_level)
                        {
                            ctx->current_drawing_aabbf = nullptr;
                        }
                        ctx->current_tab_level = current_level;
                        ctx->previous_tab_level = current_level;
                        ImGui::EndTabItem();
                    }
                }
            }
            ImGui::EndTabBar();
        }
        
        ImVec2 level_map_window_position = {};
        ImVec2 level_map_window_size = {};
        f32 inverse_camera_zoom = 1.0f / camera.zoom;
        f32 raylib_line_thickness = inverse_camera_zoom * 3.0f;
        f32 raylib_circle_radius = inverse_camera_zoom * 3.0f;
        String tooltip_override = {};
        
        ImGui::BeginChild("##level_map", image_size);
        {
            level_map_window_position = ImGui::GetWindowPos();
            level_map_window_size = ImGui::GetWindowSize();
            ImVec2 window_min = level_map_window_position;
            ImVec2 window_max = {level_map_window_position.x + level_map_window_size.x, level_map_window_position.y + level_map_window_size.y};
            
            ImDrawList *draw_list = ImGui::GetWindowDrawList();
            
            if (current_level)
            {
                BeginTextureMode(*(RenderTexture2D*)&ctx->map_texture);
                BeginMode2D(camera);
                if (ctx->use_world_bounds)
                {
                    DrawRectangleLinesEx({world_bounds->min.x, world_bounds->min.z, world_size.x, world_size.z}, raylib_line_thickness, GREEN);
                }
                if (!ctx->use_world_bounds)
                {
                    world_bounds = &current_level->bounds;
                    world_size = aabbf_size(*world_bounds);
                    world_min = current_level->bounds.min;
                    world_max = current_level->bounds.max;
                    DrawRectangleLinesEx({world_bounds->min.x, world_bounds->min.z, world_size.x, world_size.z}, raylib_line_thickness, YELLOW);
                }
                
                EndMode2D();
                EndTextureMode();
                
                if (ImGui::IsMouseHoveringRect(window_min, window_max))
                {
                    ImVec2 sample_position = ImGui::GetMousePos();
                    sample_position.x -= level_map_window_position.x;
                    sample_position.y -= level_map_window_position.y;
                    sample_position.x = sample_position.x / image_size.x * GetScreenWidth();
                    sample_position.y = (1.0f - sample_position.y / image_size.y) * GetScreenHeight();
                    
                    Vector2 sample_world_position = GetScreenToWorld2D(*(Vector2*)&sample_position, camera);
                    ctx->map_mouse_position = {sample_world_position.x, 0.0f, sample_world_position.y};
                    
                    b32 take_sample = (ImGui::GetIO().KeyMods & ImGuiMod_Ctrl) != 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                    
                    if (take_sample)
                    {
                        ctx->world_sample_point = ctx->map_mouse_position;
                    }
                }
                
                b32 set_draw[2] = {
                    ctx->level_details_draw_type & LevelDrawType_Regions,
                    ctx->level_details_draw_type & LevelDrawType_Objects
                };
                
                PointOfInterest *set_interests[2] = {
                    current_level->regions,
                    current_level->objects
                };
                
                s32 set_count[2] = {
                    current_level->regions_count,
                    current_level->objects_count,
                };
                
                for (s32 set_index = 0; set_index < ARRAY_SIZE(set_count); ++set_index)
                {
                    if (!set_draw[set_index])
                    {
                        continue;
                    }
                    
                    PointOfInterest *interests = set_interests[set_index];
                    s32 count = set_count[set_index];
                    
                    BeginTextureMode(*(RenderTexture2D*)&ctx->map_texture);
                    BeginMode2D(camera);
                    for (s32 index = 0; index < count; ++index)
                    {
                        PointOfInterest *interest = interests + index;
                        //  raylib draws weird rects and doesn't draw texture correctly when min and max is not actually in the correct order
                        //  so going to recreate the aabbf since this can currently be redrawn
                        Aabbf draw_aabb = aabbf_make(interest->region.min, interest->region.max);
                        V3f region_size = aabbf_size(draw_aabb);
                        
                        if (interest->texture.id && interest->draw)
                        {
                            Rectangle source = {};
                            Rectangle destination = {};
                            source.width = (f32)interest->texture.width;
                            source.height = (f32)(-interest->texture.height);
                            destination.x = draw_aabb.min.x;
                            destination.y = draw_aabb.min.z;
                            destination.width = region_size.x;
                            destination.height = region_size.z;
                            
                            DrawTexturePro(*(Texture2D*)&interest->texture, source, destination, {}, 0, WHITE);
                        }
                        
                        DrawRectangleLinesEx({draw_aabb.min.x, draw_aabb.min.z, region_size.x, region_size.z}, raylib_line_thickness, WHITE);
                        
                        if (ctx->map_mouse_position.x >= draw_aabb.min.x && ctx->map_mouse_position.x <= draw_aabb.max.x &&
                            ctx->map_mouse_position.z >= draw_aabb.min.z && ctx->map_mouse_position.z <= draw_aabb.max.z)
                        {
                            string_printf(&tooltip_override, "%s\n%7.2f, %7.2f, %7.2f\n%7.2f, %7.2f, %7.2f", interest->name.str, 
                                          interest->region.min.x, interest->region.min.y, interest->region.min.z,
                                          interest->region.max.x, interest->region.max.y, interest->region.max.z);
                        }
                        
                    }
                    EndMode2D();
                    EndTextureMode();
                }
                
                // map region drawing
                {
                    ImGui::Image((ImTextureID)&ctx->map_texture.texture, image_size, {}, {1.0f, 1.0f});
                    ImVec2 image_min = ImGui::GetItemRectMin();
                    ImVec2 image_max = ImGui::GetItemRectMax();
                    
                    if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(image_min, image_max))
                    {
                        ImGui::BeginTooltip();
                        if (tooltip_override.length)
                        {
                            ImGui::Text(tooltip_override.str);
                        }
                        ImGui::Text("%7.2f, %7.2f, %7.2f", ctx->map_mouse_position.x, ctx->map_mouse_position.y, ctx->map_mouse_position.z);
                        ImGui::EndTooltip();
                        s32 mouse_wheel = imgui_get_mouse_wheel(ctx->mouse_wheel_invert);
                        
                        if (ctx->current_drawing_aabbf)
                        {
                            BeginTextureMode(*(RenderTexture2D*)&ctx->map_texture);
                            BeginMode2D(camera);
                            {
                                Aabbf draw_aabb = aabbf_make(ctx->current_drawing_aabbf->min, ctx->current_drawing_aabbf->max);
                                V3f draw_size = aabbf_size(draw_aabb);
                                DrawRectangleLinesEx({draw_aabb.min.x, draw_aabb.min.z, draw_size.x, draw_size.z}, raylib_line_thickness, BLUE);
                            }
                            EndMode2D();
                            EndTextureMode();
                            
                            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                            {
                                f32 min_y = ctx->current_drawing_aabbf->min.y;
                                f32 max_y = ctx->current_drawing_aabbf->max.y;
                                ctx->current_drawing_aabbf->min = ctx->map_mouse_position;
                                ctx->current_drawing_aabbf->max = ctx->map_mouse_position;
                                
                                ctx->current_drawing_aabbf->min.y = min_y;
                                ctx->current_drawing_aabbf->max.y = max_y;
                            }
                            else if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                            {
                                f32 max_y = ctx->current_drawing_aabbf->max.y;
                                ctx->current_drawing_aabbf->max = ctx->map_mouse_position;
                                ctx->current_drawing_aabbf->max.y = max_y;
                            }
                            else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                            {
                                *ctx->current_drawing_aabbf = aabbf_make(ctx->current_drawing_aabbf->min, ctx->current_drawing_aabbf->max);
                                ctx->current_drawing_aabbf = nullptr;
                            }
                        }
                        else
                        {
                            if (ImGui::IsWindowHovered())
                            {
                                camera.zoom = CLAMP(camera.zoom + mouse_wheel * 0.2f, 0.2f, 10.0f);
                                ImVec2 drag = imgui_get_mouse_drag();
                                ImVec2 drag_offset = {drag.x / -camera.zoom, drag.y / -camera.zoom};
                                camera.target.x += drag_offset.x;
                                camera.target.y -= drag_offset.y;
                            }
                        }
                    }
                }
                
                {
                    BeginTextureMode(*(RenderTexture2D*)&ctx->map_texture);
                    BeginMode2D(camera);
                    
                    ImU32 circle_color = (ImU32)ctx->circle_color;
                    if (aabbf_contains(*world_bounds, ctx->world_sample_point))
                    {
                        V3f position = ctx->world_sample_point;
                        DrawCircleV({position.x, position.z}, raylib_circle_radius, *(Color*)&circle_color);
                    }
                    
                    if (aabbf_contains(*world_bounds, ctx->position))
                    {
                        V3f position = ctx->position;
                        DrawCircleV({position.x, position.z}, raylib_circle_radius, *(Color*)&circle_color);
                    }
                    
                    if (ctx->map_draw_scan_samples)
                    {
                        for (s32 index = 0; index < ctx->recorded_world_positions.count; ++index)
                        {
                            V3f position = ctx->recorded_world_positions.points[index];
                            b32 is_selected = ctx->selected_record_index == index;
                            if (!aabbf_filtered(ctx->filter_world_region, position, ctx->filter_type, is_selected))
                            {
                                continue;
                            }
                            
                            circle_color = ctx->circle_color;
                            if (is_selected)
                            {
                                circle_color = ctx->selected_circle_color;
                            }
                            DrawCircleV({position.x, position.z}, raylib_circle_radius, *(Color*)&circle_color);
                        }
                    }
                    EndMode2D();
                    EndTextureMode();
                }
            }
        }
        ImGui::EndChild();
        ImGui::BeginChild("##world_details", {}, 0, window_flags);
        {
            ImGui::PushItemWidth(200.0f);
            ImGui::InputFloat3("World Min", &world_bounds->min.x, "%.2f", ImGuiInputTextFlags_CharsDecimal);
            ImGui::InputFloat3("World Max", &world_bounds->max.x, "%.2f", ImGuiInputTextFlags_CharsDecimal);
            ImGui::Text("World Position: %.2f, %.2f, %.2f", ctx->position.x, ctx->position.y, ctx->position.z);
            if (ImGui::IsItemHovered())
            {
                if (ImGui::IsKeyChordPressed(ImGuiModFlags_Ctrl | ImGuiKey_C))
                {
                    String clipboard_text;
                    string_printf(&clipboard_text, "%.2f, %.2f, %.2f", ctx->position.x, ctx->position.y, ctx->position.z);
                    SetClipboardText(clipboard_text.str);
                }
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                {
                    camera.target.x = ctx->position.x;
                    camera.target.y = ctx->position.z;
                }
            }
            ImGui::SetItemTooltip("Ctrl + C to copy coordinates to clipboard\nRight Click to focus");
            ImGui::InputFloat3("Sample Point", (float*)&ctx->world_sample_point, "%.2f", ImGuiInputTextFlags_CharsDecimal);
            if (ImGui::IsItemHovered())
            {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                {
                    camera.target.x = ctx->world_sample_point.x;
                    camera.target.y = ctx->world_sample_point.z;
                }
            }
            ImGui::SetItemTooltip("Ctrl + Left click to pick a sample from map\nRight Click to focus");
            
            ImGui::PopItemWidth();
            if (ImGui::Button("Calculate World Bounds")) 
            {
                ctx->max_world_region = calculate_max_world_region(ctx->world);
            }
            ImGui::SameLine();
            ImGui::Checkbox("Use World Bounds", (bool*)&ctx->use_world_bounds);
            if (ImGui::Button("Reset Map Camera")) camera_init();
        }
        ImGui::EndChild();
        
        ImVec2 level_details_window_size = {0, 450.0f};
        
        
        ImVec2 window_padding = ImGui::GetStyle().WindowPadding;
        
        ImVec2 table_position = {level_map_window_position.x + level_map_window_size.x + window_padding.x, level_map_window_position.y};
        ImVec2 table_size = {(f32)GetScreenWidth() - table_position.x - 15.0f, 170.0f};
        do_scanned_points_table(ctx, table_position.x, table_position.y, table_size.x, table_size.y);
        
        level_details_window_size.x = (f32)GetScreenWidth() - (level_map_window_position.x + level_map_window_size.x);
        ImVec2 side_window_position = {table_position.x, table_position.y + table_size.y};
        ImGui::SetNextWindowPos(side_window_position, ImGuiCond_Always);
        ImGui::BeginChild("##level_details");
        {
            do_world_filter(ctx, true);
            BeginTextureMode(*(RenderTexture2D*)&ctx->map_texture);
            BeginMode2D(camera);
            {
                Aabbf draw_aabb = aabbf_make(ctx->filter_world_region.min, ctx->filter_world_region.max);
                V3f draw_size = aabbf_size(draw_aabb);
                DrawRectangleLinesEx({draw_aabb.min.x, draw_aabb.min.z, draw_size.x, draw_size.z}, raylib_line_thickness, BLUE);
            }
            EndMode2D();
            EndTextureMode();
            
            side_window_position.y += ImGui::GetWindowSize().y;
            
            ImVec2 level_details_window_position = ImGui::GetWindowPos();
            f32 table_width = (f32)GetScreenWidth() - level_details_window_position.x - 15.0f;
            if (current_level)
            {
                b32 region_ui_result = do_poi_table(ctx, current_level->regions, &current_level->regions_count, "Regions", table_width, table_size);
                b32 object_ui_result = do_poi_table(ctx, current_level->objects, &current_level->objects_count, "Objects", table_width, table_size);
                
                if (region_ui_result == PointOfInterestUIResult_Export || object_ui_result == PointOfInterestUIResult_Export)
                {
                    directory_push("data");
                    directory_push(current_level_name);
                    String export_file_name;
                    if (region_ui_result == PointOfInterestUIResult_Export)
                    {
                        string_printf(&export_file_name, "%s\\regions.txt", WORK_PATH.str);
                        dump_point_of_interests_to_file(export_file_name.str, current_level->regions, current_level->regions_count);
                    }
                    
                    if (object_ui_result == PointOfInterestUIResult_Export)
                    {
                        string_printf(&export_file_name, "%s\\interests.txt", WORK_PATH.str);
                        dump_point_of_interests_to_file(export_file_name.str, current_level->objects, current_level->objects_count);
                    }
                    directory_pop();
                    directory_pop();
                }
                
                if (region_ui_result == PointOfInterestUIResult_Auto_Map)
                {
                    ctx->auto_map_window_state = AutoMapWindow_Region;
                }
                if (object_ui_result == PointOfInterestUIResult_Auto_Map)
                {
                    ctx->auto_map_window_state = AutoMapWindow_Object;
                }
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
}

b32 do_auto_map_window(VisualizerCtx *ctx)
{
    ASSERT(ctx->current_tab_level);
    b32 show = true;
    const char *auto_map_window_name = ctx->auto_map_window_state == AutoMapWindow_Region ? string_table_intern("Region") : string_table_intern("Object");
    String id_buffer;
    
    ImU32 highlight_color = 0xFF2F2FFF;
    
    ImVec2 image_min = {};
    ImVec2 image_max = {};
    
    enum
    {
        AutoMapImageState_None = 0,
        AutoMapImageState_Set_1 = 1 << 0, 
        AutoMapImageState_Set_2 = 1 << 1, 
        AutoMapImageState_Picking = 1 << 2,
        AutoMapImageState_Hovered = 1 << 3,
    };
    
    string_printf(&id_buffer, "%s##auto_map_window", auto_map_window_name);
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_None;
    
    if (ctx->auto_map_image_state & AutoMapImageState_Hovered)
    {
        window_flags |= ImGuiWindowFlags_NoMove;
    }
    ImGui::Begin(id_buffer.str, (bool*)&show, window_flags);
    {
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        {
            if (ImGui::IsKeyChordPressed(ImGuiModFlags_Ctrl | ImGuiKey_V))
            {
                ctx->ui_next_action = Action_Get_Clipboard_Image;
            }
        }
        
        ctx->auto_map_image_state &= ~AutoMapImageState_Hovered;
        
        ImGui::BeginChild("##image", {400.0f, 300.0f});
        if (ctx->current_auto_map_poi)
        {
            if (STRCMP(ctx->current_auto_map_poi->image_file.str, CLIPBOARD_TEXTURE_NAME))
            {
                AppTexture *cached_clipboard_texture = (AppTexture*)hashmap_get(CACHED_TEXTURES, CLIPBOARD_TEXTURE_NAME);
                
                if (ctx->current_auto_map_poi->texture.id != cached_clipboard_texture->id)
                {
                    ctx->auto_map_image_position = {};
                }
                ctx->current_auto_map_poi->texture = *cached_clipboard_texture;
            }
            
            AppTexture *texture = &ctx->current_auto_map_poi->texture;
            ImVec2 texture_size = {(f32)texture->width, (f32)texture->height};
            // clamp within window, otherwise thing is too large..
            texture_size.x = MIN(texture_size.x, 400.0f);
            texture_size.y = MIN(texture_size.y, 300.0f);
            
            ImVec2 texture_half_extents = {texture_size.x * 0.5f, texture_size.y * 0.5f};
            ImVec2 image_position_min = texture_half_extents;
            ImVec2 image_position_max = {(f32)texture->width - texture_half_extents.x, (f32)texture->height - texture_half_extents.y};
            ctx->auto_map_image_position.x = CLAMP(ctx->auto_map_image_position.x, (s16)image_position_min.x, (s16)image_position_max.x);
            ctx->auto_map_image_position.y = CLAMP(ctx->auto_map_image_position.y, (s16)image_position_min.y, (s16)image_position_max.y);
            ImVec2 uv0 = {(f32)ctx->auto_map_image_position.x - texture_half_extents.x, (f32)ctx->auto_map_image_position.y - texture_half_extents.y};
            ImVec2 uv1 = {(f32)ctx->auto_map_image_position.x + texture_half_extents.x, (f32)ctx->auto_map_image_position.y + texture_half_extents.y};
            
            ImVec2 image_view_min = uv0;
            ImVec2 image_view_max = uv1;
            
            uv0.x /= (f32)texture->width;
            uv0.y /= (f32)texture->height;
            uv1.x /= (f32)texture->width;
            uv1.y /= (f32)texture->height;
            
            ImGui::Image((ImTextureID)texture, texture_size, uv0, uv1);
            image_min = ImGui::GetItemRectMin();
            image_max = ImGui::GetItemRectMax();
            
            if (ImGui::IsItemHovered())
            {
                ctx->auto_map_image_state |= AutoMapImageState_Hovered;
                b32 is_picking_point = (ctx->auto_map_image_state & AutoMapImageState_Picking) != AutoMapImageState_Picking;
                
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    if (ctx->auto_map_image_state & AutoMapImageState_Picking)
                    {
                        is_picking_point = true;
                        ctx->auto_map_image_state &= ~AutoMapImageState_Picking;
                    }
                    
                    ImVec2 drag = imgui_get_mouse_drag();
                    ctx->auto_map_image_position.x -= (s16)drag.x;
                    ctx->auto_map_image_position.y -= (s16)drag.y;
                }
                
                if (is_picking_point)
                {
                    V2i *auto_map_image_sample_position = nullptr;
                    
                    if (ctx->auto_map_image_state & AutoMapImageState_Set_1)
                    {
                        auto_map_image_sample_position = ctx->auto_map_image_sample_points;
                        ctx->auto_map_image_state &= ~AutoMapImageState_Set_1;
                    }
                    if (ctx->auto_map_image_state & AutoMapImageState_Set_2)
                    {
                        auto_map_image_sample_position = ctx->auto_map_image_sample_points + 1;
                        ctx->auto_map_image_state &= ~AutoMapImageState_Set_2;
                    }
                    
                    if (auto_map_image_sample_position)
                    {
                        ImVec2 texture_position = ImGui::GetMousePos();
                        texture_position.x = texture_position.x - image_min.x + (f32)ctx->auto_map_image_position.x;
                        texture_position.y = texture_position.y - image_min.y + (f32)ctx->auto_map_image_position.y;
                        texture_position.x -= texture_half_extents.x;
                        texture_position.y -= texture_half_extents.y;
                        *auto_map_image_sample_position = {(s16)texture_position.x, (s16)texture_position.y};
                    }
                }
            }
            
            ImDrawList *draw_list = ImGui::GetWindowDrawList();
            for (s32 index = 0; index < ARRAY_SIZE(ctx->auto_map_image_sample_points); ++index)
            {
                ImVec2 sample_position = {(f32)ctx->auto_map_image_sample_points[index].x, (f32)ctx->auto_map_image_sample_points[index].y};
                if (sample_position.x >= image_view_min.x && sample_position.y >= image_view_min.y &&
                    sample_position.x <= image_view_max.x && sample_position.y <= image_view_max.y)
                {
                    sample_position.x = sample_position.x - image_view_min.x + image_min.x;
                    sample_position.y = sample_position.y - image_view_min.y + image_min.y;
                    
                    if (index & 1)
                    {
                        draw_list->AddCircle(sample_position, 3.0f, ctx->selected_circle_color);
                    }
                    else
                    {
                        draw_list->AddCircle(sample_position, 3.0f, ctx->circle_color);
                    }
                }
                
            }
        }
        ImGui::EndChild();
        
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::InputText("Name##clipboard_input_name", ctx->clipboard_image_name.str, sizeof(ctx->clipboard_image_name.str)))
        {
            ctx->clipboard_image_name.length = (u8)strlen(ctx->clipboard_image_name.str);
        }
        ImGui::SetItemTooltip("clipboard images are saved as png");
        ImGui::SameLine();
        if (ImGui::Button("Save Image##save_image") && ctx->clipboard_image_name.length)
        {
            if (const char *extension = path_get_ext(ctx->clipboard_image_name.str))
            {
                ctx->clipboard_image_name.length = (u8)(extension - ctx->clipboard_image_name.str);
                ctx->clipboard_image_name.str[ctx->clipboard_image_name.length] = 0;
            }
            
            if (ctx->current_auto_map_poi)
            {
                AppTexture *texture = &ctx->current_auto_map_poi->texture;
                String texture_name;
                String output_image_file;
                
                string_printf(&texture_name, "%s.png", ctx->clipboard_image_name.str);
                
                directory_push("data");
                directory_push("images");
                string_printf(&output_image_file, "%s\\%s", WORK_PATH.str, texture_name.str);
                Image image = LoadImageFromTexture(*(Texture2D*)texture);
                ExportImage(image, output_image_file.str);
                UnloadImage(image);
                
                Texture2D file_texture = LoadTexture(output_image_file.str);
                if (file_texture.id)
                {
                    hashmap_set(CACHED_TEXTURES, texture_name.str, (AppTexture*)&file_texture);
                }
                
                directory_pop();
                directory_pop();
            }
        }
        ImGui::SameLine();
        string_printf(&id_buffer, "Auto Map##auto_map_poi_%s", auto_map_window_name);
        if (ImGui::Button(id_buffer.str))
        {
            if (ctx->current_auto_map_poi)
            {
                AppTexture texture = ctx->current_auto_map_poi->texture;
                V3f p0 = ctx->auto_map_sample_points[0];
                V3f p1 = ctx->auto_map_sample_points[1];
                
                Aabbf bounds = aabbf_make(p0, p1);
                V3f size = aabbf_size(bounds);
                
                ImVec2 uv0 = {(f32)ctx->auto_map_image_sample_points[0].x, (f32)ctx->auto_map_image_sample_points[0].y};
                ImVec2 uv1 = {(f32)ctx->auto_map_image_sample_points[1].x, (f32)ctx->auto_map_image_sample_points[1].y};
                
                uv0.x /= (f32)texture.width;
                uv0.y /= (f32)texture.height;
                uv1.x /= (f32)texture.width;
                uv1.y /= (f32)texture.height;
                
                // need to flip the v since the map origin is bottom left whereas images origins are top left
                uv0.y = 1.0f - uv0.y;
                uv1.y = 1.0f - uv1.y;
                
                f32 du = uv1.x - uv0.x;
                f32 dv = uv1.y - uv0.y;
                du = du < 0 ? -du : du;
                dv = dv < 0 ? -dv : dv;
                
                f32 u_scale = 1.0f / du;
                f32 v_scale = 1.0f / dv;
                
                size.x *= u_scale;
                size.z *= v_scale;
                
                V3f min = p0;
                min.x -= uv0.x * size.x;
                min.z -= uv0.y * size.z;
                
                V3f max = v3f_add(min, size);
                // maintain both y coordinates for min/max as min bounds doesn't do anything with y
                max.y = p1.y;
                ctx->current_auto_map_poi->region = aabbf_make(min, max);
            }
        }
        ImGui::SetItemTooltip("Will resize the image to room, needs good sample points otherwise scaling can be slightly off");
        
        for (s32 index = 0; index < ARRAY_SIZE(ctx->auto_map_sample_points); ++index)
        {
            AutoMapAction sample_action = (AutoMapAction)index;
            
            string_printf(&id_buffer, "##sample_point_%d", index);
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputFloat3(id_buffer.str, (float*)&ctx->auto_map_sample_points[index], "%.2f", ImGuiInputTextFlags_CharsDecimal);
            ImGui::SameLine();
            string_printf(&id_buffer, "Pick (%d)##sample_point_use_world_position_%d", index + 1, index);
            if (ImGui::Button(id_buffer.str))
            {
                ctx->auto_map_image_state |= (1 << index) | AutoMapImageState_Picking;
            }
            ImGui::SetItemTooltip("Press (Ctrl + %d) to set world position", index + 1);
            ImGui::SameLine();
            ImGui::Text("%5d, %5d", ctx->auto_map_image_sample_points[index].x, ctx->auto_map_image_sample_points[index].y);
            
            if (global_button_is_pressed(ctx->auto_map_buttons + sample_action))
            {
                ctx->auto_map_sample_points[index] = ctx->position;
                if (ImGui::IsMouseHoveringRect(image_min, image_max))
                {
                    ctx->auto_map_image_state |= 1 << index;
                }
            }
        }
        
        ImGui::BeginChild("##selected_details", {200.0f, 100.0f});
        {
            String name_buffer;
            if (ctx->current_auto_map_poi)
            {
                PointOfInterest *interest = ctx->current_auto_map_poi;
                string_printf(&name_buffer, interest->name.str);
                string_printf(&id_buffer, "Name##poi_name");
                if (ImGui::InputText(id_buffer.str, name_buffer.str, sizeof(name_buffer.str)))
                {
                    string_printf(&interest->name, name_buffer.str);
                }
                {
                    AppTexture *texture = (AppTexture*)hashmap_get(CACHED_TEXTURES, interest->image_file.str);
                    s32 texture_count = CACHED_TEXTURES->count;
                    
                    const char **texture_names = (const char **)CACHED_TEXTURES->keys;
                    AppTexture *textures = (AppTexture*)*CACHED_TEXTURES->values;
                    
                    s32 texture_index = (s32)(texture - textures);
                    
                    string_printf(&id_buffer, "Image##poi_textures");
                    if (ImGui::Combo(id_buffer.str, (int*)&texture_index, texture_names, texture_count))
                    {
                        interest->texture = textures[texture_index];
                        string_printf(&interest->image_file, texture_names[texture_index]);
                    }
                }
                if (ImGui::Button("Focus"))
                {
                    V3f region_size = aabbf_size(ctx->current_auto_map_poi->region);
                    camera.target.x = interest->region.min.x + region_size.x * 0.5f;
                    camera.target.y = interest->region.min.z + region_size.z * 0.5f;
                }
                ImGui::SameLine();
                ImGui::Checkbox("Draw Image", (bool*)&ctx->current_auto_map_poi->draw);
            }
            else
            {
                ImGui::Text("Add or Select an item");
            }
        }
        ImGui::EndChild();
        
        ImGui::SameLine();
        ImGui::BeginChild("##list_box_selection");
        {
            s32 *count = nullptr;
            PointOfInterest *interests = nullptr;
            if (ctx->auto_map_window_state == AutoMapWindow_Region)
            {
                interests = ctx->current_tab_level->regions;
                count = &ctx->current_tab_level->regions_count;
            }
            else if (ctx->auto_map_window_state == AutoMapWindow_Object)
            {
                interests = ctx->current_tab_level->objects;
                count = &ctx->current_tab_level->objects_count;
            }
            ASSERT(count && interests);
            
            string_printf(&id_buffer, "##list_box_%s", auto_map_window_name);
            s32 color_style_depth = 0;
            if (ImGui::BeginListBox(id_buffer.str, {160.0f, 100.0f}))
            {
                for (s32 index = 0; index < *count; ++index)
                {
                    PointOfInterest *interest = interests + index;
                    
                    if (interest == ctx->current_auto_map_poi)
                    {
                        color_style_depth++;
                        ImGui::PushStyleColor(ImGuiCol_Button, highlight_color);
                    }
                    
                    string_printf(&id_buffer, "%s##%d", interest->name.str, index);
                    if (ImGui::Button(id_buffer.str))
                    {
                        ctx->current_auto_map_poi = interest;
                    }
                    
                    if (color_style_depth)
                    {
                        ImGui::PopStyleColor();
                        color_style_depth--;
                    }
                }
                ImGui::EndListBox();
            }
            ImGui::SameLine();
            string_printf(&id_buffer, "Add##new_poi_%s", auto_map_window_name);
            if (ImGui::Button(id_buffer.str))
            {
                ctx->current_auto_map_poi = interests + *count;
                string_printf(&ctx->current_auto_map_poi->name, "New");
                (*count)++;
                
                const char **texture_names = (const char **)CACHED_TEXTURES->keys;
                AppTexture *textures = (AppTexture*)*CACHED_TEXTURES->values;
                AppTexture *clipboard_texture = (AppTexture*)hashmap_get(CACHED_TEXTURES, CLIPBOARD_TEXTURE_NAME);
                
                s32 texture_index = (s32)(clipboard_texture - textures);
                ctx->current_auto_map_poi->texture = textures[texture_index];
                string_printf(&ctx->current_auto_map_poi->image_file, texture_names[texture_index]);
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
    
    return show;
}

b32 do_settings_window(VisualizerCtx *ctx)
{
    bool show = true;
    ImGui::Begin("Settings", &show);
    {
        const char **key_code_names = get_key_code_names();
        
        String input_buffer;
        {
            ImGui::Text("Game World");
            for (s32 index = 0; index < Game_Count; ++index)
            {
                b32 is_active = ctx->game_world_to_display == index;
                const char *radio_button_name = s_game_short_names[index];
                if (index == Game_None)
                {
                    radio_button_name = "Auto";
                }
                
                if (ImGui::RadioButton(radio_button_name, is_active))
                {
                    ctx->game_world_to_display = (Game)index;
                }
                
                if (index != Game_Count - 1)
                {
                    ImGui::SameLine();
                }
            }
        }
        {
            ImGui::Text("Fling");
            ImGui::SetItemTooltip("Sets fling position");
            ImGui::SameLine();
            if (ctx->buttons[Action_Set_World_Position].key != KeyCode_Count)
            {
                string_printf(&input_buffer, "%s##set_fling_key_code", key_code_names[ctx->buttons[Action_Set_World_Position].key]);
            }
            else
            {
                string_printf(&input_buffer, "Unset##set_fling_key_code");
            }
            if (ImGui::Button(input_buffer.str)) ctx->rebind_type = RebindType_Fling;
            ImGui::SetItemTooltip("Sets fling position");
        }
        
        {
            ImGui::Text("Scan");
            ImGui::SetItemTooltip("Scan hotkey walks through all regions by scan step rate");
            ImGui::SameLine();
            if (ctx->buttons[Action_Scan_Regions].key != KeyCode_Count)
            {
                string_printf(&input_buffer, "%s##set_scan_key_code", key_code_names[ctx->buttons[Action_Scan_Regions].key]);
            }
            else
            {
                string_printf(&input_buffer, "Unset##set_scan_key_code");
            }
            if (ImGui::Button(input_buffer.str)) ctx->rebind_type = RebindType_Scan;
            ImGui::SetItemTooltip("Scan hotkey walks through all regions by scan step rate");
        }
        
        {
            ImGui::Text("Cancel");
            ImGui::SetItemTooltip("Cancel last action, ideally for longer scans. Will always be checked even if global hotkeys is disabled");
            ImGui::SameLine();
            if (ctx->buttons[Action_Cancel].key != KeyCode_Count)
            {
                string_printf(&input_buffer, "%s##set_cancel_key_code", key_code_names[ctx->buttons[Action_Cancel].key]);
            }
            else
            {
                string_printf(&input_buffer, "Unset##set_cancel_key_code");
            }
            if (ImGui::Button(input_buffer.str)) ctx->rebind_type = RebindType_Cancel;
            ImGui::SetItemTooltip("Cancel last action, ideally for longer scans. Will always be checked even if global hotkeys is disabled");
        }
        
        {
            ImGui::Text("Inventory");
            ImGui::SetItemTooltip("Inventory button that's used in game to open/close your inventory");
            ImGui::SameLine();
            if (ctx->game_inventory_key_code != KeyCode_Count)
            {
                string_printf(&input_buffer, "%s##set_inventory_key_code", key_code_names[ctx->game_inventory_key_code]);
            }
            else
            {
                string_printf(&input_buffer, "Unset##set_inventory_key_code");
            }
            if (ImGui::Button(input_buffer.str)) ctx->rebind_type = RebindType_Inventory;
            ImGui::SetItemTooltip("Inventory button that's used in game to open/close your inventory");
        }
        {
            ImGui::Text("Inventory Position");
            ImGui::SameLine();
            if (ctx->buttons[Action_Set_Inventory_Position].key != KeyCode_Count)
            {
                string_printf(&input_buffer, "%s##set_inventory_position_key_code", key_code_names[ctx->buttons[Action_Set_Inventory_Position].key]);
            }
            else
            {
                string_printf(&input_buffer,"Unset##set_inventory_position_key_code");
            }
            if (ImGui::Button(input_buffer.str)) ctx->rebind_type = RebindType_Set_Inventory_Position;
            ImGui::SetItemTooltip("Set Inventory Position without having to click on the coordinates and switch back to the game");
        }
        {
            ImGui::Checkbox("Invert Mouse Wheel Zoom", (bool*)&ctx->mouse_wheel_invert);
            if (ctx->mouse_wheel_invert)
            {
                ImGui::SetItemTooltip("Up - In\nDown - Out");
            }
            else
            {
                ImGui::SetItemTooltip("Up - Out\nDown - in");
            }
        }
    }
    ImGui::End();
    return show;
}

b32 do_about_window(VisualizerCtx *ctx)
{
    b32 show = true;
    String id_buffer;
    string_printf(&id_buffer, "About v%d.%d.%d##about_window", APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_BUILD);
    ImGui::Begin(id_buffer.str, (bool*)&show);
    {
        ImGui::Text("Tool used for BG3 to find where you place your item fling positions in the world");
        ImGui::Text("Currently only supports bg3 after v4.1.1.3905231, this is a minor patch after patch 4");
        ImGui::Text("Created by Ogam");
        if (ImGui::Button("CPRG Discord"))
        {
            OpenURL("https://discord.gg/ADCqdhwHxH");
        }
        ImGui::SameLine();
        if (ImGui::Button("Speedrun.com"))
        {
            OpenURL("https://www.speedrun.com/baldurs_gate_3/");
        }
    }
    ImGui::End();
    return show;
}

void directory_init()
{
    string_printf(&WORK_PATH, GetApplicationDirectory());
    WORK_PATH_DEPTH = 0;
}

const char *directory_get()
{
    return WORK_PATH.str;
}

const char *directory_get_relative()
{
    const char *path = WORK_PATH.str;
    const char *end = path + WORK_PATH.length;
    const char *walker = end;
    s32 offset = 0;
    while (walker > path)
    {
        if (*walker == '\\' || *walker == '/')
        {
            break;
        }
        walker--;
    }
    if (*walker == '\\' || *walker == '/')
    {
        walker++;
    }
    
    return walker;
}

void directory_push(const char *name)
{
    String temp_path;
    string_printf(&temp_path, "%s\\%s", WORK_PATH.str, name);
    ASSERT(DirectoryExists(temp_path.str));
    
    memcpy(&WORK_PATH, &temp_path, sizeof(String));
    WORK_PATH_DEPTH++;
}

void directory_pop()
{
    const char *path = WORK_PATH.str;
    const char *end = path + WORK_PATH.length;
    const char *walker = end;
    u8 offset = 0;
    while (walker > path)
    {
        if (*walker == '\\' || *walker == '/')
        {
            offset = (u8)(end - walker);
            break;
        }
        walker--;
    }
    WORK_PATH.length = WORK_PATH.length - offset;
    WORK_PATH.str[WORK_PATH.length] = '\0';
    
    if (offset)
    {
        WORK_PATH_DEPTH = MAX(WORK_PATH_DEPTH - 1, 0);
    }
}

const char* path_get_ext(const char *path)
{
    u64 length = strlen(path);
    const char *walker = path + length - 1;
    const char *extension = nullptr;
    while (walker > path)
    {
        if (*walker == '.')
        {
            extension = walker;
            break;
        }
        if (*walker == '\\' || *walker == '/')
        {
            break;
        }
        walker--;
    }
    return extension;
}

const char* path_get_file_name(const char *path)
{
    u64 length = strlen(path);
    const char *walker = path + length - 1;
    const char *file_name = nullptr;
    while (walker > path)
    {
        if (*walker == '\\' || *walker == '/')
        {
            file_name = walker;
            break;
        }
        walker--;
    }
    if (*file_name == '\\' || *file_name == '/')
    {
        file_name++;
    }
    
    return file_name;
}

void create_default_textures()
{
    ASSERT(CACHED_TEXTURES);
    
    u32 default_buffer[4];
    memset(default_buffer, 0, sizeof(default_buffer));
    Aabbi default_bounds = {};
    default_bounds.max = {1, 1};
    
    AppTexture default_texture = renderer_push_image((u8*)default_buffer, default_bounds);
    ASSERT(default_texture.id);
    AppTexture clipboard_texture = renderer_push_image((u8*)default_buffer, default_bounds);
    ASSERT(clipboard_texture.id);
    hashmap_set(CACHED_TEXTURES, DEFAULT_TEXTURE_NAME, (AppTexture*)&default_texture);
    hashmap_set(CACHED_TEXTURES, CLIPBOARD_TEXTURE_NAME, (AppTexture*)&clipboard_texture);
}

Hashmap* load_assets(Arena *arena)
{
    directory_push("data");
    FilePathList file_path_list = LoadDirectoryFilesEx(WORK_PATH.str, ".txt", true);
    PointOfInterest *regions = nullptr;
    PointOfInterest *objects = nullptr;
    s32 regions_count = 0;
    s32 objects_count = 0;
    s32 directory_count = 0;
    Hashmap *world = nullptr;
    
    {
        String previous_directory;
        for (u32 index = 0; index < file_path_list.count; ++index)
        {
            char *path = file_path_list.paths[index];
            const char *directory_path = GetDirectoryPath(path);
            const char *relative_directory = directory_path;
            const char *file_name = GetFileName(path);
            u64 directory_path_length = strlen(directory_path);
            s32 relative_directory_length = (s32)directory_path_length;
            s32 file_name_length = (s32)strlen(file_name);
            
            {
                const char *directory_walker = directory_path + directory_path_length;
                while (directory_walker > directory_path) 
                {
                    if (*directory_walker == '\\' || *directory_walker == '//')
                    {
                        directory_walker++;
                        break;
                    }
                    directory_walker--;
                }
                relative_directory = directory_walker;
                relative_directory_length = (s32)strlen(relative_directory);
                
                if (!STRCMP(previous_directory.str, relative_directory))
                {
                    string_printf(&previous_directory, relative_directory);
                    directory_count++;
                }
            }
        }
    }
    
    if (!directory_count)
    {
        return nullptr;
    }
    
    if (CACHED_TEXTURES)
    {
        for (s32 index = 0; index < CACHED_TEXTURES->count; ++index)
        {
            AppTexture *texture = (AppTexture*)CACHED_TEXTURES->values[index];
            ASSERT(texture);
            UnloadTexture(*(Texture2D*)texture);
        }
    }
    
    arena_clear(arena);
    CACHED_TEXTURES = hashmap_make(arena, sizeof(AppTexture), 512);
    world = hashmap_make(arena, sizeof(Level), directory_count);
    
    for (u32 index = 0; index < file_path_list.count; ++index)
    {
        char *path = file_path_list.paths[index];
        const char *directory_path = GetDirectoryPath(path);
        const char *relative_directory = directory_path;
        const char *file_name = GetFileName(path);
        u64 directory_path_length = strlen(directory_path);
        s32 relative_directory_length = (s32)directory_path_length;
        s32 file_name_length = (s32)strlen(file_name);
        
        {
            const char *directory_walker = directory_path + directory_path_length;
            while (directory_walker > directory_path) 
            {
                if (*directory_walker == '\\' || *directory_walker == '//')
                {
                    directory_walker++;
                    break;
                }
                directory_walker--;
            }
            relative_directory = directory_walker;
            relative_directory_length = (s32)strlen(relative_directory);
        }
        
        const char *current_relative_directory = directory_get_relative();
        if (!STRCMP(relative_directory, current_relative_directory))
        {
            if (WORK_PATH_DEPTH > 1)
            {
                Level level = {};
                level.regions = regions;
                level.objects = objects;
                level.regions_count = regions_count;
                level.objects_count = objects_count;
                
                if (regions_count == 0 && !regions)
                {
                    level.regions = (PointOfInterest*)arena_alloc(arena, sizeof(PointOfInterest) * POI_CAPACITY);
                }
                if (objects_count == 0 && !objects)
                {
                    level.objects = (PointOfInterest*)arena_alloc(arena, sizeof(PointOfInterest) * POI_CAPACITY);
                }
                
                hashmap_set(world, current_relative_directory, &level);
                
                printf("Level Loaded - %s\n", current_relative_directory);
                printf("\tRegions - %d\n", regions_count);
                printf("\tObjects - %d\n", objects_count);
                regions = nullptr;
                objects = nullptr;
                regions_count = 0;
                objects_count = 0;
                directory_pop();
            }
            directory_push(relative_directory);
        }
        
        u64 file_length = 0;
        char *file_data = (char*)read_entire_file(path, &file_length);
        
        if (s32 interest_count = parse_file_get_block_count(file_data, file_length))
        {
            PointOfInterest *interests = (PointOfInterest*)arena_alloc(arena, sizeof(PointOfInterest) * POI_CAPACITY);
            
            if (STRCMP(file_name, "regions.txt"))
            {
                regions = interests;
                regions_count = interest_count;
            }
            else if (STRCMP(file_name, "interests.txt"))
            {
                objects = interests;
                objects_count = interest_count;
            }
            
            {
                PointOfInterestCollection collection = {};
                collection.items = interests;
                collection.count = 0;
                collection.capacity = interest_count;
                //point_of_interest_parse(interests, interest_count, file_data, file_length);
                parse_file(file_data, file_length, handle_point_of_interest_block, &collection);
            }
        }
        
        free(file_data);
    }
    
    ASSERT(WORK_PATH_DEPTH <= 2);
    if (WORK_PATH_DEPTH)
    {
        const char *relative_directory = directory_get_relative();
        Level level = {};
        level.regions = regions;
        level.objects = objects;
        level.regions_count = regions_count;
        level.objects_count = objects_count;
        
        if (regions_count == 0 && !regions)
        {
            level.regions = (PointOfInterest*)arena_alloc(arena, sizeof(PointOfInterest) * POI_CAPACITY);
        }
        if (objects_count == 0 && !objects)
        {
            level.objects = (PointOfInterest*)arena_alloc(arena, sizeof(PointOfInterest) * POI_CAPACITY);
        }
        
        printf("Level Loaded - %s\n", relative_directory);
        printf("\tRegions - %d\n", regions_count);
        printf("\tObjects - %d\n", objects_count);
        hashmap_set(world, relative_directory, &level);
        
        directory_pop();
    }
    
    UnloadDirectoryFiles(file_path_list);
    
    create_default_textures();
    
    directory_push("images");
    FilePathList image_paths = LoadDirectoryFiles(WORK_PATH.str);
    for (u32 index = 0; index < image_paths.count; ++index)
    {
        const char *path = image_paths.paths[index];
        const char *file_ext = path_get_ext(path);
        b32 can_load = 
            string_table_intern(file_ext) == string_table_intern(".png") ||
            string_table_intern(file_ext) == string_table_intern(".jpg") || 
            string_table_intern(file_ext) == string_table_intern(".bmp") ||
            string_table_intern(file_ext) == string_table_intern(".dds") ||
            string_table_intern(file_ext) == string_table_intern(".tga");
        const char *file_name = path_get_file_name(path);
        
        if (!hashmap_get(CACHED_TEXTURES, file_name))
        {
            Texture2D texture = LoadTexture(path);
            if (texture.id)
            {
                hashmap_set(CACHED_TEXTURES, file_name, (AppTexture*)&texture);
            }
        }
    }
    UnloadDirectoryFiles(image_paths);
    directory_pop();
    
    for (s32 index = 0; index < world->count; ++index)
    {
        const char *level_name = (const char*)world->keys[index];
        Level *level = (Level*)world->values[index];
        Level *cached_level = (Level*)hashmap_get(world, level_name);
        ASSERT(level == cached_level);
        
        for (s32 region_index = 0; region_index < level->regions_count; ++region_index)
        {
            PointOfInterest *interest = level->regions + region_index;
            interest->draw = true;
            if (!STRCMP(DEFAULT_TEXTURE_NAME, interest->image_file.str))
            {
                if (interest->image_file.length)
                {
                    interest->texture = {};
                    if (AppTexture* texture = (AppTexture*)hashmap_get(CACHED_TEXTURES, interest->image_file.str))
                    {
                        interest->texture = *texture;
                    }
                }
            }
        }
        
        for (s32 object_index = 0; object_index < level->objects_count; ++object_index)
        {
            PointOfInterest *interest = level->objects + object_index;
            interest->draw = true;
            if (!STRCMP(DEFAULT_TEXTURE_NAME, interest->image_file.str))
            {
                if (interest->image_file.length)
                {
                    interest->texture = {};
                    if (AppTexture* texture = (AppTexture*)hashmap_get(CACHED_TEXTURES, interest->image_file.str))
                    {
                        interest->texture = *texture;
                    }
                }
            }
        }
    }
    
    directory_pop();
    ASSERT(STRCMP(WORK_PATH.str, GetApplicationDirectory()) && WORK_PATH_DEPTH == 0);
    
    return world;
}

Hashmap* load_default_assets(Arena *arena)
{
    ASSERT(arena);
    if (CACHED_TEXTURES)
    {
        for (s32 index = 0; index < CACHED_TEXTURES->count; ++index)
        {
            AppTexture *texture = (AppTexture*)CACHED_TEXTURES->values[index];
            ASSERT(texture);
            UnloadTexture(*(Texture2D*)texture);
        }
    }
    
    arena_clear(arena);
    CACHED_TEXTURES = hashmap_make(arena, sizeof(AppTexture), 512);
    Hashmap *world = hashmap_make(arena, sizeof(Level), 32);
    
    create_default_textures();
    
    return world;
}

Aabbf calculate_max_world_region(Hashmap *world)
{
    Aabbf region = {};
    if (world)
    {
        for (s32 level_index = 0; level_index < world->count; ++level_index)
        {
            Level *level = (Level*)world->values[level_index];
            level->bounds = {};
            if (level->regions_count)
            {
                level->bounds = level->regions->region;
            }
            else if (level->objects_count)
            {
                level->bounds = level->objects->region;
            }
            for (s32 index = 0; index < level->regions_count; ++index)
            {
                PointOfInterest *interest = level->regions + index;
                region = aabbf_combine(region, interest->region);
                level->bounds = aabbf_combine(level->bounds, interest->region);
            }
            for (s32 index = 0; index < level->objects_count; ++index)
            {
                PointOfInterest *interest = level->objects + index;
                region = aabbf_combine(region, interest->region);
                level->bounds = aabbf_combine(level->bounds, interest->region);
            }
        }
    }
    return region;
}

void texture_get_zoom_uv(AppTexture *texture, ImVec2 size, ImVec2 focus_point, ImVec2 *out_uv0, ImVec2 *out_uv1)
{
    ImVec2 half_extents = {size.x * 0.5f, size.y * 0.5f};
    ImVec2 min = {focus_point.x - half_extents.x, focus_point.y - half_extents.y};
    ImVec2 max = {focus_point.x + half_extents.x, focus_point.y + half_extents.y};
    ImVec2 delta = {};
    if (min.x < 0)
    {
        delta.x = texture->width - min.x;
    }
    if (min.y < 0)
    {
        delta.y = texture->height - min.y;
    }
    if (max.x > texture->width)
    {
        delta.x = texture->width - max.x;
    }
    if (max.y > texture->height)
    {
        delta.y = texture->height - max.y;
    }
    min.x += delta.x;
    min.y += delta.y;
    max.x += delta.x;
    max.y += delta.y;
    
    if (out_uv0)
    {
        *out_uv0 = {min.x / texture->width, min.y / texture->height};
    }
    
    if (out_uv1)
    {
        *out_uv1 = {max.x / texture->width, max.y / texture->height};
    }
}