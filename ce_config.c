#include "ce.h"

typedef struct BackspaceNode{
     char c;
     struct BackspaceNode* next;
} BackspaceNode;

BackspaceNode* backspace_append(BackspaceNode** tail, BackspaceNode** head, char c)
{
     BackspaceNode* new_node = malloc(sizeof(*new_node));
     if(!new_node){
          ce_message("%s() failed to malloc node", __FUNCTION__);
          return NULL;
     }

     new_node->c = c;
     new_node->next = NULL;

     if(*tail) (*tail)->next = new_node;

     (*tail) = new_node;

     if(!*head) *head = *tail;

     return new_node;
}

// string is allocated and returned, it is the user's responsibility to free it
char* backspace_get_string(BackspaceNode* head)
{
     int64_t len = 0;
     BackspaceNode* itr = head;
     while(itr){
          len++;
          itr = itr->next;
     }

     char* str = malloc(len + 1);
     if(!str){
          ce_message("%s() failed to alloc string");
          return NULL;
     }

     int64_t s = 0;
     itr = head;
     while(itr){
          str[s] = itr->c;
          s++;
          itr = itr->next;
     }

     // reverse_string
     {
          char* f = str;
          char* e = str + (len - 1);
          while(f < e){
               char t = *f;
               *f = *e;
               *e = t;
               f++;
               e--;
          }
     }

     str[len] = 0;
     return str;
}

void backspace_free(BackspaceNode** head, BackspaceNode** tail)
{
     while(*head){
          BackspaceNode* tmp = *head;
          *head = (*head)->next;
          free(tmp);
     }

     *tail = NULL;
}

typedef struct{
     bool insert;
     int last_key;
     BufferNode* current_buffer_node;
     Point start_insert;
     Point original_start_insert;
     BackspaceNode* backspace_head;
     BackspaceNode* backspace_tail;
     BufferView* buffer_view;
} ConfigState;

