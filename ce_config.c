#include "ce.h"
#include "assert.h"

#include <assert.h>
#include <ctype.h>
#include <unistd.h>

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
     char command_key; // TODO: make a command string for multi-character commands
     struct {
          // state for fF and tT
          char command_key;
          char find_char;
     } find_command;
     Point start_insert;
     Point original_start_insert;
     BufferView* view_head;
     BufferView* view_current;
} ConfigState;

typedef struct{
     BufferCommitNode* commit_tail;
     BackspaceNode* backspace_head;
     BackspaceNode* backspace_tail;
} BufferState;

bool initialize_buffer(Buffer* buffer){
     BufferState* buffer_state = calloc(1, sizeof(*buffer_state));
     if(!buffer_state){
          ce_message("failed to allocate buffer state.");
          return false;
     }

     BufferCommitNode* tail = calloc(1, sizeof(*tail));
     if(!tail){
          ce_message("failed to allocate commit history for buffer");
          free(buffer_state);
          return false;
     }

     buffer_state->commit_tail = tail;

     buffer->user_data = buffer_state;
     return true;
}

// NOTE: need to free the allocated str
char* str_from_file_stream(FILE* file){
     fseek(file, 0, SEEK_END);
     size_t file_size = ftell(file);
     fseek(file, 0, SEEK_SET);

     char* str = malloc(file_size + 1);
     fread(str, file_size, 1, file);
     str[file_size] = 0;
     return str;
}

BufferNode* new_buffer_from_string(BufferNode* head, const char* name, const char* str){
     Buffer* buffer = calloc(1, sizeof(*buffer));
     if(!buffer){
          ce_message("failed to allocate buffer");
          return NULL;
     }

     buffer->name = strdup(name);
     if(!buffer->name){
          ce_message("failed to allocate buffer name");
          free(buffer);
          return NULL;
     }

     if(!initialize_buffer(buffer)){
          free(buffer->name);
          free(buffer);
          return NULL;
     }

     ce_load_string(buffer, str);

     BufferNode* new_buffer_node = ce_append_buffer_to_list(head, buffer);
     if(!new_buffer_node){
          free(buffer->name);
          free(buffer->user_data);
          free(buffer);
          return NULL;
     }

     return new_buffer_node;
}

BufferNode* new_buffer_from_file(BufferNode* head, const char* filename)
{
     Buffer* buffer = calloc(1, sizeof(*buffer));
     if(!buffer){
          ce_message("failed to allocate buffer");
          return NULL;
     }

     if(!ce_load_file(buffer, filename)){
          free(buffer);
          return NULL;
     }

     if(!initialize_buffer(buffer)){
          free(buffer->filename);
          free(buffer);
     }

     BufferNode* new_buffer_node = ce_append_buffer_to_list(head, buffer);
     if(!new_buffer_node){
          free(buffer->filename);
          free(buffer->user_data);
          free(buffer);
          return NULL;
     }

     return new_buffer_node;
}

int64_t strlen_ignore_newlines(const char* str)
{
     int64_t count = 0;

     while(*str){
          if(*str != NEWLINE) count++;
          str++;
     }

     return count;
}

bool initializer(BufferNode* head, Point* terminal_dimensions, int argc, char** argv, void** user_data)
{
     // NOTE: need to set these in this module
     g_message_buffer = head->buffer;
     g_terminal_dimensions = terminal_dimensions;

     // setup the config's state
     ConfigState* config_state = calloc(1, sizeof(*config_state));
     if(!config_state){
          ce_message("failed to allocate config state");
          return false;
     }

     config_state->view_head = calloc(1, sizeof(*config_state->view_head));
     if(!config_state->view_head){
          ce_message("failed to allocate buffer view");
          return false;
     }

     *user_data = config_state;

     BufferNode* itr = head;

     // setup state for each buffer
     while(itr){
          initialize_buffer(itr->buffer);
          itr = itr->next;
     }

     for(int i = 0; i < argc; ++i){
          if(!new_buffer_from_file(head, argv[i])) continue;
     }

     //config_state->view_head->bottom_right = *g_terminal_dimensions; // NOTE: do we need this?
     config_state->view_head->buffer_node = (head && head->next) ? head->next : head;
     config_state->view_current = config_state->view_head;

     return true;
}

bool destroyer(BufferNode* head, void* user_data)
{
     while(head){
          BufferState* buffer_state = head->buffer->user_data;
          ce_commits_free(&buffer_state->commit_tail);
          free(buffer_state);
          head->buffer->user_data = NULL;
          head = head->next;
     }

     ConfigState* config_state = user_data;
     if(config_state->view_head){
          ce_free_views(&config_state->view_head);
          config_state->view_current = NULL;
     }
     free(config_state);
     return true;
}

