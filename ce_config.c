#include "ce.h"
#include <assert.h>

typedef struct{
     bool insert;
     bool split;
     int last_key;
     char command_key;
     BufferNode* current_buffer_node;
     Point start_insert;
} ConfigState;

typedef struct{
     Point cursor;
     int64_t start_line;
     int64_t left_collumn;
     BufferChangeNode* changes_tail;
} BufferState;

bool initializer(BufferNode* head, Point* terminal_dimensions, int argc, char** argv, void** user_data)
{
     // NOTE: need to set these in this module
     g_message_buffer = head->buffer;
     g_terminal_dimensions = terminal_dimensions;

     for(int i = 0; i < argc; ++i){
          Buffer* buffer = malloc(sizeof(*buffer));
          if(!buffer){
               ce_message("failed to allocate buffer");
               return false;
          }

          if(!ce_load_file(buffer, argv[i])){
               free(buffer);
               continue;
          }

          if(!ce_append_buffer_to_list(head, buffer)){
               free(buffer);
          }
     }

     // setup the config's state
     ConfigState* config_state = malloc(sizeof(*config_state));
     if(!config_state) return false;

     config_state->insert = false;
     config_state->split = false;
     config_state->last_key = 0;
     config_state->command_key = '\0';
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
          buffer_state->changes_tail = NULL;
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

#define COMMAND if(config_state->command_key == '\0'){ config_state->command_key = key; } else

bool key_handler(int key, BufferNode* head, void* user_data)
{
     ConfigState* config_state = user_data;
     Buffer* buffer = config_state->current_buffer_node->buffer;
     BufferState* buffer_state = buffer->user_data;
     Point* cursor = &buffer_state->cursor;

     config_state->last_key = key;

     // command keys are followed by a movement key which clears the command key
     if(config_state->command_key != '\0' && key == 27){
          // escape cancels a movement
          config_state->command_key = '\0';
          return true;
     }

     if(config_state->insert){
          assert(config_state->command_key == '\0');
          // TODO: should be a switch
          if(key == 27){ // escape
               config_state->insert = false;
               // TODO: handle newlines for saving state
               if(config_state->start_insert.x < cursor->x){
                    BufferChange change;
                    change.start = config_state->start_insert;
                    change.start.x++;
                    change.cursor = *cursor;
                    change.length = cursor->x - config_state->start_insert.x;
                    change.insertion = true;
                    ce_buffer_change(&buffer_state->changes_tail, &change);
               }
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
               if(ce_insert_char(buffer, cursor, key)) cursor->x++;
          }
     }else{
          switch((config_state->command_key != '\0') ? config_state->command_key : key){
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
               config_state->start_insert = *cursor;
               break;
          case '$':
               cursor->x += ce_find_end_of_line(buffer, cursor);
               break;
          case 'A':
          {
               cursor->x += ce_find_end_of_line(buffer, cursor) + 1;
               config_state->insert = true;
               config_state->start_insert = *cursor;
          } break;
          case 'a':
               if(buffer->lines[cursor->y]){
                    cursor->x++;
               }
               config_state->insert = true;
               config_state->start_insert = *cursor;
               break;
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
               ce_remove_char(buffer, cursor);
               config_state->insert = true;
               config_state->start_insert = *cursor;
               break;
          case '':
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
          case 'u':
               if(buffer_state->changes_tail){
                    Point new_cursor = buffer_state->changes_tail->change.cursor;
                    if(ce_buffer_undo(buffer, &buffer_state->changes_tail)){
                         *cursor = new_cursor;
                    }
               }
               break;
          case 'x':
          {
               char c;
               if(ce_get_char(buffer, cursor, &c) && ce_remove_char(buffer, cursor)){
                    BufferChange change;
                    change.insertion = false;
                    change.start = *cursor;
                    change.cursor = *cursor;
                    change.c = c;
                    ce_buffer_change(&buffer_state->changes_tail, &change);
               }
          }
          break;
          case 'f':
          {
               COMMAND{
                    int64_t x_delta = ce_find_char_forward_in_line(buffer, cursor, key);
                    if(x_delta == -1) break;
                    Point delta = {x_delta, 0};
                    ce_move_cursor(buffer, cursor, &delta);
                    config_state->command_key = '\0';
               }
          } break;
          case 't':
          {
               COMMAND{
                    int64_t x_delta = ce_find_char_forward_in_line(buffer, cursor, key);
                    if(x_delta-- <= 0) break;
                    Point delta = {x_delta, 0};
                    ce_move_cursor(buffer, cursor, &delta);
                    config_state->command_key = '\0';
               }
          } break;
          case 'F':
          {
               COMMAND{
                    int64_t x_delta = ce_find_char_backward_in_line(buffer, cursor, key);
                    if(x_delta == -1) break;
                    Point delta = {-x_delta, 0};
                    ce_move_cursor(buffer, cursor, &delta);
                    config_state->command_key = '\0';
               }
          } break;
          case 'T':
          {
               COMMAND{
                    int64_t x_delta = ce_find_char_backward_in_line(buffer, cursor, key);
                    if(x_delta-- <= -1) break;
                    Point delta = {-x_delta, 0};
                    ce_move_cursor(buffer, cursor, &delta);
                    // TODO: devise a better way to clear command_key following a movement
                    config_state->command_key = '\0';
               }
          } break;
          case 'r':
          {
               COMMAND{
                    ce_set_char(buffer, cursor, key);
                    // TODO: devise a better way to clear command_key following a movement
                    config_state->command_key = '\0';
               }
          } break;
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

     int64_t bottom = g_terminal_dimensions->y - 1;
     int64_t right = g_terminal_dimensions->x - 1;

     ce_follow_cursor(&buffer_state->cursor, &buffer_state->start_line, &buffer_state->left_collumn,
                      bottom, right);

     // print the range of lines we want to show
     Point buffer_top_left = {buffer_state->left_collumn, buffer_state->start_line};
     if(buffer->line_count){
          standend();
          Point term_top_left = {0, 0};
          Point term_bottom_right = {right, bottom};
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
     move(buffer_state->cursor.y - buffer_top_left.y, buffer_state->cursor.x - buffer_top_left.x);
}
