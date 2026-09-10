#include "app/ui.h"

#include <dcimgui.h>

typedef struct UI
{
    b32 is_hovering_any_windows;
    
    Edit_Room edit_room;
    Config_Option* room;
    b32 is_new_room;
    b32 is_editing_room;
    u64 edit_room_hash;
    
    CF_V2 edit_room_image_p0;
    CF_V2 edit_room_image_p1;
    CF_V3 edit_room_p0;
    CF_V3 edit_room_p1;
    
    CF_ARRAY(CF_V3) tooltip_scan_points;
    CF_ARRAY(CF_V3) tooltip_world_hover_points;
} UI;

UI ui;

void ui_draw_settings(void);
void ui_draw_screen_scan(void);
void ui_draw_world_map_rooms(void);
void ui_draw_world_map(void);

void ui_begin_edit_room(const char* game, const char* region, Config_Option* room, b32 is_new_room);
void ui_end_edit_room(b32 save);
void ui_draw_edit_room(void);

void ui_draw_point_cloud_table(void);
void ui_draw_tooltip_points(void);

b32 ui_is_hovering_any_windows(void)
{
    return ui.is_hovering_any_windows;
}

void handle_window_hovered(void)
{
    ui.is_hovering_any_windows = ui.is_hovering_any_windows ||
        ImGui_IsWindowHovered(ImGuiHoveredFlags_AnyWindow) ||
        ImGui_IsAnyItemHovered();
}

b32 ui_get_edit_room(Edit_Room* out_room)
{
    if (ui.is_editing_room && out_room)
    {
        *out_room = ui.edit_room;
    }
    return ui.is_editing_room;;
}

void handle_region_hovering(void)
{
    if (ui_is_hovering_any_windows())
    {
        return;
    }
    
    if (g_app->world_map.mouse_hover_region)
    {
        Config_Option* room = g_app->world_map.mouse_hover_region;
        
        Optional opt_min = config_options_get_array_float(room, "min");
        Optional opt_max = config_options_get_array_float(room, "max");
        Optional opt_image = config_options_get_string(room, "image");
        Optional opt_name = config_options_get_string(room, "name");
        
        CF_V3 min = *(CF_V3*)opt_min.custom_value;
        CF_V3 max = *(CF_V3*)opt_max.custom_value;
        
        CF_Aabb aabb = config_option_room_to_aabb(g_app->world_map.mouse_hover_region);
        
        ImGui_BeginTooltip();
        {
            if (opt_name.has_value)
            {
                ImGui_Text(opt_name.str_value);
            }
            
            ImGui_Text("Max: %.2f, %.2f, %.2f", max.x, max.y, max.z);
            ImGui_Text("Min: %.2f, %.2f, %.2f", min.x, min.y, min.z);
            
            if (opt_image.has_value)
            {
                ImGui_Text("Image: %s", opt_image.str_value);
            }
        }
        ImGui_EndTooltip();
    }
}

void ui_draw(void)
{
    ui.is_hovering_any_windows = false;
    str8 label = arena_fmt(&g_arena, "Version %s##ui_main", DIV_VERSION);
    ImGui_Begin(label, NULL, ImGuiWindowFlags_None);
    {
        handle_window_hovered();
        
        ImGui_BeginTabBar("", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs);
        {
            ui_draw_settings();
            ui_draw_world_map_rooms();
            ui_draw_screen_scan();
            
            if (g_app->state == App_State_World_Map)
            {
                ui_draw_world_map();
                handle_region_hovering();
            }
            
            ui_draw_point_cloud_table();
            
            ImGui_EndTabBar();
        }
    }
    ImGui_End();
    
    ui_draw_tooltip_points();
}