typedef struct{
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
     config_state->last_key = 0;
     config_state->current_buffer_node = head ? head->next : head;
     config_state->backspace_head = NULL;
     config_state->backspace_tail = NULL;
     config_state->buffer_view = malloc(sizeof(*config_state->buffer_view));
     if(!config_state->buffer_view){
          ce_message("failed to allocate buffer view.");
          return false;
     }

     config_state->buffer_view->buffer_node = head->next;
     config_state->buffer_view->horizontal_split = NULL;
     config_state->buffer_view->vertical_split = NULL;

     *user_data = config_state;

     // setup state for each buffer
     while(head){
          BufferState* buffer_state = malloc(sizeof(*buffer_state));
          if(!buffer_state){
               ce_message("failed to allocate buffer state.");
               return false;
          }
          buffer_state->changes_tail = malloc(sizeof(*buffer_state->changes_tail));
          buffer_state->changes_tail->change.type = BCT_NONE;
          buffer_state->changes_tail->next = NULL;
          buffer_state->changes_tail->prev = NULL;
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

     ConfigState* config_state = user_data;
     if(config_state->buffer_view){
          ce_free_view(&config_state->buffer_view);
     }
     free(user_data);
     return true;
}

bool key_handler(int key, BufferNode* head, void* user_data)
{
     ConfigState* config_state = user_data;
     Buffer* buffer = config_state->current_buffer_node->buffer;
     BufferState* buffer_state = buffer->user_data;
     Point* cursor = &buffer->cursor;

     config_state->last_key = key;

     if(config_state->insert){
          // TODO: should be a switch
          if(key == 27){ // escape
               config_state->insert = false;
               // TODO: handle newlines for saving state
               if(config_state->start_insert.x == config_state->original_start_insert.x &&
                  config_state->start_insert.y == config_state->original_start_insert.y){
                    // TODO: assert cursor is after start_insert
                    // exclusively inserts
                    BufferChange change;
                    change.type = BCT_INSERT_STRING;
                    change.start = config_state->start_insert;
                    change.cursor = config_state->start_insert;
                    change.str = ce_dupe_string(buffer, &config_state->start_insert, cursor);
                    ce_buffer_change(&buffer_state->changes_tail, &change);
               }else if(config_state->start_insert.x < config_state->original_start_insert.x ||
                        config_state->start_insert.y < config_state->original_start_insert.y){
                    if(cursor->x == config_state->start_insert.x &&
                       cursor->y == config_state->start_insert.y){
                         // exclusively backspaces!
                         BufferChange change;
                         change.type = BCT_REMOVE_STRING;
                         change.start = *cursor;
                         change.cursor = config_state->start_insert;
                         change.str = backspace_get_string(config_state->backspace_head);
                         ce_buffer_change(&buffer_state->changes_tail, &change);
                         backspace_free(&config_state->backspace_head, &config_state->backspace_tail);
                    }else{
                         // mixture of inserts and backspaces
                         BufferChange change;
                         change.type = BCT_CHANGE_STRING;
                         change.start = config_state->start_insert;
                         change.cursor = *cursor;
                         // TODO: get multi line to insert
                         change.str = ce_dupe_string(buffer, &config_state->start_insert, cursor);
                         change.changed_str = backspace_get_string(config_state->backspace_head);
                         ce_buffer_change(&buffer_state->changes_tail, &change);
                         backspace_free(&config_state->backspace_head, &config_state->backspace_tail);
                    }
               }

               if(buffer->lines[cursor->y]){
                    int64_t line_len = strlen(buffer->lines[cursor->y]);
                    if(cursor->x == line_len){
                         cursor->x--;
                    }
               }
          }else if(key == 127){ // backspace
               if(buffer->line_count){
                    if(cursor->x == 0 && cursor->y != 0){
                         int64_t line_len = 0;
                         if(buffer->lines[cursor->y - 1]) line_len = strlen(buffer->lines[cursor->y - 1]);
                         ce_append_string(buffer, cursor->y - 1, buffer->lines[cursor->y]);
                         ce_remove_line(buffer, cursor->y);
                         Point delta = {0, -1};
                         ce_move_cursor(buffer, cursor, &delta);
                         cursor->x = line_len;
                         backspace_append(&config_state->backspace_tail, &config_state->backspace_head, '\n');
                         config_state->start_insert = *cursor;
                    }else{
                         Point previous = *cursor;
                         previous.x--;
                         char c = 0;
                         if(ce_get_char(buffer, &previous, &c)){
                              if(ce_remove_char(buffer, &previous)){
                                   if(cursor->x <= config_state->start_insert.x){
                                        backspace_append(&config_state->backspace_tail, &config_state->backspace_head, c);
                                        config_state->start_insert.x--;
                                   }
                                   Point delta = {-1, 0};
                                   ce_move_cursor(buffer, cursor, &delta);
                              }
                         }
                    }
               }
          }else if(key == 126){ // delete ?
               ce_remove_char(buffer, cursor);
          }else if(key == 10){ // add empty line
               if(!buffer->line_count){
                    ce_alloc_lines(buffer, 1);
               }
               if(buffer->lines[cursor->y] && cursor->x < (int64_t)(strlen(buffer->lines[cursor->y]))){
                    char* start = buffer->lines[cursor->y] + cursor->x;
                    int64_t to_end_of_line_len = strlen(start);
                    if(ce_insert_line(buffer, cursor->y + 1, start)){
                         ce_remove_string(buffer, cursor, to_end_of_line_len);
                         cursor->y++;
                         cursor->x = 0;
                    }
               }
          }else{ // insert
               // TODO: handle newlines when inserting individual chars
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
               config_state->start_insert = *cursor;
               config_state->original_start_insert = *cursor;
               break;
          case 'a':
          {
               Point delta = {1, 0};
               ce_move_cursor(buffer, cursor, &delta);
               config_state->insert = true;
               config_state->start_insert = *cursor;
               config_state->original_start_insert = *cursor;
          } break;
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
               ce_split_view(config_state->buffer_view, config_state->current_buffer_node, true);
               break;
          case 'c':
               ce_split_view(config_state->buffer_view, config_state->current_buffer_node, false);
               break;
          case 'b':
               config_state->current_buffer_node = config_state->current_buffer_node->next;
               if(!config_state->current_buffer_node){
                    config_state->current_buffer_node = head;
               }
               break;
          case 'u':
               if(buffer_state->changes_tail && buffer_state->changes_tail->change.type != BCT_NONE){
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
                    change.type = BCT_REMOVE_CHAR;
                    change.start = *cursor;
                    change.cursor = *cursor;
                    change.c = c;
                    ce_buffer_change(&buffer_state->changes_tail, &change);
               }
          }
          break;
          case 18:
          if(buffer_state->changes_tail && buffer_state->changes_tail->next){
               Point new_cursor = buffer_state->changes_tail->next->change.start;
               if(ce_buffer_redo(buffer, &buffer_state->changes_tail)){
                    *cursor = new_cursor;
               }
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
     BufferView* buffer_view = config_state->buffer_view;
     Point* cursor = &buffer->cursor;

     ce_follow_cursor(cursor, &buffer->top_row, &buffer->left_collumn,
                      buffer_view->bottom_right.y, buffer_view->bottom_right.x);

     // print the range of lines we want to show
     if(buffer->line_count){
          standend();
          ce_draw_view(config_state->buffer_view);
     }

     attron(A_REVERSE);
     mvprintw(g_terminal_dimensions->y - 1, 0, "%s %s %d lines, key %d, cursor %ld, %ld, top left: %ld, %ld",
              config_state->insert ? "INSERT" : "NORMAL", buffer->filename, buffer->line_count, config_state->last_key,
              cursor->x, cursor->y, buffer->top_row, buffer->left_collumn);
     attroff(A_REVERSE);

     // reset the cursor
     move(cursor->y - buffer->top_row + config_state->buffer_view->top_left.y,
          cursor->x - buffer->left_collumn + config_state->buffer_view->top_left.x);
}
