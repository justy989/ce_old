#pragma once

#include "ce.h"
#include "ce_config.h"
#include "terminal.h"

bool dest_open_file(BufferNode_t* head, BufferView_t* view, const char* filename, int line, int column);
bool dest_goto_file_location_in_buffer(BufferNode_t* head, Buffer_t* buffer, int64_t line, BufferView_t* head_view,
                                       BufferView_t* view, int64_t* last_jump, char* terminal_current_directory);
void dest_jump_to_next_in_terminal(BufferNode_t* head, TerminalNode_t* terminal_head, TerminalNode_t** terminal_current,
                                   BufferView_t* view_head, BufferView_t* view_current, bool forwards);
void dest_cscope_goto_definition(BufferView_t* view_current, BufferNode_t* head, const char* search_word);
bool dest_open_file(BufferNode_t* head, BufferView_t* view, const char* filename, int line, int column);
