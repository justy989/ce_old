#pragma once

#include "ce_config.h"

TerminalNode_t* is_terminal_buffer(TerminalNode_t* terminal_head, Buffer_t* buffer);
bool terminal_start_in_view(BufferView_t* buffer_view, TerminalNode_t* node, ConfigState_t* config_state);
void terminal_resize_if_in_view(BufferView_t* view_head, TerminalNode_t* terminal_head);
bool terminal_in_view_run_command(TerminalNode_t* terminal_head, BufferView_t* view_head, const char* command);
