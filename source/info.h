#pragma once

#include "vim.h"

void info_update_buffer_list_buffer(Buffer_t* buffer_list_buffer, const BufferNode_t* head);
void info_update_mark_list_buffer(Buffer_t* mark_list_buffer, const Buffer_t* buffer);
void info_update_yank_list_buffer(Buffer_t* yank_list_buffer, const VimYankNode_t* yank_head);
void info_update_macro_list_buffer(Buffer_t* macro_list_buffer, const VimState_t* vim_state);
