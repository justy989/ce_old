#include "ce.h"

typedef struct{
     bool insert;
     bool split;
     int last_key;
     BufferNode* current_buffer_node;
} ConfigState;

typedef struct{
     Point cursor;
     int64_t start_line;
} BufferState;

bool initializer(BufferNode* head, Point* terminal_dimensions, void** user_data)
{
     // NOTE: need to set these in this module
     g_message_buffer = head->buffer;
     g_terminal_dimensions = terminal_dimensions;

     // setup the config's state
     ConfigState* config_state = malloc(sizeof(*config_state));
     if(!config_state) return false;

     config_state->insert = false;
     config_state->split = false;
     config_state->last_key = 0;
     config_state->current_buffer_node = head;

     *user_data = config_state;

     // setup state for each buffer
     while(head){
          BufferState* buffer_state = malloc(sizeof(*buffer_state));
          if(!buffer_state){
               ce_message("failed to allocate buffer state.");
               return false;
          }
          buffer_state->cursor.x = 0;
          buffer_state->cursor.y = 0;
          buffer_state->start_line = 0;
          head->buffer->user_data = buffer_state;
          head = head->next;
     }

     return true;
}

bool destroyer(BufferNode* head, void* user_data)
{
     while(head){
          BufferState* buffer_state = head->buffer->user_data;
          free(buffer_state);
          head->buffer->user_data = NULL;
          head = head->next;
     }

     free(user_data);
     return true;
}

bool key_handler(int key, BufferNode* head, void* user_data)
{
     ConfigState* config_state = user_data;
     Buffer* buffer = config_state->current_buffer_node->buffer;
     BufferState* buffer_state = buffer->user_data;
     Point* cursor = &buffer_state->cursor;

     config_state->last_key = key;

     if(config_state->insert){
          // TODO: should be a switch
          if(key == 27){ // escape
               config_state->insert = false;
               if(buffer->lines[cursor->y]){
                    int64_t line_len = strlen(buffer->lines[cursor->y]);
                    if(cursor->x == line_len){
                         cursor->x--;
                    }
               }
          }else if(key == 127){ // backspace
               if(buffer->line_count){
                    if(cursor->x == 0){
                         if(cursor->y != 0){
                              // remove the line and join the next line with the previous
                         }
                    }else{
                         if(ce_remove_char(buffer, cursor)){
                              cursor->x--;
                         }
                    }
               }
          }else if(key == 10){ // add empty line
               if(!buffer->line_count){
                    ce_alloc_lines(buffer, 1);
               }
               if(ce_insert_newline(buffer, cursor->y + 1)){
                    cursor->y++;
                    cursor->x = 0;
               }
          }else{ // insert
               if(buffer->line_count == 0) ce_alloc_lines(buffer, 1);

               if(ce_insert_char(buffer, cursor, key)) cursor->x++;
          }
     }else{
          switch(key){
          default:
               break;
          case 'q':
               return false; // exit !
          case 'j':
          {
               Point delta = {0, 1};
               ce_move_cursor(buffer, cursor, &delta);
          } break;
          case 'k':
          {
               Point delta = {0, -1};
               ce_move_cursor(buffer, cursor, &delta);
          } break;
          case 'h':
          {
               Point delta = {-1, 0};
               ce_move_cursor(buffer, cursor, &delta);
          } break;
          case 'l':
          {
               Point delta = {1, 0};
               ce_move_cursor(buffer, cursor, &delta);
          } break;
          case 'i':
               config_state->insert = true;
               break;
          case 'a':
               if(buffer->lines[cursor->y]){
                    cursor->x++;
               }
               config_state->insert = true;
          case 'd':
               // delete line
               if(buffer->line_count){
                    if(ce_remove_line(buffer, cursor->y)){
                         if(cursor->y >= buffer->line_count){
                              cursor->y = buffer->line_count - 1;
                         }
                    }
               }
               break;
          case 's':
               ce_save_buffer(buffer, buffer->filename);
               break;
          case 'v':
               config_state->split = !config_state->split;
               break;
          case 'b':
               config_state->current_buffer_node = config_state->current_buffer_node->next;
               if(!config_state->current_buffer_node){
                    config_state->current_buffer_node = head;
               }
               break;
          case 21: // Ctrl + d
          {
               Point delta = {0, -g_terminal_dimensions->y / 2};
               ce_move_cursor(buffer, cursor, &delta);
          } break;
          case 4: // Ctrl + u
          {
               Point delta = {0, g_terminal_dimensions->y / 2};
               ce_move_cursor(buffer, cursor, &delta);
          } break;
          }
     }

     return true;
}

void view_drawer(const BufferNode* head, void* user_data)
{
     (void)(head);
     ConfigState* config_state = user_data;
     Buffer* buffer = config_state->current_buffer_node->buffer;
     BufferState* buffer_state = buffer->user_data;

     int64_t draw_height = g_terminal_dimensions->y - 1;

     ce_follow_cursor(&buffer_state->cursor, &buffer_state->start_line, draw_height);

     // print the range of lines we want to show
     Point buffer_top_left = {0, buffer_state->start_line};
     if(buffer->line_count){
          standend();
          Point term_top_left = {0, 0};
          Point term_bottom_right = {g_terminal_dimensions->x - 1, draw_height};
          if(!config_state->split){
               ce_draw_buffer(buffer, &term_top_left, &term_bottom_right, &buffer_top_left);
          }else{
               term_bottom_right.x = (g_terminal_dimensions->x / 2) - 1;
               ce_draw_buffer(buffer, &term_top_left, &term_bottom_right, &buffer_top_left);

               term_top_left.x = (g_terminal_dimensions->x / 2);
               term_bottom_right.x = term_top_left.x + ((g_terminal_dimensions->x / 2) - 1);
               ce_draw_buffer(buffer, &term_top_left, &term_bottom_right, &buffer_top_left);
          }
     }

     attron(A_REVERSE);
     mvprintw(g_terminal_dimensions->y - 1, 0, "%s %s %d lines, key %d", config_state->insert ? "INSERT" : "NORMAL",
              buffer->filename, buffer->line_count, config_state->last_key);
     attroff(A_REVERSE);

     // reset the cursor
     move(buffer_state->cursor.y - buffer_top_left.y, buffer_state->cursor.x);
}