void ui_draw_settings(void)
{
    str8 label = make_arena_string(&g_arena, 1024);
    
    Camera* camera = &g_app->world_map.camera;
    bool* show_grid = &g_app->world_map.show_grid;
    s32* grid_x = &g_app->world_map.grid_x;
    s32* grid_y = &g_app->world_map.grid_y;
    f32* grid_line_thickness = &g_app->world_map.grid_line_thickness;
    CF_Color* grid_color = &g_app->world_map.grid_color;
    
    if (g_app->state == App_State_Screen_Scan)
    {
        camera = &g_app->screen_scan.camera;
        show_grid = &g_app->screen_scan.show_grid;
        grid_x = &g_app->screen_scan.grid_x;
        grid_y = &g_app->screen_scan.grid_y;
        grid_line_thickness = &g_app->screen_scan.grid_line_thickness;
        CF_Color* grid_color = &g_app->screen_scan.grid_color;
    }
    
    if (ImGui_BeginTabItem("Settings", NULL, ImGuiTabItemFlags_None))
    {
        {
            Assets* assets = &g_app->assets;
            
            ImGui_SeparatorText("Game");
            ImGui_Checkbox("Auto", (bool*)&g_app->auto_game_filter);
            
            CF_ARRAY(const char*) game_filter_copy = NULL;
            arena_array_fit(&g_arena, game_filter_copy, cf_map_size(assets->regions));
            
            const char** game_names = (const char**)cf_map_keys(assets->regions);
            for (s32 index = 0; index < cf_map_size(assets->regions); ++index)
            {
                cf_string_fmt(label, "%s##game_filter_%d", game_names[index], index);
                bool selected = array_str_contains(g_app->game_filter, game_names[index]);
                ImGui_SelectableBoolPtr(label, (bool*)&selected, ImGuiSelectableFlags_None);
                
                if (selected)
                {
                    cf_array_push(game_filter_copy, game_names[index]);
                }
            }
            
            cf_array_set(g_app->game_filter, game_filter_copy);
        }
        
        {
            ImGui_SeparatorText("Grid");
            ImGui_Checkbox("Show Grid", show_grid);
            ImGui_SliderInt("Grid X", grid_x, 0, 512);
            ImGui_SliderInt("Grid Y", grid_y, 0, 512);
            ImGui_SliderFloat("Grid Line Thickness", grid_line_thickness, 0.1f, 5.0f);
            ImGui_ColorEdit4("Grid Color", (float*)grid_color, ImGuiColorEditFlags_None);
        }
        
        // camera
        {
            ImGui_SeparatorText("Camera");
            if (ImGui_Button("Reset Camera"))
            {
                *camera = camera_defaults();
            }
            ImGui_InputFloat2("Position", (float*)&camera->position);
            ImGui_SliderFloat("Zoom", (float*)&camera->zoom, ZOOM_MIN, ZOOM_MAX);
            
            {
                CF_V2 mouse = camera_mouse(camera);
                ImGui_Text("Mouse: %.2f, %.2f", mouse.x, mouse.y);
            }
        }
        
        // world map configs
        {
            ImGui_SeparatorText("World Map Settings");
            CF_Color* character_location_color = &g_app->world_map.character_location_color;
            CF_Color* node_location_color = &g_app->world_map.node_location_color;
            CF_Color* fling_direction_color = &g_app->world_map.fling_direction_color;
            
            ImGui_ColorEdit4("Character Location", (float*)character_location_color, ImGuiColorEditFlags_None);
            ImGui_ColorEdit4("Node Location", (float*)node_location_color, ImGuiColorEditFlags_None);
            ImGui_ColorEdit4("Fling Direction", (float*)fling_direction_color, ImGuiColorEditFlags_None);
            ImGui_SliderFloat("Fling Thickness", &g_app->world_map.fling_arrow_thickness, 0.1f, 5.0f);
            ImGui_SliderFloat("Fling Arrow Width", &g_app->world_map.fling_arrow_width, 0.1f, 10.0f);
        }
        
        // game state
        if (cf_map_size(g_app->process_ptrs))
        {
            ImGui_SeparatorText("Game State");
            
            const char** keys = (const char**)cf_map_keys(g_app->process_ptrs);
            for (s32 index = 0; index < cf_map_size(g_app->process_ptrs); ++index)
            {
                Optional opt = g_app->process_ptrs[index];
                
                if (!opt.has_value)
                {
                    continue;
                }
                
                if (opt.type == Optional_Type_Float3)
                {
                    ImGui_Text("%s: %.2f, %.2f, %.2f", keys[index], opt.float3_value.x, opt.float3_value.y, opt.float3_value.z);
                }
                else if (opt.type == Optional_Type_String)
                {
                    ImGui_Text("%s: %s", keys[index], opt.str_value);
                }
            }
        }
        
        // list of selectable regions, re-orderable to adjust draw order
        // draw order is bottom to top, with top most having full opacity (100%) whereas lower
        // canvases wll have lower opacity (g_app->world_map.opacity)
        ImGui_SeparatorText("Regions");
        CF_ARRAY(Canvas*) canvases = get_sorted_canvases(g_app->game_filter, cf_array_count(g_app->game_filter));
        for (s32 index = 0; index < cf_array_count(canvases); ++index)
        {
            Canvas* canvas = canvases[index];
            cf_string_fmt(label, "%s: %s", canvas->game, canvas->region_name);
            ImGui_SelectableBoolPtr(label, (bool*)&canvas->is_visible, ImGuiSelectableFlags_None);
            
            if (ImGui_BeginDragDropSource(ImGuiDragDropFlags_None))
            {
                ImGui_SetDragDropPayload("drag_source_region_reorder", &canvas, sizeof(canvas), ImGuiCond_Always);
                ImGui_Text("Moving %s", canvas->region_name);
                ImGui_EndDragDropSource();
            }
            
            if (ImGui_BeginDragDropTarget())
            {
                const ImGuiPayload* payload = ImGui_AcceptDragDropPayload("drag_source_region_reorder", ImGuiDragDropFlags_None);
                
                if (payload) 
                {
                    Canvas* source = *(Canvas**)payload->Data;
                    
                    s32 min_order = cf_min(canvas->order, source->order);
                    s32 max_order = cf_max(canvas->order, source->order);
                    s32 replace_order = canvas->order;
                    
                    for (s32 reorder_index = 0; reorder_index < cf_array_count(canvases); ++reorder_index)
                    {
                        if (canvases[reorder_index]->order >= min_order && 
                            canvases[reorder_index]->order < max_order)
                        {
                            canvases[reorder_index]->order++;
                        }
                    }
                    
                    source->order = replace_order;
                }
                
                ImGui_EndDragDropTarget();
            }
        }
        
        ImGui_EndTabItem();
    }
}

