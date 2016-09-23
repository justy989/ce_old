#include "ce.h"
#include <assert.h>
#include <ctype.h>
#include <ftw.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#define TAB_STRING "     "

typedef struct BackspaceNode{
     char c;
     struct BackspaceNode* next;
} BackspaceNode;

BackspaceNode* backspace_push(BackspaceNode** head, char c)
{
     BackspaceNode* new_node = malloc(sizeof(*new_node));
     if(!new_node){
          ce_message("%s() failed to malloc node", __FUNCTION__);
          return NULL;
     }

     new_node->c = c;
     new_node->next = *head;
     *head = new_node;

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

     str[len] = 0;
     return str;
}

void backspace_free(BackspaceNode** head)
{
     while(*head){
          BackspaceNode* tmp = *head;
          *head = (*head)->next;
          free(tmp);
     }
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

typedef struct MarkNode{
     char reg_char;
     Point location;
     struct MarkNode* next;
}MarkNode;

typedef struct{
     BufferCommitNode* commit_tail;
     BackspaceNode* backspace_head;
     struct MarkNode* mark_head;
} BufferState;

Point* find_mark(BufferState* buffer, char mark_char)
{
     MarkNode* itr = buffer->mark_head;
     while(itr != NULL){
          if(itr->reg_char == mark_char) return &itr->location;
          itr = itr->next;
     }
     return NULL;
}

void add_mark(BufferState* buffer, char mark_char, const Point* location)
{
     Point* mark_location = find_mark(buffer, mark_char);
     if(!mark_location){
          MarkNode* new_mark = malloc(sizeof(*buffer->mark_head));
          new_mark->reg_char = mark_char;
          new_mark->next = buffer->mark_head;
          buffer->mark_head = new_mark;
          mark_location = &new_mark->location;
     }
     *mark_location = *location;
}

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
#ifndef FTW_STOP
#define FTW_STOP 1
#define FTW_CONTINUE 0
#endif

typedef struct {
     const char* search_filename;
     BufferNode* head;
     BufferNode* new_node;
} NftwState;
__thread NftwState nftw_state;

int nftw_find_file(const char* fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
     (void)sb;
     if((typeflag == FTW_F || typeflag == FTW_SL) && !strcmp(&fpath[ftwbuf->base], nftw_state.search_filename)){
          nftw_state.new_node = new_buffer_from_file(nftw_state.head, nftw_state.search_filename);
          return FTW_STOP;
     }
     return FTW_CONTINUE;
}

BufferNode* open_file_buffer(BufferNode* head, const char* filename)
{
     BufferNode* itr = head;
     while(itr){
          if(!strcmp(itr->buffer->name, filename)) break; // already open
          itr = itr->next;
     }

     if(!itr){
          // clang doesn't support nested functions so we need to deal with global state
          nftw_state.search_filename = filename;
          nftw_state.head = head;
          nftw_state.new_node = NULL;
          nftw(".", nftw_find_file, 20, FTW_CHDIR);
          return nftw_state.new_node;
     }
     return itr;
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

     // if we loaded a file, set the view to point at the file, otherwise default to looking at the message buffer
     config_state->view_head->buffer_node = (head && head->next) ? head->next : head;
     config_state->view_current = config_state->view_head;

     return true;
}

bool destroyer(BufferNode* head, void* user_data)
{
     while(head){
          BufferState* buffer_state = head->buffer->user_data;
          BufferCommitNode* itr = buffer_state->commit_tail;
          while(itr->prev) itr = itr->prev;
          ce_commits_free(itr);
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

// location is {left_column, top_line} for the view
void scroll_view_to_location(BufferView* buffer_view, const Point* location){
     // TODO: we should be able to scroll the view above our first line
     buffer_view->left_column = (location->x >= 0) ? location->x : 0;
     buffer_view->top_row = (location->y >= 0) ? location->y : 0;
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
     BufferView* buffer_view = config_state->view_current;
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
               Point end_cursor = *cursor;
               ce_clamp_cursor(buffer, &end_cursor);

               config_state->insert = false;
               if(config_state->start_insert.x == cursor->x &&
                  config_state->start_insert.y == cursor->y &&
                  config_state->original_start_insert.x == cursor->x &&
                  config_state->original_start_insert.y == cursor->y){
                  // pass
               }else{
                    if(config_state->start_insert.x == config_state->original_start_insert.x &&
                       config_state->start_insert.y == config_state->original_start_insert.y){
                         // TODO: assert cursor is after start_insert
                         // exclusively inserts
                         ce_commit_insert_string(&buffer_state->commit_tail,
                                                 &config_state->start_insert,
                                                 &config_state->original_start_insert,
                                                 &end_cursor,
                                                 ce_dupe_string(buffer, &config_state->start_insert, cursor));
                    }else if(config_state->start_insert.x < config_state->original_start_insert.x ||
                             config_state->start_insert.y < config_state->original_start_insert.y){
                         if(cursor->x == config_state->start_insert.x &&
                            cursor->y == config_state->start_insert.y){
                              // exclusively backspaces!
                              ce_commit_remove_string(&buffer_state->commit_tail,
                                                      cursor,
                                                      &config_state->original_start_insert,
                                                      &end_cursor,
                                                      backspace_get_string(buffer_state->backspace_head));
                              backspace_free(&buffer_state->backspace_head);
                         }else{
                              // mixture of inserts and backspaces
                              ce_commit_change_string(&buffer_state->commit_tail,
                                                      &config_state->start_insert,
                                                      &config_state->original_start_insert,
                                                      &end_cursor,
                                                      ce_dupe_string(buffer,
                                                                     &config_state->start_insert,
                                                                     cursor),
                                                      backspace_get_string(buffer_state->backspace_head));
                              backspace_free(&buffer_state->backspace_head);
                         }
                    }
               }

               // when we exit insert mode, do not move the cursor back unless we are at the end of the line
               *cursor = end_cursor;
          } break;
          case 127: // backspace
               if(buffer->line_count){
                    if(cursor->x <= 0){
                         if(cursor->y){
                              int64_t line_len = strlen(buffer->lines[cursor->y - 1]);
                              ce_append_string(buffer, cursor->y - 1, buffer->lines[cursor->y]);

                              if(ce_remove_line(buffer, cursor->y)){
                                   backspace_push(&buffer_state->backspace_head, '\n');
                                   cursor->y--;
                                   cursor->x = line_len;
                                   config_state->start_insert = *cursor;
                              }
                         }
                    }else{
                         Point previous = *cursor;
                         previous.x--;
                         char c = 0;
                         if(ce_get_char(buffer, &previous, &c)){
                              if(ce_remove_char(buffer, &previous)){
                                   if(cursor->x <= config_state->start_insert.x){
                                        backspace_push(&buffer_state->backspace_head, c);
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
          case '\t':
          {
               ce_insert_string(buffer, cursor, TAB_STRING);
               Point delta = {5, 0};
               ce_move_cursor(buffer, cursor, &delta);
          } break;
          case 10: // return
          {
               char* start = buffer->lines[cursor->y] + cursor->x;
               int64_t to_end_of_line_len = strlen(start);

               if(ce_insert_line(buffer, cursor->y + 1, start)){
                    if(to_end_of_line_len){
                         ce_remove_string(buffer, cursor, to_end_of_line_len);
                    }
                    cursor->y++;
                    cursor->x = 0;
               }
          } break;
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
          case 'J':
          {
               Point join_loc = {strlen(buffer->lines[cursor->y]), cursor->y};
               if(ce_join_line(buffer, cursor->y)){
                    ce_commit_change_char(&buffer_state->commit_tail,
                                          &join_loc, cursor, cursor, ' ', '\n');
               }
          } break;
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
          case 'w':
          case 'W':
          {
               cursor->x += ce_find_next_word(buffer, cursor, key == 'w');
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
          {
               Point end_of_line = {0, cursor->y};
               if(ce_insert_char(buffer, &end_of_line, '\n')){
                    cursor->x = 0;
                    enter_insert_mode(config_state, cursor);
               }
          } break;
          case 'o':
          {
               Point end_of_line = *cursor;
               end_of_line.x = strlen(buffer->lines[cursor->y]);

               if(ce_insert_char(buffer, &end_of_line, '\n')){
                    cursor->y++;
                    cursor->x = 0;
                    enter_insert_mode(config_state, cursor);
               }
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
               if(should_handle_command(config_state, key)){
                    add_mark(buffer_state, key, cursor);
                    config_state->command_key = '\0';
               }
          } break;
          case '\'':
          {
               if(should_handle_command(config_state, key)){
                    Point* marked_location = find_mark(buffer_state, key);
                    cursor->y = marked_location->y;
                    config_state->command_key = '\0';
               }
          } break;
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
               Point delete_end = {cursor->x + n_deletes, cursor->y};
               char* save_string = ce_dupe_string(buffer, cursor, &delete_end);
               if(ce_remove_string(buffer, cursor, n_deletes)){
                    ce_commit_remove_string(&buffer_state->commit_tail, cursor, cursor, cursor, save_string);
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
                              Point delete_end = {cursor->x + n_deletes, cursor->y};
                              char* save_string = ce_dupe_string(buffer, cursor, &delete_end);
                              if(ce_remove_string(buffer, cursor, n_deletes)){
                                   ce_commit_remove_string(&buffer_state->commit_tail, cursor, cursor, cursor, save_string);
                              }
                              enter_insert_mode(config_state, cursor);
                         }
                         else{
                              // delete line
                              Point delete_begin = {0, cursor->y};
                              char* save_string = ce_dupe_line(buffer, cursor->y);
                              if(ce_remove_line(buffer, cursor->y)){
                                   ce_commit_remove_string(&buffer_state->commit_tail, &delete_begin, cursor, cursor, save_string);
                              }
                              ce_clamp_cursor(buffer, cursor);
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
                              Point delete_end = {cursor->x + n_deletes, cursor->y};
                              char* save_string = ce_dupe_string(buffer, cursor, &delete_end);
                              if(ce_remove_string(buffer, cursor, n_deletes)){
                                   ce_commit_remove_string(&buffer_state->commit_tail, cursor, cursor, cursor, save_string);
                              }
                         } break;
                         case 'b':
                         case 'B':
                         {
                              int64_t n_deletes = ce_find_delta_to_beginning_of_word(buffer, cursor, key == 'b');
                              Point delete_begin = {cursor->x - n_deletes, cursor->y};
                              char* save_string = ce_dupe_string(buffer, &delete_begin, cursor );
                              if(ce_remove_string(buffer, &delete_begin, n_deletes)){
                                   ce_commit_remove_string(&buffer_state->commit_tail, &delete_begin, cursor, &delete_begin, save_string);
                                   ce_set_cursor(buffer, cursor, &delete_begin);
                              }
                         } break;
                         case 'w':
                         case 'W':
                         {
                              int64_t n_deletes = ce_find_next_word(buffer, cursor, key == 'w');
                              Point delete_end = {cursor->x + n_deletes, cursor->y};
                              char* save_string = ce_dupe_string(buffer, cursor, &delete_end);
                              if(ce_remove_string(buffer, cursor, n_deletes)){
                                   ce_commit_remove_string(&buffer_state->commit_tail, cursor, cursor, cursor, save_string);
                              }
                         } break;
                         case '$':
                         {
                              int64_t n_deletes = ce_find_delta_to_end_of_line(buffer, cursor) + 1;
                              Point delete_end = {cursor->x + n_deletes, cursor->y};
                              char* save_string = ce_dupe_string(buffer, cursor, &delete_end);
                              if(ce_remove_string(buffer, cursor, n_deletes)){
                                   ce_commit_remove_string(&buffer_state->commit_tail, cursor, cursor, cursor, save_string);
                              }
                         } break;
                         case '%':
                         {
                              Point delta;
                              if(!ce_find_match(buffer, cursor, &delta)) break;
                              // TODO: delete across line boundaries
                              assert(delta.y == 0);
                              if(delta.x > 0){
                                   for(int64_t i = 0; i < delta.x + 1; i++){
                                        ce_remove_char(buffer, cursor);
                                   }
                              }
                              else{
                                   for(int64_t i = 0; i < -delta.x + 1; i++){
                                        ce_remove_char(buffer, cursor);
                                        cursor->x--;
                                   }
                                   cursor->x++;
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
          {
               char c;
               if(ce_get_char(buffer, cursor, &c) && ce_remove_char(buffer, cursor)){
                    ce_commit_remove_char(&buffer_state->commit_tail, cursor, cursor, cursor, c);
               }
               enter_insert_mode(config_state, cursor);
          } break;
          case '':
               ce_save_buffer(buffer, buffer->filename);
               break;
          case 'v':
          {
               BufferView* new_view = ce_split_view(config_state->view_current, config_state->view_current->buffer_node, false);
               if(new_view){
                    Point top_left = {0, 0};
                    Point bottom_right = {g_terminal_dimensions->x - 1, g_terminal_dimensions->y - 2}; // account for statusbar
                    ce_calc_views(config_state->view_head, &top_left, &bottom_right);
                    new_view->cursor = config_state->view_current->cursor;
                    new_view->top_row = config_state->view_current->top_row;
                    new_view->left_column = config_state->view_current->left_column;
               }
          } break;
          case '':
          {
               BufferView* new_view = ce_split_view(config_state->view_current, config_state->view_current->buffer_node, true);
               if(new_view){
                    Point top_left = {0, 0};
                    Point bottom_right = {g_terminal_dimensions->x - 1, g_terminal_dimensions->y - 2}; // account for statusbar
                    ce_calc_views(config_state->view_head, &top_left, &bottom_right);
                    new_view->cursor = config_state->view_current->cursor;
                    new_view->top_row = config_state->view_current->top_row;
                    new_view->left_column = config_state->view_current->left_column;
               }
          } break;
          case 14: // Ctrl + n
          {
               Point save_cursor = config_state->view_current->cursor;
               config_state->view_current->buffer_node->buffer->cursor = config_state->view_current->cursor;
               ce_remove_view(&config_state->view_head, config_state->view_current);
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
                    ce_clamp_cursor(buffer, cursor);
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
          case 'H':
          {
               // move cursor to top line of view
               // TODO: sometimes it would be nicer to work in absolutes instead of in deltas
               Point delta = {0, buffer_view->top_row - cursor->y};
               ce_move_cursor(buffer, cursor, &delta);
          } break;
          case 'M':
          {
               // move cursor to middle line of view
               // TODO: sometimes it would be nicer to work in absolutes instead of in deltas
               Point delta = {0, (buffer_view->top_row + (g_terminal_dimensions->y - 1) / 2)
                    - cursor->y};
               ce_move_cursor(buffer, cursor, &delta);
          } break;
          case 'L':
          {
               // move cursor to bottom line of view
               Point delta = {0, (buffer_view->top_row + g_terminal_dimensions->y - 1)
                    - (cursor->y + 1)};
               ce_move_cursor(buffer, cursor, &delta);
          } break;
          case 'z':
          {
               if(should_handle_command(config_state, key)){
                    switch(key){
                    case 't':
                    {
                         Point location = {0, cursor->y};
                         scroll_view_to_location(config_state->view_current, &location);
                    } break;
                    case 'z':
                    {
                         // center view on cursor
                         Point location = {0, cursor->y - (g_terminal_dimensions->y/2)};
                         scroll_view_to_location(config_state->view_current, &location);
                    } break;
                    case 'b':
                    {
                         // move current line to bottom of view
                         Point location = {0, cursor->y - g_terminal_dimensions->y};
                         scroll_view_to_location(config_state->view_current, &location);
                    } break;
                    }
                    // TODO: devise a better way to clear command_key following a movement
                    config_state->command_key = '\0';
               }
          } break;
          case 'G':
          {
               Point delta = {0, buffer->line_count - cursor->y};
               ce_move_cursor(buffer, cursor, &delta);
          } break;
          case '>':
          {
               if(should_handle_command(config_state, key)){
                    if(key == '>'){
                         Point loc = {0, cursor->y};
                         ce_insert_string(buffer, &loc, TAB_STRING);
                         ce_commit_insert_string(&buffer_state->commit_tail, &loc, cursor, cursor, strdup(TAB_STRING));
                    }
                    config_state->command_key = '\0';
               }
          } break;
          case 'g':
          {
               if(should_handle_command(config_state, key)){
                    switch(key){
                    case 'g':
                    {
                         Point delta = {0, -cursor->y};
                         ce_move_cursor(buffer, cursor, &delta);
                    } break;
#if 0
                    case 't':
                    {
                    // TODO: we will want to creating multiple BufferView* workspaces and switch between those using "gt"
                         config_state->current_buffer_node = config_state->current_buffer_node->next;
                         if(!config_state->current_buffer_node){
                              config_state->current_buffer_node = head;
                         }
                    } break;
#endif
                    case 'f':
                    {
                         if(!buffer->lines[cursor->y]) break;
                         // TODO: get word under the cursor and unify with '*' impl
                         int64_t word_len = ce_find_delta_to_end_of_word(buffer, cursor, true)+1;
                         if(buffer->lines[cursor->y][cursor->x+word_len] == '.'){
                              Point ext_start = {cursor->x+word_len, cursor->y};
                              int64_t extension_len = ce_find_delta_to_end_of_word(buffer, &ext_start, true);
                              if(extension_len != -1) word_len += extension_len+1;
                         }
                         char* filename = alloca(word_len+1);
                         strncpy(filename, &buffer->lines[cursor->y][cursor->x], word_len);
                         filename[word_len] = '\0';

                         BufferNode* file_buffer = open_file_buffer(head, filename);
                         if(file_buffer) buffer_view->buffer_node = file_buffer;
                         else ce_message("file %s not found", filename);
                    } break;
                    }
                    // TODO: devise a better way to clear command_key following a movement
                    config_state->command_key = '\0';
               }
          } break;
          case '%':
          {
               Point delta;
               if(ce_find_match(buffer, cursor, &delta)){
                    ce_move_cursor(buffer, cursor, &delta);
               }
          } break;
          case '*':
          {
               if(!buffer->lines[cursor->y]) break;
               // TODO: search for the word under the cursor
               int64_t word_len = ce_find_delta_to_end_of_word(buffer, cursor, true)+1;
               char* search_str = alloca(word_len+1);
               strncpy(search_str, &buffer->lines[cursor->y][cursor->x], word_len);
               search_str[word_len] = '\0';

               Point delta;
               if(ce_find_str(buffer, cursor, search_str, &delta)){
                    ce_move_cursor(buffer, cursor, &delta);
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
               Point point = {cursor->x - config_state->view_current->left_column + config_state->view_current->top_left.x,
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
               Point point = {cursor->x - config_state->view_current->left_column + config_state->view_current->top_left.x,
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
          case '=':
          {
               if(should_handle_command(config_state, key)){
                    int in_fds[2]; // 0 = child stdin
                    int out_fds[2]; // 1 = child stdout
                    if(pipe(in_fds) == -1 || pipe(out_fds) == -1){
                         ce_message("pipe failed %s", strerror(errno));
                         return true;
                    }
                    pid_t pid = fork();
                    if(pid == -1){
                         ce_message("fork failed %s", strerror(errno));
                         return true;
                    }
                    if(pid == 0){
                         // child process
                         close(in_fds[1]); // close parent fds
                         close(out_fds[0]);

                         close(0); // close stdin
                         close(1); // close stdout
                         dup(in_fds[0]); // new stdin
                         dup(out_fds[1]); // new stdout
                         int64_t cursor_position = cursor->x+1;
                         for(int64_t y_itr = 0; y_itr < cursor->y; y_itr++){
                              cursor_position += strlen(buffer->lines[y_itr]);
                         }
                         char* cursor_arg;
                         asprintf(&cursor_arg, "-cursor=%"PRId64, cursor_position);

                         char* line_arg;
                         asprintf(&line_arg, "-lines=%"PRId64":%"PRId64, cursor->y+1, cursor->y+1);

                         int ret = execlp("clang-format", "clang-format", line_arg, cursor_arg, (char *)NULL);
                         assert(ret != -1);
                         exit(1); // we should never reach here
                    }

                    // parent process
                    close(in_fds[0]); // close child fds
                    close(out_fds[1]);

                    FILE* child_stdin = fdopen(in_fds[1], "w");
                    FILE* child_stdout = fdopen(out_fds[0], "r");
                    assert(child_stdin);
                    assert(child_stdout);

                    for(int i = 0; i < buffer->line_count; i++){
                         if(fputs(buffer->lines[i], child_stdin) == EOF || fputc('\n', child_stdin) == EOF){
                              ce_message("issue with fputs");
                              return true;
                         }
                    }
                    fclose(child_stdin);
                    close(in_fds[1]);

                    char formatted_line_buf[BUFSIZ];
                    formatted_line_buf[0] = 0;

                    // read cursor position
                    fgets(formatted_line_buf, BUFSIZ, child_stdout);
                    int cursor_position = -1;
                    sscanf(formatted_line_buf, "{ \"Cursor\": %d", &cursor_position);

                    // blow away all lines in the file
                    for(int64_t i = buffer->line_count - 1; i > 0; i--){
                         ce_remove_line(buffer, i);
                    }

                    for(int64_t i = 0; ; i++){
                         if(fgets(formatted_line_buf, BUFSIZ, child_stdout) == NULL) break;
                         size_t new_line_len = strlen(formatted_line_buf) - 1;
                         assert(formatted_line_buf[new_line_len] == '\n');
                         formatted_line_buf[new_line_len] = 0;
                         ce_insert_line(buffer, i, formatted_line_buf);
#if 0
                         if(cursor_position > 0){
                              cursor_position -= new_line_len+1;
                              if(cursor_position <= 0){
                                   Point new_cursor_location = {-cursor_position, i};
                                   ce_message("moving cursor to %ld", -cursor_position);
                                   ce_set_cursor(buffer, cursor, &new_cursor_location);
                              }
                         }
#endif
                    }
                    cursor->x = 0;
                    cursor->y = 0;
                    if(!ce_advance_cursor(buffer, cursor, cursor_position-1))
                         ce_message("failed to advance cursor");

#if 0
                    // TODO: use -output-replacements-xml to support undo
                    char* formatted_line = strdup(formatted_line_buf);
                    // save the current line in undo history
                    Point delete_begin = {0, cursor->y};
                    char* save_string = ce_dupe_line(buffer, cursor->y);
                    if(!ce_remove_line(buffer, cursor->y)){
                         ce_message("ce_remove_string failed");
                         return true;
                    }
                    ce_insert_string(buffer, &delete_begin, formatted_line);
                    ce_commit_change_string(&buffer_state->commit_tail, &delete_begin, cursor, cursor, formatted_line, save_string);
#endif

                    fclose(child_stdout);
                    close(in_fds[0]);

                    // wait for the child process to complete
                    int wstatus;
                    do {
                         pid_t w = waitpid(pid, &wstatus, WUNTRACED | WCONTINUED);
                         if (w == -1) {
                              perror("waitpid");
                              exit(EXIT_FAILURE);
                         }

                         if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) != 0) {
                              ce_message("clang-format process exited, status=%d\n", WEXITSTATUS(wstatus));
                         } else if (WIFSIGNALED(wstatus)) {
                              ce_message("clang-format process killed by signal %d\n", WTERMSIG(wstatus));
                         } else if (WIFSTOPPED(wstatus)) {
                              ce_message("clang-format process stopped by signal %d\n", WSTOPSIG(wstatus));
                         } else if (WIFCONTINUED(wstatus)) {
                              ce_message("clang-format process continued\n");
                         }
                    } while (!WIFEXITED(wstatus) && !WIFSIGNALED(wstatus));
                    config_state->command_key = '\0';
               }
          } break;
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

     Point top_left = {0, 0};
     Point bottom_right = {g_terminal_dimensions->x - 1, g_terminal_dimensions->y - 2}; // account for statusbar
     ce_calc_views(config_state->view_head, &top_left, &bottom_right);
     ce_follow_cursor(cursor, &buffer_view->left_column, &buffer_view->top_row,
                      buffer_view->bottom_right.x - buffer_view->top_left.x,
                      buffer_view->bottom_right.y - buffer_view->top_left.y,
                      buffer_view->bottom_right.x == (g_terminal_dimensions->x - 1),
                      buffer_view->bottom_right.y == (g_terminal_dimensions->y - 2));

     // print the range of lines we want to show
     if(buffer->line_count){
          standend();
          // NOTE: always draw from the head
          ce_draw_views(config_state->view_head);
     }

     attron(A_REVERSE);
     // draw all blanks at the bottom
     move(g_terminal_dimensions->y - 1, 0);
     for(int i = 0; i < g_terminal_dimensions->x; ++i) addch(' ');

     // draw the status line
     mvprintw(g_terminal_dimensions->y - 1, 0, "%s %s %lld lines, k %lld, c %lld, %lld, v %lld, %lld -> %lld, %lld t: %lld, %lld",
              config_state->insert ? "INSERT" : "NORMAL", buffer->filename, buffer->line_count, config_state->last_key,
              cursor->x, cursor->y, buffer_view->top_left.x, buffer_view->top_left.y, buffer_view->bottom_right.x,
              buffer_view->bottom_right.y, g_terminal_dimensions->x, g_terminal_dimensions->y);
     attroff(A_REVERSE);

     // reset the cursor
     move(cursor->y - buffer_view->top_row + buffer_view->top_left.y,
          cursor->x - buffer_view->left_column + buffer_view->top_left.x);
}
