#ifndef CE_CONFIG_H
#define CE_CONFIG_H

// configuration module to control the editor, builds into ce_config.so and can be rebuilt reloaded at runtime with F5

#include <sys/time.h>

#include "ce.h"
#include "ce_vim.h"
#include "ce_terminal.h"

#define DRAW_USEC_LIMIT 33333

typedef struct InputHistoryNode_t{
     char* entry;
     struct InputHistoryNode_t* next;
     struct InputHistoryNode_t* prev;
}InputHistoryNode_t;

typedef struct{
     InputHistoryNode_t* head;
     InputHistoryNode_t* tail;
     InputHistoryNode_t* cur;
}InputHistory_t;

typedef struct{
     BufferCommitNode_t* commit_tail;
     VimBufferState_t vim_buffer_state;
}BufferState_t;

typedef struct TabView_t{
     BufferView_t* view_head;
     BufferView_t* view_current;
     BufferView_t* view_previous;
     BufferView_t* view_input_save;
     BufferView_t* view_overrideable;
     Buffer_t* overriden_buffer;
     struct TabView_t* next;
}TabView_t;

typedef struct CompleteNode_t{
     char* option;
     struct CompleteNode_t* next;
     struct CompleteNode_t* prev;
}CompleteNode_t;

typedef struct{
     CompleteNode_t* head;
     CompleteNode_t* tail;
     CompleteNode_t* current;
     Point_t start;
}AutoComplete_t;

typedef struct TerminalNode_t{
     Terminal_t terminal;
     Buffer_t* buffer;
     pthread_t check_update_thread;
     int64_t last_jump_location;
     struct TerminalNode_t* next;
}TerminalNode_t;

typedef struct{
     char filepath[PATH_MAX];
     Point_t location;
}Jump_t;

#define JUMP_LIST_MAX 32

typedef struct{
     Jump_t jumps[JUMP_LIST_MAX];
     int64_t jump_current;
}BufferViewState_t;

typedef struct{
     ce_command* func;
     const char* name;
}CommandEntry_t;

typedef struct{
     bool input;
     const char* input_message;
     int input_key;

     Buffer_t* completion_buffer;

     Buffer_t input_buffer;
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

     InputHistory_t search_history;
     InputHistory_t load_file_history;

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

     bool quit;

     BufferNode_t** save_buffer_head;
}ConfigState_t;

#endif
