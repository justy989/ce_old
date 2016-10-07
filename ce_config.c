#include "ce.h"
#include <assert.h>
#include <ctype.h>
#include <ftw.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#define TAB_STRING "     "
#define SCROLL_LINES 1

const char* modified_string(Buffer* buffer)
{
     return buffer->modified ? "*" : "";
}

const char* readonly_string(Buffer* buffer)
{
     return buffer->readonly ? "[RO]" : "";
}

int64_t count_digits(int64_t n)
{
     int count = 0;
     while(n > 0){
          n /= 10;
          count++;
     }

     return count;
}

void view_drawer(const BufferNode* head, void* user_data);

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
          ce_message("%s() failed to alloc string", __FUNCTION__);
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

// TODO: move this to ce.h
typedef struct InputHistoryNode {
     char* entry;
     struct InputHistoryNode* next;
     struct InputHistoryNode* prev;
} InputHistoryNode;

typedef struct {
     InputHistoryNode* head;
     InputHistoryNode* tail;
     InputHistoryNode* cur;
} InputHistory;

bool input_history_init(InputHistory* history)
{
     InputHistoryNode* node = calloc(1, sizeof(*node));
     if(!node){
          ce_message("%s() failed to malloc input history node", __FUNCTION__);
          return false;
     }

     history->head = node;
     history->tail = node;
     history->cur = node;

     return true;
}

bool input_history_commit_current(InputHistory* history)
{
     assert(history->tail);
     assert(history->cur);

     InputHistoryNode* node = calloc(1, sizeof(*node));
     if(!node){
          ce_message("%s() failed to malloc input history node", __FUNCTION__);
          return false;
     }

     history->tail->next = node;
     node->prev = history->tail;

     history->tail = node;
     history->cur = node;

     return true;
}

bool input_history_update_current(InputHistory* history, char* duped)
{
     if(!history->cur) return false;
     if(history->cur->entry) free(history->cur->entry);
     history->cur->entry = duped;
     return true;
}

void input_history_free(InputHistory* history)
{
     while(history->head){
          free(history->head->entry);
          InputHistoryNode* tmp = history->head;
          history->head = history->head->next;
          free(tmp);
     }

     history->tail = NULL;
     history->cur = NULL;
}

bool input_history_next(InputHistory* history)
{
     if(!history->cur) return false;
     if(!history->cur->next) return false;

     history->cur = history->cur->next;
     return true;
}

bool input_history_prev(InputHistory* history)
{
     if(!history->cur) return false;
     if(!history->cur->prev) return false;

     history->cur = history->cur->prev;
     return true;
}

typedef enum{
     VM_NORMAL,
     VM_INSERT,
     VM_VISUAL_RANGE,
     VM_VISUAL_LINE,
     VM_VISUAL_BLOCK,
} VimMode;

// TODO: move yank stuff to ce.h
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
     VimMode vim_mode;
     bool input;
     const char* input_message;
     char input_key;
     Buffer* shell_command_buffer; // Allocate so it can be part of the buffer list and get free'd at the end
     Buffer input_buffer;
     Buffer buffer_list_buffer;
     int64_t input_last_error;
     int last_key;
     uint64_t command_multiplier;
     int command_key;
     uint64_t movement_multiplier;
     int movement_keys[2];
     struct {
          // state for fF and tT
          char command_key;
          char find_char;
     } find_command;
     struct {
          Direction direction;
     } search_command;
     Point start_insert;
     Point original_start_insert;
     Point visual_start;
     struct YankNode* yank_head;
     BufferView* view_head;
     BufferView* view_current;
     BufferView* view_input;
     BufferView* view_save;
     InputHistory shell_command_history;
     InputHistory search_history;
     InputHistory load_file_history;
} ConfigState;

typedef struct MarkNode{
     char reg_char;
     Point location;
     struct MarkNode* next;
} MarkNode;

typedef struct{
     BufferCommitNode* commit_tail;
     BackspaceNode* backspace_head;
     struct MarkNode* mark_head;
} BufferState;

YankNode* find_yank(ConfigState* config, char reg_char){
     YankNode* itr = config->yank_head;
     while(itr != NULL){
          if(itr->reg_char == reg_char) return itr;
          itr = itr->next;
     }
     return NULL;
}

// for now the yanked string is user allocated. eventually will probably
// want to change this interface so that everything is hidden
void add_yank(ConfigState* config, char reg_char, const char* yank_text, YankMode mode){
     YankNode* node = find_yank(config, reg_char);
     if(node != NULL){
          free((void*)node->text);
     }
     else{
          YankNode* new_yank = malloc(sizeof(*config->yank_head));
          new_yank->reg_char = reg_char;
          new_yank->next = config->yank_head;
          node = new_yank;
          config->yank_head = new_yank;
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

     if(str){
          ce_load_string(buffer, str);
     }

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

void enter_normal_mode(ConfigState* config_state)
{
     config_state->vim_mode = VM_NORMAL;
}

void enter_insert_mode(ConfigState* config_state, Point* cursor)
{
     if(config_state->view_current->buffer->readonly) return;
     config_state->vim_mode = VM_INSERT;
     config_state->start_insert = *cursor;
     config_state->original_start_insert = *cursor;
}

void enter_visual_range_mode(ConfigState* config_state, BufferView* buffer_view)
{
     config_state->vim_mode = VM_VISUAL_RANGE;
     config_state->visual_start = buffer_view->cursor;
}

void enter_visual_line_mode(ConfigState* config_state, BufferView* buffer_view)
{
     config_state->vim_mode = VM_VISUAL_LINE;
     config_state->visual_start = buffer_view->cursor;
}

void commit_insert_mode_changes(ConfigState* config_state, Buffer* buffer, BufferState* buffer_state, Point* cursor, Point* end_cursor)
{
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
               Point last_inserted_char = {cursor->x, cursor->y};
               ce_advance_cursor(buffer, &last_inserted_char, -1);
               ce_commit_insert_string(&buffer_state->commit_tail,
                                       &config_state->start_insert,
                                       &config_state->original_start_insert,
                                       end_cursor,
                                       ce_dupe_string(buffer, &config_state->start_insert, &last_inserted_char));
          }else if(config_state->start_insert.x < config_state->original_start_insert.x ||
                   config_state->start_insert.y < config_state->original_start_insert.y){
               if(cursor->x == config_state->start_insert.x &&
                  cursor->y == config_state->start_insert.y){
                    // exclusively backspaces!
                    ce_commit_remove_string(&buffer_state->commit_tail,
                                            cursor,
                                            &config_state->original_start_insert,
                                            end_cursor,
                                            backspace_get_string(buffer_state->backspace_head));
                    backspace_free(&buffer_state->backspace_head);
               }else{
                    // mixture of inserts and backspaces
                    Point last_inserted_char = {cursor->x, cursor->y};
                    ce_advance_cursor(buffer, &last_inserted_char, -1);
                    ce_commit_change_string(&buffer_state->commit_tail,
                                            &config_state->start_insert,
                                            &config_state->original_start_insert,
                                            end_cursor,
                                            ce_dupe_string(buffer,
                                                           &config_state->start_insert,
                                                           &last_inserted_char),
                                            backspace_get_string(buffer_state->backspace_head));
                    backspace_free(&buffer_state->backspace_head);
               }
          }
     }
}

void clear_keys(ConfigState* config_state)
{
     config_state->command_multiplier = 0;
     config_state->command_key = '\0';
     config_state->movement_multiplier = 0;
     memset(config_state->movement_keys, 0, sizeof config_state->movement_keys);
}

void input_start(ConfigState* config_state, const char* input_message, char input_key)
{
     ce_clear_lines(config_state->view_input->buffer);
     ce_alloc_lines(config_state->view_input->buffer, 1);
     config_state->view_input->cursor = (Point){0, 0};
     config_state->input_message = input_message;
     config_state->input_key = input_key;
     config_state->view_save = config_state->view_current;
     config_state->view_current = config_state->view_input;
     enter_insert_mode(config_state, &config_state->view_input->cursor);
}

#define input_end() ({ \
     config_state->input = false;\
     config_state->view_current = config_state->view_save; \
     buffer = config_state->view_current->buffer; \
     buffer_state = buffer->user_data; \
     buffer_view = config_state->view_current; \
     cursor = &config_state->view_current->cursor; \
     config_state->input_last_error = 0; \
 })

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

Buffer* open_file_buffer(BufferNode* head, const char* filename)
{
     BufferNode* itr = head;
     while(itr){
          if(!strcmp(itr->buffer->name, filename)){
               return itr->buffer; // already open
          }
          itr = itr->next;
     }

     if(access(filename, F_OK) == 0){
          BufferNode* node = new_buffer_from_file(head, filename);
          if(node) return node->buffer;
     }

     // clang doesn't support nested functions so we need to deal with global state
     nftw_state.search_filename = filename;
     nftw_state.head = head;
     nftw_state.new_node = NULL;
     nftw(".", nftw_find_file, 20, FTW_CHDIR);
     if(nftw_state.new_node) return nftw_state.new_node->buffer;

     ce_message("file %s not found", filename); 

     return NULL;
}

