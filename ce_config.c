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
     uint64_t command_multiplier;
     char command_key;
     uint64_t movement_multiplier;
     char movement_keys[2];
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
} MarkNode;

typedef enum{
     YANK_NORMAL,
     YANK_LINE,
} YankMode;
typedef struct YankNode{
     char reg_char;
     const char* text;
     YankMode mode;
     struct YankNode* next;
} YankNode;

typedef struct{
     BufferCommitNode* commit_tail;
     BackspaceNode* backspace_head;
     struct MarkNode* mark_head;
     struct YankNode* yank_head;
} BufferState;

YankNode* find_yank(BufferState* buffer, char reg_char){
     YankNode* itr = buffer->yank_head;
     while(itr != NULL){
          if(itr->reg_char == reg_char) return itr;
          itr = itr->next;
     }
     return NULL;
}

// for now the yanked string is user allocated. eventually will probably
// want to change this interface so that everything is hidden
void add_yank(BufferState* buffer, char reg_char, const char* yank_text, YankMode mode){
     YankNode* node = find_yank(buffer, reg_char);
     if(node != NULL){
          free((void*)node->text);
     }
     else{
          YankNode* new_yank = malloc(sizeof(*buffer->yank_head));
          new_yank->reg_char = reg_char;
          new_yank->next = buffer->yank_head;
          node = new_yank;
          buffer->yank_head = new_yank;
     }
     node->text = yank_text;
     node->mode = mode;
}

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

void clear_keys(ConfigState* config_state)
{
     config_state->command_multiplier = 0;
     config_state->command_key = '\0';
     config_state->movement_multiplier = 0;
     memset(config_state->movement_keys, 0, sizeof config_state->movement_keys);
}

// returns true if key may be interpreted as a multiplier given the current mulitplier state
bool key_is_multiplier(uint64_t multiplier, char key)
{
     return (key >='1' && key <= '9') || (key == '0' && multiplier > 0);
}

typedef enum{
     MOVEMENT_CONTINUE = '\0',
     MOVEMENT_COMPLETE,
     MOVEMENT_INVALID
} movement_state_t;

// return false invalid/incomplete movement in config state, otherwise returns true + moves cursor + movement_end
movement_state_t try_generic_movement(ConfigState* config_state, Buffer* buffer, Point* cursor, Point* movement_start, Point* movement_end)
{
     *movement_start = *movement_end = *cursor;

     char key0 = config_state->movement_keys[0];
     char key1 = config_state->movement_keys[1];
     uint64_t multiplier = config_state->movement_multiplier;

     if(key0 == MOVEMENT_CONTINUE) return MOVEMENT_CONTINUE;

     for(size_t mm=0; mm < multiplier; mm++) {
          switch(key0){
          case 'h':
          {
               Point delta = {-1, 0};
               ce_move_cursor(buffer, movement_end, &delta);
          } break;
          case 'j':
          {
               Point delta = {0, 1};
               ce_move_cursor(buffer, movement_end, &delta);
          } break;
          case 'k':
          {
               Point delta = {0, -1};
               ce_move_cursor(buffer, movement_end, &delta);
          } break;
          case 'l':
          {
               Point delta = {1, 0};
               ce_move_cursor(buffer, movement_end, &delta);
          } break;
          case 'i':
               switch(key1){
               case 'w':
               {
                    char curr_char;
                    bool success = ce_get_char(buffer, movement_start, &curr_char);
                    if(!success) return MOVEMENT_INVALID;

                    if(ce_ispunct(curr_char)){
                         success = ce_get_homogenous_adjacents(buffer, movement_start, movement_end, ce_ispunct);
                         if(!success) return MOVEMENT_INVALID;
                    }else if(isblank(curr_char)){
                         success = ce_get_homogenous_adjacents(buffer, movement_start, movement_end, isblank);
                         if(!success) return MOVEMENT_INVALID;
                    }else{
                         success = ce_get_homogenous_adjacents(buffer, movement_start, movement_end, ce_iswordchar);
                         if(!success) return MOVEMENT_INVALID;
                    }
               } break;
               case MOVEMENT_CONTINUE:
                    return MOVEMENT_CONTINUE;
               default:
                    return MOVEMENT_INVALID;
               }
               break;
          case 'e':
          case 'E':
               movement_end->x += ce_find_delta_to_end_of_word(buffer, movement_end, key0 == 'e') + 1;
               break;
          case 'b':
          case 'B':
               cursor->x -= ce_find_delta_to_beginning_of_word(buffer, cursor, key0 == 'b');
               break;
          case 'w':
          case 'W':
               movement_end->x += ce_find_next_word(buffer, movement_end, key0 == 'w');
               break;
          case '$':
               movement_end->x += ce_find_delta_to_end_of_line(buffer, movement_end) + 1;
               break;
          case '%':
          {
               Point delta;
               if(!ce_find_match(buffer, cursor, &delta)) break;

               // TODO: movement across line boundaries
               assert(delta.y == 0);

               // we always want start >= end
               if(delta.y < 0 || (delta.x < 0 && delta.y == 0)){
                    movement_start->x = cursor->x + delta.x;
                    movement_start->y = cursor->y + delta.y;
               }
               else{
                    movement_end->x = cursor->x + delta.x;
                    movement_end->y = cursor->y + delta.y;
               }
          } break;
          default:
               return MOVEMENT_INVALID;
          }
     }

     return MOVEMENT_COMPLETE;
}

