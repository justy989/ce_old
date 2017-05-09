#include "terminal_helper.h"
#include "view.h"
#include "misc.h"

extern pthread_mutex_t draw_lock;

typedef struct{
     ConfigState_t* config_state;
     TerminalNode_t* terminal_node;
}TerminalCheckUpdateData_t;

static void terminal_check_update_cleanup(void* data)
{
     // release locks we could be holding
     pthread_mutex_unlock(&draw_lock);
     free(data);
}

static void* terminal_check_update(void* data)
{
     pthread_cleanup_push(terminal_check_update_cleanup, data);

     TerminalCheckUpdateData_t* check_update_data = data;
     ConfigState_t* config_state = check_update_data->config_state;
     Terminal_t* terminal = &check_update_data->terminal_node->terminal;
     struct timeval current_time;
     uint64_t elapsed = 0;

     while(terminal->is_alive){
          sem_wait(terminal->updated);

          BufferView_t* terminal_view = ce_buffer_in_view(config_state->tab_current->view_head, terminal->buffer);
          if(terminal_view){
               terminal_view->cursor = terminal->cursor;
               view_follow_cursor(terminal_view, LNT_NONE);
          }else{
               continue;
          }

          if(config_state->vim_state.mode == VM_INSERT && !terminal->is_alive){
               vim_enter_normal_mode(&config_state->vim_state);
          }

          // make sure the other view drawer is done before drawing
          pthread_mutex_lock(&draw_lock);

          // wait for our interval limit, before drawing
          do{
               gettimeofday(&current_time, NULL);
               elapsed = (current_time.tv_sec - config_state->last_draw_time.tv_sec) * 1000000LL +
                         (current_time.tv_usec - config_state->last_draw_time.tv_usec);
          }while(elapsed < DRAW_USEC_LIMIT);

          view_drawer(config_state);
          pthread_mutex_unlock(&draw_lock);
     }

     pthread_cleanup_pop(data);
     return NULL;
}

TerminalNode_t* is_terminal_buffer(TerminalNode_t* terminal_head, Buffer_t* buffer)
{
     while(terminal_head){
          if(terminal_head->buffer == buffer) return terminal_head;

          terminal_head = terminal_head->next;
     }

     return NULL;
}

bool terminal_start_in_view(BufferView_t* buffer_view, TerminalNode_t* node, ConfigState_t* config_state)
{
     // TODO: create buffer_view_width() and buffer_view_height()
     int64_t width = buffer_view->bottom_right.x - buffer_view->top_left.x;
     int64_t height = buffer_view->bottom_right.y - buffer_view->top_left.y;

     if(!terminal_init(&node->terminal, width, height, node->buffer)){
          return false;
     }

     TerminalCheckUpdateData_t* check_update_data = calloc(1, sizeof(*check_update_data));
     check_update_data->config_state = config_state;
     check_update_data->terminal_node = node;

     int rc = pthread_create(&node->check_update_thread, NULL, terminal_check_update, check_update_data);
     if(rc != 0){
          ce_message("pthread_create() for terminal_check_update() failed");
          return false;
     }

     return true;
}

void terminal_resize_if_in_view(BufferView_t* view_head, TerminalNode_t* terminal_head)
{
     while(terminal_head){
          BufferView_t* term_view = ce_buffer_in_view(view_head, terminal_head->buffer);
          if(term_view){
               int64_t new_width = term_view->bottom_right.x - term_view->top_left.x;
               int64_t new_height = term_view->bottom_right.y - term_view->top_left.y;
               terminal_resize(&terminal_head->terminal, new_width, new_height);
          }

          terminal_head = terminal_head->next;
     }
}

bool terminal_in_view_run_command(TerminalNode_t* terminal_head, BufferView_t* view_head, const char* command)
{
     TerminalNode_t* term_itr = terminal_head;
     while(term_itr){
          if(ce_buffer_in_view(view_head, term_itr->buffer)){
               while(*command){
                    terminal_send_key(&term_itr->terminal, *command);
                    command++;
               }

               misc_move_jump_location_to_end_of_output(term_itr);
               terminal_send_key(&term_itr->terminal, KEY_ENTER);
               return true;
          }

          term_itr = term_itr->next;
     }

     return false;
}
