#ifndef UI_H
#define UI_H

b32 ui_is_hovering_any_windows(void);
b32 ui_get_edit_room(Edit_Room* out_room);

void ui_draw(void);
void ui_push_tooltip_scan_point(CF_V3 point);
void ui_push_tooltip_world_hover_point(CF_V3 point);

#endif //UI_H