bool is_movement_buffer_full(ConfigState* config_state)
{
     size_t max_movement_keys = sizeof config_state->movement_keys;
     size_t n_movement_keys = strnlen(config_state->movement_keys, max_movement_keys);
     return n_movement_keys == max_movement_keys;
}

bool key_handler(int key, BufferNode* head, void* user_data)
{
     ConfigState* config_state = user_data;
     Buffer* buffer = config_state->view_current->buffer_node->buffer;
     BufferState* buffer_state = buffer->user_data;
     BufferView* buffer_view = config_state->view_current;
     Point* cursor = &config_state->view_current->cursor;
     config_state->last_key = key;

     if(config_state->insert){
          assert(config_state->command_key == '\0');
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
          if(config_state->command_key == '\0' && config_state->command_multiplier == 0){
               // this is the first key entered

               if(key >='1' && key <= '9'){
                    // this key is part of a command multiplier
                    config_state->command_multiplier *= 10;
                    config_state->command_multiplier += key - '0';
                    return true;
               }
               else{
                    // this key is a command
                    config_state->command_key = key;
                    config_state->command_multiplier = 1;
               }
          }
          else if(config_state->command_key == '\0'){
               // the previous key was part of a command multiplier
               assert(config_state->command_multiplier != 0);

               if(key >='0' && key <= '9'){
                    // this key is part of a command multiplier
                    config_state->command_multiplier *= 10;
                    config_state->command_multiplier += key - '0';
                    return true;
               }
               else {
                    // this key is a command
                    config_state->command_key = key;
               }
          }
          else if(config_state->movement_keys[0] == '\0' && config_state->movement_multiplier == 0){
               // this is the first key entered after the command
               assert(config_state->command_multiplier != 0);
               assert(config_state->command_key != '\0');

               if(key >='1' && key <= '9'){
                    // this key is part of a movement multiplier
                    config_state->movement_multiplier *= 10;
                    config_state->movement_multiplier += key - '0';
                    return true;
               }
               else{
                    // this key is a part of a movement
                    config_state->movement_keys[0] = key;
                    config_state->movement_multiplier = 1;
               }
          }
          else if(config_state->movement_keys[0] == '\0'){
               // the previous key was part of a movement multiplier
               assert(config_state->command_multiplier != 0);
               assert(config_state->command_key != '\0');
               assert(config_state->movement_multiplier != 0);

               if(key >='0' && key <= '9'){
                    // this key is part of a movement multiplier
                    config_state->movement_multiplier *= 10;
                    config_state->movement_multiplier += key - '0';
                    return true;
               }
               else {
                    // this key is part of a movement
                    config_state->movement_keys[0] = key;
               }
          }
          else {
               // the previous key was part of a movement
               assert(config_state->command_multiplier != 0);
               assert(config_state->command_key != '\0');
               assert(config_state->movement_multiplier != 0);
               assert(config_state->movement_keys[0] != '\0');

               // this key is part of a movement
               assert(!is_movement_buffer_full(config_state));
               config_state->movement_keys[strlen(config_state->movement_keys)] = key;
          }

          assert(config_state->command_multiplier != 0);
          assert(config_state->command_key != '\0');
          // The movement (and its multiplier) may or may not be set at this point. Some commands, like 'G', don't
          // require a movement (or a movement multiplier). Other commands, like 'd', require a movement and therefore
          // must handle the case a movement is not available yet.

          Point movement_start, movement_end;

          switch(config_state->command_key){
          case 27: // ESC
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
          case 'k':
          case 'h':
          case 'l':
          {
               // h,j,k,l are both commands and movements. We handle the command case here, but leverage the movement
               // logic to avoid code duplication.
               config_state->movement_keys[0] = key;
               config_state->movement_multiplier = 1;

               for(size_t cm = 0; cm < config_state->command_multiplier; cm++){
                    movement_state_t m_state = try_generic_movement(config_state, buffer, cursor, &movement_start, &movement_end);
                    assert(m_state == MOVEMENT_COMPLETE);

                    // this is a generic movement
                    ce_set_cursor(buffer, cursor, &movement_end);
               }
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
               Point begin_line = {0, cursor->y};
               if(ce_insert_char(buffer, &begin_line, '\n')){
                    ce_commit_insert_char(&buffer_state->commit_tail, &begin_line, cursor, &begin_line, '\n');
                    cursor->x = 0;
                    enter_insert_mode(config_state, cursor);
               }
          } break;
          case 'o':
          {
               Point end_of_line = *cursor;
               end_of_line.x = strlen(buffer->lines[cursor->y]);

               if(ce_insert_char(buffer, &end_of_line, '\n')){
                    Point next_cursor = {0, cursor->y+1};
                    ce_commit_insert_char(&buffer_state->commit_tail, &end_of_line, cursor, &next_cursor, '\n');
                    *cursor = next_cursor;
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
               char mark = config_state->movement_keys[0];
               switch(mark){
               case MOVEMENT_CONTINUE:
                    return true; // no movement yet, wait for one!
               default:
                    add_mark(buffer_state, mark, cursor);
                    break;
               }
          } break;
          case '\'':
          {
               Point* marked_location;
               char mark = config_state->movement_keys[0];
               switch(mark){
               case MOVEMENT_CONTINUE:
                    return true; // no movement yet, wait for one!
               default:
                    marked_location = find_mark(buffer_state, mark);
                    if(marked_location) cursor->y = marked_location->y;
                    break;
               }
          } break;
          case 'a':
          {
               if(buffer->lines[cursor->y] && cursor->x < (int64_t)(strlen(buffer->lines[cursor->y]))){
                    cursor->x++;
               }
               enter_insert_mode(config_state, cursor);
          } break;
          case 'y':
          {
               movement_state_t m_state = try_generic_movement(config_state, buffer, cursor, &movement_start, &movement_end);
               switch(m_state){
               case MOVEMENT_CONTINUE:
                    return true; // no movement yet, wait for one!
               case MOVEMENT_COMPLETE:
                    add_yank(buffer_state, '0', ce_dupe_string(buffer, &movement_start, &movement_end), YANK_NORMAL);
                    add_yank(buffer_state, '"', ce_dupe_string(buffer, &movement_start, &movement_end), YANK_NORMAL);
                    break;
               case MOVEMENT_INVALID:
                    switch(config_state->movement_keys[0]){
                    case 'y':
                         add_yank(buffer_state, '0', strdup(buffer->lines[cursor->y]), YANK_LINE);
                         add_yank(buffer_state, '"', strdup(buffer->lines[cursor->y]), YANK_LINE);
                         break;
                    default:
                         break;
                    }
                    break;
               }
          } break;
          case 'p':
          {
               YankNode* yank = find_yank(buffer_state, '"');
               if(yank){
                    switch(yank->mode){
                    case YANK_LINE:
                    {
                         // TODO: bring this all into a ce_commit_insert_line function
                         Point new_line_begin = {0, cursor->y+1};
                         Point cur_line_end = {strlen(buffer->lines[cursor->y]), cursor->y};
                         size_t len = strlen(yank->text) + 1; // account for newline
                         char* save_str = malloc(len + 1);
                         save_str[0] = '\n'; // prepend a new line to create a line
                         memcpy(save_str + 1, yank->text, len);
                         bool res = ce_insert_string(buffer, &cur_line_end, save_str);
                         assert(res);
                         ce_commit_insert_string(&buffer_state->commit_tail,
                                                 &cur_line_end, cursor, &new_line_begin,
                                                 save_str);
                         ce_set_cursor(buffer, cursor, &new_line_begin);
                    } break;
                    case YANK_NORMAL:
                         if(strnlen(buffer->lines[cursor->y], 1)){
                              cursor->x++; // don't increment x for blank lines
                         } else assert(cursor->x == 0);

                         ce_insert_string(buffer, cursor, yank->text);
                         ce_commit_insert_string(&buffer_state->commit_tail,
                                                 cursor, cursor, cursor,
                                                 strdup(yank->text));
                         break;
                    }
               }
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
                    add_yank(buffer_state, '"', strdup(save_string), YANK_NORMAL);
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
                         add_yank(buffer_state, '"', strdup(save_string), YANK_NORMAL);
                    }
               }
               if(key == 'C') enter_insert_mode(config_state, cursor);
          } break;
          case 'c':
          case 'd':
          {
               for(size_t cm = 0; cm < config_state->command_multiplier; cm++){
                    movement_state_t m_state = try_generic_movement(config_state, buffer, cursor, &movement_start, &movement_end);
                    if(m_state == MOVEMENT_CONTINUE) return true;

                    YankMode yank_mode = YANK_NORMAL;
                    if(m_state == MOVEMENT_INVALID){
                         switch(config_state->movement_keys[0]){
                              case 'c':
                                   movement_start = *cursor;
                                   ce_move_cursor_to_soft_beginning_of_line(buffer, &movement_start);
                                   movement_end = (Point) {strlen(buffer->lines[cursor->y]), cursor->y}; // TODO: causes ce_dupe_string to fail (not on buffer)
                                   yank_mode = YANK_LINE;
                                   break;
                              case 'd':
                              {
                                   Point delta = {-cursor->x, 0};
                                   ce_move_cursor(buffer, cursor, &delta);
                                   movement_start = (Point) {0, cursor->y};
                                   movement_end = (Point) {strlen(buffer->lines[cursor->y])+1, cursor->y}; // TODO: causes ce_dupe_string to fail (not on buffer)
                                   yank_mode = YANK_LINE;
                              } break;
                              default:
                                   // not a valid movement
                                   clear_keys(config_state);
                                   return true;
                         }
                    }

                    // this is a generic movement
                    assert(movement_end.x >= movement_start.x);
                    assert(movement_start.y == movement_end.y); // TODO support deleting over line boundaries

                    // generic movement behavior override
                    if(config_state->movement_keys[0] == '%')
                         movement_end.x++; // include matched char

                    // delete all chars movement_start..movement_end inclusive
                    int64_t n_deletes = ce_compute_length(&movement_start, &movement_end);
                    char* save_string = (config_state->movement_keys[0] == 'd') ?
                         strdup(buffer->lines[movement_start.y]) :
                         ce_dupe_string(buffer, &movement_start, &movement_end);
                    if(ce_remove_string(buffer, &movement_start, n_deletes)){
                         ce_commit_remove_string(&buffer_state->commit_tail, &movement_start, cursor, &movement_start, save_string);
                         add_yank(buffer_state, '"', strdup(save_string), yank_mode);
                    }

                    ce_set_cursor(buffer, cursor, &movement_start);
               }
               if(config_state->command_key=='c') enter_insert_mode(config_state,cursor);

          } break;
          case 's':
          {
               char c;
               if(ce_get_char(buffer, cursor, &c) && ce_remove_char(buffer, cursor)){
                    ce_commit_remove_char(&buffer_state->commit_tail, cursor, cursor, cursor, c);
               }
               enter_insert_mode(config_state, cursor);
          } break;
          case 'Z':
               switch(config_state->movement_keys[0]){
               case MOVEMENT_CONTINUE:
                    // no movement yet, wait for one!
                    return true;
               case 'Z':
                    ce_save_buffer(buffer, buffer->filename);
                    clear_keys(config_state);
                    return false; // quit
               default:
                    break;
               }
          case '':
               ce_save_buffer(buffer, buffer->filename);
               break;
          case 'v':
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
          case '':
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
          {
               if(buffer_state->commit_tail && buffer_state->commit_tail->next){
                    ce_commit_redo(buffer, &buffer_state->commit_tail, cursor);
               }
          } break;
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
               // return if we're still waiting on a movement
               if(config_state->movement_keys[0] == MOVEMENT_CONTINUE) return true;
               for(size_t cm = 0; cm < config_state->command_multiplier; cm++){
                    config_state->find_command.command_key = config_state->command_key;
                    config_state->find_command.find_char = config_state->movement_keys[0];
                    find_command(config_state->command_key, config_state->movement_keys[0], buffer, cursor);
               }
          } break;
          case 'r':
          {
               switch(config_state->movement_keys[0]){
               case MOVEMENT_CONTINUE:
                    // no movement yet, wait for one!
                    return true;
               default:
                    ce_set_char(buffer, cursor, key);
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
               Point location;
               switch(config_state->movement_keys[0]){
               case MOVEMENT_CONTINUE:
                    // no movement yet, wait for one!
                    return true;
               case 't':
                    location = (Point) {0, cursor->y};
                    scroll_view_to_location(config_state->view_current, &location);
                    break;
               case 'z':
                    // center view on cursor
                    location = (Point) {0, cursor->y - (g_terminal_dimensions->y/2)};
                    scroll_view_to_location(config_state->view_current, &location);
                    break;
               case 'b':
                    // move current line to bottom of view
                    location = (Point) {0, cursor->y - g_terminal_dimensions->y};
                    scroll_view_to_location(config_state->view_current, &location);
                    break;
               }
          } break;
          case 'G':
          {
               Point delta = {0, buffer->line_count - cursor->y};
               ce_move_cursor(buffer, cursor, &delta);
          } break;
          case '>':
          {
               switch(config_state->movement_keys[0]){
               case MOVEMENT_CONTINUE:
                    // no movement yet, wait for one!
                    return true;
               case '>':
               {
                    Point loc = {0, cursor->y};
                    ce_insert_string(buffer, &loc, TAB_STRING);
                    ce_commit_insert_string(&buffer_state->commit_tail, &loc, cursor, cursor, strdup(TAB_STRING));
               } break;
               }
          } break;
          case 'g':
          {
               switch(config_state->movement_keys[0]){
               case MOVEMENT_CONTINUE:
                    // no movement yet, wait for one!
                    return true;
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
               if(config_state->movement_keys[0] != MOVEMENT_CONTINUE){
                    int64_t begin_format_line;
                    int64_t end_format_line;
                    switch(key){
                    case '=':
                         begin_format_line = cursor->y;
                         end_format_line = cursor->y;
                         break;
                    case 'G':
                         begin_format_line = cursor->y;
                         end_format_line = buffer->line_count-1;
                         break;
                    }
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
                         asprintf(&line_arg, "-lines=%"PRId64":%"PRId64, begin_format_line+1, end_format_line+1);

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
                    for(int64_t i = buffer->line_count - 1; i >= 0; i--){
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

          // if we've made it to here, the command is complete
          clear_keys(config_state);
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
     mvprintw(g_terminal_dimensions->y - 1, 0, "%s %s %ld lines, k %d, c %ld, %ld, v %ld, %ld -> %ld, %ld t: %ld, %ld",
              config_state->insert ? "INSERT" : "NORMAL", buffer->filename, buffer->line_count, config_state->last_key,
              cursor->x, cursor->y, buffer_view->top_left.x, buffer_view->top_left.y, buffer_view->bottom_right.x, buffer_view->bottom_right.y, g_terminal_dimensions->x, g_terminal_dimensions->y);
     attroff(A_REVERSE);

     // reset the cursor
     move(cursor->y - buffer_view->top_row + buffer_view->top_left.y,
          cursor->x - buffer_view->left_column + buffer_view->top_left.x);
}
