
#ifndef _Inputs
#define _Inputs

void center_selected();
void center_tracked();
void identify_object_under_cursor(ImGuiIO &io);
void pan_with_crosshairs(ImGuiIO &io);
void process_key_cmd_char(char c);
void process_keyboard_commands(ImGuiIO &io);
void do_find();
int lookfor_cb(ImGuiInputTextCallbackData* data);

#endif