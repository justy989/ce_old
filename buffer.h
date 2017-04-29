#pragma once

#include "ce.h"
#include "ce_config.h"
#include "terminal.h"

bool buffer_initialize(Buffer_t* buffer);
BufferNode_t* buffer_create_empty(BufferNode_t** head, const char* name);
BufferNode_t* buffer_create_from_file(BufferNode_t** head, const char* filename);
void buffer_state_free(BufferState_t* buffer_state);
bool buffer_delete_at_index(BufferNode_t** head, TabView_t* tab_head, int64_t buffer_index, TerminalNode_t** terminal_head,
                            TerminalNode_t** terminal_current);
