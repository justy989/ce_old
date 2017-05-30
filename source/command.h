#pragma once

#include <inttypes.h>
#include <stdbool.h>

typedef enum{
     CS_SUCCESS,
     CS_FAILURE,
     CS_PRINT_HELP,
}CommandStatus_t;

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

typedef CommandStatus_t ce_command (Command_t*, void*);

typedef struct{
     ce_command* func;
     const char* name;
     const char* usage; // optional
     const char* description; // optional
     const char* supported; // optional
}CommandEntry_t;

void command_entry_log(CommandEntry_t* entry);

bool command_parse(Command_t* command, const char* string);
void command_free(Command_t* command);
void command_log(Command_t* command);

CommandEntry_t* commands_init(int64_t* command_entry_count);

// default commands
CommandStatus_t command_help(Command_t* command, void* user_data);
CommandStatus_t command_quit_all(Command_t* command, void* user_data);
CommandStatus_t command_save(Command_t* command, void* user_data);
CommandStatus_t command_save_and_close_view(Command_t* command, void* user_data);
CommandStatus_t command_redraw(Command_t* command, void* user_data);
CommandStatus_t command_highlight_line(Command_t* command, void* user_data);
CommandStatus_t command_line_number(Command_t* command, void* user_data);
CommandStatus_t command_noh(Command_t* command, void* user_data);
CommandStatus_t command_keybind_add(Command_t* command, void* user_data);

CommandStatus_t command_buffer_rename(Command_t* command, void* user_data);
CommandStatus_t command_buffer_reload(Command_t* command, void* user_data);
CommandStatus_t command_buffer_new(Command_t* command, void* user_data);
CommandStatus_t command_buffer_syntax(Command_t* command, void* user_data);

CommandStatus_t command_move_on_screen(Command_t* command, void* user_data);
CommandStatus_t command_move_half_page(Command_t* command, void* user_data);

CommandStatus_t command_view_split(Command_t* command, void* user_data);
CommandStatus_t command_view_close(Command_t* command, void* user_data);
CommandStatus_t command_view_scroll(Command_t* command, void* user_data);
CommandStatus_t command_view_switch(Command_t* command, void* user_data);

CommandStatus_t command_tab_new(Command_t* command, void* user_data);
CommandStatus_t command_tab_next(Command_t* command, void* user_data);
CommandStatus_t command_tab_previous(Command_t* command, void* user_data);

CommandStatus_t command_show_buffers(Command_t* command, void* user_data);
CommandStatus_t command_show_marks(Command_t* command, void* user_data);
CommandStatus_t command_show_macros(Command_t* command, void* user_data);
CommandStatus_t command_show_yanks(Command_t* command, void* user_data); // LOL

CommandStatus_t command_switch_buffer_dialogue(Command_t* command, void* user_data);
CommandStatus_t command_command_dialogue(Command_t* command, void* user_data);
CommandStatus_t command_search_dialogue(Command_t* command, void* user_data);
CommandStatus_t command_load_file_dialogue(Command_t* command, void* user_data);
CommandStatus_t command_replace_dialogue(Command_t* command, void* user_data);
CommandStatus_t command_cancel_dialogue(Command_t* command, void* user_data);

CommandStatus_t command_terminal_goto(Command_t* command, void* user_data);
CommandStatus_t command_terminal_new(Command_t* command, void* user_data);
CommandStatus_t command_terminal_jump_to_dest(Command_t* command, void* user_data);
CommandStatus_t command_terminal_run_man(Command_t* command, void* user_data);
CommandStatus_t command_terminal_run_command(Command_t* command, void* user_data);

CommandStatus_t command_jump_next(Command_t* command, void* user_data);
CommandStatus_t command_jump_previous(Command_t* command, void* user_data);

CommandStatus_t command_completion_toggle(Command_t* command, void* user_data);
CommandStatus_t command_completion_apply(Command_t* command, void* user_data);
CommandStatus_t command_completion_next(Command_t* command, void* user_data);
CommandStatus_t command_completion_previous(Command_t* command, void* user_data);

CommandStatus_t command_cscope_goto_definition(Command_t* command, void* user_data);
CommandStatus_t command_goto_file_under_cursor(Command_t* command, void* user_data);
CommandStatus_t command_macro_backslashes(Command_t* command, void* user_data);
