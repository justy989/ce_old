#ifndef CE_CONFIG_H
#define CE_CONFIG_H

// configuration module to control the editor, builds into ce_config.so and can be rebuilt reloaded at runtime with F5

#include <sys/time.h>

#include "ce.h"
#include "vim.h"
#include "ce_terminal.h"
#include "tab_view.h"
#include "text_history.h"
#include "auto_complete.h"
#include "jump.h"

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
     ce_command* func;
     const char* name;
     bool hidden;
}CommandEntry_t;

typedef struct{
     bool input;
     const char* input_message;
     int input_key;
     VimMode_t input_mode_save;
     Point_t input_visual_save;

     Buffer_t* completion_buffer;

     Buffer_t input_buffer;
     Buffer_t clang_completion_buffer;
     Buffer_t buffer_list_buffer;
     Buffer_t mark_list_buffer;
     Buffer_t yank_list_buffer;
     Buffer_t macro_list_buffer;
     Buffer_t* buffer_before_query;

     VimState_t vim_state;

     int last_key;

     TabView_t* tab_head;
     TabView_t* tab_current;

     BufferView_t* view_input;
     BufferView_t* view_auto_complete;

     TextHistory_t search_history;
     TextHistory_t command_history;

     pthread_t clang_complete_thread;

     TerminalNode_t* terminal_head;
     TerminalNode_t* terminal_current; // most recent terminal in focus

     AutoComplete_t auto_complete;

     LineNumberType_t line_number_type;
     HighlightLineType_t highlight_line_type;

     char editting_register;

     bool do_not_highlight_search;

     char* load_file_search_path;

     struct timeval last_draw_time;

     CommandEntry_t* command_entries;
     int64_t command_entry_count;
     int64_t max_auto_complete_height;

     bool quit;

     BufferNode_t** save_buffer_head;
}ConfigState_t;

typedef struct{
     ConfigState_t* config_state;
     BufferNode_t* head;
}CommandData_t;

#endif