void find_command(int command_key, int find_char, Buffer* buffer, Point* cursor)
{
     switch(command_key){
     case 'f':
     {
          int64_t x_delta = ce_find_delta_to_char_forward_in_line(buffer, cursor, find_char);
          if(x_delta == -1) break;
          Point delta = {x_delta, 0};
          ce_move_cursor(buffer, cursor, &delta);
     } break;
     case 't':
     {
          Point search_point = {cursor->x + 1, cursor->y};
          int64_t x_delta = ce_find_delta_to_char_forward_in_line(buffer, &search_point, find_char);
          if(x_delta <= 0) break;
          Point delta = {x_delta, 0};
          ce_move_cursor(buffer, cursor, &delta);
     } break;
     case 'F':
     {
          int64_t x_delta = ce_find_delta_to_char_backward_in_line(buffer, cursor, find_char);
          if(x_delta == -1) break;
          Point delta = {-x_delta, 0};
          ce_move_cursor(buffer, cursor, &delta);
     } break;
     case 'T':
     {
          Point search_point = {cursor->x - 1, cursor->y};
          int64_t x_delta = ce_find_delta_to_char_backward_in_line(buffer, &search_point, find_char);
          if(x_delta <= 0) break;
          Point delta = {-x_delta, 0};
          ce_move_cursor(buffer, cursor, &delta);
     } break;
     default:
          assert(0);
          break;
     }
}

void enter_insert_mode(ConfigState* config_state, Point* cursor)
{
     config_state->insert = true;
     config_state->start_insert = *cursor;
     config_state->original_start_insert = *cursor;
}

bool should_handle_command(ConfigState* config_state, char key)
{
     if(config_state->command_key == '\0'){
          config_state->command_key = key;
          return false;
     }
     return true;
}