bool initializer(BufferNode* head, Point* terminal_dimensions, int argc, char** argv, void** user_data)
{
     // NOTE: need to set these in this module
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

     config_state->view_input = calloc(1, sizeof(*config_state->view_input));
     if(!config_state->view_input){
          ce_message("failed to allocate buffer view for input");
          return false;
     }

     // setup input buffer
     ce_alloc_lines(&config_state->input_buffer, 1);
     initialize_buffer(&config_state->input_buffer);
     config_state->input_buffer.name = strdup("input");
     config_state->view_input->buffer = &config_state->input_buffer;

     // setup buffer list buffer
     config_state->buffer_list_buffer.name = strdup("buffers");
     config_state->buffer_list_buffer.readonly = true;

     // setup shell command buffer
     config_state->shell_command_buffer = calloc(1, sizeof(*config_state->shell_command_buffer));
     config_state->shell_command_buffer->name = strdup("shell output");
     initialize_buffer(config_state->shell_command_buffer);
     config_state->shell_command_buffer->readonly = true;
     BufferNode* new_buffer_node = ce_append_buffer_to_list(head, config_state->shell_command_buffer);
     if(!new_buffer_node){
          ce_message("failed to add shell command buffer to list");
          return false;
     }

     *user_data = config_state;

     BufferNode* itr = head;

     // setup state for each buffer
     while(itr){
          initialize_buffer(itr->buffer);
          itr = itr->next;
     }

     config_state->view_head->buffer = head->buffer;
     config_state->view_current = config_state->view_head;

     for(int i = 0; i < argc; ++i){
          BufferNode* node = new_buffer_from_file(head, argv[i]);

          // if we loaded a file, set the view to point at the file
          if(i == 0 && node) config_state->view_current->buffer = node->buffer;
     }

     input_history_init(&config_state->shell_command_history);
     input_history_init(&config_state->search_history);
     input_history_init(&config_state->load_file_history);

     // setup colors for syntax highlighting
     init_pair(S_NORMAL, COLOR_FOREGROUND, COLOR_BACKGROUND);
     init_pair(S_KEYWORD, COLOR_BLUE, COLOR_BACKGROUND);
     init_pair(S_COMMENT, COLOR_GREEN, COLOR_BACKGROUND);
     init_pair(S_STRING, COLOR_RED, COLOR_BACKGROUND);
     init_pair(S_CONSTANT, COLOR_MAGENTA, COLOR_BACKGROUND);
     init_pair(S_PREPROCESSOR, COLOR_YELLOW, COLOR_BACKGROUND);
     init_pair(S_DIFF_ADD, COLOR_GREEN, COLOR_BACKGROUND);
     init_pair(S_DIFF_REMOVE, COLOR_RED, COLOR_BACKGROUND);

     init_pair(S_NORMAL_HIGHLIGHTED, COLOR_FOREGROUND, COLOR_BRIGHT_BLACK);
     init_pair(S_KEYWORD_HIGHLIGHTED, COLOR_BLUE, COLOR_BRIGHT_BLACK);
     init_pair(S_COMMENT_HIGHLIGHTED, COLOR_GREEN, COLOR_BRIGHT_BLACK);
     init_pair(S_STRING_HIGHLIGHTED, COLOR_RED, COLOR_BRIGHT_BLACK);
     init_pair(S_CONSTANT_HIGHLIGHTED, COLOR_MAGENTA, COLOR_BRIGHT_BLACK);
     init_pair(S_PREPROCESSOR_HIGHLIGHTED, COLOR_YELLOW, COLOR_BRIGHT_BLACK);
     init_pair(S_DIFF_ADD_HIGHLIGHTED, COLOR_GREEN, COLOR_BRIGHT_BLACK);
     init_pair(S_DIFF_REMOVE_HIGHLIGHTED, COLOR_RED, COLOR_BRIGHT_BLACK);

     // Doesn't work in insert mode :<
     //define_key("h", KEY_LEFT);
     //define_key("j", KEY_DOWN);
     //define_key("k", KEY_UP);
     //define_key("l", KEY_RIGHT);

     define_key("\x7F", KEY_BACKSPACE); // Backspace  (127) (0x7F) ASCII "DEL" Delete
     define_key("\x15", KEY_NPAGE);     // ctrl + d    (21) (0x15) ASCII "NAK" Negative Acknowledgement
     define_key("\x04", KEY_PPAGE);     // ctrl + u     (4) (0x04) ASCII "EOT" End of Transmission
     define_key("\x11", KEY_CLOSE);     // ctrl + q    (17) (0x11) ASCII "DC1" Device Control 1
     define_key("\x12", KEY_REDO);      // ctrl + r    (18) (0x12) ASCII "DC2" Device Control 2
     define_key("\x17", KEY_SAVE);      // ctrl + w    (23) (0x17) ASCII "ETB" End of Transmission Block
     //define_key("\x0A", KEY_ENTER);     // Enter       (10) (0x0A) ASCII "LF"  NL Line Feed, New Line

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

     // input buffer
     {
          ce_free_buffer(&config_state->input_buffer);
          BufferState* buffer_state = config_state->input_buffer.user_data;
          BufferCommitNode* itr = buffer_state->commit_tail;
          while(itr->prev) itr = itr->prev;
          ce_commits_free(itr);

          free(config_state->view_input);
     }

     ce_free_buffer(&config_state->buffer_list_buffer);

     // history
     input_history_free(&config_state->shell_command_history);
     input_history_free(&config_state->search_history);
     input_history_free(&config_state->load_file_history);

     free(config_state);
     return true;
}

// location is {left_column, top_line} for the view
void scroll_view_to_location(BufferView* buffer_view, const Point* location){
     // TODO: we should be able to scroll the view above our first line
     buffer_view->left_column = (location->x >= 0) ? location->x : 0;
     buffer_view->top_row = (location->y >= 0) ? location->y : 0;
}

void center_view(BufferView* view)
{
     int64_t view_height = view->bottom_right.y - view->top_left.y;
     Point location = (Point) {0, view->cursor.y - (view_height / 2)};
     scroll_view_to_location(view, &location);
}

void find_command(int command_key, int find_char, Buffer* buffer, Point* cursor)
{
     switch(command_key){
     case 'f':
     {
          int64_t x_delta = ce_find_delta_to_char_forward_in_line(buffer, cursor, find_char);
          if(x_delta == -1) break;
          ce_move_cursor(buffer, cursor, (Point){x_delta, 0});
     } break;
     case 't':
     {
          Point search_point = {cursor->x + 1, cursor->y};
          int64_t x_delta = ce_find_delta_to_char_forward_in_line(buffer, &search_point, find_char);
          if(x_delta <= 0) break;
          ce_move_cursor(buffer, cursor, (Point){x_delta, 0});
     } break;
     case 'F':
     {
          int64_t x_delta = ce_find_delta_to_char_backward_in_line(buffer, cursor, find_char);
          if(x_delta == -1) break;
          ce_move_cursor(buffer, cursor, (Point){-x_delta, 0});
     } break;
     case 'T':
     {
          Point search_point = {cursor->x - 1, cursor->y};
          int64_t x_delta = ce_find_delta_to_char_backward_in_line(buffer, &search_point, find_char);
          if(x_delta <= 0) break;
          ce_move_cursor(buffer, cursor, (Point){-x_delta, 0});
     } break;
     default:
          assert(0);
          break;
     }
}

// returns true if key may be interpreted as a multiplier given the current mulitplier state
bool key_is_multiplier(uint64_t multiplier, char key)
{
     return (key >='1' && key <= '9') || (key == '0' && multiplier > 0);
}

int ispunct_or_iswordchar(int c)
{
     return ce_ispunct(c) || ce_iswordchar(c);
}

int isnotquote(int c)
{
     return c != '"';
}

typedef enum{
     MOVEMENT_CONTINUE = '\0',
     MOVEMENT_COMPLETE,
     MOVEMENT_INVALID
} movement_state_t;

