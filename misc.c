#include "misc.h"

const char* misc_buffer_flag_string(Buffer_t* buffer)
{
     switch(buffer->status){
     default:
          break;
     case BS_READONLY:
          return "[RO] ";
     case BS_NEW_FILE:
          return "[NEW] ";
     case BS_MODIFIED:
          return "*";
     }

     return "";
}

int64_t misc_count_digits(int64_t n)
{
     int64_t count = 0;
     while(n > 0){
          n /= 10;
          count++;
     }

     return count;
}

Point_t misc_get_cursor_on_user_terminal(const Point_t* cursor, const BufferView_t* buffer_view, LineNumberType_t line_number_type)
{
     Point_t p = {cursor->x - buffer_view->left_column + buffer_view->top_left.x,
                  cursor->y - buffer_view->top_row + buffer_view->top_left.y};
     p.x += ce_get_line_number_column_width(line_number_type, buffer_view->buffer->line_count, buffer_view->top_left.y,
                                            buffer_view->bottom_right.y);
     return p;
}

void misc_get_user_terminal_view_rect(TabView_t* tab_head, Point_t* top_left, Point_t* bottom_right)
{
     *top_left = (Point_t){0, 0};
     *bottom_right = (Point_t){g_terminal_dimensions->x - 1, g_terminal_dimensions->y - 1};

     // if we have multiple tabs
     if(tab_head->next) top_left->y++;
}

void misc_move_cursor_half_page_up(BufferView_t* view)
{
     int64_t view_height = view->bottom_right.y - view->top_left.y;
     Point_t delta = { 0, -view_height / 2 };
     ce_move_cursor(view->buffer, &view->cursor, delta);
}

void misc_move_cursor_half_page_down(BufferView_t* view)
{
     int64_t view_height = view->bottom_right.y - view->top_left.y;
     Point_t delta = { 0, view_height / 2 };
     ce_move_cursor(view->buffer, &view->cursor, delta);
}

void misc_move_jump_location_to_end_of_output(TerminalNode_t* terminal_node)
{
     terminal_node->last_jump_location = terminal_node->buffer->line_count - 1;
}

void misc_quit_and_prompt_if_unsaved(ConfigState_t* config_state, BufferNode_t* head)
{
     uint64_t unsaved_buffers = 0;
     BufferNode_t* itr = head;
     while(itr){
          if(itr->buffer->status == BS_MODIFIED) unsaved_buffers++;
          itr = itr->next;
     }

     if(unsaved_buffers){
          input_start(&config_state->input, &config_state->tab_current->view_current, &config_state->vim_state,
                      "Unsaved buffers... Quit anyway? (y/n)", 'q');
     }else{
          config_state->quit = true;
     }
}
