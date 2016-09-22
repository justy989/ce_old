#include "ce.h"

#include <assert.h>
#include <ctype.h>
#define _XOPEN_SOURCE 500
#define __USE_XOPEN_EXTENDED
#define __USE_GNU
#include <ftw.h>
#include <inttypes.h>
#include <unistd.h>

typedef struct{
     bool insert;
     bool split;
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
     BufferNode* current_buffer_node;
     Point start_insert;
} ConfigState;

typedef struct MarkNode{
     char reg_char;
     Point location;
     struct MarkNode* next;
}MarkNode;

typedef struct{
     Point cursor;
     int64_t start_line;
     int64_t left_column;
     BufferChangeNode* changes_tail;
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
     buffer_state->cursor.x = 0;
     buffer_state->cursor.y = 0;
     buffer_state->start_line = 0;
     buffer_state->changes_tail = NULL;
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

bool initializer(BufferNode* head, Point* terminal_dimensions, int argc, char** argv, void** user_data)
{
     // NOTE: need to set these in this module
     g_message_buffer = head->buffer;
     g_terminal_dimensions = terminal_dimensions;

     // setup the config's state
     ConfigState* config_state = calloc(1, sizeof(*config_state));
     if(!config_state) return false;

     config_state->current_buffer_node = head;
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

// location is {left_column, top_line} for the view
void scroll_view_to_location(BufferState* buffer_state, const Point* location){
     // TODO: we should be able to scroll the view above our first line
     buffer_state->left_column = (location->x >= 0) ? location->x : 0;
     buffer_state->start_line = (location->y >= 0) ? location->y : 0;
}


void find_command(int command_key, int find_char, Buffer* buffer, Point* cursor)
{
     switch(command_key){
     case 'f':
     {
          int64_t x_delta = ce_find_char_forward_in_line(buffer, cursor, find_char);
          if(x_delta == -1) break;
          Point delta = {x_delta, 0};
          ce_move_cursor(buffer, cursor, &delta);
     } break;
     case 't':
     {
          Point search_point = {cursor->x + 1, cursor->y};
          int64_t x_delta = ce_find_char_forward_in_line(buffer, &search_point, find_char);
          if(x_delta <= 0) break;
          Point delta = {x_delta, 0};
          ce_move_cursor(buffer, cursor, &delta);
     } break;
     case 'F':
     {
          int64_t x_delta = ce_find_char_backward_in_line(buffer, cursor, find_char);
          if(x_delta == -1) break;
          Point delta = {-x_delta, 0};
          ce_move_cursor(buffer, cursor, &delta);
     } break;
     case 'T':
     {
          Point search_point = {cursor->x - 1, cursor->y};
          int64_t x_delta = ce_find_char_backward_in_line(buffer, &search_point, find_char);
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
}

void clear_keys(ConfigState* config_state)
{
     config_state->command_multiplier = 0;
     config_state->command_key = '\0';
     config_state->movement_multiplier = 0;
     memset(config_state->movement_keys, 0, sizeof config_state->movement_keys);
}

// returns true if key may be interpretted as a multiplier given the current mulitplier state
bool key_is_multiplier(uint64_t multiplier, char key)
{
     return (key >='1' && key <= '9') || (key == '0' && multiplier > 0);
}

typedef enum{
     CE_MOVEMENT_COMPLETE,
     CE_MOVEMENT_CONTINUE = '\0',
     CE_MOVEMENT_INVALID
} movement_state_t;

// return false invalid/incomplete movement in config state, otherwise returns true + moves cursor + movement_end
movement_state_t try_generic_movement(ConfigState* config_state, Buffer* buffer, Point* cursor, Point* movement_end)
{
     *movement_end = *cursor;

     char key0 = config_state->movement_keys[0];
     char key1 = config_state->movement_keys[1];
     uint64_t multi = config_state->movement_multiplier;

     switch(key0){
     case 'i':
          switch(key1){
#if 0
          case 'w':
               cursor->x -= ce_find_beginning_of_word(buffer, movement_start, true);
               for(size_t i=0; i<config_state->movement_multiplier; i++){
                    movement_end->x += ce_find_end_of_word(buffer, movement_end, true);
               }
               break;
#endif
          case '\0':
               return CE_MOVEMENT_CONTINUE;
          default:
               return CE_MOVEMENT_INVALID;
          }
          break;
     case 'e':
     case 'E':
          for(size_t mm=0; mm < multi; mm++)
               movement_end->x += ce_find_end_of_word(buffer, movement_end, key0 == 'e') + 1;
          break;
     case 'b':
     case 'B':
          for(size_t mm=0; mm < multi; mm++)
               cursor->x -= ce_find_beginning_of_word(buffer, cursor, key0 == 'b');
          break;
     case 'w':
     case 'W':
          for(size_t mm=0; mm < multi; mm++)
               movement_end->x += ce_find_next_word(buffer, movement_end, key0 == 'w');
          break;
     case '$':
          for(size_t mm=0; mm < multi; mm++)
               movement_end->x += ce_find_end_of_line(buffer, movement_end) + 1;
          break;
#if 0
     // RDA disabling for now
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
#endif
     default:
          return CE_MOVEMENT_INVALID;
     }

     return CE_MOVEMENT_COMPLETE;
}

bool is_movement_buffer_full(ConfigState* config_state)
{
     size_t max_movement_keys = sizeof config_state->movement_keys;
     size_t n_movement_keys = strnlen(config_state->movement_keys, max_movement_keys);
     return n_movement_keys == max_movement_keys;
}

bool key_handler(int key, BufferNode* head, void* user_data)
{
     (void) head; //RDA
     ConfigState* config_state = user_data;
     Buffer* buffer = config_state->current_buffer_node->buffer;
     BufferState* buffer_state = buffer->user_data;
     Point* cursor = &buffer_state->cursor;

     config_state->last_key = key;

     if(config_state->insert){
          assert(config_state->command_key == '\0');
          switch(key){
          case 27: // escape
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
               break;
          case 127: // backspace
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
               break;
          case 10: // add empty line
               if(!buffer->line_count){
                    ce_alloc_lines(buffer, 1);
               }
               if(ce_insert_newline(buffer, cursor->y + 1)){
                    cursor->y++;
                    cursor->x = 0;
               }
               break;
          default:
               if(ce_insert_char(buffer, cursor, key)) cursor->x++;
               break;
          }
     }else{
          Point movement_end;

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
               // this key is part of a movement
               assert(!is_movement_buffer_full(config_state));
               config_state->movement_keys[strlen(config_state->movement_keys)] = key;
          }

          assert(config_state->command_key != '\0');

          switch(config_state->command_key){
          case 27:
          default:
               // escape and invalid movements cancel a command
               clear_keys(config_state);
               break;
          case 'q':
               return false; // exit !
          case 'j':
          {
               Point delta = {0, 1};
               ce_move_cursor(buffer, cursor, &delta);
               clear_keys(config_state);
          } break;
          case 'k':
          {
               Point delta = {0, -1};
               ce_move_cursor(buffer, cursor, &delta);
               clear_keys(config_state);
          } break;
          case 'b':
          case 'B':
          {
               cursor->x -= ce_find_beginning_of_word(buffer, cursor, key == 'b');
               clear_keys(config_state);
          } break;
          case 'e':
          case 'E':
          {
               cursor->x += ce_find_end_of_word(buffer, cursor, key == 'e');
               clear_keys(config_state);
          } break;
          case 'w':
          case 'W':
          {
               cursor->x += ce_find_next_word(buffer, cursor, key == 'w');
               clear_keys(config_state);
          } break;
          case 'h':
          {
               Point delta = {-1, 0};
               ce_move_cursor(buffer, cursor, &delta);
               clear_keys(config_state);
          } break;
          case 'l':
          {
               Point delta = {1, 0};
               ce_move_cursor(buffer, cursor, &delta);
               clear_keys(config_state);
          } break;
          case 'i':
               enter_insert_mode(config_state, cursor);
               clear_keys(config_state);
               clear_keys(config_state);
               break;
          case 'I':
          {
               ce_move_cursor_to_soft_beginning_of_line(buffer, cursor);
               enter_insert_mode(config_state, cursor);
               clear_keys(config_state);
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
               clear_keys(config_state);
          } break;
          case '^':
          {
               ce_move_cursor_to_soft_beginning_of_line(buffer, cursor);
               clear_keys(config_state);
          } break;
          case '0':
               cursor->x = 0;
               clear_keys(config_state);
               break;
          case '$':
               cursor->x += ce_find_end_of_line(buffer, cursor);
               clear_keys(config_state);
               break;
          case 'A':
          {
               cursor->x += ce_find_end_of_line(buffer, cursor) + 1;
               enter_insert_mode(config_state, cursor);
               clear_keys(config_state);
          } break;
          case 'm':
          {
               char mark = config_state->movement_keys[0];
               if(mark != '\0'){
                    add_mark(buffer_state, mark, cursor);
                    clear_keys(config_state);
               }
          } break;
          case '\'':
          {
               char mark = config_state->movement_keys[0];
               if(mark != '\0'){
                    Point* marked_location = find_mark(buffer_state, key);
                    cursor->y = marked_location->y;
                    clear_keys(config_state);
               }
          } break;
          case 'a':
               if(buffer->lines[cursor->y]){
                    cursor->x++;
               }
               enter_insert_mode(config_state, cursor);
               clear_keys(config_state);
               break;
          case 'C':
          case 'D':
          {
               int64_t n_deletes = ce_find_end_of_line(buffer, cursor) + 1;
               while(n_deletes){
                    ce_remove_char(buffer, cursor);
                    n_deletes--;
               }
               if(key == 'C') enter_insert_mode(config_state, cursor);
               clear_keys(config_state);
          } break;
          case 'S':
          {
               // TODO: unify with cc
               ce_move_cursor_to_soft_beginning_of_line(buffer, cursor);
               int64_t n_deletes = ce_find_end_of_line(buffer, cursor) + 1;
               while(n_deletes){
                    ce_remove_char(buffer, cursor);
                    n_deletes--;
               }
               enter_insert_mode(config_state, cursor);
               clear_keys(config_state);
          } break;
          case 'c':
          {
               for(size_t cm=0; cm<config_state->command_multiplier; cm++){
                    if(try_generic_movement(config_state, buffer, cursor, &movement_end)){
                         // this is a generic movement
                         assert(movement_end.x >= cursor->x);
                         size_t n_deletes = movement_end.x - cursor->x;

                         for(size_t i=0; i < n_deletes; i++){
                              ce_remove_char(buffer, cursor);
                         }
                         enter_insert_mode(config_state, cursor);
                    }
                    else if(config_state->movement_keys[0] == 'c'){
                         // custom movement: delete line
                         if(buffer->line_count){
                              if(ce_remove_line(buffer, cursor->y)){
                                   // TODO more explicit method to put cursor on the line
                                   Point delta = {0, 0};
                                   ce_move_cursor(buffer, cursor, &delta);
                              }
                         }
                         enter_insert_mode(config_state, cursor);
                    }
                    else if(is_movement_buffer_full(config_state)){
                         // not a valid movement
                         clear_keys(config_state);
                         return true;
                    }
               }
          } break;
          case 'd':
          {
               for(size_t cm = 0; cm < config_state->command_multiplier; cm++){
                    movement_state_t m_state = try_generic_movement(config_state, buffer, cursor, &movement_end);
                    if(m_state == CE_MOVEMENT_CONTINUE) return true;

                    if(m_state == CE_MOVEMENT_INVALID){
                         switch(config_state->movement_keys[0]){
                              case 'd':
                              {
                                   // TODO: fill out movement_start and movement_end
                                   Point delta = {-cursor->x, 0};
                                   ce_move_cursor(buffer, cursor, &delta);
                              } break;
                              default:
                                   // not a valid movement
                                   clear_keys(config_state);
                                   return true;
                         }
                    }
                    // this is a generic movement
                    assert(movement_end.x >= cursor->x);
                    size_t n_deletes = movement_end.x - cursor->x;

                    for(size_t i=0; i < n_deletes ; i++){
                         ce_remove_char(buffer, cursor);
                    }
               }

               clear_keys(config_state);
          } break;
          case 's':
               ce_remove_char(buffer, cursor);
               enter_insert_mode(config_state, cursor);
               clear_keys(config_state);
               break;
          case '':
               ce_save_buffer(buffer, buffer->filename);
               clear_keys(config_state);
               break;
          case 'v':
               config_state->split = !config_state->split;
               clear_keys(config_state);
               break;
          case 'u':
               if(buffer_state->changes_tail){
                    Point new_cursor = buffer_state->changes_tail->change.cursor;
                    if(ce_buffer_undo(buffer, &buffer_state->changes_tail)){
                         *cursor = new_cursor;
                    }
               }
               clear_keys(config_state);
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
               clear_keys(config_state);
          }
          break;
          case ';':
          {
               if(config_state->find_command.command_key == '\0') break;
               find_command(config_state->find_command.command_key,
                            config_state->find_command.find_char, buffer, cursor);
               clear_keys(config_state);
          } break;
          case ',':
          {
               if(config_state->find_command.command_key == '\0') break;
               char command_key = config_state->find_command.command_key;
               if(isupper(command_key)) command_key = tolower(command_key);
               else command_key = toupper(command_key);

               find_command(command_key, config_state->find_command.find_char, buffer, cursor);
               clear_keys(config_state);
          } break;
#if 0
// RDA
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
#endif
          case 'H':
          {
               // move cursor to top line of view
               // TODO: sometimes it would be nicer to work in absolutes instead of in deltas
               Point delta = {0, buffer_state->start_line - cursor->y};
               ce_move_cursor(buffer, cursor, &delta);
               clear_keys(config_state);
          } break;
          case 'M':
          {
               // move cursor to middle line of view
               // TODO: sometimes it would be nicer to work in absolutes instead of in deltas
               Point delta = {0, (buffer_state->start_line + (g_terminal_dimensions->y - 1) / 2)
                    - cursor->y};
               ce_move_cursor(buffer, cursor, &delta);
               clear_keys(config_state);
          } break;
          case 'L':
          {
               // move cursor to bottom line of view
               Point delta = {0, (buffer_state->start_line + g_terminal_dimensions->y - 1)
                    - (cursor->y + 1)};
               ce_move_cursor(buffer, cursor, &delta);
               clear_keys(config_state);
          } break;
#if 0
// RDA
          case 'z':
          {
               if(should_handle_command(config_state, key)){
                    switch(key){
                    case 't':
                    {
                         Point location = {0, cursor->y};
                         scroll_view_to_location(buffer_state, &location);
                    } break;
                    case 'z':
                    {
                         // center view on cursor
                         Point location = {0, cursor->y - (g_terminal_dimensions->y/2)};
                         scroll_view_to_location(buffer_state, &location);
                    } break;
                    case 'b':
                    {
                         // move current line to bottom of view
                         Point location = {0, cursor->y - g_terminal_dimensions->y};
                         scroll_view_to_location(buffer_state, &location);
                    } break;
                    }
                    // TODO: devise a better way to clear command_key following a movement
                    config_state->command_key = '\0';
               }
          } break;
#endif
          case 'G':
          {
               Point delta = {0, buffer->line_count - cursor->y};
               ce_move_cursor(buffer, cursor, &delta);
               clear_keys(config_state);
          } break;
//RDA
          case 'g':
          {
               switch(config_state->movement_keys[0]){
               case 'g':
               {
                    Point delta = {0, -cursor->y};
                    ce_move_cursor(buffer, cursor, &delta);
               } break;
               case 't':
               {
                    config_state->current_buffer_node = config_state->current_buffer_node->next;
                    if(!config_state->current_buffer_node){
                         config_state->current_buffer_node = head;
                    }
               } break;
               case 'f':
               {
                    if(!buffer->lines[cursor->y]) break;
                    // TODO: get word under the cursor and unify with '*' impl
                    int64_t word_len = ce_find_end_of_word(buffer, cursor, true)+1;
                    if(buffer->lines[cursor->y][cursor->x+word_len] == '.'){
                         Point ext_start = {cursor->x+word_len, cursor->y};
                         int64_t extension_len = ce_find_end_of_word(buffer, &ext_start, true);
                         if(extension_len != -1) word_len += extension_len+1;
                    }
                    char* filename = alloca(word_len+1);
                    strncpy(filename, &buffer->lines[cursor->y][cursor->x], word_len);
                    filename[word_len] = '\0';

                    BufferNode* file_buffer = open_file_buffer(head, filename);
                    if(file_buffer) config_state->current_buffer_node = file_buffer;
                    else ce_message("file %s not found", filename);
               } break;
               case CE_MOVEMENT_CONTINUE:
                    return true;
               default:
                    // invalid command key
                    clear_keys(config_state);
               }
          } break;
          case '%':
          {
               Point delta;
               if(ce_find_match(buffer, cursor, &delta)){
                    ce_move_cursor(buffer, cursor, &delta);
               }
               clear_keys(config_state);
          } break;
          case '*':
          {
               if(!buffer->lines[cursor->y]) break;
               // TODO: search for the word under the cursor
               int64_t word_len = ce_find_end_of_word(buffer, cursor, true)+1;
               char* search_str = alloca(word_len+1);
               strncpy(search_str, &buffer->lines[cursor->y][cursor->x], word_len);
               search_str[word_len] = '\0';

               Point delta;
               if(ce_find_str(buffer, cursor, search_str, &delta)){
                    ce_move_cursor(buffer, cursor, &delta);
               }
               clear_keys(config_state);
          } break;
          case 21: // Ctrl + d
          {
               Point delta = {0, -g_terminal_dimensions->y / 2};
               ce_move_cursor(buffer, cursor, &delta);
               clear_keys(config_state);
          } break;
          case 4: // Ctrl + u
          {
               Point delta = {0, g_terminal_dimensions->y / 2};
               ce_move_cursor(buffer, cursor, &delta);
               clear_keys(config_state);
          } break;
#if 0
               /* TODO: execute arbitrary shell command */
               FILE* pfile = popen("echo justin stinkkkks", "r");
               char* pstr = str_from_file_stream(pfile);
               config_state->current_buffer_node = new_buffer_from_string(head, "test_buffer", pstr);
               free(pstr);
               pclose(pfile);
#endif
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

     ce_follow_cursor(&buffer_state->cursor, &buffer_state->start_line, &buffer_state->left_column,
                      bottom, right);

     // print the range of lines we want to show
     Point buffer_top_left = {buffer_state->left_column, buffer_state->start_line};
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
     mvprintw(g_terminal_dimensions->y - 1, 0, "%s %s %"PRId64" lines, key %d", config_state->insert ? "INSERT" : "NORMAL",
              buffer->name, buffer->line_count, config_state->last_key);
     attroff(A_REVERSE);

     // reset the cursor
     move(buffer_state->cursor.y - buffer_top_left.y, buffer_state->cursor.x - buffer_top_left.x);
}
