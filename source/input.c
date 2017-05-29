#include "input.h"
#include "ce_config.h"

#include <pthread.h>

extern pthread_mutex_t view_input_save_lock;

TextHistory_t* input_get_history(Input_t* input)
{
     TextHistory_t* history = NULL;

     switch(input->type){
     default:
          break;
     case INPUT_SEARCH:
     case INPUT_REVERSE_SEARCH:
          history = &input->search_history;
          break;
     case INPUT_COMMAND:
          history = &input->command_history;
          break;
     }

     return history;
}

void input_start(Input_t* input, BufferView_t** view, VimState_t* vim_state, const char* message, int type)
{
     ce_clear_lines(&input->buffer);
     ce_alloc_lines(&input->buffer, 1);

     // TODO: create init view function, I'm sure this code is used elsewhere?
     input->view->cursor = (Point_t){0, 0};
     input->view->left_column = 0;
     input->view->top_row = 0;
     input->message = message;
     input->type = type;
     input->vim_mode_save = vim_state->mode;

     if(vim_state->mode == VM_VISUAL_LINE || vim_state->mode == VM_VISUAL_RANGE){
          input->visual_save = vim_state->visual_start;
     }

     // TODO: is this lock still needed?
     pthread_mutex_lock(&view_input_save_lock);
     input->view_save = (*view);
     input->cursor_save = (*view)->cursor;
     input->scroll_top_row_save = (*view)->top_row;
     input->scroll_left_column_save = (*view)->left_column;
     pthread_mutex_unlock(&view_input_save_lock);
     *view = input->view;

     vim_enter_insert_mode(vim_state, (*view)->buffer);

     // reset input history back to tail
     TextHistory_t* history = input_get_history(input);
     if(history) history->cur = history->tail;
}

void input_end(Input_t* input, VimState_t* vim_state)
{
     input->type = 0;

     switch(input->vim_mode_save){
     default:
     case VM_NORMAL:
          vim_enter_normal_mode(vim_state);
          break;
     case VM_VISUAL_RANGE:
          vim_enter_visual_range_mode(vim_state, input->visual_save);
          break;
     case VM_VISUAL_LINE:
          vim_enter_visual_line_mode(vim_state, input->visual_save);
          break;
     }
}

void input_cancel(Input_t* input, BufferView_t** view, VimState_t* vim_state)
{
     pthread_mutex_lock(&view_input_save_lock);
     (*view) = input->view_save;
     (*view)->cursor = input->cursor_save;
     (*view)->top_row = input->scroll_top_row_save;
     (*view)->left_column = input->scroll_left_column_save;
     pthread_mutex_unlock(&view_input_save_lock);

     if(input->type == INPUT_LOAD_FILE || input->type == INPUT_COMMAND){
          // free the search path so we can re-use it
          free(input->load_file_search_path);
          input->load_file_search_path = NULL;
     }

     input_end(input, vim_state);
}

void input_commit_to_history(Buffer_t* input_buffer, TextHistory_t* history)
{
     if(!input_buffer->line_count) return;

     char* saved = ce_dupe_buffer(input_buffer);

     if(!history->tail->prev || (history->tail->prev && strcmp(saved, history->tail->prev->entry) != 0)){
          history->tail->entry = saved;
          text_history_commit_current(history);
     }
}

bool input_history_iterate(Input_t* input, bool previous)
{
     BufferState_t* buffer_state = input->view->buffer->user_data;
     TextHistory_t* history = input_get_history(input);
     if(!history) return false;

     bool success = false;

     if(previous){
          success = text_history_prev(history);
     }else{
          success = text_history_next(history);
     }

     if(success){
          ce_clear_lines(input->view->buffer);
          if(history->cur->entry) ce_append_string(input->view->buffer, 0, history->cur->entry);
          input->view->cursor = (Point_t){0, 0};
          ce_move_cursor_to_end_of_file(input->view->buffer, &input->view->cursor);

          BufferCommitNode_t* node = buffer_state->commit_tail;
          while(node->prev) node = node->prev;
          ce_commits_free(node);

          buffer_state->commit_tail = calloc(1, sizeof(*buffer_state->commit_tail));
          if(!buffer_state->commit_tail){
               ce_message("failed to allocate commit history for buffer");
               return false;
          }
     }

     return success;
}
