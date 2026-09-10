#include <cute.h>

#include "unity.h"

int main(int argc, char* argv[])
{
    init_memory(CF_MB * 32);
    
    s32 display_index = 0;
    s32 options = CF_APP_OPTIONS_WINDOW_POS_CENTERED_BIT | CF_APP_OPTIONS_RESIZABLE_BIT;
    s32 width = 1024;
    s32 height = 768;
    
    CF_Result result = cf_make_app("Divinity Fling Visualizer", display_index, 0, 0, width, height, options, argv[0]);
    
    cf_app_init_imgui();
    
    make_app();
    
    while (cf_app_is_running())
    {
        cf_app_update(app_update);
        
        app_draw();
        cf_app_draw_onto_screen(false);
    }
    
    destroy_app();
    
    cf_destroy_app();
    
    return 0;
}
