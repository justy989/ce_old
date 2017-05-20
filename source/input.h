#pragma once

#include "ce.h"
#include "vim.h"
#include "text_history.h"

typedef struct{
     int key;
     const char* message;

     VimMode_t vim_mode_save;
     Point_t visual_save;
     Point_t cursor_save;
     int64_t scroll_top_row_save;
     int64_t scroll_left_column_save;
     BufferView_t* view_save;

     BufferView_t* view;
     Buffer_t buffer;

     TextHistory_t search_history;
     TextHistory_t command_history;

     char* load_file_search_path;
}Input_t;

TextHistory_t* input_get_history(Input_t* input);
void input_start(Input_t* input, BufferView_t** view, VimState_t* vim_state, const char* message, int key);
void input_end(Input_t* input, VimState_t* vim_state);
void input_cancel(Input_t* input, BufferView_t** view, VimState_t* vim_state);
void input_commit_to_history(Buffer_t* input_buffer, TextHistory_t* history);
bool input_history_iterate(Input_t* input, bool previous);
