#ifndef APP_H
#define APP_H

#ifndef DIV_VERSION
#define DIV_VERSION "0.3.0"
#endif

#define CANVAS_WIDTH 2048
#define CANVAS_HEIGHT 2048

#define CLIPBOARD_FILE ".clipboard.png"
#define ZOOM_MIN 0.1f
#define ZOOM_MAX 10.0f

typedef enum App_State
{
    App_State_Screen_Scan,
    App_State_World_Map,
} App_State;

typedef struct Camera
{
    CF_V2 position;
    CF_V2 motion;
    f32 zoom;
} Camera;

typedef struct Edit_Room
{
    const char* game;
    const char* region;
    char name[1024];
    char image[1024];
    CF_Aabb3 aabb;
    CF_Color chroma;
} Edit_Room;

typedef struct Canvas
{
    const char* game;
    const char* region_name;
    CF_Canvas canvas;
    CF_DrawList draw_list;
    
    CF_Aabb aabb;
    
    s32 order;
    b32 is_visible;
    b32 is_draw_list_dirty;
} Canvas;

typedef struct Input
{
    CF_V2 mouse_motion;
    f32 mouse_wheel;
} Input;

typedef struct Scan_Shape
{
    b32 is_circle;
    
    struct
    {
        CF_V2 p0;
        CF_V2 p1;
    } bb;
    CF_Circle c;
} Scan_Shape;

typedef struct Filter_Shape
{
    b32 is_game_screen_space;
    b32 is_circle;
    
    struct
    {
        CF_V2 p0;
        CF_V2 p1;
    } bb;
    CF_Circle c;
} Filter_Shape;

typedef struct Scan_Point
{
    s32 x;
    s32 y;
    CF_V3 p;
} Scan_Point;

typedef struct App
{
    Process_Info process;
    CF_Threadpool* threadpool;
    Assets assets;
    
    Input input;
    
    CF_Arena process_arena;
    
    CF_MAP(const char*) process_names;
    
    Config_Option* game_signature;
    Config_Option* game_address;
    
    CF_MAP(Optional) process_ptrs;
    
    CF_ARRAY(const char*) game_filter;
    b32 auto_game_filter;
    
    // current game viewport
    struct
    {
        // screen space camera from game screen shot
        Camera camera;
        
        s32 grid_x;
        s32 grid_y;
        f32 grid_line_thickness;
        CF_Color grid_color;
        
        Scan_Shape shape;
        
        CF_KeyButton inventory_key;
        s32 inventory_x;
        s32 inventory_y;
        
        s32 step_rate;
        
        CF_Coroutine bind_inventory_location_co;
        CF_Coroutine bind_inventory_key_co;
        CF_Coroutine scan_region_co;
        
        bool show_grid;
        bool show_inventory_item_location;
        bool show_points;
    } screen_scan;
    
    // overworld map of current level
    struct
    {
        // top down camera of the entire level's world
        Camera camera;
        f32 opacity;
        
        CF_Color character_location_color;
        CF_Color node_location_color;
        CF_Color fling_direction_color;
        f32 fling_arrow_thickness;
        f32 fling_arrow_width;
        
        s32 grid_x;
        s32 grid_y;
        f32 grid_line_thickness;
        CF_Color grid_color;
        
        // cleared out at start of every frame
        // when hovered on the actual world map
        Config_Option* mouse_hover_region;
        // cleared out on start of ui frame
        // when hovered on ui like the region list
        Config_Option* ui_hover_region;
        
        bool show_grid;
        bool show_region_size;
        bool show_points;
    } world_map;
    
    struct
    {
        CF_ARRAY(Scan_Point) points;
        CF_ARRAY(Scan_Point) filter_points;
        CF_ARRAY(Filter_Shape) filter_shapes;
        
        CF_Coroutine draw_filter_shape_co;
        
        s32 highlight_shape_index;
    } scan_points;
    
    struct
    {
        CF_Sprite game_screen_sprite;
        CF_Sprite clipboard_sprite;
        
        CF_MAP(Canvas) canvases;
        CF_Shader transparency_shader;
        CF_Shader chroma_shader;
        
        s32 width;
        s32 height;
        CF_V2 screen_size;
    } draw;
    
    App_State state;
} App;

extern App* g_app;

void app_save_config(void);
void app_load_config(void);

App* make_app(void);
void destroy_app(void);
void app_update(void* udata);
void app_draw(void);

Camera camera_defaults(void);
// returns a list of canvases based off of game_filters
// if filter_count is 0 then all canvases are returned
CF_ARRAY(Canvas*) get_sorted_canvases(const char** game_filters, s32 filter_count);
Config* get_region_config(const char* game, const char* region_name);

CF_V2 camera_mouse(Camera* camera);
CF_V2 world_map_mouse(void);
CF_Aabb config_option_room_to_aabb(Config_Option* room);
CF_Aabb3 config_option_room_to_aabb3(Config_Option* room);

void update_sprite_data(CF_Sprite* sprite, CF_Pixel* pixels, s32 w, s32 h);

void save_clipboard_image(void);
b32 move_clipboard_file(const char* new_path);

u64 hash_edit_room(Edit_Room* room);

void game_screenshot(void);
void game_press_inventory(void);

void screen_scan_position_to_process_position(CF_V2 position, s32* out_x, s32 *out_y);
CF_V2 process_position_to_screen_scan_position(s32 x, s32 y);
s32 screen_scan_radius_to_process_radius(f32 radius);
f32 process_radius_to_screen_scan_radius(s32 radius);

void start_bind_inventory_location(void);
void start_bind_inventory_key(void);
void start_scan_region(void);
void stop_scan_region(void);
void start_draw_filter_shape(b32 is_circle);
void stop_draw_filter_shape(void);

void add_filter_shape(Filter_Shape shape);
void clear_filter_shapes(void);

CF_ARRAY(Scan_Point) get_filtered_scan_points(void);
void mark_filter_scan_points_dirty(void);

#endif //APP_H