// given the current config state, buffer, and cursor: determines whether or not a generic movement has been provided
//      return values:
//              - MOVEMENT_COMPLETE: a generic movement has been provided and movement_start + movement_end now point to
//                                   start and end of the movement (inclusive)
//              - MOVEMENT_CONTINUE: a portion of a generic movement has been provided, and this function should be
//                                   called again once another key is available
//              - MOVEMENT_INVALID:  the movement provided by the user is not a generic movement
movement_state_t try_generic_movement(ConfigState* config_state, Buffer* buffer, Point* cursor, Point* movement_start, Point* movement_end)
{
     *movement_start = *movement_end = *cursor;

     int key0 = config_state->movement_keys[0];
     int key1 = config_state->movement_keys[1];
     uint64_t multiplier = config_state->movement_multiplier;

     if(key0 == MOVEMENT_CONTINUE) return MOVEMENT_CONTINUE;

     for(size_t mm=0; mm < multiplier; mm++) {
          switch(key0){
          case KEY_LEFT:
          case 'h':
          {
               ce_move_cursor(buffer, movement_end, (Point){-1, 0});
          } break;
          case KEY_DOWN:
          case 'j':
          {
               *movement_start = (Point){0, cursor->y};
               ce_move_cursor(buffer, movement_end, (Point){0, 1});
               ce_move_cursor_to_end_of_line(buffer, movement_end);
          } break;
          case KEY_UP:
          case 'k':
          {
               *movement_start = (Point){0, cursor->y};
               ce_move_cursor(buffer, movement_start, (Point){0, -1});
               ce_move_cursor_to_end_of_line(buffer, movement_end);
          } break;
          case KEY_RIGHT:
          case 'l':
          {
               ce_move_cursor(buffer, movement_end, (Point){1, 0});
          } break;
          case '0':
          {
               movement_start->x = 0;
               ce_move_cursor(buffer, movement_end, (Point){-1, 0});
          } break;
          case '^':
          {
               Point begin_line_cursor = *cursor;
               ce_move_cursor_to_soft_beginning_of_line(buffer, &begin_line_cursor);
               ce_move_cursor(buffer, &begin_line_cursor, (Point){-1, 0});
               if(cursor->x < begin_line_cursor.x) *movement_end = begin_line_cursor;
               else *movement_start = begin_line_cursor;
          } break;
          case 'f':
          case 't':
          case 'F':
          case 'T':
          {
               if(key1 == MOVEMENT_CONTINUE) return MOVEMENT_CONTINUE;
               config_state->find_command.command_key = key0;
               config_state->find_command.find_char = key1;
               find_command(key0, key1, buffer, movement_end);
          } break;
          case 'i':
               switch(key1){
               case 'w':
               {
                    if(!ce_get_word_at_location(buffer, movement_start, movement_start, movement_end))
                         return MOVEMENT_INVALID;
               } break;
               case 'W':
               {
                    char curr_char;
                    bool success = ce_get_char(buffer, movement_start, &curr_char);
                    if(!success) return MOVEMENT_INVALID;

                    if(isblank(curr_char)){
                         success = ce_get_homogenous_adjacents(buffer, movement_start, movement_end, isblank);
                         if(!success) return MOVEMENT_INVALID;
                    }else{
                         assert(ispunct_or_iswordchar(curr_char));
                         success = ce_get_homogenous_adjacents(buffer, movement_start, movement_end, ispunct_or_iswordchar);
                         if(!success) return MOVEMENT_INVALID;
                    }
               } break;
               case '"':
               {
                    if(!ce_get_homogenous_adjacents(buffer, movement_start, movement_end, isnotquote)) return MOVEMENT_INVALID;
                    if(movement_start->x == movement_end->x && movement_start->y == movement_end->y) return MOVEMENT_INVALID;
                    assert(movement_start->y == movement_end->y);
               } break;
               case MOVEMENT_CONTINUE:
                    return MOVEMENT_CONTINUE;
               default:
                    return MOVEMENT_INVALID;
               }
               break;
          case 'a':
               switch(key1){
               case 'w':
               {
                    char c;
                    bool success = ce_get_char(buffer, movement_start, &c);
                    if(!success) return MOVEMENT_INVALID;

#define SLURP_RIGHT(condition)\
               do{ movement_end->x++; if(!ce_get_char(buffer, movement_end, &c)) break; }while(condition(c));\
               movement_end->x--;
#define SLURP_LEFT(condition)\
               do{ movement_start->x--; if(!ce_get_char(buffer, movement_start, &c)) break; }while(condition(c));\
               movement_start->x++;

                    if(ce_iswordchar(c)){
                         SLURP_RIGHT(ce_iswordchar);

                         if(isblank(c)){
                              SLURP_RIGHT(isblank);
                              SLURP_LEFT(ce_iswordchar);

                         }else if(ce_ispunct(c)){
                              SLURP_LEFT(ce_iswordchar);
                              SLURP_LEFT(isblank);
                         }
                    }else if(ce_ispunct(c)){
                         SLURP_RIGHT(ce_ispunct);

                         if(isblank(c)){
                              SLURP_RIGHT(isblank);
                              SLURP_LEFT(ce_ispunct);

                         }else if(ce_ispunct(c)){
                              SLURP_LEFT(ce_ispunct);
                              SLURP_LEFT(isblank);
                         }
                    }else{
                         assert(isblank(c));
                         SLURP_RIGHT(isblank);

                         if(ce_ispunct(c)){
                              SLURP_RIGHT(ce_ispunct);
                              SLURP_LEFT(isblank);

                         }else if(ce_iswordchar(c)){
                              SLURP_RIGHT(ce_iswordchar);
                              SLURP_LEFT(isblank);

                         }else{
                              SLURP_LEFT(isblank);

                              if(ce_ispunct(c)){
                                   SLURP_LEFT(ce_ispunct);

                              }else if(ce_iswordchar(c)){
                                   SLURP_LEFT(ce_iswordchar);
                              }
                         }
                    }
               } break;
               case 'W':
               {
                    char c;
                    bool success = ce_get_char(buffer, movement_start, &c);
                    if(!success) return MOVEMENT_INVALID;

                    if(ispunct_or_iswordchar(c)){
                         SLURP_RIGHT(ispunct_or_iswordchar);

                         if(isblank(c)){
                              SLURP_RIGHT(isblank);
                              SLURP_LEFT(ispunct_or_iswordchar);
                         }
                    }else{
                         assert(isblank(c));
                         SLURP_RIGHT(isblank);

                         if(ispunct_or_iswordchar(c)){
                              SLURP_RIGHT(ispunct_or_iswordchar);
                              SLURP_LEFT(isblank);

                         }else{
                              SLURP_LEFT(isblank);
                              SLURP_LEFT(ispunct_or_iswordchar);
                         }
                    }
               } break;
               case '"':
               {
                    if(!ce_get_homogenous_adjacents(buffer, movement_start, movement_end, isnotquote)) return MOVEMENT_INVALID;
                    assert(movement_start->x > 0);
                    assert(movement_end->x + 1 < (int64_t)strlen(buffer->lines[movement_end->y]));
                    movement_start->x--;
                    movement_end->x++;
               } break;
               case MOVEMENT_CONTINUE:
                    return MOVEMENT_CONTINUE;
               default:
                    return MOVEMENT_INVALID;
               }
               break;
          case 'e':
          case 'E':
               movement_end->x += ce_find_delta_to_end_of_word(buffer, movement_end, key0 == 'e');
               break;
          case 'b':
          case 'B':
               ce_move_cursor_to_beginning_of_word(buffer, movement_start, key0 == 'b');
               ce_move_cursor(buffer, movement_end, (Point){-1, 0});
               break;
          case 'w':
          case 'W':
               movement_end->x += ce_find_delta_to_next_word(buffer, movement_end, key0 == 'w');
               break;
          case '$':
               ce_move_cursor_to_end_of_line(buffer, movement_end);
               break;
          case '%':
          {
               Point delta;
               if(!ce_find_delta_to_match(buffer, cursor, &delta)) break;

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
          case 'G':
          {
               ce_move_cursor_to_end_of_file(buffer, movement_end);
               ce_move_cursor_to_end_of_line(buffer, movement_end);
          } break;
          case 'g':
               switch(key1){
               case 'g':
                    ce_move_cursor_to_beginning_of_file(buffer, movement_start);
                    break;
               case MOVEMENT_CONTINUE:
                    return MOVEMENT_CONTINUE;
               default:
                    return MOVEMENT_INVALID;
               }
               break;
          default:
               return MOVEMENT_INVALID;
          }
     }

     return MOVEMENT_COMPLETE;
}

static size_t movement_buffer_len(const int* mov_buf, size_t size)
{
     size_t len;
     for(len=0; len < size && mov_buf[len] != 0; len++);
     return len;
}

bool is_movement_buffer_full(ConfigState* config_state)
{
     size_t max_movement_keys = sizeof config_state->movement_keys;
     size_t n_movement_keys = movement_buffer_len(config_state->movement_keys, max_movement_keys);
     return n_movement_keys == max_movement_keys;
}

void scroll_view_to_last_line(BufferView* view)
{
     view->top_row = view->buffer->line_count - (view->bottom_right.y - view->top_left.y);
     if(view->top_row < 0) view->top_row = 0;
}

// NOTE: clear commits then create the initial required entry to restart
void reset_buffer_commits(BufferCommitNode** tail)
{
     BufferCommitNode* node = *tail;
     while(node->prev) node = node->prev;
     ce_commits_free(node);

     BufferCommitNode* new_tail = calloc(1, sizeof(*new_tail));
     if(!new_tail){
          ce_message("failed to allocate commit history for buffer");
          return;
     }

     *tail = new_tail;
}

bool goto_file_destination_in_buffer(BufferNode* head, Buffer* buffer, int64_t line, BufferView* head_view,
                                     BufferView* view)
{
     assert(line >= 0);
     assert(line < buffer->line_count);

     char file_tmp[BUFSIZ];
     char line_number_tmp[BUFSIZ];
     char* file_end = strpbrk(buffer->lines[line], ": ");
     if(!file_end) return false;
     int64_t filename_len = file_end - buffer->lines[line];
     strncpy(file_tmp, buffer->lines[line], filename_len);
     file_tmp[filename_len] = 0;
     if(access(file_tmp, F_OK) == -1) return false; // file does not exist
     char* line_number_begin_delim = NULL;
     char* line_number_end_delim = NULL;
     if(*file_end == ' '){
          // format: 'filepath search_symbol line '
          char* second_space = strchr(file_end + 1, ' ');
          if(!second_space) return false;
          line_number_begin_delim = second_space;
          line_number_end_delim = strchr(second_space + 1, ' ');
          if(!line_number_end_delim) return false;
     }else{
          // format: 'filepath:line:column:'
          line_number_begin_delim = file_end;
          char* second_colon = strchr(line_number_begin_delim + 1, ':');
          if(!second_colon) return false;
          line_number_end_delim = second_colon;
     }

     int64_t line_number_len = line_number_end_delim - (line_number_begin_delim + 1);
     strncpy(line_number_tmp, line_number_begin_delim + 1, line_number_len);
     line_number_tmp[line_number_len] = 0;

     bool all_digits = true;
     for(char* c = line_number_tmp; *c; c++){
          if(!isdigit(*c)){
               all_digits = false;
               break;
          }
     }

     if(!all_digits) return false;

     Buffer* new_buffer = open_file_buffer(head, file_tmp);
     if(new_buffer){
          view->buffer = new_buffer;
          Point dst = {0, atoi(line_number_tmp) - 1}; // line numbers are 1 indexed
          ce_set_cursor(new_buffer, &view->cursor, &dst);

          // check for optional column number
          char* third_colon = strchr(line_number_end_delim + 1, ':');
          if(third_colon){
               line_number_len = third_colon - (line_number_end_delim + 1);
               strncpy(line_number_tmp, line_number_end_delim + 1, line_number_len);
               line_number_tmp[line_number_len] = 0;

               all_digits = true;
               for(char* c = line_number_tmp; *c; c++){
                    if(!isdigit(*c)){
                         all_digits = false;
                         break;
                    }
               }

               if(all_digits){
                    dst.x = atoi(line_number_tmp) - 1; // column numbers are 1 indexed
                    assert(dst.x >= 0);
                    ce_set_cursor(new_buffer, &view->cursor, &dst);
               }else{
                    ce_move_cursor_to_soft_beginning_of_line(new_buffer, &view->cursor);
               }
          }else{
               ce_move_cursor_to_soft_beginning_of_line(new_buffer, &view->cursor);
          }

          center_view(view);
          BufferView* command_view = ce_buffer_in_view(head_view, buffer);
          if(command_view) command_view->top_row = line;
          return true;
     }

     return false;
}

// TODO: rather than taking in config_state, I'd like to take in only the parts it needs, if it's too much, config_state is fine
void jump_to_next_shell_command_file_destination(BufferNode* head, ConfigState* config_state, bool forwards)
{
     Buffer* command_buffer = config_state->shell_command_buffer;
     int64_t lines_checked = 0;
     int64_t delta = forwards ? 1 : -1;
     for(int64_t i = config_state->input_last_error + delta; lines_checked < command_buffer->line_count;
         i += delta, lines_checked++){
          if(i == command_buffer->line_count && forwards){
               i = 0;
          }else if(i <= 0 && !forwards){
               i = command_buffer->line_count - 1;
          }

          if(goto_file_destination_in_buffer(head, command_buffer, i, config_state->view_head,
                                             config_state->view_current)){
               config_state->input_last_error = i;
               break;
          }
     }
}

bool commit_input_to_history(ConfigState* config_state, InputHistory* history)
{
     if(config_state->view_input->buffer->line_count){
          char* saved = ce_dupe_buffer(config_state->view_input->buffer);
          if((history->cur->prev && strcmp(saved, history->cur->prev->entry) != 0) ||
             !history->cur->prev){
               history->cur = history->tail;
               input_history_update_current(history, saved);
               input_history_commit_current(history);
          }
     }else{
          return false;
     }

     return true;
}

InputHistory* history_from_input_key(ConfigState* config_state)
{
     InputHistory* history = NULL;

     switch(config_state->input_key){
     default:
          break;
     case '/':
     case '?':
          history = &config_state->search_history;
          break;
     case 24: // Ctrl + x
          history = &config_state->shell_command_history;
          break;
     case 6: // Ctrl + f
          history = &config_state->load_file_history;
          break;
     }

     return history;
}

void view_follow_cursor(BufferView* current_view)
{
     ce_follow_cursor(&current_view->cursor, &current_view->left_column, &current_view->top_row,
                      current_view->bottom_right.x - current_view->top_left.x,
                      current_view->bottom_right.y - current_view->top_left.y,
                      current_view->bottom_right.x == (g_terminal_dimensions->x - 1),
                      current_view->bottom_right.y == (g_terminal_dimensions->y - 2));
}

void split_view(BufferView* head_view, BufferView* current_view, bool horizontal)
{
     BufferView* new_view = ce_split_view(current_view, current_view->buffer, horizontal);
     if(new_view){
          Point top_left = {0, 0};
          Point bottom_right = {g_terminal_dimensions->x - 1, g_terminal_dimensions->y - 2}; // account for statusbar
          ce_calc_views(head_view, &top_left, &bottom_right);
          view_follow_cursor(current_view);
          new_view->cursor = current_view->cursor;
          new_view->top_row = current_view->top_row;
          new_view->left_column = current_view->left_column;
     }
}

void handle_mouse_event(ConfigState* config_state, Buffer* buffer, BufferState* buffer_state, BufferView* buffer_view, Point* cursor)
{
     MEVENT event;
     if(getmouse(&event) == OK){
          bool enter_insert;
          if((enter_insert = config_state->vim_mode == VM_INSERT)){
               Point end_cursor = *cursor;
               ce_clamp_cursor(buffer, &end_cursor);
               enter_normal_mode(config_state);
               commit_insert_mode_changes(config_state, buffer, buffer_state, cursor, &end_cursor);
               *cursor = end_cursor;
          }
#ifdef MOUSE_DIAG
          ce_message("0x%x", event.bstate);
          if(event.bstate & BUTTON1_PRESSED)
               ce_message("%s", "BUTTON1_PRESSED");
          else if(event.bstate & BUTTON1_RELEASED)
               ce_message("%s", "BUTTON1_RELEASED");
          else if(event.bstate & BUTTON1_CLICKED)
               ce_message("%s", "BUTTON1_CLICKED");
          else if(event.bstate & BUTTON1_DOUBLE_CLICKED)
               ce_message("%s", "BUTTON1_DOUBLE_CLICKED");
          else if(event.bstate & BUTTON1_TRIPLE_CLICKED)
               ce_message("%s", "BUTTON1_TRIPLE_CLICKED");
          else if(event.bstate & BUTTON2_PRESSED)
               ce_message("%s", "BUTTON2_PRESSED");
          else if(event.bstate & BUTTON2_RELEASED)
               ce_message("%s", "BUTTON2_RELEASED");
          else if(event.bstate & BUTTON2_CLICKED)
               ce_message("%s", "BUTTON2_CLICKED");
          else if(event.bstate & BUTTON2_DOUBLE_CLICKED)
               ce_message("%s", "BUTTON2_DOUBLE_CLICKED");
          else if(event.bstate & BUTTON2_TRIPLE_CLICKED)
               ce_message("%s", "BUTTON2_TRIPLE_CLICKED");
          else if(event.bstate & BUTTON3_PRESSED)
               ce_message("%s", "BUTTON3_PRESSED");
          else if(event.bstate & BUTTON3_RELEASED)
               ce_message("%s", "BUTTON3_RELEASED");
          else if(event.bstate & BUTTON3_CLICKED)
               ce_message("%s", "BUTTON3_CLICKED");
          else if(event.bstate & BUTTON3_DOUBLE_CLICKED)
               ce_message("%s", "BUTTON3_DOUBLE_CLICKED");
          else if(event.bstate & BUTTON3_TRIPLE_CLICKED)
               ce_message("%s", "BUTTON3_TRIPLE_CLICKED");
          else if(event.bstate & BUTTON4_PRESSED)
               ce_message("%s", "BUTTON4_PRESSED");
          else if(event.bstate & BUTTON4_RELEASED)
               ce_message("%s", "BUTTON4_RELEASED");
          else if(event.bstate & BUTTON4_CLICKED)
               ce_message("%s", "BUTTON4_CLICKED");
          else if(event.bstate & BUTTON4_DOUBLE_CLICKED)
               ce_message("%s", "BUTTON4_DOUBLE_CLICKED");
          else if(event.bstate & BUTTON4_TRIPLE_CLICKED)
               ce_message("%s", "BUTTON4_TRIPLE_CLICKED");
          else if(event.bstate & BUTTON_SHIFT)
               ce_message("%s", "BUTTON_SHIFT");
          else if(event.bstate & BUTTON_CTRL)
               ce_message("%s", "BUTTON_CTRL");
          else if(event.bstate & BUTTON_ALT)
               ce_message("%s", "BUTTON_ALT");
          else if(event.bstate & REPORT_MOUSE_POSITION)
               ce_message("%s", "REPORT_MOUSE_POSITION");
          else if(event.bstate & ALL_MOUSE_EVENTS)
               ce_message("%s", "ALL_MOUSE_EVENTS");
#endif
          if(event.bstate & BUTTON1_PRESSED){ // Left click OSX
               Point click = {event.x, event.y};
               config_state->view_current = ce_find_view_at_point(config_state->view_head, &click);
               click = (Point) {event.x - (config_state->view_current->top_left.x - config_state->view_current->left_column),
                                event.y - (config_state->view_current->top_left.y - config_state->view_current->top_row)};
               ce_set_cursor(config_state->view_current->buffer,
                             &config_state->view_current->cursor,
                             &click);
          }
#ifdef SCROLL_SUPPORT
          // This feature is currently unreliable and is only known to work for Ryan :)
          else if(event.bstate & (BUTTON_ALT | BUTTON2_CLICKED)){
               Point next_line = {0, cursor->y + SCROLL_LINES};
               if(ce_point_on_buffer(buffer, &next_line)){
                    Point scroll_location = {0, buffer_view->top_row + SCROLL_LINES};
                    scroll_view_to_location(buffer_view, &scroll_location);
                    if(buffer_view->cursor.y < buffer_view->top_row)
                         ce_move_cursor(buffer, cursor, (Point){0, SCROLL_LINES});
               }
          }else if(event.bstate & BUTTON4_TRIPLE_CLICKED){
               Point next_line = {0, cursor->y - SCROLL_LINES};
               if(ce_point_on_buffer(buffer, &next_line)){
                    Point scroll_location = {0, buffer_view->top_row - SCROLL_LINES};
                    scroll_view_to_location(buffer_view, &scroll_location);
                    if(buffer_view->cursor.y > buffer_view->top_row + (buffer_view->bottom_right.y - buffer_view->top_left.y))
                         ce_move_cursor(buffer, cursor, (Point){0, -SCROLL_LINES});
               }
          }
#else
          (void) buffer;
          (void) buffer_view;
#endif
          // if we left insert and haven't switched views, enter insert mode
          if(enter_insert && config_state->view_current == buffer_view) enter_insert_mode(config_state, cursor);
     }
}

void half_page_up(BufferView* view)
{
     int64_t view_height = view->bottom_right.y - view->top_left.y;
     Point delta = { 0, -view_height / 2 };
     ce_move_cursor(view->buffer, &view->cursor, delta);
}

void half_page_down(BufferView* view)
{
     int64_t view_height = view->bottom_right.y - view->top_left.y;
     Point delta = { 0, view_height / 2 };
     ce_move_cursor(view->buffer, &view->cursor, delta);
}

bool scroll_z_cursor(ConfigState* config_state)
{
     BufferView* buffer_view = config_state->view_current;
     Point* cursor = &buffer_view->cursor;
     Point location;
     switch(config_state->movement_keys[0]){
     case MOVEMENT_CONTINUE:
          // no movement yet, wait for one!
          return true;
     case 't':
          location = (Point){0, cursor->y};
          scroll_view_to_location(buffer_view, &location);
          break;
     case 'z': {
          center_view(buffer_view);
     } break;
     case 'b': {
          // move current line to bottom of view
          location = (Point){0, cursor->y - buffer_view->bottom_right.y};
          scroll_view_to_location(buffer_view, &location);
     } break;
     }

     return false;
}

void yank_visual_range(ConfigState* config_state)
{
     Buffer* buffer = config_state->view_current->buffer;
     Point* cursor = &config_state->view_current->cursor;
     Point start = config_state->visual_start;
     Point end = {cursor->x, cursor->y};

     const Point* a = &start;
     const Point* b = &end;

     ce_sort_points(&a, &b);

     add_yank(config_state, '0', ce_dupe_string(buffer, a, b), YANK_NORMAL);
     add_yank(config_state, '"', ce_dupe_string(buffer, a, b), YANK_NORMAL);
}

void yank_visual_lines(ConfigState* config_state)
{
     Buffer* buffer = config_state->view_current->buffer;
     Point* cursor = &config_state->view_current->cursor;
     int64_t start_line = config_state->visual_start.y;
     int64_t end_line = cursor->y;

     if(start_line > end_line){
          int64_t tmp = start_line;
          start_line = end_line;
          end_line = tmp;
     }

     Point start = {0, start_line};
     Point end = {ce_last_index(buffer->lines[end_line]), end_line};

     add_yank(config_state, '0', ce_dupe_string(buffer, &start, &end), YANK_LINE);
     add_yank(config_state, '"', ce_dupe_string(buffer, &start, &end), YANK_LINE);
}

void remove_visual_range(ConfigState* config_state)
{
     Point* cursor = &config_state->view_current->cursor;
     Buffer* buffer = config_state->view_current->buffer;
     BufferState* buffer_state = buffer->user_data;
     Point start = config_state->visual_start;
     Point end = {cursor->x, cursor->y};

     const Point* a = &start;
     const Point* b = &end;

     ce_sort_points(&a, &b);

     char* removed_str = ce_dupe_string(buffer, a, b);
     int64_t remove_len = ce_compute_length(buffer, a, b);
     if(ce_remove_string(buffer, a, remove_len)){
          ce_commit_remove_string(&buffer_state->commit_tail, a, cursor, a, removed_str);
          ce_set_cursor(buffer, cursor, a);
     }else{
          free(removed_str);
     }
     enter_normal_mode(config_state);
}

void remove_visual_lines(ConfigState* config_state)
{
     Point* cursor = &config_state->view_current->cursor;
     Buffer* buffer = config_state->view_current->buffer;
     BufferState* buffer_state = buffer->user_data;
     int64_t start_line = config_state->visual_start.y;
     int64_t end_line = cursor->y;

     if(start_line > end_line){
          int64_t tmp = start_line;
          start_line = end_line;
          end_line = tmp;
     }

     Point start = {0, start_line};
     Point end = {ce_last_index(buffer->lines[end_line]), end_line};

     char* removed_str = ce_dupe_lines(buffer, start.y, end.y);
     int64_t remove_len = strlen(removed_str);
     if(ce_remove_string(buffer, &start, remove_len)){
          ce_commit_remove_string(&buffer_state->commit_tail, &start, cursor, &start,
                                  removed_str);
          ce_set_cursor(buffer, cursor, &start);
     }else{
          free(removed_str);
     }
     enter_normal_mode(config_state);
}

void iterate_history_input(ConfigState* config_state, bool previous)
{
     BufferState* buffer_state = config_state->view_input->buffer->user_data;
     InputHistory* history = history_from_input_key(config_state);
     if(!history) return;

     // update the current history node if we are at the tail to save what the user typed
     // skip this if they haven't typed anything
     if(history->tail == history->cur &&
        config_state->view_input->buffer->line_count){
          input_history_update_current(history, ce_dupe_buffer(config_state->view_input->buffer));
     }

     bool success = false;

     if(previous){
          success = input_history_prev(history);
     }else{
          success = input_history_next(history);
     }

     if(success){
          ce_clear_lines(config_state->view_input->buffer);
          ce_append_string(config_state->view_input->buffer, 0, history->cur->entry);
          config_state->view_input->cursor = (Point){0, 0};
          ce_move_cursor_to_end_of_file(config_state->view_input->buffer, &config_state->view_input->cursor);
          reset_buffer_commits(&buffer_state->commit_tail);
     }
}

void switch_to_view_at_point(ConfigState* config_state, const Point* point)
{
     BufferView* next_view = ce_find_view_at_point(config_state->view_head, point);
     if(next_view){
          // save cursor in buffer info
          config_state->view_current->buffer->cursor = config_state->view_current->cursor;
          config_state->view_current = next_view;
          enter_normal_mode(config_state);
     }
}

void update_buffer_list_buffer(ConfigState* config_state, const BufferNode* head)
{
     char buffer_info[BUFSIZ];
     config_state->buffer_list_buffer.readonly = false;
     ce_clear_lines(&config_state->buffer_list_buffer);

     // calc maxes of things we care about for formatting
     int64_t max_buffer_lines = 0;
     int64_t max_name_len = 0;
     int64_t buffer_count = 0;
     const BufferNode* itr = head;
     while(itr){
          if(max_buffer_lines < itr->buffer->line_count) max_buffer_lines = itr->buffer->line_count;
          int64_t name_len = strlen(itr->buffer->name);
          if(max_name_len < name_len) max_name_len = name_len;
          buffer_count++;
          itr = itr->next;
     }

     int64_t max_buffer_lines_digits = count_digits(max_buffer_lines);
     if(max_buffer_lines_digits < 5) max_buffer_lines_digits = 5; // account for "lines" string row header
     if(max_name_len < 11) max_name_len = 11; // account for "buffer name" string row header

     // build format string, OMG THIS IS SO UNREADABLE HOLY MOLY BATMAN
     char format_string[BUFSIZ];
     snprintf(format_string, BUFSIZ, "%%-%lds %%-%lds (%%ld buffers)", max_name_len + 4,
              max_buffer_lines_digits);
     snprintf(buffer_info, BUFSIZ, format_string, "buffer name", "lines", buffer_count);
     ce_append_line(&config_state->buffer_list_buffer, buffer_info);
     snprintf(format_string, BUFSIZ, "%%4s%%-%lds %%%ldld", max_name_len, max_buffer_lines_digits);

     itr = head;
     while(itr){
          const char* buffer_flag_str = itr->buffer->readonly ? readonly_string(itr->buffer) :
                                                                modified_string(itr->buffer);
          snprintf(buffer_info, BUFSIZ, format_string, buffer_flag_str, itr->buffer->name,
                   itr->buffer->line_count);
          ce_append_line(&config_state->buffer_list_buffer, buffer_info);
          itr = itr->next;
     }
}

bool key_handler(int key, BufferNode* head, void* user_data)
{
     ConfigState* config_state = user_data;
     Buffer* buffer = config_state->view_current->buffer;
     BufferState* buffer_state = buffer->user_data;
     BufferView* buffer_view = config_state->view_current;
     Point* cursor = &config_state->view_current->cursor;
     config_state->last_key = key;
     Point movement_start, movement_end;

     if(config_state->vim_mode == VM_INSERT){
          assert(config_state->command_key == '\0');
          switch(key){
          case 27: // ESC
          {
               Point end_cursor = *cursor;
               ce_clamp_cursor(buffer, &end_cursor);
               enter_normal_mode(config_state);
               commit_insert_mode_changes(config_state, buffer, buffer_state, cursor, &end_cursor);

               // when we exit insert mode, do not move the cursor back unless we are at the end of the line
               *cursor = end_cursor;
          } break;
          case KEY_MOUSE:
               handle_mouse_event(config_state, buffer, buffer_state, buffer_view, cursor);
               break;
          case KEY_BACKSPACE:
               if(buffer->line_count){
                    if(cursor->x <= 0){
                         if(cursor->y){
                              int64_t prev_line_len = strlen(buffer->lines[cursor->y - 1]);
                              int64_t cur_line_len = strlen(buffer->lines[cursor->y]);

                              if(cur_line_len){
                                   ce_append_string(buffer, cursor->y - 1, buffer->lines[cursor->y]);
                              }

                              if(ce_remove_line(buffer, cursor->y)){
                                   backspace_push(&buffer_state->backspace_head, '\n');
                                   cursor->y--;
                                   cursor->x = prev_line_len;
                                   config_state->start_insert = *cursor;
                              }
                         }
                    }else{
                         Point previous = *cursor;
                         previous.x--;
                         char c = 0;
                         if(ce_get_char(buffer, &previous, &c)){
                              if(ce_remove_char(buffer, &previous)){
                                   if(previous.x < config_state->start_insert.x){
                                        backspace_push(&buffer_state->backspace_head, c);
                                        config_state->start_insert.x--;
                                   }
                                   // cannot use move_cursor due to not being able to be ahead of the last character
                                   cursor->x--;
                              }
                         }
                    }
               }
               break;
          case KEY_DC:
               // TODO: with our current insert mode undo implementation we can't support this 
               // ce_remove_char(buffer, cursor);
               break;
          case '\t':
          {
               ce_insert_string(buffer, cursor, TAB_STRING);
               ce_move_cursor(buffer, cursor, (Point){5, 0});
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

                    // indent if necessary
                    Point prev_line = {0, cursor->y-1};
                    int64_t indent_len = ce_get_indentation_for_next_line(buffer, &prev_line, strlen(TAB_STRING));
                    if(indent_len > 0){
                         char* indent = malloc(indent_len + 1);
                         memset(indent, ' ', indent_len);
                         indent[indent_len] = '\0';

                         if(ce_insert_string(buffer, cursor, indent))
                              cursor->x += indent_len;
                    }
               }
          } break;
          case KEY_UP:
          case KEY_DOWN:
          case KEY_LEFT:
          case KEY_RIGHT:
          {
               config_state->movement_keys[0] = key;
               config_state->movement_multiplier = 1;

               movement_state_t m_state = try_generic_movement(config_state, buffer, cursor, &movement_start, &movement_end);
               if(m_state == MOVEMENT_CONTINUE) return true;

               // this is a generic movement
               if(movement_end.x == cursor->x && movement_end.y == cursor->y)
                    ce_set_cursor(buffer, cursor, &movement_start);
               else
                    ce_set_cursor(buffer, cursor, &movement_end);
          } break;
          case '}':
          {
               if(ce_insert_char(buffer, cursor, key)){

                    Point match;
                    if(ce_find_match(buffer, cursor, &match) && match.y != cursor->y){

                         // get the match's sbol (that's the indentation we're matching)
                         Point sbol_match = {0, match.y};
                         ce_move_cursor_to_soft_beginning_of_line(buffer, &sbol_match);

                         int64_t n_deletes = CE_MIN((int64_t) strlen(TAB_STRING), cursor->x - sbol_match.x);

                         bool can_unindent = true;
                         for(Point iter = {0, cursor->y}; ce_point_on_buffer(buffer, &iter) && iter.x < n_deletes; iter.x++)
                              can_unindent &= isblank(ce_get_char_raw(buffer, &iter));

                         if(can_unindent){
                              cursor->x -= n_deletes;
                              ce_remove_string(buffer, cursor, n_deletes);
                         }
                    }

                    cursor->x++;
               }
          } break;
          case 14: // Ctrl + n
               if(config_state->input){
                    iterate_history_input(config_state, false);
                    if(buffer->line_count && buffer->lines[cursor->y][0]) cursor->x++;
                    enter_insert_mode(config_state, cursor);
               }
               break;
          case 16: // Ctrl + p
               if(config_state->input){
                    iterate_history_input(config_state, true);
                    if(buffer->line_count && buffer->lines[cursor->y][0]) cursor->x++;
                    enter_insert_mode(config_state, cursor);
               }
               break;
          default:
               if(ce_insert_char(buffer, cursor, key)) cursor->x++;
               break;
          }
     }else{
          if(config_state->command_key == '\0' && config_state->command_multiplier == 0){
               // this is the first key entered

               if(key >= '1' && key <= '9'){
                    // this key is part of a command multiplier
                    config_state->command_multiplier *= 10;
                    config_state->command_multiplier += key - '0';
                    return true;
               }else{
                    // this key is a command
                    config_state->command_key = key;
                    config_state->command_multiplier = 1;
               }
          }else if(config_state->command_key == '\0'){
               // the previous key was part of a command multiplier
               assert(config_state->command_multiplier != 0);

               if(key >= '0' && key <= '9'){
                    // this key is part of a command multiplier
                    config_state->command_multiplier *= 10;
                    config_state->command_multiplier += key - '0';
                    return true;
               }else{
                    // this key is a command
                    config_state->command_key = key;
               }
          }else if(config_state->movement_keys[0] == '\0' && config_state->movement_multiplier == 0){
               // this is the first key entered after the command
               assert(config_state->command_multiplier != 0);
               assert(config_state->command_key != '\0');

               if(key >= '1' && key <= '9'){
                    // this key is part of a movement multiplier
                    config_state->movement_multiplier *= 10;
                    config_state->movement_multiplier += key - '0';
                    return true;
               } else {
                    // this key is a part of a movement
                    config_state->movement_keys[0] = key;
                    config_state->movement_multiplier = 1;
               }
          }else if(config_state->movement_keys[0] == '\0'){
               // the previous key was part of a movement multiplier
               assert(config_state->command_multiplier != 0);
               assert(config_state->command_key != '\0');
               assert(config_state->movement_multiplier != 0);

               if(key >= '0' && key <= '9'){
                    // this key is part of a movement multiplier
                    config_state->movement_multiplier *= 10;
                    config_state->movement_multiplier += key - '0';
                    return true;
               }else{
                    // this key is part of a movement
                    config_state->movement_keys[0] = key;
               }
          }else{
               // the previous key was part of a movement
               assert(config_state->command_multiplier != 0);
               assert(config_state->command_key != '\0');
               assert(config_state->movement_multiplier != 0);
               assert(config_state->movement_keys[0] != '\0');

               // this key is part of a movement
               assert(!is_movement_buffer_full(config_state));
               int move_key_idx = movement_buffer_len(config_state->movement_keys, sizeof config_state->movement_keys);
               config_state->movement_keys[move_key_idx] = key;
          }

          assert(config_state->command_multiplier != 0);
          assert(config_state->command_key != '\0');
          // The movement (and its multiplier) may or may not be set at this point. Some commands, like 'G', don't
          // require a movement (or a movement multiplier). Other commands, like 'd', require a movement and therefore
          // must handle the case a movement is not available yet.

          switch(config_state->command_key){
          default:
               break;
          case 27: // ESC
          {
               if(config_state->input){
                    input_end();
                    config_state->input = false;
               }
               if(config_state->vim_mode != VM_NORMAL){
                    enter_normal_mode(config_state);
               }
          } break;
          case KEY_MOUSE:
               handle_mouse_event(config_state, buffer, buffer_state, buffer_view, cursor);
               break;
          case 'J':
          {
               if(cursor->y == buffer->line_count - 1) break; // nothing to join
               Point join_loc = {strlen(buffer->lines[cursor->y]), cursor->y};
               Point end_join_loc = {0, cursor->y+1};
               ce_move_cursor_to_soft_beginning_of_line(buffer, &end_join_loc);
               if(!end_join_loc.x) end_join_loc = join_loc;
               else end_join_loc.x--;
               char* save_str = ce_dupe_string(buffer, &join_loc, &end_join_loc);
               assert(save_str[0] == '\n');
               if(ce_remove_string(buffer, &join_loc, ce_compute_length(buffer, &join_loc, &end_join_loc))){
                    ce_insert_string(buffer, &join_loc, " ");
                    ce_commit_change_string(&buffer_state->commit_tail, &join_loc, cursor, cursor, strdup("\n"), save_str);
               }
          } break;
          case KEY_UP:
          case KEY_DOWN:
          case 'j':
          case 'k':
               for(size_t cm = 0; cm < config_state->command_multiplier; cm++){
                    ce_move_cursor(buffer, cursor, (Point){0, (config_state->command_key == 'j' || config_state->command_key == KEY_DOWN) ? 1 : -1});
               }
          break;
          case 'B':
          case 'b':
          {
               for(size_t cm = 0; cm < config_state->command_multiplier; cm++){
                    ce_move_cursor_to_beginning_of_word(buffer, cursor, key == 'b');
               }
          } break;
          case '^':
          {
               ce_move_cursor_to_soft_beginning_of_line(buffer, cursor);
          } break;
          case '0':
          {
               ce_move_cursor_to_beginning_of_line(buffer, cursor);
          } break;
          case 'h':
          case 'l':
          case 'w':
          case 'W':
          case '$':
          case 'e':
          case 'E':
          case 'f':
          case 't':
          case 'F':
          case 'T':
          case KEY_LEFT:
          case KEY_RIGHT:
          {
               config_state->movement_keys[0] = config_state->command_key;
               config_state->movement_multiplier = 1;

               for(size_t cm = 0; cm < config_state->command_multiplier; cm++){
                    movement_state_t m_state = try_generic_movement(config_state, buffer, cursor, &movement_start, &movement_end);
                    if(m_state == MOVEMENT_CONTINUE) return true;

                    // this is a generic movement
                    if(movement_end.x == cursor->x && movement_end.y == cursor->y)
                         ce_set_cursor(buffer, cursor, &movement_start);
                    else
                         ce_set_cursor(buffer, cursor, &movement_end);
               }
          } break;
          case 'G':
          {
               config_state->movement_keys[0] = config_state->command_key;
               config_state->movement_multiplier = 1;

               for(size_t cm = 0; cm < config_state->command_multiplier; cm++){
                    movement_state_t m_state = try_generic_movement(config_state, buffer, cursor, &movement_start, &movement_end);
                    if(m_state == MOVEMENT_CONTINUE) return true;

                    movement_end.x = 0; // G has a slightly different behavior as a command
                    ce_set_cursor(buffer, cursor, &movement_end);
               }
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

               // indent if necessary
               int64_t indent_len = ce_get_indentation_for_next_line(buffer, cursor, strlen(TAB_STRING));
               char* indent_nl = malloc(sizeof '\n' + indent_len + sizeof '\0');
               memset(&indent_nl[0], ' ', indent_len);
               indent_nl[indent_len] = '\n';
               indent_nl[indent_len + 1] = '\0';

               if(ce_insert_string(buffer, &begin_line, indent_nl)){
                    Point next_cursor = {indent_len, cursor->y};
                    ce_commit_insert_string(&buffer_state->commit_tail, &begin_line, cursor, &next_cursor, indent_nl);
                    *cursor = next_cursor;
                    enter_insert_mode(config_state, cursor);
               }
          } break;
          case 'o':
          {
               Point end_of_line = *cursor;
               end_of_line.x = strlen(buffer->lines[cursor->y]);

               // indent if necessary
               int64_t indent_len = ce_get_indentation_for_next_line(buffer, cursor, strlen(TAB_STRING));
               char* nl_indent = malloc(sizeof '\n' + indent_len + sizeof '\0');
               nl_indent[0] = '\n';
               memset(&nl_indent[1], ' ', indent_len);
               nl_indent[1 + indent_len] = '\0';

               if(ce_insert_string(buffer, &end_of_line, nl_indent)){
                    Point next_cursor = {indent_len, cursor->y+1};
                    ce_commit_insert_string(&buffer_state->commit_tail, &end_of_line, cursor, &next_cursor, nl_indent);
                    *cursor = next_cursor;
                    enter_insert_mode(config_state, cursor);
               }
          } break;
          case 'A':
          {
               cursor->x = strlen(buffer->lines[cursor->y]);
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
               if(config_state->vim_mode == VM_VISUAL_RANGE){
                    yank_visual_range(config_state);
                    enter_normal_mode(config_state);
               }else if(config_state->vim_mode == VM_VISUAL_LINE){
                    yank_visual_lines(config_state);
                    enter_normal_mode(config_state);
               }else{
                    movement_state_t m_state = try_generic_movement(config_state, buffer, cursor, &movement_start, &movement_end);
                    switch(m_state){
                    case MOVEMENT_CONTINUE:
                         return true; // no movement yet, wait for one!
                    case MOVEMENT_COMPLETE:
                    {
                         YankMode yank_mode; 
                         switch(config_state->movement_keys[0]){
                         case KEY_UP:
                         case KEY_DOWN:
                         case 'j':
                         case 'k':
                              yank_mode = YANK_LINE;
                              break;
                         default:
                              yank_mode = YANK_NORMAL; 
                         }
                         if(strchr("wW", config_state->movement_keys[0])){
                              movement_end.x--; // exclude movement_end char
                              assert(movement_end.x >= 0);
                         }
                         add_yank(config_state, '0', ce_dupe_string(buffer, &movement_start, &movement_end), yank_mode);
                         add_yank(config_state, '"', ce_dupe_string(buffer, &movement_start, &movement_end), yank_mode);
                    } break;
                    case MOVEMENT_INVALID:
                         switch(config_state->movement_keys[0]){
                         case 'y':
                              add_yank(config_state, '0', strdup(buffer->lines[cursor->y]), YANK_LINE);
                              add_yank(config_state, '"', strdup(buffer->lines[cursor->y]), YANK_LINE);
                              break;
                         default:
                              break;
                         }
                         break;
                    }
               }
          } break;
          case 'P':
          case 'p':
          {
               YankNode* yank = find_yank(config_state, '"');
               if(yank){
                    // NOTE: unsure why this isn't plug and play !
                    if(config_state->vim_mode == VM_VISUAL_RANGE){
                         remove_visual_range(config_state);
                    }else if(config_state->vim_mode == VM_VISUAL_LINE){
                         remove_visual_lines(config_state);
                    }

                    switch(yank->mode){
                    case YANK_LINE:
                    {
                         size_t len = strlen(yank->text);
                         char* save_str = malloc(len + 2); // newline and '\0'
                         Point insert_loc;
                         Point cursor_loc;
                         if(config_state->command_key == 'p'){
                              // TODO: bring this all into a ce_commit_insert_line function

                              save_str[0] = '\n'; // prepend a new line to create a line
                              memcpy(save_str + 1, yank->text, len + 1); // also copy the '\0'

                              cursor_loc = (Point){0, cursor->y+1};

                              // insert at the end of the current line
                              insert_loc = (Point){strlen(buffer->lines[cursor->y]), cursor->y};
                         }
                         else{
                              save_str[len] = '\n'; // append a new line to create a line
                              save_str[len+1] = '\0';
                              memcpy(save_str, yank->text, len);

                              cursor_loc = (Point){0, cursor->y};

                              // insert at the beginning of the current line
                              insert_loc = (Point){0, cursor->y};
                         }

                         bool res = ce_insert_string(buffer, &insert_loc, save_str);
                         assert(res);
                         ce_commit_insert_string(&buffer_state->commit_tail,
                                                 &insert_loc, cursor, &cursor_loc,
                                                 save_str);
                         ce_set_cursor(buffer, cursor, &cursor_loc);
                    } break;
                    case YANK_NORMAL:
                    {
                         Point insert_cursor = *cursor;
                         if(config_state->command_key == 'p'){
                              if(strnlen(buffer->lines[cursor->y], 1)){
                                   insert_cursor.x++; // don't increment x for blank lines
                              } else assert(cursor->x == 0);
                         }

                         ce_insert_string(buffer, &insert_cursor, yank->text);
                         ce_commit_insert_string(&buffer_state->commit_tail,
                                                 &insert_cursor, cursor, &insert_cursor,
                                                 strdup(yank->text));
                         *cursor = insert_cursor;
                    } break;
                    }
               }
          } break;
          case 'C':
          case 'D':
          {
               if(config_state->vim_mode == VM_VISUAL_RANGE){
                    yank_visual_range(config_state);
                    remove_visual_range(config_state);
               }else if(config_state->vim_mode == VM_VISUAL_LINE){
                    yank_visual_lines(config_state);
                    remove_visual_lines(config_state);
               }else{
                    Point end = *cursor;
                    ce_move_cursor_to_end_of_line(buffer, &end);
                    int64_t n_deletes = strlen(&buffer->lines[cursor->y][cursor->x]);
                    if(n_deletes){
                         char* save_string = ce_dupe_string(buffer, cursor, &end);
                         if(ce_remove_string(buffer, cursor, n_deletes)){
                              ce_commit_remove_string(&buffer_state->commit_tail, cursor, cursor, cursor, save_string);
                              add_yank(config_state, '"', strdup(save_string), YANK_NORMAL);
                         }
                    }
               }
               if(key == 'C') enter_insert_mode(config_state, cursor);
               else ce_clamp_cursor(buffer, cursor);
          } break;
          case 'S':
          {
               config_state->movement_multiplier = 1;
               config_state->movement_keys[0] = 'c';
               config_state->command_key = 'c';
          } // fall through to 'cc'
          case 'c':
          case 'd':
          {
               if(config_state->vim_mode == VM_VISUAL_RANGE){
                    yank_visual_range(config_state);
                    remove_visual_range(config_state);
               }else if(config_state->vim_mode == VM_VISUAL_LINE){
                    yank_visual_lines(config_state);
                    remove_visual_lines(config_state);
               }else{
                    for(size_t cm = 0; cm < config_state->command_multiplier; cm++){
                         movement_state_t m_state = try_generic_movement(config_state, buffer, cursor, &movement_start, &movement_end);
                         if(m_state == MOVEMENT_CONTINUE) return true;

                         if(m_state == MOVEMENT_INVALID){
                              switch(config_state->movement_keys[0]){
                                   case 'c':
                                        movement_start = *cursor;
                                        ce_move_cursor_to_soft_beginning_of_line(buffer, &movement_start);
                                        movement_end = (Point) {ce_last_index(buffer->lines[cursor->y]), cursor->y}; // TODO: causes ce_dupe_string to fail (not on buffer)
                                        break;
                                   case 'd':
                                        ce_move_cursor(buffer, cursor, (Point){-cursor->x, 0});
                                        movement_start = (Point) {0, cursor->y};
                                        movement_end = (Point) {ce_last_index(buffer->lines[cursor->y]), cursor->y}; // TODO: causes ce_dupe_string to fail (not on buffer)
                                        break;
                                   default:
                                        // not a valid movement
                                        clear_keys(config_state);
                                        return true;
                              }
                         }
                    else if(strchr("wW", config_state->movement_keys[0])){
                         movement_end.x--; // exclude movement_end char
                         assert(movement_end.x >= 0);
                    }

                    Point yank_end = movement_end;
                    YankMode yank_mode;
                    switch(config_state->movement_keys[0]){
                         case 'd':
                         case KEY_UP:
                         case KEY_DOWN:
                         case 'j':
                         case 'k':
                             yank_mode = YANK_LINE;
                             // include the newline in the delete
                             movement_end.x = strlen(buffer->lines[movement_end.y]);
                             break;
                         case 'c':
                             yank_mode = YANK_LINE;
                             break;
                         default:
                             yank_mode = YANK_NORMAL;
                         }

                         // this is a generic movement

                         // delete all chars movement_start..movement_end inclusive
                         int64_t n_deletes = ce_compute_length(buffer, &movement_start, &movement_end);
                         if(!n_deletes){
                              // nothing to do
                              clear_keys(config_state);
                              return true;
                         }

                         char* save_string = ce_dupe_string(buffer, &movement_start, &movement_end);
                         char* yank_string = ce_dupe_string(buffer, &movement_start, &yank_end);

                         if(ce_remove_string(buffer, &movement_start, n_deletes)){
                              ce_commit_remove_string(&buffer_state->commit_tail, &movement_start, cursor, &movement_start, save_string);
                              add_yank(config_state, '"', yank_string, yank_mode);
                         }

                         *cursor = movement_start;
                    }
               }

               if(config_state->command_key=='c') enter_insert_mode(config_state, cursor);
               else ce_clamp_cursor(buffer, cursor);

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
          case KEY_SAVE:
               ce_save_buffer(buffer, buffer->filename);
               break;
          case 'v':
          {
               enter_visual_range_mode(config_state, buffer_view);
          } break;
          case 'V':
          {
               enter_visual_line_mode(config_state, buffer_view);
          }
          break;
          case 7: // Ctrl + g
          {
               split_view(config_state->view_head, config_state->view_current, false);
          } break;
          case 22: // Ctrl + v
          {
               split_view(config_state->view_head, config_state->view_current, true);
          } break;
          case KEY_CLOSE:
          {
               Point save_cursor = config_state->view_current->cursor;
               config_state->view_current->buffer->cursor = config_state->view_current->cursor;

               // if we try to quit the last view, quit !
               if(config_state->view_current == config_state->view_head &&
                  config_state->view_head->next_horizontal == NULL &&
                  config_state->view_head->next_vertical == NULL){
                    return false;
               }
               ce_remove_view(&config_state->view_head, config_state->view_current);
               BufferView* new_view = ce_find_view_at_point(config_state->view_head, &save_cursor);
               if(new_view){
                    config_state->view_current = new_view;
               }else{
                    config_state->view_current = config_state->view_head;
               }
          } break;
          case 2: // Ctrl + b
          {
               update_buffer_list_buffer(config_state, head);
               config_state->buffer_list_buffer.readonly = true;
               config_state->view_current->buffer->cursor = *cursor;
               config_state->view_current->buffer = &config_state->buffer_list_buffer;
               config_state->view_current->cursor = (Point){0, 1};
          } break;
          case 'u':
               if(buffer_state->commit_tail && buffer_state->commit_tail->commit.type != BCT_NONE){
                    ce_commit_undo(buffer, &buffer_state->commit_tail, cursor);
                    if(buffer_state->commit_tail->commit.type == BCT_NONE){
                         buffer->modified = false;
                    }
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
          case KEY_REDO:
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
          case 'r':
          {
               switch(config_state->movement_keys[0]){
               case MOVEMENT_CONTINUE:
                    // no movement yet, wait for one!
                    return true;
               default:
               {
                    char ch = 0;
                    if(ce_get_char(buffer, cursor, &ch)){
                         if(ce_set_char(buffer, cursor, key)){
                              ce_commit_change_char(&buffer_state->commit_tail, cursor, cursor, cursor, key, ch);
                         }
                    }
               }
               }
          } break;
          case 'H':
          {
               // move cursor to top line of view
               Point location = {cursor->x, buffer_view->top_row};
               ce_set_cursor(buffer, cursor, &location);
          } break;
          case 'M':
          {
               // move cursor to middle line of view
               int64_t view_height = buffer_view->bottom_right.y - buffer_view->top_left.y;
               Point location = {cursor->x, buffer_view->top_row + (view_height/2)};
               ce_set_cursor(buffer, cursor, &location);
          } break;
          case 'L':
          {
               // move cursor to bottom line of view
               int64_t view_height = buffer_view->bottom_right.y - buffer_view->top_left.y;
               Point location = {cursor->x, buffer_view->top_row + view_height};
               ce_set_cursor(buffer, cursor, &location);
          } break;
          case 'z':
               if(scroll_z_cursor(config_state)) return true;
          break;
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
                    config_state->movement_keys[1] = config_state->movement_keys[0];
                    config_state->movement_keys[0] = config_state->command_key;
                    config_state->movement_multiplier = 1;

                    for(size_t cm = 0; cm < config_state->command_multiplier; cm++){
                         movement_state_t m_state = try_generic_movement(config_state, buffer, cursor, &movement_start, &movement_end);
                         if(m_state == MOVEMENT_CONTINUE) return true;

                         ce_move_cursor_to_soft_beginning_of_line(buffer, &movement_start);
                         ce_set_cursor(buffer, cursor, &movement_start);
                    }
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

                    Buffer* file_buffer = open_file_buffer(head, filename);
                    if(file_buffer) buffer_view->buffer = file_buffer;
               } break;
               }
          } break;
          case '%':
          {
               Point delta;
               if(ce_find_delta_to_match(buffer, cursor, &delta)){
                    ce_move_cursor(buffer, cursor, delta);
               }
          } break;
          case KEY_NPAGE:
          {
               half_page_up(config_state->view_current);
          } break;
          case KEY_PPAGE:
          {
               half_page_down(config_state->view_current);
          } break;
          case 8: // Ctrl + h
          {
               if(config_state->input) break;
               // TODO: consolidate into function for use with other window movement keys, and for use in insert mode?
               Point point = {config_state->view_current->top_left.x - 2, // account for window separator
                              cursor->y - config_state->view_current->top_row + config_state->view_current->top_left.y};
               if(point.x < 0) point.x += g_terminal_dimensions->x - 1;
               switch_to_view_at_point(config_state, &point);
          }
          break;
          case 10: // Ctrl + j
               if(config_state->input){
                    input_end();
                    switch(config_state->input_key) {
                    default:
                         break;
                    case ':':
                    {
                         if(config_state->view_input->buffer->line_count){
                              int64_t line = atoi(config_state->view_input->buffer->lines[0]);
                              if(line > 0){
                                   config_state->view_current->cursor = (Point){0, line - 1};
                                   ce_move_cursor_to_soft_beginning_of_line(config_state->view_current->buffer,
                                                                            &config_state->view_current->cursor);
                              }
                         }
                    } break;
                    case 6: // Ctrl + f
                         // just grab the first line and load it as a file
                         commit_input_to_history(config_state, &config_state->load_file_history);
                         for(int64_t i = 0; i < config_state->view_input->buffer->line_count; ++i){
                              Buffer* new_buffer = open_file_buffer(head, config_state->view_input->buffer->lines[i]);
                              if(i == 0 && new_buffer){
                                   config_state->view_current->buffer = new_buffer;
                                   config_state->view_current->cursor = (Point){0, 0};
                              }
                         }
                         break;
                    case '/':
                         if(config_state->view_input->buffer->line_count){
                              commit_input_to_history(config_state, &config_state->search_history);
                              config_state->search_command.direction = CE_DOWN;
                              add_yank(config_state, '/', strdup(config_state->view_input->buffer->lines[0]), YANK_NORMAL);
                              goto search;
                         }
                         break;
                    case '?':
                         if(config_state->view_input->buffer->line_count){
                              commit_input_to_history(config_state, &config_state->search_history);
                              config_state->search_command.direction = CE_UP;
                              add_yank(config_state, '/', strdup(config_state->view_input->buffer->lines[0]), YANK_NORMAL);
                              goto search;
                         }
                         break;
                    case 24: // Ctrl + x
                    {
                         commit_input_to_history(config_state, &config_state->shell_command_history);

                         // if we found an existing command buffer, clear it and use it
                         Buffer* command_buffer = config_state->shell_command_buffer;
                         command_buffer->readonly = false;
                         ce_clear_lines(command_buffer);
                         command_buffer->cursor = (Point){0, 0};
                         BufferView* command_view = ce_buffer_in_view(config_state->view_head, command_buffer);

                         if(command_view){
                              command_view->cursor = (Point){0, 0};
                              command_view->top_row = 0;
                         }else{
                              config_state->view_current->buffer = command_buffer;
                              config_state->view_current->cursor = (Point){0, 0};
                              config_state->view_current->top_row = 0;
                              command_view = config_state->view_current;
                         }

                         assert(command_view);
                         for(int64_t i = 0; i < config_state->view_input->buffer->line_count; ++i){
                              // run the command
                              char cmd[BUFSIZ];
                              snprintf(cmd, BUFSIZ, "%s 2>&1", config_state->view_input->buffer->lines[i]);
                              FILE* pfile = popen(cmd, "r");

                              // append the command
                              snprintf(cmd, BUFSIZ, "+ %s", config_state->view_input->buffer->lines[i]);
                              ce_append_line(command_buffer, cmd);

                              // load one line at a time
                              while(fgets(cmd, BUFSIZ, pfile) != NULL){
                                   // strip newline
                                   size_t cmd_len = strlen(cmd);
                                   assert(cmd[cmd_len-1] == NEWLINE);
                                   cmd[cmd_len-1] = 0;

                                   ce_append_line(command_buffer, cmd);

                                   scroll_view_to_last_line(command_view);
                                   command_view->cursor.y = command_view->top_row;

                                   erase();
                                   view_drawer(head, user_data);
                                   refresh();
                              }

                              // append the return code
                              int exit_code = pclose(pfile);
                              snprintf(cmd, BUFSIZ, "+ exit %d", WEXITSTATUS(exit_code));
                              ce_append_line(command_buffer, cmd);

                              // add blank for readability
                              ce_append_line(command_buffer, "");

                              scroll_view_to_last_line(command_view);
                              command_view->cursor.y = command_view->top_row;
                         }
                         command_buffer->readonly = true;
                         command_buffer->modified = false;
                    } break;
                    }
               }else if(config_state->view_current->buffer == &config_state->buffer_list_buffer){
                    int64_t line = cursor->y - 1; // account for buffer list row header
                    if(line < 0) break;
                    BufferNode* itr = head;

                    while(line > 0){
                         itr = itr->next;
                         if(!itr) break;
                         line--;
                    }

                    if(!itr) break;

                    config_state->view_current->buffer = itr->buffer;
                    config_state->view_current->cursor = itr->buffer->cursor;
                    center_view(config_state->view_current);
               }else if(config_state->view_current->buffer == config_state->shell_command_buffer){
                    BufferView* view_to_change = config_state->view_current;
                    if(view_to_change->next_horizontal) view_to_change = view_to_change->next_horizontal;
                    else if(view_to_change->next_vertical) view_to_change = view_to_change->next_vertical;
                    goto_file_destination_in_buffer(head, config_state->shell_command_buffer, cursor->y,
                                                    config_state->view_head, view_to_change);
               }else{
                    Point point = {cursor->x - config_state->view_current->left_column + config_state->view_current->top_left.x,
                         config_state->view_current->bottom_right.y + 2}; // account for window separator
                    if(point.y >= g_terminal_dimensions->y - 1) point.y = 0;
                    switch_to_view_at_point(config_state, &point);
               }
               break;
          case 11: // Ctrl + k
          {
               if(config_state->input) break;
               Point point = {cursor->x - config_state->view_current->left_column + config_state->view_current->top_left.x,
                              config_state->view_current->top_left.y - 2}; // account for window separator
               switch_to_view_at_point(config_state, &point);
          }
          break;
          case 12: // Ctrl + l
          {
               if(config_state->input) break;
               Point point = {config_state->view_current->bottom_right.x + 2, // account for window separator
                              cursor->y - config_state->view_current->top_row + config_state->view_current->top_left.y};
               if(point.x >= g_terminal_dimensions->x - 1) point.x = 0;
               switch_to_view_at_point(config_state, &point);
          }
          break;
          case ':':
          {
               if(config_state->input) break;
               config_state->input = true;
               input_start(config_state, "Goto Line", key);
          }
          break;
          case '#':
          {
               if(!buffer->lines[cursor->y]) break;

               Point word_start, word_end;
               if(!ce_get_word_at_location(buffer, cursor, &word_start, &word_end)) break;
               char* search_str = ce_dupe_string(buffer, &word_start, &word_end);
               add_yank(config_state, '/', search_str, YANK_NORMAL);
               config_state->search_command.direction = CE_UP;
               goto search;
          } break;
          case '*':
          {
               if(!buffer->lines[cursor->y]) break;

               Point word_start, word_end;
               if(!ce_get_word_at_location(buffer, cursor, &word_start, &word_end)) break;
               char* search_str = ce_dupe_string(buffer, &word_start, &word_end);
               add_yank(config_state, '/', search_str, YANK_NORMAL);
               config_state->search_command.direction = CE_DOWN;
               goto search;
          } break;
          case '/':
          {
               if(config_state->input) break;
               config_state->input = true;
               input_start(config_state, "Search", key);
               break;
          }
          case '?':
          {
               if(config_state->input) break;
               config_state->input = true;
               input_start(config_state, "Reverse Search", key);
               break;
          }
          case 'n':
search:
          {
               YankNode* yank = find_yank(config_state, '/');
               if(yank){
                    assert(yank->mode == YANK_NORMAL);
                    Point match;
                    if(ce_find_string(buffer, cursor, yank->text, &match, config_state->search_command.direction)){
                         ce_set_cursor(buffer, cursor, &match);
                         center_view(config_state->view_current);
                    }
               }
          } break;
          case 'N':
          {
               YankNode* yank = find_yank(config_state, '/');
               if(yank){
                    assert(yank->mode == YANK_NORMAL);
                    Point match;
                    if(ce_find_string(buffer, cursor, yank->text, &match, ce_reverse_direction(config_state->search_command.direction))){
                         ce_set_cursor(buffer, cursor, &match);
                         center_view(config_state->view_current);
                    }
               }
          } break;
          break;
          case '=':
          {
               if(config_state->movement_keys[0] == MOVEMENT_CONTINUE) return true;
               else{
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

                    // TODO support undo with clang-format
                    ce_commits_free(buffer_state->commit_tail);
                    buffer_state->commit_tail = NULL;

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
          case 24: // Ctrl + x
          {
               if(config_state->input) break;
               config_state->input = true;
               input_start(config_state, "Shell Command", key);
          } break;
          case 14: // Ctrl + n
               if(config_state->input){
                    iterate_history_input(config_state, false);
               }else{
                    jump_to_next_shell_command_file_destination(head, config_state, true);
               }
               break;
          case 16: // Ctrl + p
               if(config_state->input){
                    iterate_history_input(config_state, true);
               }else{
                    jump_to_next_shell_command_file_destination(head, config_state, false);
               }
               break;
          case 6: // Ctrl + f
          {
               if(config_state->input) break;
               config_state->input = true;
               input_start(config_state, "Load File", key);
          } break;
          }
     }

     // if we've made it to here, the command is complete
     clear_keys(config_state);

     return true;
}

void view_drawer(const BufferNode* head, void* user_data)
{
     (void)(head);
     ConfigState* config_state = user_data;
     Buffer* buffer = config_state->view_current->buffer;
     BufferView* buffer_view = config_state->view_current;
     Point* cursor = &config_state->view_current->cursor;

     Point top_left = {0, 0};
     Point bottom_right = {g_terminal_dimensions->x - 1, g_terminal_dimensions->y - 2}; // account for statusbar
     int64_t input_view_height = config_state->view_input->buffer->line_count;
     if(input_view_height) input_view_height--;
     Point input_top_left = {0, (g_terminal_dimensions->y - 2) - input_view_height};
     if(input_top_left.y < 1) input_top_left.y = 1; // clamp to growing to 1, account for input message
     Point input_bottom_right = {g_terminal_dimensions->x - 1, g_terminal_dimensions->y - 2}; // account for statusbar
     ce_calc_views(config_state->view_head, &top_left, &bottom_right);
     
     if(ce_buffer_in_view(config_state->view_head, &config_state->buffer_list_buffer)){
          update_buffer_list_buffer(config_state, head);
     }

     if(config_state->input){
          ce_calc_views(config_state->view_input, &input_top_left, &input_bottom_right);
     }

     view_follow_cursor(buffer_view);

     // setup highlight
     if(config_state->vim_mode == VM_VISUAL_RANGE){
          const Point* start = &config_state->visual_start;
          const Point* end = &config_state->view_current->cursor;

          ce_sort_points(&start, &end);

          buffer->highlight_start = *start;
          buffer->highlight_end = *end;
     }else if(config_state->vim_mode == VM_VISUAL_LINE){
          int64_t start_line = config_state->visual_start.y;
          int64_t end_line = config_state->view_current->cursor.y;

          if(start_line > end_line){
               int64_t tmp = start_line;
               start_line = end_line;
               end_line = tmp;
          }

          buffer->highlight_start = (Point){0, start_line};
          buffer->highlight_end = (Point){strlen(config_state->view_current->buffer->lines[end_line]), end_line};
     }else{
          buffer->highlight_start = (Point){0, 0};
          buffer->highlight_end = (Point){-1, 0};
     }

     standend();

     const char* search = NULL;
     YankNode* yank = find_yank(config_state, '/');
     if(yank) search = yank->text;

     // NOTE: always draw from the head
     ce_draw_views(config_state->view_head, search);

     if(config_state->input){
          attron(A_REVERSE);
          move(input_top_left.y - 1, 0);
          for(int i = 0; i < g_terminal_dimensions->x; ++i) addch(' ');
          mvprintw(input_top_left.y - 1, 0, "%s (ctrl+n next ctrl+p prev)", config_state->input_message);
          attroff(A_REVERSE);
          for(int y = input_top_left.y; y <= input_bottom_right.y; ++y){
               move(y, 0);
               clrtoeol();
          }
          ce_draw_views(config_state->view_input, search);
     }

     attron(A_REVERSE);
     // draw all blanks at the bottom
     move(g_terminal_dimensions->y - 1, 0);
     for(int i = 0; i < g_terminal_dimensions->x; ++i) addch(' ');

     static const char* mode_names[] = {
          "NORMAL",
          "INSERT",
          "VISUAL",
          "VISUAL LINE",
          "VISUAL BLOCK",
     };

     // draw the status line
     mvprintw(g_terminal_dimensions->y - 1, 0, "%s %s%s%s %ld lines, k %s %d, c %ld, %ld, v %ld, %ld -> %ld, %ld t: %ld, %ld",
              mode_names[config_state->vim_mode], modified_string(buffer), readonly_string(buffer), buffer->filename, buffer->line_count, keyname(config_state->last_key), config_state->last_key,
              cursor->x, cursor->y, buffer_view->top_left.x, buffer_view->top_left.y, buffer_view->bottom_right.x, buffer_view->bottom_right.y, g_terminal_dimensions->x, g_terminal_dimensions->y);
     attroff(A_REVERSE);

     // reset the cursor
     move(cursor->y - buffer_view->top_row + buffer_view->top_left.y,
          cursor->x - buffer_view->left_column + buffer_view->top_left.x);
}