void ui_draw_screen_scan(void)
{
    if (ImGui_BeginTabItem("Scan", NULL, ImGuiTabItemFlags_None))
    {
        g_app->state = App_State_Screen_Scan;
        
        Scan_Shape* scan_shape = &g_app->screen_scan.shape;
        
        if (ImGui_Button("Screenshot"))
        {
            game_screenshot();
        }
        
        {
            ImGui_SeparatorText("Scan Shape");
            ImGui_Checkbox("Is Circle", (bool*)&scan_shape->is_circle);
            
            if (scan_shape->is_circle)
            {
                if (!ui_is_hovering_any_windows())
                {
                    if (cf_mouse_just_pressed(CF_MOUSE_BUTTON_LEFT))
                    {
                        scan_shape->c.p = camera_mouse(&g_app->screen_scan.camera);
                    }
                    if (cf_mouse_down(CF_MOUSE_BUTTON_LEFT))
                    {
                        CF_V2 p = camera_mouse(&g_app->screen_scan.camera);
                        scan_shape->c.r = cf_distance(p, scan_shape->c.p);
                    }
                }
                
                ImGui_InputFloat2("Center##scan_shape_circle_p", (float*)&scan_shape->c.p);
                ImGui_InputFloat("Radius##scan_shape_circle_r", (float*)&scan_shape->c.r);
            }
            else
            {
                if (!ui_is_hovering_any_windows())
                {
                    if (cf_mouse_just_pressed(CF_MOUSE_BUTTON_LEFT))
                    {
                        scan_shape->bb.p0 = camera_mouse(&g_app->screen_scan.camera);
                    }
                    if (cf_mouse_down(CF_MOUSE_BUTTON_LEFT))
                    {
                        scan_shape->bb.p1 = camera_mouse(&g_app->screen_scan.camera);
                    }
                }
                
                ImGui_InputFloat2("P0##scan_shape_bb_p0", (float*)&scan_shape->bb.p0);
                ImGui_InputFloat2("P1##scan_shape_bb_p1", (float*)&scan_shape->bb.p1);
            }
        }
        
        {
            ImGui_SeparatorText("Binds");
            ImGui_Checkbox("Show Item Location", &g_app->screen_scan.show_inventory_item_location);
            
            if (ImGui_Button("Inventory"))
            {
                start_bind_inventory_key();
            }
            ImGui_SameLine();
            ImGui_Text(cf_key_button_to_string(g_app->screen_scan.inventory_key));
            
            if (ImGui_Button("Inventory Item Location"))
            {
                start_bind_inventory_location();
            }
            ImGui_Text("%d, %d", g_app->screen_scan.inventory_x, g_app->screen_scan.inventory_y);
            
            if (coroutine_is_alive(g_app->screen_scan.bind_inventory_key_co))
            {
                ImGui_Separator();
                ImGui_Text("Binding inventory key..");
                ImGui_Text("Press any key to bind inventory key");
                ImGui_Text("Press Escape to cancel");
            }
            if (coroutine_is_alive(g_app->screen_scan.bind_inventory_location_co))
            {
                ImGui_Separator();
                ImGui_Text("Binding inventory location..");
                ImGui_Text("Left Mouse Click to bind item location");
                ImGui_Text("Press Escape to cancel");
            }
        }
        
        {
            ImGui_SeparatorText("Scan");
            ImGui_SliderInt("Step Rate", &g_app->screen_scan.step_rate, 1, 512);
            
            if (ImGui_Button("Scan"))
            {
                start_scan_region();
            }
            
            if (coroutine_is_alive(g_app->screen_scan.scan_region_co))
            {
                ImGui_Separator();
                ImGui_Text("Scanning..");
                ImGui_Text("Press Escape to cancel");
                
                Optional opt_node_location = cf_map_get(g_app->process_ptrs, cf_sintern("node_location"));
                if (opt_node_location.has_value)
                {
                    CF_V3 p = opt_node_location.float3_value;
                    ImGui_Text("%.2f, %.2f, %.2f", p.x, p.y, p.z);
                }
            }
        }
        
        {
            ImGui_SeparatorText("Points");
            ImGui_Checkbox("Show Points", &g_app->screen_scan.show_points);
        }
        
        ImGui_EndTabItem();
    }
}

