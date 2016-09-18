#include "ce.h"
#include "assert.h"

#include <assert.h>
#include <ctype.h>
#include <unistd.h>

typedef struct{
     bool insert;
     bool split;
     int last_key;
     char command_key; // TODO: make a command string for multi-character commands
     struct {
          // state for fF and tT
          char command_key;
          char find_char;
     } find_command;
     BufferNode* current_buffer_node;
     Point start_insert;
} ConfigState;

typedef struct{
     Point cursor;
     int64_t start_line;
     int64_t left_collumn;
     BufferChangeNode* changes_tail;
} BufferState;

bool initialize_buffer(Buffer* buffer){
     BufferState* buffer_state = malloc(sizeof(*buffer_state));
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
     Buffer* buffer = malloc(sizeof(*buffer));
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
          case 'b':
          case 'B':
          {
               cursor->x -= ce_find_beginning_of_word(buffer, cursor, key == 'b');
          } break;
          case 'e':
          case 'E':
          {
               cursor->x += ce_find_end_of_word(buffer, cursor, key == 'e');
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
          case '^':
          {
               ce_move_cursor_to_soft_beginning_of_line(buffer, cursor);
          } break;
          case '0':
               cursor->x = 0;
               break;
          case '$':
               cursor->x += ce_find_end_of_line(buffer, cursor);
               break;
          case 'A':
          {
               cursor->x += ce_find_end_of_line(buffer, cursor) + 1;
               enter_insert_mode(config_state, cursor);
          } break;
          case 'm':
          {
               FILE* pfile = popen("echo justin stinkkkks", "r");
               char* pstr = str_from_file_stream(pfile);
               config_state->current_buffer_node = new_buffer_from_string(head, "test_buffer", pstr);
               free(pstr);
               pclose(pfile);
               break;
          }
          case 'a':
               if(buffer->lines[cursor->y]){
                    cursor->x++;
               }
               enter_insert_mode(config_state, cursor);
               break;
          case 'c':
          case 'd':
               COMMAND{
                    if(key == config_state->command_key){ // cc or dd
                         // delete line
                         if(buffer->line_count){
                              if(ce_remove_line(buffer, cursor->y)){
                                   // TODO more explicit method to put cursor on the line
                                   Point delta = {0, 0};
                                   ce_move_cursor(buffer, cursor, &delta);
                              }
                         }
                    }
                    // TODO: provide a vim movement function which gives you
                    // the appropriate delta for the movement key sequence and
                    // use that everywhere
                    else if(key == 'e' || key == 'E'){
                         int64_t n_deletes = ce_find_end_of_word(buffer, cursor, key == 'e') + 1;
                         while(n_deletes){
                              ce_remove_char(buffer, cursor);
                              n_deletes--;
                         }
                    }
                    else if(key == 'b' || key == 'B'){
                         int64_t n_deletes = ce_find_beginning_of_word(buffer, cursor, key == 'b');
                         while(n_deletes){
                              cursor->x--;
                              ce_remove_char(buffer, cursor);
                              n_deletes--;
                         }
                    }

                    if(config_state->command_key == 'c') enter_insert_mode(config_state, cursor);
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
               config_state->split = !config_state->split;
               break;
          case 'g':
               COMMAND{
                    if(key == 't'){
                         config_state->current_buffer_node = config_state->current_buffer_node->next;
                         if(!config_state->current_buffer_node){
                              config_state->current_buffer_node = head;
                         }
                    }
                    config_state->command_key = '\0';
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
               COMMAND{
                    config_state->find_command.command_key = config_state->command_key;
                    config_state->find_command.find_char = key;
                    find_command(config_state->command_key, key, buffer, cursor);
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
              buffer->name, buffer->line_count, config_state->last_key);
     attroff(A_REVERSE);

     // reset the cursor
     move(buffer_state->cursor.y - buffer_top_left.y, buffer_state->cursor.x - buffer_top_left.x);
}
