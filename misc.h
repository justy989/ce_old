#pragma once

#include "ce.h"
#include "ce_config.h"

const char* misc_buffer_flag_string(Buffer_t* buffer);
int64_t misc_count_digits(int64_t n);
Point_t misc_get_cursor_on_user_terminal(const Point_t* cursor, const BufferView_t* buffer_view, LineNumberType_t line_number_type);
void misc_get_user_terminal_view_rect(TabView_t* tab_head, Point_t* top_left, Point_t* bottom_right);
void misc_move_cursor_half_page_up(BufferView_t* view); // TODO: use direction up/down from ce.h
void misc_move_cursor_half_page_down(BufferView_t* view);
void misc_move_jump_location_to_end_of_output(TerminalNode_t* terminal_node);
void misc_quit_and_prompt_if_unsaved(ConfigState_t* config_state, BufferNode_t* head);