void ui_draw_world_map_rooms(void)
{
    static str8 search_buffer = NULL;
    if (search_buffer == NULL)
    {
        cf_string_fit(search_buffer, 1024);
    }
    
    Assets* assets = &g_app->assets;
    
    str8 label = make_arena_string(&g_arena, 256);
    
    if (ImGui_BeginTabItem("Rooms", NULL, ImGuiTabItemFlags_None))
    {
        g_app->state = App_State_World_Map;
        
        {
            ImGui_SeparatorText("Region Settings");
            ImGui_Checkbox("Show Points", &g_app->world_map.show_points);
            ImGui_SliderFloat("Layer Opacity", &g_app->world_map.opacity, 0.0f, 1.0f);
            ImGui_Checkbox("Show Region Bounds", &g_app->world_map.show_region_size);
            ImGui_InputText("Search", search_buffer, cf_array_capacity(search_buffer), ImGuiInputTextFlags_None);
        }
        
        {
            ImGui_SeparatorText("Regions");
            CF_ARRAY(Canvas*) canvases = get_sorted_canvases(g_app->game_filter, cf_array_count(g_app->game_filter));
            
            for (s32 canvas_index = 0; canvas_index < cf_array_count(canvases); ++canvas_index)
            {
                Canvas* canvas = canvases[canvas_index];
                // skip anything not visible
                if (!canvas->is_visible)
                {
                    continue;
                }
                
                CF_ARRAY(Config) regions = cf_map_get(assets->regions, canvas->game);
                for (s32 region_index = 0; region_index < cf_array_count(regions); ++region_index)
                {
                    Config* region = regions + region_index;
                    if (cf_sintern(region->tag) == canvas->region_name)
                    {
                        if (ImGui_CollapsingHeader(canvas->region_name, ImGuiTreeNodeFlags_None))
                        {
                            cf_string_fmt(label, "Add##%s_%s_room_%d", canvas->game, canvas->region_name, region_index);
                            if (ImGui_Button(label))
                            {
                                ui_begin_edit_room(canvas->game, canvas->region_name, NULL, true);
                            }
                            
                            // check per room
                            Config_Option* rooms = region->options;
                            for (s32 room_index = 0; room_index < cf_array_count(rooms); ++room_index)
                            {
                                Config_Option* room = rooms + room_index;
                                
                                Optional opt_min = config_options_get_array_float(room, "min");
                                Optional opt_max = config_options_get_array_float(room, "max");
                                Optional opt_image = config_options_get_string(room, "image");
                                Optional opt_name = config_options_get_string(room, "name");
                                
                                // room name filter
                                if (opt_name.has_value && 
                                    cf_string_count(search_buffer) && 
                                    !CF_STRSTR(opt_name.str_value, search_buffer))
                                {
                                    continue;
                                }
                                
                                CF_V3 min = *(CF_V3*)opt_min.custom_value;
                                CF_V3 max = *(CF_V3*)opt_max.custom_value;
                                
                                CF_V2 min2 = cf_v2(min.x, min.z);
                                CF_V2 max2 = cf_v2(max.x, max.z);
                                
                                CF_Aabb aabb = cf_make_aabb(min2, max2);
                                
                                cf_string_fmt(label, "##%s_%s_%d", canvas->game, canvas->region_name, room_index);
                                ImGui_BeginChild(label, (ImVec2){ 0 }, ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
                                {
                                    CF_Sprite* sprite_ptr = cf_map_get_ptr(assets->sprites, cf_sintern(opt_image.str_value));
                                    
                                    ImGui_Text("Name: %s", opt_name.str_value);
                                    ImGui_Text("Image: %s", opt_image.str_value);
                                    ImGui_Text("Max: %.2f, %.2f, %.2f", max.x, max.y, max.z);
                                    ImGui_Text("Min: %.2f, %.2f, %.2f", min.x, min.y, min.z);
                                    if (ImGui_Button("Edit"))
                                    {
                                        ui_begin_edit_room(canvas->game, canvas->region_name, room, false);
                                    }
                                    
                                    if (ImGui_IsWindowHovered(ImGuiHoveredFlags_None))
                                    {
                                        g_app->world_map.ui_hover_region = room;
                                    }
                                }
                                ImGui_EndChild();
                            }
                        }
                    }
                }
            }
        }
        ImGui_EndTabItem();
    }
}

void ui_draw_world_map(void)
{
    ui_draw_edit_room();
}

void ui_begin_edit_room(const char* game, const char* region, Config_Option* room, b32 is_new_room)
{
    // can't begin if missing game / region or all ready editing a room
    if (game == NULL || region == NULL || ui.is_editing_room)
    {
        return;
    }
    
    // can't begin is trying to edit a room and there's no room
    if (room == NULL && !is_new_room)
    {
        return;
    }
    
    MEMZERO(&ui.edit_room);
    ui.edit_room.game = cf_sintern(game);
    ui.edit_room.region = cf_sintern(region);
    ui.is_new_room = is_new_room;
    ui.room = room;
    ui.edit_room_image_p0 = cf_v2(0);
    ui.edit_room_p0 = cf_v3(0);
    ui.edit_room_image_p1 = cf_v2(0);
    ui.edit_room_p1 = cf_v3(0);
    
    if (!is_new_room)
    {
        Optional opt_name = config_options_get_string(room, "name");
        Optional opt_image = config_options_get_string(room, "image");
        Optional opt_chroma = config_options_get_color(room, "chroma");
        
        ui.edit_room.aabb = config_option_room_to_aabb3(room);
        if (opt_name.has_value)
        {
            CF_SNPRINTF(ui.edit_room.name, sizeof(ui.edit_room.name), opt_name.str_value);
        }
        else
        {
            CF_MEMSET(ui.edit_room.name, 0, sizeof(ui.edit_room.name));
        }
        
        if (opt_image.has_value)
        {
            CF_SNPRINTF(ui.edit_room.image, sizeof(ui.edit_room.image), opt_image.str_value);
        }
        else
        {
            CF_MEMSET(ui.edit_room.image, 0, sizeof(ui.edit_room.image));
        }
        
        if (opt_chroma.has_value)
        {
            ui.edit_room.chroma = opt_chroma.color_value;
        }
    }
    
    ui.is_editing_room = true;
    
    ui.edit_room_hash = hash_edit_room(&ui.edit_room);;
}

void ui_end_edit_room(b32 save)
{
    Assets* assets = &g_app->assets;
    Edit_Room* edit_room = &ui.edit_room;
    
    ui.is_editing_room = false;
    
    // ignore if not saving or no change
    if (!save || hash_edit_room(edit_room) == ui.edit_room_hash)
    {
        return;
    }
    
    if (ui.is_new_room)
    {
        CF_ARRAY(Config) regions = cf_map_get(assets->regions, edit_room->game);
        for (s32 region_index = 0; region_index < cf_array_count(regions); ++region_index)
        {
            Config* region = regions + region_index;
            if (cf_sintern(region->tag) == edit_room->region)
            {
                ui.room = config_make_options(region);
                break;
            }
        }
    }
    
    {
        CF_ARRAY(Config) regions = cf_map_get(assets->regions, edit_room->game);
        
        // update room details
        for (s32 region_index = 0; region_index < cf_array_count(regions); ++region_index)
        {
            Config* region = regions + region_index;
            if (cf_sintern(region->tag) == edit_room->region)
            {
                mount_data_read_directory();
                mount_data_write_directory();
                
                if (cf_string_equ(edit_room->image, CLIPBOARD_FILE))
                {
                    CF_SNPRINTF(edit_room->image, sizeof(edit_room->image), "images/%s_%s.png", edit_room->game, edit_room->name);
                    move_file(CLIPBOARD_FILE, edit_room->image);
                    CF_SNPRINTF(edit_room->image, sizeof(edit_room->image), "%s_%s.png", edit_room->game, edit_room->name);
                }
                
                config_options_update_kv(region, ui.room, "name", edit_room->name);
                config_options_update_kv(region, ui.room, "image", edit_room->image);
                
                CF_ARRAY(str8) min_strs = v3_to_string_list(edit_room->aabb.min);
                CF_ARRAY(str8) max_strs = v3_to_string_list(edit_room->aabb.max);
                str8 chroma_str = color_to_str8(edit_room->chroma);
                
                config_options_update_kv_list(region, ui.room, "min", min_strs, cf_array_count(min_strs));
                config_options_update_kv_list(region, ui.room, "max", max_strs, cf_array_count(max_strs));
                config_options_update_kv(region, ui.room, "chroma", chroma_str);
                
                Canvas* canvas = cf_map_get_ptr(g_app->draw.canvases, edit_room->region);
                if (canvas)
                {
                    canvas->is_draw_list_dirty = true;
                }
                
                // try to save to disk
                if (config_options_save_to_file(region, ui.room))
                {
                    printf("Saved %s - %s: %s\n", edit_room->game, edit_room->region, edit_room->name);
                }
                else
                {
                    printf("Failed to save %s - %s: %s\n", edit_room->game, edit_room->region, edit_room->name);
                }
                dismount_data_directory();
                
                break;
            }
        }
    }
}

void ui_draw_edit_room_icon(const char* label, ImVec2 p, ImVec2 min, ImVec2 max)
{
    ImDrawList* draw_list = ImGui_GetWindowDrawList();
    p.x += min.x;
    p.y += min.y;
    if (p.x >= min.x && p.y >= min.y &&
        p.x <= max.x && p.y <= max.y)
    {
        ImDrawList_AddCircleFilled(draw_list, p, 1, (ImU32){ 0xffffffff }, 8);
        ImDrawList_AddCircle(draw_list, p, 10, (ImU32){ 0xffffffff });
        ImDrawList_AddText(draw_list, p, (ImU32){ 0xffffffff }, label);
    }
}

void ui_draw_edit_room(void)
{
    if (!ui.is_editing_room)
    {
        return;
    }
    
    Assets* assets = &g_app->assets;
    Edit_Room* room = &ui.edit_room;
    
    const char** image_names = (const char**)cf_map_keys(assets->sprites);
    CF_ARRAY(const char*) image_list = NULL;
    arena_array_fit(&g_arena, image_list, cf_map_size(assets->sprites) + 1);
    
    mount_data_read_directory();
    if (cf_fs_file_exists(CLIPBOARD_FILE))
    {
        cf_array_push(image_list, CLIPBOARD_FILE);
    }
    dismount_data_directory();
    
    for (s32 index = 0; index < cf_map_size(assets->sprites); ++index)
    {
        cf_array_push(image_list, image_names[index]);
    }
    
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_HorizontalScrollbar;
    
    ImGui_Begin("Room Edit", NULL, window_flags);
    {
        if (cf_key_ctrl() && cf_key_just_pressed(CF_KEY_V))
        {
            save_clipboard_image();
        }
        
        Optional opt_node_location = cf_map_get(g_app->process_ptrs, cf_sintern("node_location"));
        
        CF_V2 mouse_location = world_map_mouse();
        CF_V3 node_location = cf_v3(0);
        
        if (opt_node_location.has_value)
        {
            node_location = opt_node_location.float3_value;
        }
        
        CF_TemporaryImage img = { 0 };
        // image
        {
            CF_Sprite* mapping_sprite = cf_map_get_ptr(assets->sprites, cf_sintern(room->image));
            if (!mapping_sprite && cf_string_equ(room->image, CLIPBOARD_FILE))
            {
                mapping_sprite = &g_app->draw.clipboard_sprite;
            }
            
            if (mapping_sprite)
            {
                img = cf_fetch_image(mapping_sprite);
                ImTextureRef tex_ref = { ._TexID = cf_texture_handle(img.tex) };
                
                CF_V2 size = cf_v2((f32)img.w, (f32)img.h);
                
                CF_V2 uv0 = img.u;
                CF_V2 uv1 = img.v;
                
                SWAP(uv0.y, uv1.y);
                
                ImGui_ImageEx(tex_ref, *(ImVec2*)&size, *(ImVec2*)&uv0, *(ImVec2*)&uv1);
                if (ImGui_IsItemHovered(ImGuiHoveredFlags_None))
                {
                    ImVec2 min = ImGui_GetItemRectMin();
                    ImVec2 max = ImGui_GetItemRectMax();
                    ImVec2 pos = ImGui_GetMousePos();
                    ImVec2 image_pos = (ImVec2){ pos.x - min.x, pos.y - min.y };
                    
                    ImGui_BeginTooltip();
                    ImGui_Text("min: %.2f, %.2f", min.x, min.y);
                    ImGui_Text("max: %.2f, %.2f", max.x, max.y);
                    ImGui_Text("pos: %.2f, %.2f", pos.x, pos.y);
                    ImGui_Text("image pos: %.2f, %.2f", image_pos.x, image_pos.y);
                    ImGui_Text("image uv: %.2f, %.2f", image_pos.x / img.w, image_pos.y / img.h);
                    ImGui_EndTooltip();
                    
                    ui_draw_edit_room_icon("1", *(ImVec2*)&ui.edit_room_image_p0, min, max);
                    ui_draw_edit_room_icon("2", *(ImVec2*)&ui.edit_room_image_p1, min, max);
                    
                    // async input so you don't need to focus the app to set points
                    if (async_get_key_down(CF_KEY_1))
                    {
                        ui.edit_room_image_p0 = cf_v2(image_pos.x, image_pos.y);
                        ui.edit_room_p0 = node_location;
                    }
                    if (async_get_key_down(CF_KEY_2))
                    {
                        ui.edit_room_image_p1 = cf_v2(image_pos.x, image_pos.y);
                        ui.edit_room_p1 = node_location;
                    }
                }
            }
        }
        
        ImGui_Text("Node: %.2f, %.2f, %.2f", node_location.x, node_location.y, node_location.z);
        ImGui_Text("Press 1 to set P1");
        ImGui_Text("Press 2 to set P2");
        
        // resolve from world samples -> image space -> resized world space
        if (ImGui_Button("Auto Map") && img.tex.id)
        {
            CF_V3 min = cf_min(ui.edit_room_p0, ui.edit_room_p1);
            CF_V3 max = cf_max(ui.edit_room_p0, ui.edit_room_p1);
            
            CF_Aabb3 aabb = cf_make_aabb3(min, max);
            CF_V3 extents = cf_extents_aabb3(aabb);
            
            CF_V3 p0 = ui.edit_room_p0;
            CF_V3 p1 = ui.edit_room_p1;
            
            CF_V2 uv0 = cf_v2(ui.edit_room_image_p0.x / img.w, ui.edit_room_image_p0.y / img.h);
            CF_V2 uv1 = cf_v2(ui.edit_room_image_p1.x / img.w, ui.edit_room_image_p1.y / img.h);
            
            uv0.y = 1.0f - uv0.y;
            uv1.y = 1.0f - uv1.y;
            
            // ensure uv0 is bottom left and uv1 is top right
            if (uv0.x > uv1.x)
            {
                SWAP(uv0.x, uv1.x);
            }
            if (uv0.y > uv1.y)
            {
                SWAP(uv0.y, uv1.y);
            }
            
            CF_V2 duv = cf_abs(cf_sub(uv1, uv0));
            // uv is all ready in scaled image space [0, 1] so resize this to match scale to fit image size
            CF_V2 scaled_duv = cf_div(cf_v2(1), duv);
            CF_V3 scaled_extents = cf_v3(extents.x * scaled_duv.x, extents.y, extents.z * scaled_duv.y);
            
            // offset min from uv0 so this aligns with bottom left
            min.x -= uv0.x * scaled_extents.x;
            min.z -= uv0.y * scaled_extents.z;
            
            max = cf_add(min, scaled_extents);
            
            ui.edit_room.aabb = cf_make_aabb3(min, max);
        }
        ImGui_SameLine();
        if (ImGui_Button("Center Camera"))
        {
            CF_V3 center = cf_center_aabb3(ui.edit_room.aabb);
            g_app->world_map.camera.position = cf_v2(-center.x, -center.z);
        }
        
        ImGui_Text("Game: %s", room->game);
        ImGui_Text("Region: %s", room->region);
        
        ImGui_InputText("Name", room->name, sizeof(room->name), ImGuiInputTextFlags_None);
        
        s32 image_selection = 0;
        CF_Sprite* sprite_ptr = cf_map_get_ptr(assets->sprites, cf_sintern(room->image));
        if (sprite_ptr)
        {
            for (s32 index = 0; index < cf_array_count(image_list); ++index)
            {
                if (cf_string_equ(image_list[index], room->image))
                {
                    image_selection = index;
                    break;
                }
            }
        }
        
        if (ImGui_ComboChar("Image", &image_selection, image_list, cf_array_count(image_list)))
        {
            CF_SNPRINTF(room->image, sizeof(room->image), image_list[image_selection]);
        }
        
        ImGui_ColorEdit3("Chroma", (f32*)&room->chroma, ImGuiColorEditFlags_None);
        ImGui_SliderFloat("Chroma Threshold", &room->chroma.a, 0.0f, 1.0f);
        
        ImGui_InputFloat3("Min", (f32*)&room->aabb.min);
        ImGui_InputFloat3("Max", (f32*)&room->aabb.max);
        
        if (ImGui_Button("Save"))
        {
            ui_end_edit_room(true);
        }
        ImGui_SameLine();
        if (ImGui_Button("Close"))
        {
            ui_end_edit_room(false);
        }
    }
    ImGui_End();
}

void ui_draw_point_cloud_table(void)
{
    g_app->scan_points.highlight_shape_index = -1;
    if (ImGui_BeginTabItem("Scan Locations", NULL, ImGuiTabItemFlags_None))
    {
        CF_ARRAY(Scan_Point) points = g_app->scan_points.points;
        CF_ARRAY(Filter_Shape) shapes = g_app->scan_points.filter_shapes;
        
        str8 label = make_arena_string(&g_arena, 256);
        
        {
            ImGui_SeparatorText("Filter");
            ImGui_Text("Count: %d", cf_array_count(shapes));
            if (ImGui_Button("Add Circle##add_filter_shape_circle"))
            {
                start_draw_filter_shape(true);
            }
            ImGui_SameLine();
            if (ImGui_Button("Add Box##add_filter_shape_box"))
            {
                start_draw_filter_shape(false);
            }
            ImGui_SameLine();
            if (ImGui_Button("Clear##shapes"))
            {
                clear_filter_shapes();
                stop_draw_filter_shape();
            }
            
            ImGui_BeginChild("##point_cloud_shapes_window", (ImVec2){ .x = 0, .y = 256 }, ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
            {
                for (s32 index = 0; index < cf_array_count(shapes); ++index)
                {
                    Filter_Shape shape = shapes[index];
                    cf_string_fmt(label, "%d##filter_shape_%d", index, index);
                    ImGui_BeginChild(label, (ImVec2){ 0 }, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_None);
                    {
                        ImGui_Text("Is Screen Space: %s", shape.is_game_screen_space ? "Yes" : "No");
                        if (shape.is_circle)
                        {
                            cf_string_fmt(label, "Center##filter_shape_circle_%d", index);
                            ImGui_InputFloat2(label, (float*)&shape.c.p);
                            cf_string_fmt(label, "Radius##filter_shape_radius_%d", index);
                            ImGui_InputFloat(label, (float*)&shape.c.r);
                        }
                        else
                        {
                            cf_string_fmt(label, "P0##filter_shape_bb_p0_%d", index);
                            ImGui_InputFloat2(label, (float*)&shape.bb.p0);
                            cf_string_fmt(label, "P1##filter_shape_bb_p1_%d", index);
                            ImGui_InputFloat2(label, (float*)&shape.bb.p1);
                        }
                        
                        if (ImGui_IsWindowHovered(ImGuiHoveredFlags_None))
                        {
                            g_app->scan_points.highlight_shape_index = index;
                        }
                        
                        cf_string_fmt(label, "Del##filter_shape_del_%d", index);
                        if (ImGui_Button(label))
                        {
                            array_remove(shapes, index);
                            mark_filter_scan_points_dirty();
                        }
                    }
                    ImGui_EndChild();
                }
            }
            ImGui_EndChild();
        }
        
        {
            ImGui_SeparatorText("Points");
            if (ImGui_Button("Clear##points"))
            {
                cf_array_clear(points);
                mark_filter_scan_points_dirty();
            }
            ImGui_SameLine();
            ImGui_Text("Count: %d", cf_array_count(points));
            
            if (cf_array_count(points) > 0)
            {
                ImGui_BeginChild("##point_cloud_points_window", (ImVec2){ .x = 0, .y = 256 }, ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
                {
                    for (s32 index = 0; index < cf_array_count(points); ++index)
                    {
                        Scan_Point point = points[index];
                        
                        cf_string_fmt(label, "Del##%d", index);
                        if (ImGui_Button(label))
                        {
                            array_remove(points, index);
                        }
                        ImGui_SameLine();
                        ImGui_Text("%.2f, %.2f, %.2f", point.p.x, point.p.y, point.p.z);
                        if (ImGui_IsItemHovered(ImGuiHoveredFlags_None))
                        {
                            ui_push_tooltip_scan_point(point.p);
                        }
                    }
                }
                ImGui_EndChild();
            }
        }
        
        ImGui_EndTabItem();
    }
}

void ui_push_tooltip_scan_point(CF_V3 point)
{
    cf_array_push(ui.tooltip_scan_points, point);
}

void ui_push_tooltip_world_hover_point(CF_V3 point)
{
    cf_array_push(ui.tooltip_world_hover_points, point);
}

void ui_draw_tooltip_points(void)
{
    Assets* assets = &g_app->assets;
    
    {
        CF_ARRAY(CF_V3) world_points = ui.tooltip_world_hover_points;
        if (cf_array_count(world_points) > 0)
        {
            ImGui_BeginTooltip();
            for (s32 point_index = 0; point_index < cf_array_count(world_points); ++point_index)
            {
                CF_V3 point = world_points[point_index];
                ImGui_Text("%.2f, %.2f, %.2f", point.x, point.y, point.z);
            }
            ImGui_EndTooltip();
        }
        cf_array_clear(world_points);
    }
    
    {
        CF_ARRAY(CF_V3) scan_points = ui.tooltip_scan_points;
        
        if (cf_array_count(scan_points) > 0)
        {
            ImGui_BeginTooltip();
            
            CF_ARRAY(Canvas*) canvases = get_sorted_canvases(g_app->game_filter, cf_array_count(g_app->game_filter));
            
            // walk through all points and clip it against which which room the point is inside of
            // and show tooltip
            for (s32 point_index = 0; point_index < cf_array_count(scan_points); ++point_index)
            {
                CF_V3 point = scan_points[point_index];
                
                for (s32 canvas_index = 0; canvas_index < cf_array_count(canvases); ++canvas_index)
                {
                    Canvas* canvas = canvases[canvas_index];
                    
                    if (!canvas->is_visible)
                    {
                        continue;
                    }
                    
                    Config* region = get_region_config(canvas->game, canvas->region_name);
                    CF_ARRAY(Config_Option) rooms = region->options;
                    for (s32 room_index = 0; room_index < cf_array_count(rooms); ++room_index)
                    {
                        Config_Option* room = rooms + room_index;
                        Optional opt_image = config_options_get_string(room, "image");
                        Optional opt_min = config_options_get_float3(room, "min");
                        Optional opt_max = config_options_get_float3(room, "max");
                        
                        if (opt_min.has_value && opt_max.has_value)
                        {
                            CF_Aabb3 aabb = cf_make_aabb3(opt_min.float3_value, opt_max.float3_value);
                            CF_V3 extents = cf_extents_aabb3(aabb);
                            
                            if (cf_contains_point_aabb3(aabb, point))
                            {
                                ImGui_SeparatorText(canvas->region_name);
                                
                                if (opt_image.has_value)
                                {
                                    CF_Sprite* sprite = cf_map_get_ptr(assets->sprites, cf_sintern(opt_image.str_value));
                                    if (sprite)
                                    {
                                        CF_TemporaryImage img = cf_fetch_image(sprite);
                                        ImTextureRef tex_ref = { ._TexID = cf_texture_handle(img.tex) };
                                        ImVec2 size = { 128, 128 };
                                        size.x = cf_min(size.x, (f32)img.w);
                                        size.y = cf_min(size.y, (f32)img.h);
                                        
                                        // get uv half extents for preview image
                                        CF_V2 uv_half_extents = cf_v2(size.x / img.w, size.y / img.h);
                                        uv_half_extents = cf_mul(uv_half_extents, 0.5f);
                                        
                                        // get local point and convert to uv space of image [0, 1]
                                        CF_V3 local_point = cf_sub(point, aabb.min);
                                        CF_V2 local_uv = cf_div(cf_v2(local_point.x, local_point.z), cf_v2(extents.x, extents.z));
                                        // flip y since image is top left origin
                                        local_uv.y = 1.0f - local_uv.y;
                                        
                                        // convert from local uv space to texture uv
                                        CF_V2 duv = cf_abs(cf_sub(img.u, img.v));
                                        CF_V2 uv = cf_mul(duv, local_uv);
                                        
                                        CF_V2 uv0 = cf_sub(uv, cf_v2(uv_half_extents.x,uv_half_extents.y));
                                        CF_V2 uv1 = cf_add(uv, cf_v2(uv_half_extents.x, uv_half_extents.y));
                                        
                                        ImGui_ImageEx(tex_ref, size, *(ImVec2*)&uv0, *(ImVec2*)&uv1);
                                    }
                                }
                                
                                ImGui_Text("%.2f, %.2f, %.2f", point.x, point.y, point.z);
                            }
                        }
                    }
                }
            }
            
            ImGui_EndTooltip();
        }
        
        cf_array_clear(scan_points);
    }
}