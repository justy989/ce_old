#include "view.h"
#include "terminal_helper.h"

// location is {left_column, top_line} for the view
void view_scroll_to_location(BufferView_t* buffer_view, const Point_t* location)
{
     // TODO: should we be able to scroll the view above our first line?
     buffer_view->left_column = (location->x >= 0) ? location->x : 0;
     buffer_view->top_row = (location->y >= 0) ? location->y : 0;
}

void view_center(BufferView_t* view)
{
     int64_t view_height = view->bottom_right.y - view->top_left.y;
     Point_t location = (Point_t) {0, view->cursor.y - (view_height / 2)};
     view_scroll_to_location(view, &location);
}

void view_center_when_cursor_outside_portion(BufferView_t* view, float portion_start, float portion_end)
{
     int64_t view_height = view->bottom_right.y - view->top_left.y;
     Point_t location = (Point_t) {0, view->cursor.y - (view_height / 2)};

     int64_t current_scroll_y = view->cursor.y - view->top_row;
     float y_portion = ((float)(current_scroll_y) / (float)(view_height));

     if(y_portion < portion_start || y_portion > portion_end){
          view_scroll_to_location(view, &location);
     }
}

void view_follow_cursor(BufferView_t* current_view, LineNumberType_t line_number_type)
{
     ce_follow_cursor(current_view->cursor, &current_view->left_column, &current_view->top_row,
                      current_view->bottom_right.x - current_view->top_left.x,
                      current_view->bottom_right.y - current_view->top_left.y,
                      current_view->bottom_right.x == (g_terminal_dimensions->x - 1),
                      current_view->bottom_right.y == (g_terminal_dimensions->y - 2),
                      line_number_type, current_view->buffer->line_count);
}

void view_follow_highlight(BufferView_t* current_view)
{
     ce_follow_cursor(current_view->buffer->highlight_start, &current_view->left_column, &current_view->top_row,
                      current_view->bottom_right.x - current_view->top_left.x,
                      current_view->bottom_right.y - current_view->top_left.y,
                      current_view->bottom_right.x == (g_terminal_dimensions->x - 1),
                      current_view->bottom_right.y == (g_terminal_dimensions->y - 2),
                      LNT_NONE, current_view->buffer->line_count);
}

void view_split(BufferView_t* head_view, BufferView_t* current_view, bool horizontal, LineNumberType_t line_number_type)
{
     BufferView_t* new_view = ce_split_view(current_view, current_view->buffer, horizontal);
     if(new_view){
          Point_t top_left = {0, 0};
          Point_t bottom_right = {g_terminal_dimensions->x - 1, g_terminal_dimensions->y - 1};
          ce_calc_views(head_view, top_left, bottom_right);
          view_follow_cursor(current_view, line_number_type);
          new_view->cursor = current_view->cursor;
          new_view->top_row = current_view->top_row;
          new_view->left_column = current_view->left_column;

          BufferViewState_t* buffer_view_state = calloc(1, sizeof(*buffer_view_state));
          if(!buffer_view_state){
               ce_message("failed to allocate buffer view state");
          }else{
               new_view->user_data = buffer_view_state;
          }
     }
}

void view_switch_to_point(bool input, BufferView_t* view_input, VimState_t* vim_state, TabView_t* tab,
                          TerminalNode_t* terminal_head, TerminalNode_t** terminal_current, Point_t point)
{
     BufferView_t* next_view = NULL;

     if(point.x < 0) point.x = g_terminal_dimensions->x - 1;
     if(point.y < 0) point.y = g_terminal_dimensions->y - 1;
     if(point.x >= g_terminal_dimensions->x) point.x = 0;
     if(point.y >= g_terminal_dimensions->y) point.y = 0;

     if(input) next_view = ce_find_view_at_point(view_input, point);
     vim_stop_recording_macro(vim_state);
     if(!next_view) next_view = ce_find_view_at_point(tab->view_head, point);

     if(next_view){
          // save view and cursor
          tab->view_previous = tab->view_current;
          tab->view_current->buffer->cursor = tab->view_current->cursor;
          tab->view_current = next_view;
          vim_enter_normal_mode(vim_state);

          TerminalNode_t* terminal_node = is_terminal_buffer(terminal_head, next_view->buffer);
          if(terminal_node) *terminal_current = terminal_node;
     }
}

void view_switch_to_buffer_list(Buffer_t* buffer_list_buffer, BufferView_t* buffer_view, BufferView_t* view_head, const BufferNode_t* buffer_head)
{
     Buffer_t* buffer = buffer_view->buffer;
     Point_t* cursor = &buffer_view->cursor;

     JumpArray_t* jump_array = &((BufferViewState_t*)(buffer_view->user_data))->jump_array;
     jump_insert(jump_array, buffer->filename, *cursor);

     buffer->cursor = buffer_view->cursor;

     // try to find a better place to put the cursor to start
     const BufferNode_t* itr = buffer_head;
     int64_t buffer_index = 1;
     bool found_good_buffer_index = false;
     while(itr){
          if(itr->buffer->status != BS_READONLY && !ce_buffer_in_view(view_head, itr->buffer)){
               found_good_buffer_index = true;
               break;
          }
          itr = itr->next;
          buffer_index++;
     }

     buffer_view->buffer->cursor = *cursor;
     buffer_view->buffer = buffer_list_buffer;
     buffer_view->top_row = 0;
     buffer_view->cursor = (Point_t){0, found_good_buffer_index ? buffer_index : 1};
}

void view_override_with_buffer(BufferView_t* view, Buffer_t* new_buffer, Buffer_t** buffer_before_override)
{
     *buffer_before_override = view->buffer;
     (*buffer_before_override)->cursor = view->cursor;

     view->buffer = new_buffer;
     view->cursor = (Point_t){0, 0};
     view->top_row = 0;
}

void view_move_cursor_half_page_up(BufferView_t* view)
{
     int64_t view_height = view->bottom_right.y - view->top_left.y;
     Point_t delta = { 0, -view_height / 2 };
     ce_move_cursor(view->buffer, &view->cursor, delta);
}

void view_move_cursor_half_page_down(BufferView_t* view)
{
     int64_t view_height = view->bottom_right.y - view->top_left.y;
     Point_t delta = { 0, view_height / 2 };
     ce_move_cursor(view->buffer, &view->cursor, delta);
}

