#pragma once

#include <inttypes.h>
#include <stdbool.h>

typedef enum{
     CAT_INTEGER,
     CAT_DECIMAL,
     CAT_STRING,
     CAT_COUNT
}CommandArgType_t;

typedef struct{
     CommandArgType_t type;

     union{
          int64_t integer;
          double decimal;
          char* string;
     };
}CommandArg_t;

#define COMMAND_NAME_MAX_LEN 128

typedef struct{
     char name[COMMAND_NAME_MAX_LEN];
     CommandArg_t* args;
     int64_t arg_count;
}Command_t;

typedef void ce_command (Command_t*, void*);

typedef struct{
     ce_command* func;
     const char* name;
     bool hidden;
}CommandEntry_t;

bool command_parse(Command_t* command, const char* string);
void command_free(Command_t* command);
void command_log(Command_t* command);

CommandEntry_t* commands_init(int64_t* command_entry_count);

// default commands
void command_reload_buffer(Command_t* command, void* user_data);
void command_syntax(Command_t* command, void* user_data);
void command_quit_all(Command_t* command, void* user_data);
void command_cscope_goto_definition(Command_t* command, void* user_data);
void command_noh(Command_t* command, void* user_data);
void command_line_number(Command_t* command, void* user_data);
void command_highlight_line(Command_t* command, void* user_data);
void command_new_buffer(Command_t* command, void* user_data);
void command_rename(Command_t* command, void* user_data);
void command_save(Command_t* command, void* user_data);
void command_save_and_close_view(Command_t* command, void* user_data);
void command_macro_backslashes(Command_t* command, void* user_data);
void command_goto_file_under_cursor(Command_t* command, void* user_data);
void command_redraw(Command_t* command, void* user_data);

void command_move_on_screen(Command_t* command, void* user_data);
void command_move_half_page(Command_t* command, void* user_data);

void command_view_split(Command_t* command, void* user_data);
void command_view_close(Command_t* command, void* user_data);
void command_view_scroll(Command_t* command, void* user_data);
void command_view_switch(Command_t* command, void* user_data);

void command_tab_new(Command_t* command, void* user_data);
void command_tab_next(Command_t* command, void* user_data);
void command_tab_previous(Command_t* command, void* user_data);

void command_show_buffers(Command_t* command, void* user_data);
void command_show_marks(Command_t* command, void* user_data);
void command_show_macros(Command_t* command, void* user_data);
void command_show_yanks(Command_t* command, void* user_data); // LOL

void command_switch_buffer_dialogue(Command_t* command, void* user_data);
void command_command_dialogue(Command_t* command, void* user_data);
void command_search_dialogue(Command_t* command, void* user_data);
void command_load_file_dialogue(Command_t* command, void* user_data);
void command_replace_dialogue(Command_t* command, void* user_data);
void command_cancel_dialogue(Command_t* command, void* user_data);

void command_terminal_goto(Command_t* command, void* user_data);
void command_terminal_new(Command_t* command, void* user_data);
void command_terminal_jump_to_dest(Command_t* command, void* user_data);
void command_terminal_run_man(Command_t* command, void* user_data);
void command_terminal_run_command(Command_t* command, void* user_data);

void command_jump_next(Command_t* command, void* user_data);
void command_jump_previous(Command_t* command, void* user_data);

void command_keybind_add(Command_t* command, void* user_data);

void command_completion_toggle(Command_t* command, void* user_data);
void command_completion_apply(Command_t* command, void* user_data);
void command_completion_next(Command_t* command, void* user_data);
void command_completion_previous(Command_t* command, void* user_data);
