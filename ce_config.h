#ifndef CE_CONFIG_H
#define CE_CONFIG_H

// configuration module to control the editor, builds into ce_config.so and can be rebuilt reloaded at runtime with F5

#include "ce.h"
#include "ce_vim.h"
#include "ce_terminal.h"

typedef struct InputHistoryNode_t {
     char* entry;
     struct InputHistoryNode_t* next;
     struct InputHistoryNode_t* prev;
} InputHistoryNode_t;

typedef struct {
     InputHistoryNode_t* head;
     InputHistoryNode_t* tail;
     InputHistoryNode_t* cur;
} InputHistory_t;

typedef struct{
     BufferCommitNode_t* commit_tail;
     VimBufferState_t vim_buffer_state;
} BufferState_t;

typedef struct TabView_t{
     BufferView_t* view_head;
     BufferView_t* view_current;
     BufferView_t* view_previous;
     BufferView_t* view_input_save;
     BufferView_t* view_overrideable;
     Buffer_t* overriden_buffer;
     struct TabView_t* next;
} TabView_t;

typedef struct CompleteNode_t{
     char* option;
     struct CompleteNode_t* next;
     struct CompleteNode_t* prev;
} CompleteNode_t;

typedef struct{
     CompleteNode_t* head;
     CompleteNode_t* tail;
     CompleteNode_t* current;
     Point_t start;
} AutoComplete_t;

typedef struct{
     bool input;
     const char* input_message;
     int input_key;

     Buffer_t* completion_buffer;    // same as shell_command_buffer

     Buffer_t input_buffer;
     Buffer_t buffer_list_buffer;
     Buffer_t mark_list_buffer;
     Buffer_t yank_list_buffer;
     Buffer_t macro_list_buffer;
     Buffer_t* buffer_before_query;

     VimState_t vim_state;

     int64_t last_command_buffer_jump;
     int last_key;

     TabView_t* tab_head;
     TabView_t* tab_current;

     BufferView_t* view_input;

     InputHistory_t search_history;
     InputHistory_t load_file_history;

     Terminal_t terminal;
     pthread_t terminal_check_update_thread;

     AutoComplete_t auto_complete;

     LineNumberType_t line_number_type;
     HighlightLineType_t highlight_line_type;

     char editting_register;

     bool quit;
} ConfigState_t;

#endif