bool key_handler(int key, BufferNode* head, void* user_data)
{
     ConfigState* config_state = user_data;
     Buffer* buffer = config_state->view_current->buffer_node->buffer;
     BufferState* buffer_state = buffer->user_data;
     Point* cursor = &config_state->view_current->cursor;
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
          switch(key){
          case 27: // escape
          {
               config_state->insert = false;
               // TODO: handle newlines for saving state
               if(config_state->start_insert.x == config_state->original_start_insert.x &&
                  config_state->start_insert.y == config_state->original_start_insert.y){
                    // TODO: assert cursor is after start_insert
                    // exclusively inserts
                    ce_commit_insert_string(&buffer_state->commit_tail, &config_state->start_insert, &config_state->original_start_insert,
                                            cursor, ce_dupe_string(buffer, &config_state->start_insert, cursor));
               }else if(config_state->start_insert.x < config_state->original_start_insert.x ||
                        config_state->start_insert.y < config_state->original_start_insert.y){
                    if(cursor->x == config_state->start_insert.x &&
                       cursor->y == config_state->start_insert.y){
                         // exclusively backspaces!
                         ce_commit_remove_string(&buffer_state->commit_tail, cursor, &config_state->original_start_insert,
                                                 cursor, backspace_get_string(buffer_state->backspace_head));
                         backspace_free(&buffer_state->backspace_head, &buffer_state->backspace_tail);
                    }else{
                         // mixture of inserts and backspaces
                         ce_commit_change_string(&buffer_state->commit_tail, &config_state->start_insert, &config_state->original_start_insert,
                                                 cursor, ce_dupe_string(buffer, &config_state->start_insert, cursor),
                                                 backspace_get_string(buffer_state->backspace_head));
                         backspace_free(&buffer_state->backspace_head, &buffer_state->backspace_tail);
                    }
               }

               if(buffer->lines[cursor->y]){
                    int64_t line_len = strlen(buffer->lines[cursor->y]);
                    if(cursor->x == line_len){
                         cursor->x--;
                    }
               }
          } break;
          case 127: // backspace
               if(buffer->line_count){
                    if(cursor->x == 0 && cursor->y != 0){
                         int64_t line_len = 0;
                         if(buffer->lines[cursor->y - 1]){
                              line_len = strlen(buffer->lines[cursor->y - 1]);
                              ce_append_string(buffer, cursor->y - 1, buffer->lines[cursor->y]);
                         }else{
                              buffer->lines[cursor->y - 1] = strdup(buffer->lines[cursor->y]);
                         }
                         ce_remove_line(buffer, cursor->y);
                         Point delta = {0, -1};
                         ce_move_cursor(buffer, cursor, &delta);
                         cursor->x = line_len;
                         backspace_append(&buffer_state->backspace_tail, &buffer_state->backspace_head, '\n');
                         config_state->start_insert = *cursor;
                    }else{
                         Point previous = *cursor;
                         previous.x--;
                         char c = 0;
                         if(ce_get_char(buffer, &previous, &c)){
                              if(ce_remove_char(buffer, &previous)){
                                   if(cursor->x <= config_state->start_insert.x){
                                        backspace_append(&buffer_state->backspace_tail, &buffer_state->backspace_head, c);
                                        config_state->start_insert.x--;
                                   }
                                   // cannot use move_cursor due to not being able to be ahead of the last character
                                   cursor->x--;
                              }
                         }
                    }
               }
          case 126: // delete ?
               //ce_remove_char(buffer, cursor);
               break;
          case 10: // return
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
               break;
          default:
               if(ce_insert_char(buffer, cursor, key)) cursor->x++;
               break;
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
          case 'b':
          case 'B':
          {
               cursor->x -= ce_find_delta_to_beginning_of_word(buffer, cursor, key == 'b');
          } break;
          case 'e':
          case 'E':
          {
               cursor->x += ce_find_delta_to_end_of_word(buffer, cursor, key == 'e');
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
               enter_insert_mode(config_state, cursor);
               break;
          case 'I':
          {
               ce_move_cursor_to_soft_beginning_of_line(buffer, cursor);
               enter_insert_mode(config_state, cursor);
          } break;
          case 'O':
          case 'o':
          {
               if(!buffer->line_count){
                    ce_alloc_lines(buffer, 1);
               }
               if(ce_insert_newline(buffer, cursor->y + (key == 'o'))){
                    cursor->y += (key == 'o');
                    cursor->x = 0;
               }
               enter_insert_mode(config_state, cursor);
          } break;
          case '^':
          {
               ce_move_cursor_to_soft_beginning_of_line(buffer, cursor);
          } break;
          case '0':
               cursor->x = 0;
               break;
          case '$':
               cursor->x += ce_find_delta_to_end_of_line(buffer, cursor);
               break;
          case 'A':
          {
               cursor->x += ce_find_delta_to_end_of_line(buffer, cursor) + 1;
               enter_insert_mode(config_state, cursor);
          } break;
          case 'm':
          {
               FILE* pfile = popen("echo justin stinkkkks", "r");
               char* pstr = str_from_file_stream(pfile);
               config_state->view_current->buffer_node = new_buffer_from_string(head, "test_buffer", pstr);
               free(pstr);
               pclose(pfile);
               break;
          }
          case 'a':
          {
               if(buffer->lines[cursor->y] && cursor->x < (int64_t)(strlen(buffer->lines[cursor->y]))){
                    cursor->x++;
               }
               enter_insert_mode(config_state, cursor);
          } break;
          case 'C':
          case 'D':
          {
               int64_t n_deletes = ce_find_delta_to_end_of_line(buffer, cursor) + 1;
               if(n_deletes){
                    Point end = *cursor;
                    end.x += n_deletes;
                    char* save_string = ce_dupe_string(buffer, cursor, &end);
                    if(ce_remove_string(buffer, cursor, n_deletes)){
                         ce_commit_remove_string(&buffer_state->commit_tail, cursor, cursor, cursor, save_string);
                    }
               }
               if(key == 'C') enter_insert_mode(config_state, cursor);
          } break;
          case 'S':
          {
               // TODO: unify with cc
               ce_move_cursor_to_soft_beginning_of_line(buffer, cursor);
               int64_t n_deletes = ce_find_delta_to_end_of_line(buffer, cursor) + 1;
               while(n_deletes){
                    ce_remove_char(buffer, cursor);
                    n_deletes--;
               }
               enter_insert_mode(config_state, cursor);
          } break;
          case 'c':
          case 'd':
               if(should_handle_command(config_state, key)){
                    bool handled_key = true;
                    if(key == config_state->command_key){ // cc or dd
                         if(key == 'c'){
                              // TODO: unify with 'S'
                              ce_move_cursor_to_soft_beginning_of_line(buffer, cursor);
                              int64_t n_deletes = ce_find_delta_to_end_of_line(buffer, cursor) + 1;
                              while(n_deletes){
                                   ce_remove_char(buffer, cursor);
                                   n_deletes--;
                              }
                              enter_insert_mode(config_state, cursor);
                         }
                         else{
                              // delete line
                              if(buffer->line_count){
                                   if(ce_remove_line(buffer, cursor->y)){
                                        // TODO more explicit method to put cursor on the line
                                        Point delta = {0, 0};
                                        ce_move_cursor(buffer, cursor, &delta);
                                   }
                              }
                         }
                    }
                    else{
                         // TODO: provide a vim movement function which gives you
                         // the appropriate delta for the movement key sequence and
                         // use that everywhere?
                         switch(key){
                         case 'e':
                         case 'E':
                         {
                              int64_t n_deletes = ce_find_delta_to_end_of_word(buffer, cursor, key == 'e') + 1;
                              while(n_deletes){
                                   ce_remove_char(buffer, cursor);
                                   n_deletes--;
                              }
                         } break;
                         case 'b':
                         case 'B':
                         {
                              int64_t n_deletes = ce_find_delta_to_beginning_of_word(buffer, cursor, key == 'b');
                              while(n_deletes){
                                   cursor->x--;
                                   ce_remove_char(buffer, cursor);
                                   n_deletes--;
                              }
                         } break;
                         case '$':
                         {
                              int64_t n_deletes = ce_find_delta_to_end_of_line(buffer, cursor) + 1;
                              while(n_deletes){
                                   ce_remove_char(buffer, cursor);
                                   n_deletes--;
                              }
                         } break;
                         default:
                              handled_key = false;
                         }
                    }

                    if(handled_key && config_state->command_key == 'c') enter_insert_mode(config_state, cursor);
                    config_state->command_key = '\0';
               }
               break;
          case 's':
               ce_remove_char(buffer, cursor);
               enter_insert_mode(config_state, cursor);
               break;
          case '':
               ce_save_buffer(buffer, buffer->filename);
               break;
          case 'v':
          {
               BufferView* new_view = ce_split_view(config_state->view_current, config_state->view_current->buffer_node, true);
               if(new_view){
                    new_view->cursor = config_state->view_current->cursor;
                    new_view->top_row = config_state->view_current->top_row;
                    new_view->left_collumn = config_state->view_current->left_collumn;
               }
          } break;
          case '':
          {
               BufferView* new_view = ce_split_view(config_state->view_current, config_state->view_current->buffer_node, false);
               if(new_view){
                    new_view->cursor = config_state->view_current->cursor;
                    new_view->top_row = config_state->view_current->top_row;
                    new_view->left_collumn = config_state->view_current->left_collumn;
               }
          } break;
          case '':
          {
               Point save_cursor = config_state->view_current->cursor;
               config_state->view_current->buffer_node->buffer->cursor = config_state->view_current->cursor;
               ce_remove_view(config_state->view_head, config_state->view_current);
               BufferView* new_view = ce_find_view_at_point(config_state->view_head, &save_cursor);
               if(new_view){
                    config_state->view_current = new_view;
               }else{
                    config_state->view_current = config_state->view_head;
               }
          } break;
          case '':
          {
               // save cursor in buffer
               config_state->view_current->buffer_node->buffer->cursor = config_state->view_current->cursor;

               // get the next buffer
               config_state->view_current->buffer_node = config_state->view_current->buffer_node->next;
               if(!config_state->view_current->buffer_node){
                    config_state->view_current->buffer_node = head;
               }

               // load the cursor from the buffer
               config_state->view_current->cursor = config_state->view_current->buffer_node->buffer->cursor;
          } break;
          case 'g':
               if(should_handle_command(config_state, key)){
                    // TODO: we will want to creating multiple BufferView* workspaces and switch between those using "gt"
                    if(key == 't'){
                    }
                    config_state->command_key = '\0';
               }
               break;
          case 'u':
               if(buffer_state->commit_tail && buffer_state->commit_tail->commit.type != BCT_NONE){
                    ce_commit_undo(buffer, &buffer_state->commit_tail, cursor);
               }
               break;
          case 'x':
          {
               char c;
               if(ce_get_char(buffer, cursor, &c) && ce_remove_char(buffer, cursor)){
                    ce_commit_remove_char(&buffer_state->commit_tail, cursor, cursor, cursor, c);
               }
          }
          break;
          case 18:
          if(buffer_state->commit_tail && buffer_state->commit_tail->next){
               ce_commit_redo(buffer, &buffer_state->commit_tail, cursor);
          }
          break;
          case ';':
          {
               if(config_state->find_command.command_key == '\0') break;
               find_command(config_state->find_command.command_key,
                            config_state->find_command.find_char, buffer, cursor);
          } break;
          case ',':
          {
               if(config_state->find_command.command_key == '\0') break;
               char command_key = config_state->find_command.command_key;
               if(isupper(command_key)) command_key = tolower(command_key);
               else command_key = toupper(command_key);

               find_command(command_key, config_state->find_command.find_char, buffer, cursor);
          } break;
          case 'f':
          case 't':
          case 'F':
          case 'T':
          {
               if(should_handle_command(config_state, key)){
                    config_state->find_command.command_key = config_state->command_key;
                    config_state->find_command.find_char = key;
                    find_command(config_state->command_key, key, buffer, cursor);
                    // TODO: devise a better way to clear command_key following a movement
                    config_state->command_key = '\0';
               }
          } break;
          case 'r':
          {
               if(should_handle_command(config_state, key)){
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
          case 8: // Ctrl + h
          {
               // TODO: consolidate into function for use with other window movement keys, and for use in insert mode?
               Point point = {config_state->view_current->top_left.x - 2, // account for window separator
                              cursor->y - config_state->view_current->top_row + config_state->view_current->top_left.y};
               if(point.x < 0) point.x += g_terminal_dimensions->x - 1;
               BufferView* next_view = ce_find_view_at_point(config_state->view_head, &point);
               if(next_view){
                    // save cursor
                    config_state->view_current->buffer_node->buffer->cursor = config_state->view_current->cursor;
                    config_state->view_current = next_view;
               }
          }
          break;
          case 10: // Ctrl + j
          {
               Point point = {cursor->x - config_state->view_current->left_collumn + config_state->view_current->top_left.x,
                              config_state->view_current->bottom_right.y + 2}; // account for window separator
               if(point.y >= g_terminal_dimensions->y - 1) point.y = 0;
               BufferView* next_view = ce_find_view_at_point(config_state->view_head, &point);
               if(next_view){
                    // save cursor
                    config_state->view_current->buffer_node->buffer->cursor = config_state->view_current->cursor;
                    config_state->view_current = next_view;
               }
          }
          break;
          case 11: // Ctrl + k
          {
               Point point = {cursor->x - config_state->view_current->left_collumn + config_state->view_current->top_left.x,
                              config_state->view_current->top_left.y - 2}; // account for window separator
               BufferView* next_view = ce_find_view_at_point(config_state->view_head, &point);
               if(next_view){
                    // save cursor
                    config_state->view_current->buffer_node->buffer->cursor = config_state->view_current->cursor;
                    config_state->view_current = next_view;
               }
          }
          break;
          case 12: // Ctrl + l
          {
               Point point = {config_state->view_current->bottom_right.x + 2, // account for window separator
                              cursor->y - config_state->view_current->top_row + config_state->view_current->top_left.y};
               if(point.x >= g_terminal_dimensions->x - 1) point.x = 0;
               BufferView* next_view = ce_find_view_at_point(config_state->view_head, &point);
               if(next_view){
                    // save cursor
                    config_state->view_current->buffer_node->buffer->cursor = config_state->view_current->cursor;
                    config_state->view_current = next_view;
               }
          }
          break;
          }
     }

     return true;
}

void view_drawer(const BufferNode* head, void* user_data)
{
     (void)(head);
     ConfigState* config_state = user_data;
     Buffer* buffer = config_state->view_current->buffer_node->buffer;
     BufferView* buffer_view = config_state->view_current;
     Point* cursor = &config_state->view_current->cursor;

     ce_calc_views(config_state->view_head);
     ce_follow_cursor(cursor, &buffer_view->top_row, &buffer_view->left_collumn,
                      buffer_view->bottom_right.y - buffer_view->top_left.y,
                      buffer_view->bottom_right.x - buffer_view->top_left.x);

     // print the range of lines we want to show
     if(buffer->line_count){
          standend();
          // NOTE: always draw from the head
          ce_draw_views(config_state->view_head);
     }

     attron(A_REVERSE);
     mvprintw(g_terminal_dimensions->y - 1, 0, "%s %s %d lines, k %d, c %ld, %ld, b: %ld, %ld, v: %ld, %ld -> %ld, %ld t: %ld, %ld",
              config_state->insert ? "INSERT" : "NORMAL", buffer->filename, buffer->line_count, config_state->last_key,
              cursor->x, cursor->y, buffer_view->top_row, buffer_view->left_collumn, buffer_view->top_left.x, buffer_view->top_left.y,
              buffer_view->bottom_right.x, buffer_view->bottom_right.y, g_terminal_dimensions->x, g_terminal_dimensions->y);
     attroff(A_REVERSE);

     // reset the cursor
     move(cursor->y - buffer_view->top_row + buffer_view->top_left.y,
          cursor->x - buffer_view->left_collumn + buffer_view->top_left.x);
}
