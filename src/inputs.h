
#ifndef _Inputs
#define _Inputs

void center_selected();
void center_tracked();
void identify_object_under_cursor(ImGuiIO &io);
void pan_with_crosshairs(ImGuiIO &io);
void show_menu();
void process_key_cmd_char(char c);
void process_key_cmd_ctrl_char(char c);
void process_keyboard_commands(ImGuiIO &io);
void process_key_arrowup();
void process_key_arrowdn();
void process_key_arrowleft();
void process_key_arrowright();
void process_key_home();
void process_key_end();
void process_key_F1();
void process_key_F2();
void process_key_F3();
void process_key_F4();
void process_key_F5();
void process_key_F6();
void process_key_F7();
void process_key_F8();
void process_key_F9();
void process_key_F10();
void process_key_F11();
void process_key_F12();
void do_find();
int lookfor_cb(ImGuiInputTextCallbackData* data);

#endif