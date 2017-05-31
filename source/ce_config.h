#ifndef CE_CONFIG_H
#define CE_CONFIG_H

// configuration module to control the editor, builds into ce_config.so and can be rebuilt reloaded at runtime with F5

#include <sys/time.h>

#include "ce.h"
#include "vim.h"
#include "terminal.h"
#include "tab_view.h"
#include "input.h"
#include "auto_complete.h"
#include "jump.h"
#include "command.h"

// NOTE: 60 fps limit
#define DRAW_USEC_LIMIT 16666

typedef struct{
     BufferCommitNode_t* commit_tail;
     VimBufferState_t vim_buffer_state;
}BufferState_t;

typedef struct TerminalNode_t{
     Terminal_t terminal;
     Buffer_t* buffer;
     pthread_t check_update_thread;
     int64_t last_jump_location;
     struct TerminalNode_t* next;
}TerminalNode_t;

typedef struct{
     JumpArray_t jump_array;
}BufferViewState_t;

typedef struct{
     int* keys;
     int64_t key_count;
     Command_t command;
     VimMode_t vim_mode;
}KeyBind_t;

typedef struct{
     KeyBind_t* binds;
     int64_t count;
}KeyBinds_t;

typedef struct{
     int keys[4];
     const char* command;
}KeyBindDef_t;

typedef struct{
     Buffer_t buffer_list_buffer;
     Buffer_t mark_list_buffer;
     Buffer_t yank_list_buffer;
     Buffer_t macro_list_buffer;
     Buffer_t clang_completion_buffer;

     Buffer_t* completion_buffer;

     Buffer_t* buffer_before_query;

     Input_t input;

     VimState_t vim_state;
     VimKeyHandlerResultType_t last_vim_result_type;

     TabView_t* tab_head;
     TabView_t* tab_current;

     BufferView_t* view_auto_complete;

     pthread_t clang_complete_thread;

     TerminalNode_t* terminal_head;
     TerminalNode_t* terminal_current; // most recent terminal in focus

     AutoComplete_t auto_complete;

     LineNumberType_t line_number_type;
     HighlightLineType_t highlight_line_type;

     char editting_register;

     bool do_not_highlight_search;

     struct timeval last_draw_time;

     CommandEntry_t* command_entries;
     int64_t command_entry_count;
     int64_t max_auto_complete_height;

     KeyBinds_t binds[VM_COUNT];

     int* keys;
     int64_t key_count;

     bool quit;

     BufferNode_t** save_buffer_head;
}ConfigState_t;

typedef struct{
     ConfigState_t* config_state;
     BufferNode_t** head;
}CommandData_t;

pthread_mutex_t view_input_save_lock;
pthread_mutex_t draw_lock;
pthread_mutex_t completion_lock;

void view_drawer(void* user_data);

#endif
