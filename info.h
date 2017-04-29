#pragma once

#include "ce_config.h"

void info_update_buffer_list_buffer(Buffer_t* buffer_list_buffer, const BufferNode_t* head);
void info_update_mark_list_buffer(ConfigState_t* config_state, const Buffer_t* buffer);
void info_update_yank_list_buffer(ConfigState_t* config_state);
void info_update_macro_list_buffer(ConfigState_t* config_state);
