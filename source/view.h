#pragma once

#include "ce.h"
#include "ce_config.h"
#include "vim.h"
#include "terminal.h"

void view_scroll_to_location(BufferView_t* buffer_view, const Point_t* location);
void view_center(BufferView_t* view);
void view_center_when_cursor_outside_portion(BufferView_t* view, float portion_start, float portion_end);
void view_follow_cursor(BufferView_t* current_view, LineNumberType_t line_number_type);
void view_follow_highlight(BufferView_t* current_view);
void view_split(BufferView_t* head_view, BufferView_t* current_view, bool horizontal, LineNumberType_t line_number_type);
void view_switch_to_point(bool input, BufferView_t* view_input, VimState_t* vim_state, TabView_t* tab,
                          TerminalNode_t* terminal_head, TerminalNode_t** terminal_current, Point_t point);
void view_switch_to_buffer_list(Buffer_t* buffer_list_buffer, BufferView_t* buffer_view, BufferView_t* view_head,
                                const BufferNode_t* buffer_head);
void view_override_with_buffer(BufferView_t* view, Buffer_t* new_buffer, Buffer_t** buffer_before_override);
void view_move_cursor_half_page_up(BufferView_t* view); // TODO: use direction up/down from ce.h
void view_move_cursor_half_page_down(BufferView_t* view);
