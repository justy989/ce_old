#include "ce.h"
#include <assert.h>
#include <ctype.h>
#include <ftw.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>

#define TAB_STRING "     "
#define SCROLL_LINES 1

const char* modified_string(Buffer_t* buffer)
{
     return buffer->modified ? "*" : "";
}

const char* readonly_string(Buffer_t* buffer)
{
     return buffer->readonly ? " [RO]" : "";
}

typedef struct{
     char** commands;
     int64_t command_count;
     Buffer_t* output_buffer;
     BufferNode_t* buffer_node_head;
     BufferView_t* view_head;
     BufferView_t* view_current;
     int shell_command_input_fd;
     int shell_command_output_fd;
     void* user_data;
} ShellCommandData_t;

typedef struct{
     char** input;
     int64_t input_count;
     Buffer_t* shell_command_buffer;
     int shell_command_input_fd;
     int shell_command_output_fd;
} ShellInputData_t;

ShellCommandData_t shell_command_data;
pthread_mutex_t draw_lock;
pthread_mutex_t shell_buffer_lock;

int64_t count_digits(int64_t n)
{
     int count = 0;
     while(n > 0){
          n /= 10;
          count++;
     }

     return count;
}

void view_drawer(const BufferNode_t* head, void* user_data);

typedef struct BackspaceNode_t{
     char c;
     struct BackspaceNode_t* next;
} BackspaceNode_t;

BackspaceNode_t* backspace_push(BackspaceNode_t** head, char c)
{
     BackspaceNode_t* new_node = malloc(sizeof(*new_node));
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
char* backspace_get_string(BackspaceNode_t* head)
{
     int64_t len = 0;
     BackspaceNode_t* itr = head;
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

void backspace_free(BackspaceNode_t** head)
{
     while(*head){
          BackspaceNode_t* tmp = *head;
          *head = (*head)->next;
          free(tmp);
     }
}

// TODO: move this to ce.h
typedef struct InputHistoryNode_t {
     char* entry;
     struct InputHistoryNode_t* next;
     struct InputHistoryNode_t* prev;
} InputHistoryNode_t;

typedef struct {
     InputHistoryNode_t* head;
     InputHistoryNode_t* tail;
     InputHistoryNode_t* cur;
} InputHistory_t;

bool input_history_init(InputHistory_t* history)
{
     InputHistoryNode_t* node = calloc(1, sizeof(*node));
     if(!node){
          ce_message("%s() failed to malloc input history node", __FUNCTION__);
          return false;
     }

     history->head = node;
     history->tail = node;
     history->cur = node;

     return true;
}

bool input_history_commit_current(InputHistory_t* history)
{
     assert(history->tail);
     assert(history->cur);

     InputHistoryNode_t* node = calloc(1, sizeof(*node));
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

bool input_history_update_current(InputHistory_t* history, char* duped)
{
     if(!history->cur) return false;
     if(history->cur->entry) free(history->cur->entry);
     history->cur->entry = duped;
     return true;
}

void input_history_free(InputHistory_t* history)
{
     while(history->head){
          free(history->head->entry);
          InputHistoryNode_t* tmp = history->head;
          history->head = history->head->next;
          free(tmp);
     }

     history->tail = NULL;
     history->cur = NULL;
}

bool input_history_next(InputHistory_t* history)
{
     if(!history->cur) return false;
     if(!history->cur->next) return false;

     history->cur = history->cur->next;
     return true;
}

bool input_history_prev(InputHistory_t* history)
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
} VimMode_t;

// TODO: move yank stuff to ce.h
typedef enum{
     YANK_NORMAL,
     YANK_LINE,
} YankMode_t;

typedef struct YankNode_t{
     char reg_char;
     const char* text;
     YankMode_t mode;
     struct YankNode_t* next;
} YankNode_t;

typedef struct TabView_t{
     BufferView_t* view_head;
     BufferView_t* view_current;
     BufferView_t* view_previous;
     BufferView_t* view_input_save;
     struct TabView_t* next;
} TabView_t;

TabView_t* tab_view_insert(TabView_t* head)
{
     // find the tail
     TabView_t* itr = head; // if we use tab_current, we *may* be closer to the tail !
     while(itr->next) itr = itr->next;

     // create the new tab
     TabView_t* new_tab = calloc(1, sizeof(*new_tab));
     if(!new_tab){
          ce_message("failed to allocate tab");
          return NULL;
     }

     // attach to the end of the tail
     itr->next = new_tab;

     // allocate the view
     new_tab->view_head = calloc(1, sizeof(*new_tab->view_head));
     if(!new_tab->view_head){
          ce_message("failed to allocate new view for tab");
          return NULL;
     }

     return new_tab;
}

void tab_view_remove(TabView_t** head, TabView_t* view)
{
     if(!*head || !view) return;

     // TODO: free any open views
     TabView_t* tmp = *head;
     if(*head == view){
          *head = view->next;
          free(tmp);
          return;
     }

     while(tmp->next != view) tmp = tmp->next;
     tmp->next = view->next;
     free(view);
}

typedef struct{
     VimMode_t vim_mode;
     bool input;
     const char* input_message;
     char input_key;
     Buffer_t* shell_command_buffer; // Allocate so it can be part of the buffer list and get free'd at the end
     Buffer_t input_buffer;
     Buffer_t buffer_list_buffer;
     int64_t last_command_buffer_jump;
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
          Direction_t direction;
     } search_command;
     Point_t start_insert;
     Point_t original_start_insert;
     Point_t visual_start;
     struct YankNode_t* yank_head;
     TabView_t* tab_head;
     TabView_t* tab_current;
     BufferView_t* view_input;
     InputHistory_t shell_command_history;
     InputHistory_t shell_input_history;
     InputHistory_t search_history;
     InputHistory_t load_file_history;
     Point_t start_search;
     pthread_t shell_command_thread;
     pthread_t shell_input_thread;
} ConfigState_t;

typedef struct MarkNode_t{
     char reg_char;
     Point_t location;
     struct MarkNode_t* next;
} MarkNode_t;

typedef struct{
     BufferCommitNode_t* commit_tail;
     BackspaceNode_t* backspace_head;
     struct MarkNode_t* mark_head;
} BufferState_t;

YankNode_t* find_yank(ConfigState_t* config, char reg_char){
     YankNode_t* itr = config->yank_head;
     while(itr != NULL){
          if(itr->reg_char == reg_char) return itr;
          itr = itr->next;
     }
     return NULL;
}

// for now the yanked string is user allocated. eventually will probably
// want to change this interface so that everything is hidden
void add_yank(ConfigState_t* config, char reg_char, const char* yank_text, YankMode_t mode){
     YankNode_t* node = find_yank(config, reg_char);
     if(node != NULL){
          free((void*)node->text);
     }
     else{
          YankNode_t* new_yank = malloc(sizeof(*config->yank_head));
          new_yank->reg_char = reg_char;
          new_yank->next = config->yank_head;
          node = new_yank;
          config->yank_head = new_yank;
     }
     node->text = yank_text;
     node->mode = mode;
}

Point_t* find_mark(BufferState_t* buffer, char mark_char)
{
     MarkNode_t* itr = buffer->mark_head;
     while(itr != NULL){
          if(itr->reg_char == mark_char) return &itr->location;
          itr = itr->next;
     }
     return NULL;
}

void add_mark(BufferState_t* buffer, char mark_char, const Point_t* location)
{
     Point_t* mark_location = find_mark(buffer, mark_char);
     if(!mark_location){
          MarkNode_t* new_mark = malloc(sizeof(*buffer->mark_head));
          new_mark->reg_char = mark_char;
          new_mark->next = buffer->mark_head;
          buffer->mark_head = new_mark;
          mark_location = &new_mark->location;
     }
     *mark_location = *location;
}

bool initialize_buffer(Buffer_t* buffer){
     BufferState_t* buffer_state = calloc(1, sizeof(*buffer_state));
     if(!buffer_state){
          ce_message("failed to allocate buffer state.");
          return false;
     }

     BufferCommitNode_t* tail = calloc(1, sizeof(*tail));
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

Buffer_t* new_buffer_from_string(BufferNode_t* head, const char* name, const char* str){
     Buffer_t* buffer = calloc(1, sizeof(*buffer));
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

     BufferNode_t* new_buffer_node = ce_append_buffer_to_list(head, buffer);
     if(!new_buffer_node){
          free(buffer->name);
          free(buffer->user_data);
          free(buffer);
          return NULL;
     }

     return buffer;
}

BufferNode_t* new_buffer_from_file(BufferNode_t* head, const char* filename)
{
     Buffer_t* buffer = calloc(1, sizeof(*buffer));
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

     BufferNode_t* new_buffer_node = ce_append_buffer_to_list(head, buffer);
     if(!new_buffer_node){
          free(buffer->filename);
          free(buffer->user_data);
          free(buffer);
          return NULL;
     }

     return new_buffer_node;
}

void enter_normal_mode(ConfigState_t* config_state)
{
     config_state->vim_mode = VM_NORMAL;
}

void enter_insert_mode(ConfigState_t* config_state, Point_t* cursor)
{
     if(config_state->tab_current->view_current->buffer->readonly) return;
     config_state->vim_mode = VM_INSERT;
     config_state->start_insert = *cursor;
     config_state->original_start_insert = *cursor;
}

void enter_visual_range_mode(ConfigState_t* config_state, BufferView_t* buffer_view)
{
     config_state->vim_mode = VM_VISUAL_RANGE;
     config_state->visual_start = buffer_view->cursor;
}

void enter_visual_line_mode(ConfigState_t* config_state, BufferView_t* buffer_view)
{
     config_state->vim_mode = VM_VISUAL_LINE;
     config_state->visual_start = buffer_view->cursor;
}

void commit_insert_mode_changes(ConfigState_t* config_state, Buffer_t* buffer, BufferState_t* buffer_state, Point_t* cursor, Point_t* end_cursor)
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
               Point_t last_inserted_char = {cursor->x, cursor->y};
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
                    Point_t last_inserted_char = {end_cursor->x, end_cursor->y};
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

void clear_keys(ConfigState_t* config_state)
{
     config_state->command_multiplier = 0;
     config_state->command_key = '\0';
     config_state->movement_multiplier = 0;
     memset(config_state->movement_keys, 0, sizeof config_state->movement_keys);
}

// location is {left_column, top_line} for the view
void scroll_view_to_location(BufferView_t* buffer_view, const Point_t* location){
     // TODO: we should be able to scroll the view above our first line
     buffer_view->left_column = (location->x >= 0) ? location->x : 0;
     buffer_view->top_row = (location->y >= 0) ? location->y : 0;
}

void center_view(BufferView_t* view)
{
     int64_t view_height = view->bottom_right.y - view->top_left.y;
     Point_t location = (Point_t) {0, view->cursor.y - (view_height / 2)};
     scroll_view_to_location(view, &location);
}

InputHistory_t* history_from_input_key(ConfigState_t* config_state)
{
     InputHistory_t* history = NULL;

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
     case 9: // Ctrl + i
          history = &config_state->shell_input_history;
          break;
     case 6: // Ctrl + f
          history = &config_state->load_file_history;
          break;
     }

     return history;
}

void input_start(ConfigState_t* config_state, const char* input_message, char input_key)
{
     ce_clear_lines(config_state->view_input->buffer);
     ce_alloc_lines(config_state->view_input->buffer, 1);
     config_state->input = true;
     config_state->view_input->cursor = (Point_t){0, 0};
     config_state->input_message = input_message;
     config_state->input_key = input_key;
     config_state->tab_current->view_input_save = config_state->tab_current->view_current;
     config_state->tab_current->view_current = config_state->view_input;
     enter_insert_mode(config_state, &config_state->view_input->cursor);

     // reset input history back to tail
     InputHistory_t* history = history_from_input_key(config_state);
     if(history) history->cur = history->tail;
}

void input_end(ConfigState_t* config_state)
{
     config_state->input = false;
     config_state->tab_current->view_current = config_state->tab_current->view_input_save; \
}

void input_cancel(ConfigState_t* config_state)
{
     if(config_state->input_key == '/' || config_state->input_key == '?'){
          config_state->tab_current->view_input_save->cursor = config_state->start_search;
          center_view(config_state->tab_current->view_input_save);
     }
     input_end(config_state);
}

#ifndef FTW_STOP
#define FTW_STOP 1
#define FTW_CONTINUE 0
#endif

typedef struct {
     const char* search_filename;
     BufferNode_t* head;
     BufferNode_t* new_node;
} NftwState_t;
__thread NftwState_t nftw_state;

int nftw_find_file(const char* fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
     (void)sb;
     if((typeflag == FTW_F || typeflag == FTW_SL) && !strcmp(&fpath[ftwbuf->base], nftw_state.search_filename)){
          nftw_state.new_node = new_buffer_from_file(nftw_state.head, nftw_state.search_filename);
          return FTW_STOP;
     }
     return FTW_CONTINUE;
}

Buffer_t* open_file_buffer(BufferNode_t* head, const char* filename)
{
     BufferNode_t* itr = head;
     while(itr){
          if(!strcmp(itr->buffer->name, filename)){
               return itr->buffer; // already open
          }
          itr = itr->next;
     }

     if(access(filename, F_OK) == 0){
          BufferNode_t* node = new_buffer_from_file(head, filename);
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

bool initializer(BufferNode_t* head, Point_t* terminal_dimensions, int argc, char** argv, void** user_data)
{
     // NOTE: need to set these in this module
     g_terminal_dimensions = terminal_dimensions;

     // setup the config's state
     ConfigState_t* config_state = calloc(1, sizeof(*config_state));
     if(!config_state){
          ce_message("failed to allocate config state");
          return false;
     }

     config_state->tab_head = calloc(1, sizeof(*config_state->tab_head));
     if(!config_state->tab_head){
          ce_message("failed to allocate tab");
          return false;
     }

     config_state->tab_current = config_state->tab_head;

     config_state->tab_head->view_head = calloc(1, sizeof(*config_state->tab_head->view_head));
     if(!config_state->tab_head->view_head){
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
     initialize_buffer(&config_state->buffer_list_buffer);
     config_state->buffer_list_buffer.readonly = true;

     // setup shell command buffer
     bool found_shell_command_buffer = false;

     // if we reload, the shell command buffer may already exist, don't recreate it
     BufferNode_t* itr = head;
     while(itr){
          if(strcmp(itr->buffer->name, "shell_output") == 0){
               found_shell_command_buffer = true;
               config_state->shell_command_buffer = itr->buffer;
               break;
          }
          itr = itr->next;
     }

     if(!found_shell_command_buffer){
          config_state->shell_command_buffer = calloc(1, sizeof(*config_state->shell_command_buffer));
          config_state->shell_command_buffer->name = strdup("shell_output");
          config_state->shell_command_buffer->readonly = true;
          initialize_buffer(config_state->shell_command_buffer);
          ce_alloc_lines(config_state->shell_command_buffer, 1);
          BufferNode_t* new_buffer_node = ce_append_buffer_to_list(head, config_state->shell_command_buffer);
          if(!new_buffer_node){
               ce_message("failed to add shell command buffer to list");
               return false;
          }
     }

     *user_data = config_state;

     // setup state for each buffer
     itr = head;
     while(itr){
          initialize_buffer(itr->buffer);
          itr = itr->next;
     }

     config_state->tab_current->view_head->buffer = head->buffer;
     config_state->tab_current->view_current = config_state->tab_current->view_head;

     for(int i = 0; i < argc; ++i){
          BufferNode_t* node = new_buffer_from_file(head, argv[i]);

          // if we loaded a file, set the view to point at the file
          if(i == 0 && node) config_state->tab_current->view_current->buffer = node->buffer;
     }

     input_history_init(&config_state->shell_command_history);
     input_history_init(&config_state->shell_input_history);
     input_history_init(&config_state->search_history);
     input_history_init(&config_state->load_file_history);

     // setup colors for syntax highlighting
     init_pair(S_NORMAL, COLOR_FOREGROUND, COLOR_BACKGROUND);
     init_pair(S_KEYWORD, COLOR_BLUE, COLOR_BACKGROUND);
     init_pair(S_TYPE, COLOR_BRIGHT_BLUE, COLOR_BACKGROUND);
     init_pair(S_CONTROL, COLOR_YELLOW, COLOR_BACKGROUND);
     init_pair(S_COMMENT, COLOR_GREEN, COLOR_BACKGROUND);
     init_pair(S_STRING, COLOR_RED, COLOR_BACKGROUND);
     init_pair(S_CONSTANT, COLOR_MAGENTA, COLOR_BACKGROUND);
     init_pair(S_PREPROCESSOR, COLOR_BRIGHT_MAGENTA, COLOR_BACKGROUND);
     init_pair(S_DIFF_ADD, COLOR_GREEN, COLOR_BACKGROUND);
     init_pair(S_DIFF_REMOVE, COLOR_RED, COLOR_BACKGROUND);

     init_pair(S_NORMAL_HIGHLIGHTED, COLOR_FOREGROUND, COLOR_BRIGHT_BLACK);
     init_pair(S_KEYWORD_HIGHLIGHTED, COLOR_BLUE, COLOR_BRIGHT_BLACK);
     init_pair(S_TYPE_HIGHLIGHTED, COLOR_BRIGHT_BLUE, COLOR_BRIGHT_BLACK);
     init_pair(S_CONTROL_HIGHLIGHTED, COLOR_YELLOW, COLOR_BRIGHT_BLACK);
     init_pair(S_COMMENT_HIGHLIGHTED, COLOR_GREEN, COLOR_BRIGHT_BLACK);
     init_pair(S_STRING_HIGHLIGHTED, COLOR_RED, COLOR_BRIGHT_BLACK);
     init_pair(S_CONSTANT_HIGHLIGHTED, COLOR_MAGENTA, COLOR_BRIGHT_BLACK);
     init_pair(S_PREPROCESSOR_HIGHLIGHTED, COLOR_BRIGHT_MAGENTA, COLOR_BRIGHT_BLACK);
     init_pair(S_DIFF_ADD_HIGHLIGHTED, COLOR_GREEN, COLOR_BRIGHT_BLACK);
     init_pair(S_DIFF_REMOVE_HIGHLIGHTED, COLOR_RED, COLOR_BRIGHT_BLACK);

     init_pair(S_TRAILING_WHITESPACE, COLOR_FOREGROUND, COLOR_RED);

     init_pair(S_BORDERS, COLOR_WHITE, COLOR_BACKGROUND);

     init_pair(S_TAB_NAME, COLOR_WHITE, COLOR_BACKGROUND);
     init_pair(S_CURRENT_TAB_NAME, COLOR_CYAN, COLOR_BACKGROUND);

     init_pair(S_VIEW_STATUS, COLOR_CYAN, COLOR_BACKGROUND);
     init_pair(S_INPUT_STATUS, COLOR_RED, COLOR_BACKGROUND);

     // Doesn't work in insert mode :<
     //define_key("h", KEY_LEFT);
     //define_key("j", KEY_DOWN);
     //define_key("k", KEY_UP);
     //define_key("l", KEY_RIGHT);

     define_key(NULL, KEY_BACKSPACE);   // Blow away backspace
     define_key("\x7F", KEY_BACKSPACE); // Backspace  (127) (0x7F) ASCII "DEL" Delete
     define_key("\x15", KEY_NPAGE);     // ctrl + d    (21) (0x15) ASCII "NAK" Negative Acknowledgement
     define_key("\x04", KEY_PPAGE);     // ctrl + u     (4) (0x04) ASCII "EOT" End of Transmission
     define_key("\x11", KEY_CLOSE);     // ctrl + q    (17) (0x11) ASCII "DC1" Device Control 1
     define_key("\x12", KEY_REDO);      // ctrl + r    (18) (0x12) ASCII "DC2" Device Control 2
     define_key("\x17", KEY_SAVE);      // ctrl + w    (23) (0x17) ASCII "ETB" End of Transmission Block
     //define_key("\x0A", KEY_ENTER);     // Enter       (10) (0x0A) ASCII "LF"  NL Line Feed, New Line

     pthread_mutex_init(&draw_lock, NULL);
     return true;
}

bool destroyer(BufferNode_t* head, void* user_data)
{
     while(head){
          BufferState_t* buffer_state = head->buffer->user_data;
          BufferCommitNode_t* itr = buffer_state->commit_tail;
          while(itr->prev) itr = itr->prev;
          ce_commits_free(itr);
          free(buffer_state);
          head->buffer->user_data = NULL;
          head = head->next;
     }

     ConfigState_t* config_state = user_data;
     TabView_t* tab_itr = config_state->tab_head;
     while(tab_itr){
          if(tab_itr->view_head){
               ce_free_views(&tab_itr->view_head);
          }
          tab_itr = tab_itr->next;
     }

     tab_itr = config_state->tab_head;
     while(tab_itr){
          TabView_t* tmp = tab_itr;
          tab_itr = tab_itr->next;
          free(tmp);
     }

     // input buffer
     {
          ce_free_buffer(&config_state->input_buffer);
          BufferState_t* buffer_state = config_state->input_buffer.user_data;
          BufferCommitNode_t* itr = buffer_state->commit_tail;
          while(itr->prev) itr = itr->prev;
          ce_commits_free(itr);

          free(config_state->view_input);
     }

     if(config_state->shell_command_thread){
          pthread_cancel(config_state->shell_command_thread);
          pthread_join(config_state->shell_command_thread, NULL);
     }

     if(config_state->shell_input_thread){
          pthread_cancel(config_state->shell_input_thread);
          pthread_join(config_state->shell_input_thread, NULL);
     }

     ce_free_buffer(&config_state->buffer_list_buffer);

     // history
     input_history_free(&config_state->shell_command_history);
     input_history_free(&config_state->shell_input_history);
     input_history_free(&config_state->search_history);
     input_history_free(&config_state->load_file_history);

     free(config_state);
     return true;
}

void find_command(int command_key, int find_char, Buffer_t* buffer, Point_t* cursor)
{
     switch(command_key){
     case 'f':
     {
          int64_t x_delta = ce_find_delta_to_char_forward_in_line(buffer, cursor, find_char);
          if(x_delta == -1) break;
          ce_move_cursor(buffer, cursor, (Point_t){x_delta, 0});
     } break;
     case 't':
     {
          Point_t search_point = {cursor->x + 1, cursor->y};
          int64_t x_delta = ce_find_delta_to_char_forward_in_line(buffer, &search_point, find_char);
          if(x_delta <= 0) break;
          ce_move_cursor(buffer, cursor, (Point_t){x_delta, 0});
     } break;
     case 'F':
     {
          int64_t x_delta = ce_find_delta_to_char_backward_in_line(buffer, cursor, find_char);
          if(x_delta == -1) break;
          ce_move_cursor(buffer, cursor, (Point_t){-x_delta, 0});
     } break;
     case 'T':
     {
          Point_t search_point = {cursor->x - 1, cursor->y};
          int64_t x_delta = ce_find_delta_to_char_backward_in_line(buffer, &search_point, find_char);
          if(x_delta <= 0) break;
          ce_move_cursor(buffer, cursor, (Point_t){-x_delta, 0});
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
movement_state_t try_generic_movement(ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor, Point_t* movement_start, Point_t* movement_end)
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
               ce_move_cursor(buffer, movement_end, (Point_t){-1, 0});
          } break;
          case KEY_DOWN:
          case 'j':
          {
               *movement_start = (Point_t){0, cursor->y};
               ce_move_cursor(buffer, movement_end, (Point_t){0, 1});
               ce_move_cursor_to_end_of_line(buffer, movement_end);
          } break;
          case KEY_UP:
          case 'k':
          {
               *movement_start = (Point_t){0, cursor->y};
               ce_move_cursor(buffer, movement_start, (Point_t){0, -1});
               ce_move_cursor_to_end_of_line(buffer, movement_end);
          } break;
          case KEY_RIGHT:
          case 'l':
          {
               ce_move_cursor(buffer, movement_end, (Point_t){1, 0});
          } break;
          case '0':
          {
               movement_start->x = 0;
               ce_move_cursor(buffer, movement_end, (Point_t){-1, 0});
          } break;
          case '^':
          {
               Point_t begin_line_cursor = *cursor;
               ce_move_cursor_to_soft_beginning_of_line(buffer, &begin_line_cursor);
               ce_move_cursor(buffer, &begin_line_cursor, (Point_t){-1, 0});
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
               ce_move_cursor(buffer, movement_end, (Point_t){-1, 0});
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
               Point_t delta;
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

bool is_movement_buffer_full(ConfigState_t* config_state)
{
     size_t max_movement_keys = sizeof config_state->movement_keys;
     size_t n_movement_keys = movement_buffer_len(config_state->movement_keys, max_movement_keys);
     return n_movement_keys == max_movement_keys;
}

void scroll_view_to_last_line(BufferView_t* view)
{
     view->top_row = view->buffer->line_count - (view->bottom_right.y - view->top_left.y);
     if(view->top_row < 0) view->top_row = 0;
}

// NOTE: clear commits then create the initial required entry to restart
void reset_buffer_commits(BufferCommitNode_t** tail)
{
     BufferCommitNode_t* node = *tail;
     while(node->prev) node = node->prev;
     ce_commits_free(node);

     BufferCommitNode_t* new_tail = calloc(1, sizeof(*new_tail));
     if(!new_tail){
          ce_message("failed to allocate commit history for buffer");
          return;
     }

     *tail = new_tail;
}

// NOTE: modify last_jump if we succeed
bool goto_file_destination_in_buffer(BufferNode_t* head, Buffer_t* buffer, int64_t line, BufferView_t* head_view,
                                     BufferView_t* view, int64_t* last_jump)
{
     if(!buffer->line_count) return false;

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

     Buffer_t* new_buffer = open_file_buffer(head, file_tmp);
     if(new_buffer){
          view->buffer = new_buffer;
          Point_t dst = {0, atoi(line_number_tmp) - 1}; // line numbers are 1 indexed
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
          BufferView_t* command_view = ce_buffer_in_view(head_view, buffer);
          if(command_view) command_view->top_row = line;
          *last_jump = line;
          return true;
     }

     return false;
}

// TODO: rather than taking in config_state, I'd like to take in only the parts it needs, if it's too much, config_state is fine
void jump_to_next_shell_command_file_destination(BufferNode_t* head, ConfigState_t* config_state, bool forwards)
{
     Buffer_t* command_buffer = config_state->shell_command_buffer;
     int64_t lines_checked = 0;
     int64_t delta = forwards ? 1 : -1;
     for(int64_t i = config_state->last_command_buffer_jump + delta; lines_checked < command_buffer->line_count;
         i += delta, lines_checked++){
          if(i == command_buffer->line_count && forwards){
               i = 0;
          }else if(i <= 0 && !forwards){
               i = command_buffer->line_count - 1;
          }

          if(goto_file_destination_in_buffer(head, command_buffer, i, config_state->tab_current->view_head,
                                             config_state->tab_current->view_current, &config_state->last_command_buffer_jump)){
               // NOTE: change the cursor, so when you go back to that buffer, your cursor is on the line we last jumped to
               command_buffer->cursor.x = 0;
               command_buffer->cursor.y = i;
               BufferView_t* command_view = ce_buffer_in_view(config_state->tab_current->view_head, command_buffer);
               if(command_view) command_view->cursor = command_buffer->cursor;
               break;
          }
     }
}

bool commit_input_to_history(Buffer_t* input_buffer, InputHistory_t* history)
{
     if(input_buffer->line_count){
          char* saved = ce_dupe_buffer(input_buffer);
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

void view_follow_cursor(BufferView_t* current_view)
{
     ce_follow_cursor(&current_view->cursor, &current_view->left_column, &current_view->top_row,
                      current_view->bottom_right.x - current_view->top_left.x,
                      current_view->bottom_right.y - current_view->top_left.y,
                      current_view->bottom_right.x == (g_terminal_dimensions->x - 1),
                      current_view->bottom_right.y == (g_terminal_dimensions->y - 2));
}

void split_view(BufferView_t* head_view, BufferView_t* current_view, bool horizontal)
{
     BufferView_t* new_view = ce_split_view(current_view, current_view->buffer, horizontal);
     if(new_view){
          Point_t top_left = {0, 0};
          Point_t bottom_right = {g_terminal_dimensions->x - 1, g_terminal_dimensions->y - 2}; // account for statusbar
          ce_calc_views(head_view, &top_left, &bottom_right);
          view_follow_cursor(current_view);
          new_view->cursor = current_view->cursor;
          new_view->top_row = current_view->top_row;
          new_view->left_column = current_view->left_column;
     }
}

void handle_mouse_event(ConfigState_t* config_state, Buffer_t* buffer, BufferState_t* buffer_state, BufferView_t* buffer_view, Point_t* cursor)
{
     MEVENT event;
     if(getmouse(&event) == OK){
          bool enter_insert;
          if((enter_insert = config_state->vim_mode == VM_INSERT)){
               Point_t end_cursor = *cursor;
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
               Point_t click = {event.x, event.y};
               config_state->tab_current->view_current = ce_find_view_at_point(config_state->tab_current->view_head, &click);
               click = (Point_t) {event.x - (config_state->tab_current->view_current->top_left.x - config_state->tab_current->view_current->left_column),
                                event.y - (config_state->tab_current->view_current->top_left.y - config_state->tab_current->view_current->top_row)};
               ce_set_cursor(config_state->tab_current->view_current->buffer,
                             &config_state->tab_current->view_current->cursor,
                             &click);
          }
#ifdef SCROLL_SUPPORT
          // This feature is currently unreliable and is only known to work for Ryan :)
          else if(event.bstate & (BUTTON_ALT | BUTTON2_CLICKED)){
               Point_t next_line = {0, cursor->y + SCROLL_LINES};
               if(ce_point_on_buffer(buffer, &next_line)){
                    Point_t scroll_location = {0, buffer_view->top_row + SCROLL_LINES};
                    scroll_view_to_location(buffer_view, &scroll_location);
                    if(buffer_view->cursor.y < buffer_view->top_row)
                         ce_move_cursor(buffer, cursor, (Point_t){0, SCROLL_LINES});
               }
          }else if(event.bstate & BUTTON4_TRIPLE_CLICKED){
               Point_t next_line = {0, cursor->y - SCROLL_LINES};
               if(ce_point_on_buffer(buffer, &next_line)){
                    Point_t scroll_location = {0, buffer_view->top_row - SCROLL_LINES};
                    scroll_view_to_location(buffer_view, &scroll_location);
                    if(buffer_view->cursor.y > buffer_view->top_row + (buffer_view->bottom_right.y - buffer_view->top_left.y))
                         ce_move_cursor(buffer, cursor, (Point_t){0, -SCROLL_LINES});
               }
          }
#else
          (void) buffer;
          (void) buffer_view;
#endif
          // if we left insert and haven't switched views, enter insert mode
          if(enter_insert && config_state->tab_current->view_current == buffer_view) enter_insert_mode(config_state, cursor);
     }
}

void half_page_up(BufferView_t* view)
{
     int64_t view_height = view->bottom_right.y - view->top_left.y;
     Point_t delta = { 0, -view_height / 2 };
     ce_move_cursor(view->buffer, &view->cursor, delta);
}

void half_page_down(BufferView_t* view)
{
     int64_t view_height = view->bottom_right.y - view->top_left.y;
     Point_t delta = { 0, view_height / 2 };
     ce_move_cursor(view->buffer, &view->cursor, delta);
}

bool scroll_z_cursor(ConfigState_t* config_state)
{
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Point_t* cursor = &buffer_view->cursor;
     Point_t location;
     switch(config_state->movement_keys[0]){
     case MOVEMENT_CONTINUE:
          // no movement yet, wait for one!
          return true;
     case 't':
          location = (Point_t){0, cursor->y};
          scroll_view_to_location(buffer_view, &location);
          break;
     case 'z': {
          center_view(buffer_view);
     } break;
     case 'b': {
          // move current line to bottom of view
          location = (Point_t){0, cursor->y - buffer_view->bottom_right.y};
          scroll_view_to_location(buffer_view, &location);
     } break;
     }

     return false;
}

void yank_visual_range(ConfigState_t* config_state)
{
     Buffer_t* buffer = config_state->tab_current->view_current->buffer;
     Point_t* cursor = &config_state->tab_current->view_current->cursor;
     Point_t start = config_state->visual_start;
     Point_t end = {cursor->x, cursor->y};

     const Point_t* a = &start;
     const Point_t* b = &end;

     ce_sort_points(&a, &b);

     add_yank(config_state, '0', ce_dupe_string(buffer, a, b), YANK_NORMAL);
     add_yank(config_state, '"', ce_dupe_string(buffer, a, b), YANK_NORMAL);
}

void yank_visual_lines(ConfigState_t* config_state)
{
     Buffer_t* buffer = config_state->tab_current->view_current->buffer;
     Point_t* cursor = &config_state->tab_current->view_current->cursor;
     int64_t start_line = config_state->visual_start.y;
     int64_t end_line = cursor->y;

     if(start_line > end_line){
          int64_t tmp = start_line;
          start_line = end_line;
          end_line = tmp;
     }

     Point_t start = {0, start_line};
     Point_t end = {ce_last_index(buffer->lines[end_line]), end_line};

     add_yank(config_state, '0', ce_dupe_string(buffer, &start, &end), YANK_LINE);
     add_yank(config_state, '"', ce_dupe_string(buffer, &start, &end), YANK_LINE);
}

void remove_visual_range(ConfigState_t* config_state)
{
     Point_t* cursor = &config_state->tab_current->view_current->cursor;
     Buffer_t* buffer = config_state->tab_current->view_current->buffer;
     BufferState_t* buffer_state = buffer->user_data;
     Point_t start = config_state->visual_start;
     Point_t end = {cursor->x, cursor->y};

     const Point_t* a = &start;
     const Point_t* b = &end;

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

void remove_visual_lines(ConfigState_t* config_state)
{
     Point_t* cursor = &config_state->tab_current->view_current->cursor;
     Buffer_t* buffer = config_state->tab_current->view_current->buffer;
     BufferState_t* buffer_state = buffer->user_data;
     int64_t start_line = config_state->visual_start.y;
     int64_t end_line = cursor->y;

     if(start_line > end_line){
          int64_t tmp = start_line;
          start_line = end_line;
          end_line = tmp;
     }

     Point_t start = {0, start_line};
     Point_t end = {ce_last_index(buffer->lines[end_line]), end_line};

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

bool iterate_history_input(ConfigState_t* config_state, bool previous)
{
     BufferState_t* buffer_state = config_state->view_input->buffer->user_data;
     InputHistory_t* history = history_from_input_key(config_state);
     if(!history) return false;

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
          config_state->view_input->cursor = (Point_t){0, 0};
          ce_move_cursor_to_end_of_file(config_state->view_input->buffer, &config_state->view_input->cursor);
          reset_buffer_commits(&buffer_state->commit_tail);
     }

     return success;
}

void switch_to_view_at_point(ConfigState_t* config_state, const Point_t* point)
{
     BufferView_t* next_view = ce_find_view_at_point(config_state->tab_current->view_head, point);
     if(next_view){
          // save view and cursor
          config_state->tab_current->view_previous = config_state->tab_current->view_current;
          config_state->tab_current->view_current->buffer->cursor = config_state->tab_current->view_current->cursor;
          config_state->tab_current->view_current = next_view;
          enter_normal_mode(config_state);
     }
}

void update_buffer_list_buffer(ConfigState_t* config_state, const BufferNode_t* head)
{
     char buffer_info[BUFSIZ];
     config_state->buffer_list_buffer.readonly = false;
     ce_clear_lines(&config_state->buffer_list_buffer);

     // calc maxes of things we care about for formatting
     int64_t max_buffer_lines = 0;
     int64_t max_name_len = 0;
     int64_t buffer_count = 0;
     const BufferNode_t* itr = head;
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
     // build header
     snprintf(format_string, BUFSIZ, "%%5s %%-%"PRId64"s %%-%"PRId64"s (%%"PRId64" buffers)", max_name_len,
              max_buffer_lines_digits);
     snprintf(buffer_info, BUFSIZ, format_string, "flags", "buffer name", "lines", buffer_count);
     ce_append_line(&config_state->buffer_list_buffer, buffer_info);

     // build buffer info
     snprintf(format_string, BUFSIZ, "%%5s %%-%"PRId64"s %%%"PRId64 PRId64, max_name_len, max_buffer_lines_digits);

     itr = head;
     while(itr){
          const char* buffer_flag_str = itr->buffer->readonly ? readonly_string(itr->buffer) :
                                                                modified_string(itr->buffer);
          snprintf(buffer_info, BUFSIZ, format_string, buffer_flag_str, itr->buffer->name,
                   itr->buffer->line_count);
          ce_append_line(&config_state->buffer_list_buffer, buffer_info);
          itr = itr->next;
     }
     config_state->buffer_list_buffer.modified = false;
     config_state->buffer_list_buffer.readonly = true;
}

Point_t get_cursor_on_terminal(const Point_t* cursor, const BufferView_t* buffer_view)
{
     Point_t p = {cursor->x - buffer_view->left_column + buffer_view->top_left.x,
                cursor->y - buffer_view->top_row + buffer_view->top_left.y};
     return p;
}

void get_terminal_view_rect(TabView_t* tab_head, Point_t* top_left, Point_t* bottom_right)
{
     *top_left = (Point_t){0, 0};
     *bottom_right = (Point_t){g_terminal_dimensions->x - 1, g_terminal_dimensions->y - 1};

     // if we have multiple tabs
     if(tab_head->next){
          top_left->y++;
     }
}

// NOTE: stderr is redirected to stdout
pid_t bidirectional_popen(const char* cmd, int* in_fd, int* out_fd)
{
     int input_fds[2];
     int output_fds[2];

     if(pipe(input_fds) != 0) return 0;
     if(pipe(output_fds) != 0) return 0;

     pid_t pid = fork();
     if(pid < 0) return 0;

     if(pid == 0){
          close(input_fds[1]);
          close(output_fds[0]);

          dup2(input_fds[0], STDIN_FILENO);
          dup2(output_fds[1], STDOUT_FILENO);
          dup2(output_fds[1], STDERR_FILENO);

          execl("/bin/sh", "/bin/sh", "-c", cmd, NULL);
          assert(0);
     }else{
         close(input_fds[0]);
         close(output_fds[1]);

         *in_fd = input_fds[1];
         *out_fd = output_fds[0];
     }

     return pid;
}

void redraw_if_shell_command_buffer_in_view(BufferView_t* view_head, Buffer_t* shell_command_buffer,
                                            BufferNode_t* head_buffer_node, void* user_data)
{
     BufferView_t* command_view = ce_buffer_in_view(view_head, shell_command_buffer);
     if(command_view && shell_command_buffer->line_count > command_view->top_row &&
         shell_command_buffer->line_count <
        (command_view->top_row + (command_view->bottom_right.y - command_view->top_left.y))){
          view_drawer(head_buffer_node, user_data);
     }
}

// NOTE: runs N commands where each command is newline separated
void* run_shell_commands(void* user_data)
{
     char tmp[BUFSIZ];
     int in_fd;
     int out_fd;

     for(int64_t i = 0; i < shell_command_data.command_count; ++i){
          char* current_command = shell_command_data.commands[i];

          pid_t cmd_pid = bidirectional_popen(current_command, &in_fd, &out_fd);
          if(cmd_pid <= 0) goto clean_exit;

          shell_command_data.shell_command_input_fd = in_fd;
          shell_command_data.shell_command_output_fd = out_fd;

          // append the command to the buffer
          snprintf(tmp, BUFSIZ, "+ %s", current_command);

          pthread_mutex_lock(&shell_buffer_lock);
          ce_append_line_readonly(shell_command_data.output_buffer, tmp);
          ce_append_char_readonly(shell_command_data.output_buffer, NEWLINE);
          pthread_mutex_unlock(&shell_buffer_lock);

          view_drawer(shell_command_data.buffer_node_head, user_data);

          // load one line at a time
          bool first_block = true;
          int exit_code;
          struct timeval tv = {};
          fd_set out_fd_set;

          while(true){
               FD_ZERO(&out_fd_set);
               FD_SET(out_fd, &out_fd_set);

               // has the command generated any output we should read?
               int ch = 0;
               int rc = select(out_fd + 1, &out_fd_set, NULL, NULL, &tv);
               if(rc == -1){
                    // NOTE: since this is in a thread, is errno pointless to check?
                    ce_message("select() failed: '%s'", strerror(errno));
                    goto clean_exit;
               }else if(rc == 0){
                    // nothing happend on the fd
                    if(first_block){
                         redraw_if_shell_command_buffer_in_view(shell_command_data.view_head,
                                                                shell_command_data.output_buffer,
                                                                shell_command_data.buffer_node_head,
                                                                user_data);
                    }

                    first_block = false;
                    continue;
               }else{
                    assert(FD_ISSET(out_fd, &out_fd_set));
                    int count = read(out_fd, &ch, 1);
                    if(count <= 0){
                         // check if the pid has exitted
                         int status;
                         pid_t check_pid = waitpid(cmd_pid, &status, WNOHANG);
                         if(check_pid > 0){
                              exit_code = WEXITSTATUS(status);
                              break;
                         }

                         continue;
                    }else{
                         first_block = true;
                    }
               }

               if(!isprint(ch) && ch != NEWLINE) ch = '~';

               pthread_mutex_lock(&shell_buffer_lock);
               if(!ce_append_char_readonly(shell_command_data.output_buffer, ch)){
                    pthread_mutex_unlock(&shell_buffer_lock);
                    goto clean_exit;
               }
               pthread_mutex_unlock(&shell_buffer_lock);

               if(ch == NEWLINE){
                    redraw_if_shell_command_buffer_in_view(shell_command_data.view_head,
                                                           shell_command_data.output_buffer,
                                                           shell_command_data.buffer_node_head,
                                                           user_data);
               }
          }

          // append the return code
          shell_command_data.shell_command_input_fd = 0;
          snprintf(tmp, BUFSIZ, "+ exit %d", exit_code);

          pthread_mutex_lock(&shell_buffer_lock);
          ce_append_line_readonly(shell_command_data.output_buffer, tmp);
          ce_append_line_readonly(shell_command_data.output_buffer, "");
          pthread_mutex_unlock(&shell_buffer_lock);

          view_drawer(shell_command_data.buffer_node_head, user_data);
     }

clean_exit:

     for(int64_t i = 0; i < shell_command_data.command_count; ++i) free(shell_command_data.commands[i]);
     free(shell_command_data.commands);
     return NULL;
}

void* send_shell_input(void* data)
{
     ShellInputData_t* shell_input_data = data;
     struct timeval tv = {};
     fd_set out_fd_set;

     for(int64_t i = 0; i < shell_input_data->input_count; ++i){
          char* input = shell_input_data->input[i];

          // NOTE: Here we are sharing the shell_command_buffer with the run_shell_commands
          //       thread. I'm sure we will need to do locking around this.

          // put the line in the output buffer
          pthread_mutex_lock(&shell_buffer_lock);
          ce_append_string_readonly(shell_input_data->shell_command_buffer,
                                    shell_input_data->shell_command_buffer->line_count - 1,
                                    input);
          ce_append_char_readonly(shell_input_data->shell_command_buffer, NEWLINE);
          pthread_mutex_unlock(&shell_buffer_lock);

          // send the input to the shell command
          write(shell_input_data->shell_command_input_fd, input, strlen(input));
          write(shell_input_data->shell_command_input_fd, "\n", 1);

          // NOTE: Wait for output and then a for select to tell us no output is available
          //       This might be the best we can do for all programs?
          bool saw_output = false;
          while(true){
               FD_ZERO(&out_fd_set);
               FD_SET(shell_input_data->shell_command_output_fd, &out_fd_set);

               int rc = select(shell_input_data->shell_command_output_fd + 1, &out_fd_set,
                               NULL, NULL, &tv);
               if(rc == -1){
                    ce_message("select() failed: '%s'", strerror(errno));
                    break;
               }else if(rc == 0){
                    if(saw_output) break;
               }else{
                    saw_output = true;
               }
          }
     }

     return NULL;
}

void indent_line(Buffer_t* buffer, BufferCommitNode_t** commit_tail, int64_t line, Point_t* cursor)
{
     Point_t loc = {0, line};
     ce_insert_string(buffer, &loc, TAB_STRING);
     ce_commit_insert_string(commit_tail, &loc, cursor, cursor, strdup(TAB_STRING));
}

void unindent_line(Buffer_t* buffer, BufferCommitNode_t** commit_tail, int64_t line, Point_t* cursor)
{
     // find whitespace prepending line
     int64_t whitespace_count = 0;
     const int64_t tab_len = strlen(TAB_STRING);
     for(int i = 0; i < tab_len; ++i){
          if(isblank(buffer->lines[line][i])){
               whitespace_count++;
          }else{
               break;
          }
     }

     if(whitespace_count){
          Point_t loc = {0, line};
          ce_remove_string(buffer, &loc, whitespace_count);
          ce_commit_remove_string(commit_tail, &loc, cursor, cursor, strdup(TAB_STRING));
     }

}

bool key_handler(int key, BufferNode_t* head, void* user_data)
{
     ConfigState_t* config_state = user_data;
     Buffer_t* buffer = config_state->tab_current->view_current->buffer;
     BufferState_t* buffer_state = buffer->user_data;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Point_t* cursor = &config_state->tab_current->view_current->cursor;
     config_state->last_key = key;
     Point_t movement_start, movement_end;

     if(config_state->vim_mode == VM_INSERT){
          assert(config_state->command_key == '\0');
          switch(key){
          case 27: // ESC
          {
               Point_t end_cursor = *cursor;
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
                         Point_t previous = *cursor;
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
               ce_move_cursor(buffer, cursor, (Point_t){5, 0});
          } break;
          case 10: // return
          {
               if(!buffer->lines) ce_alloc_lines(buffer, 1);
               char* start = buffer->lines[cursor->y] + cursor->x;
               int64_t to_end_of_line_len = strlen(start);

               if(ce_insert_line(buffer, cursor->y + 1, start)){
                    if(to_end_of_line_len){
                         ce_remove_string(buffer, cursor, to_end_of_line_len);
                    }
                    cursor->y++;
                    cursor->x = 0;

                    // indent if necessary
                    Point_t prev_line = {0, cursor->y-1};
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

                    Point_t match;
                    if(ce_find_match(buffer, cursor, &match) && match.y != cursor->y){

                         // get the match's sbol (that's the indentation we're matching)
                         Point_t sbol_match = {0, match.y};
                         ce_move_cursor_to_soft_beginning_of_line(buffer, &sbol_match);

                         int64_t n_deletes = CE_MIN((int64_t) strlen(TAB_STRING), cursor->x - sbol_match.x);

                         bool can_unindent = true;
                         for(Point_t iter = {0, cursor->y}; ce_point_on_buffer(buffer, &iter) && iter.x < n_deletes; iter.x++)
                              can_unindent &= isblank(ce_get_char_raw(buffer, &iter));

                         if(can_unindent){
                              cursor->x -= n_deletes;
                              if(ce_remove_string(buffer, cursor, n_deletes)){
                                   if(config_state->start_insert.y == cursor->y &&
                                      config_state->start_insert.x > cursor->x){
                                        config_state->start_insert.x = cursor->x;
                                        for(int i = 0; i < n_deletes; ++i){
                                             backspace_push(&buffer_state->backspace_head, ' ');
                                        }
                                   }
                              }
                         }
                    }

                    cursor->x++;
               }
          } break;
          case 14: // Ctrl + n
               if(config_state->input){
                    if(iterate_history_input(config_state, false)){
                         if(buffer->line_count && buffer->lines[cursor->y][0]) cursor->x++;
                         enter_normal_mode(config_state);
                    }
               }
               break;
          case 16: // Ctrl + p
               if(config_state->input){
                    if(iterate_history_input(config_state, true)){
                         if(buffer->line_count && buffer->lines[cursor->y][0]) cursor->x++;
                         enter_normal_mode(config_state);
                    }
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
                    input_cancel(config_state);
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
               Point_t join_loc = {strlen(buffer->lines[cursor->y]), cursor->y};
               Point_t end_join_loc = {0, cursor->y+1};
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
                    ce_move_cursor(buffer, cursor, (Point_t){0, (config_state->command_key == 'j' || config_state->command_key == KEY_DOWN) ? 1 : -1});
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
               Point_t begin_line = {0, cursor->y};

               // indent if necessary
               int64_t indent_len = ce_get_indentation_for_next_line(buffer, cursor, strlen(TAB_STRING));
               char* indent_nl = malloc(sizeof '\n' + indent_len + sizeof '\0');
               memset(&indent_nl[0], ' ', indent_len);
               indent_nl[indent_len] = '\n';
               indent_nl[indent_len + 1] = '\0';

               if(ce_insert_string(buffer, &begin_line, indent_nl)){
                    Point_t next_cursor = {indent_len, cursor->y};
                    ce_commit_insert_string(&buffer_state->commit_tail, &begin_line, cursor, &next_cursor, indent_nl);
                    *cursor = next_cursor;
                    enter_insert_mode(config_state, cursor);
               }
          } break;
          case 'o':
          {
               Point_t end_of_line = *cursor;
               end_of_line.x = strlen(buffer->lines[cursor->y]);

               // indent if necessary
               int64_t indent_len = ce_get_indentation_for_next_line(buffer, cursor, strlen(TAB_STRING));
               char* nl_indent = malloc(sizeof '\n' + indent_len + sizeof '\0');
               nl_indent[0] = '\n';
               memset(&nl_indent[1], ' ', indent_len);
               nl_indent[1 + indent_len] = '\0';

               if(ce_insert_string(buffer, &end_of_line, nl_indent)){
                    Point_t next_cursor = {indent_len, cursor->y+1};
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
               Point_t* marked_location;
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
                         YankMode_t yank_mode;
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
               YankNode_t* yank = find_yank(config_state, '"');
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
                         Point_t insert_loc;
                         Point_t cursor_loc;
                         if(config_state->command_key == 'p'){
                              // TODO: bring this all into a ce_commit_insert_line function

                              save_str[0] = '\n'; // prepend a new line to create a line
                              memcpy(save_str + 1, yank->text, len + 1); // also copy the '\0'

                              cursor_loc = (Point_t){0, cursor->y+1};

                              // insert at the end of the current line
                              insert_loc = (Point_t){strlen(buffer->lines[cursor->y]), cursor->y};
                         }
                         else{
                              save_str[len] = '\n'; // append a new line to create a line
                              save_str[len+1] = '\0';
                              memcpy(save_str, yank->text, len);

                              cursor_loc = (Point_t){0, cursor->y};

                              // insert at the beginning of the current line
                              insert_loc = (Point_t){0, cursor->y};
                         }

                         bool res __attribute__((unused)) = ce_insert_string(buffer, &insert_loc, save_str);
                         assert(res);
                         ce_commit_insert_string(&buffer_state->commit_tail,
                                                 &insert_loc, cursor, &cursor_loc,
                                                 save_str);
                         ce_set_cursor(buffer, cursor, &cursor_loc);
                    } break;
                    case YANK_NORMAL:
                    {
                         Point_t insert_cursor = *cursor;
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
                    Point_t end = *cursor;
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
                                        movement_end = (Point_t) {ce_last_index(buffer->lines[cursor->y]), cursor->y}; // TODO: causes ce_dupe_string to fail (not on buffer)
                                        break;
                                   case 'd':
                                        ce_move_cursor(buffer, cursor, (Point_t){-cursor->x, 0});
                                        movement_start = (Point_t) {0, cursor->y};
                                        movement_end = (Point_t) {ce_last_index(buffer->lines[cursor->y]), cursor->y}; // TODO: causes ce_dupe_string to fail (not on buffer)
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

                    Point_t yank_end = movement_end;
                    YankMode_t yank_mode;
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
               split_view(config_state->tab_current->view_head, config_state->tab_current->view_current, false);
          } break;
          case 22: // Ctrl + v
          {
               split_view(config_state->tab_current->view_head, config_state->tab_current->view_current, true);
          } break;
          case KEY_CLOSE: // Ctrl + q
          {
               if(config_state->input){
                    input_cancel(config_state);
                    break;
               }

               Point_t save_cursor_on_terminal = get_cursor_on_terminal(cursor, buffer_view);
               config_state->tab_current->view_current->buffer->cursor = config_state->tab_current->view_current->cursor;

               if(ce_remove_view(&config_state->tab_current->view_head, config_state->tab_current->view_current)){
                    // if head is NULL, then we have removed the view head, and there were no other views, head is NULL
                    if(!config_state->tab_current->view_head){
                         if(config_state->tab_current->next){
                              config_state->tab_current->next = config_state->tab_current->next;
                              TabView_t* tmp = config_state->tab_current;
                              config_state->tab_current = config_state->tab_current->next;
                              tab_view_remove(&config_state->tab_head, tmp);
                              break;
                         }else{
                              if(config_state->tab_current == config_state->tab_head) return false;

                              TabView_t* itr = config_state->tab_head;
                              while(itr && itr->next != config_state->tab_current) itr = itr->next;
                              assert(itr);
                              assert(itr->next == config_state->tab_current);
                              tab_view_remove(&config_state->tab_head, config_state->tab_current);
                              config_state->tab_current = itr;
                              break;
                         }
                    }

                    if(config_state->tab_current->view_current == config_state->tab_current->view_previous){
                         config_state->tab_current->view_previous = NULL;
                    }

                    Point_t top_left;
                    Point_t bottom_right;
                    get_terminal_view_rect(config_state->tab_head, &top_left, &bottom_right);

                    ce_calc_views(config_state->tab_current->view_head, &top_left, &bottom_right);
                    BufferView_t* new_view = ce_find_view_at_point(config_state->tab_current->view_head, &save_cursor_on_terminal);
                    if(new_view){
                         config_state->tab_current->view_current = new_view;
                    }else{
                         config_state->tab_current->view_current = config_state->tab_current->view_head;
                    }
               }
          } break;
          case 2: // Ctrl + b
          {
               update_buffer_list_buffer(config_state, head);
               config_state->buffer_list_buffer.readonly = true;
               config_state->tab_current->view_current->buffer->cursor = *cursor;
               config_state->tab_current->view_current->buffer = &config_state->buffer_list_buffer;
               config_state->tab_current->view_current->cursor = (Point_t){0, 1};
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
               Point_t location = {cursor->x, buffer_view->top_row};
               ce_set_cursor(buffer, cursor, &location);
          } break;
          case 'M':
          {
               // move cursor to middle line of view
               int64_t view_height = buffer_view->bottom_right.y - buffer_view->top_left.y;
               Point_t location = {cursor->x, buffer_view->top_row + (view_height/2)};
               ce_set_cursor(buffer, cursor, &location);
          } break;
          case 'L':
          {
               // move cursor to bottom line of view
               int64_t view_height = buffer_view->bottom_right.y - buffer_view->top_left.y;
               Point_t location = {cursor->x, buffer_view->top_row + view_height};
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
                    if(config_state->vim_mode == VM_VISUAL_RANGE ||
                       config_state->vim_mode == VM_VISUAL_LINE){
                         const Point_t* a = cursor;
                         const Point_t* b = &config_state->visual_start;
                         ce_sort_points(&a, &b);
                         for(int64_t i = a->y; i <= b->y; ++i){
                              if(!buffer->lines[i][0]) continue;
                              indent_line(buffer, &buffer_state->commit_tail, i, cursor);
                         }
                    }else{
                         indent_line(buffer, &buffer_state->commit_tail, cursor->y, cursor);
                    }
                    break;
               }
          } break;
          case '<':
          {
               switch(config_state->movement_keys[0]){
               case MOVEMENT_CONTINUE:
                    // no movement yet, wait for one!
                    return true;
               case '<':
                    if(config_state->vim_mode == VM_VISUAL_RANGE ||
                       config_state->vim_mode == VM_VISUAL_LINE){
                         const Point_t* a = cursor;
                         const Point_t* b = &config_state->visual_start;
                         ce_sort_points(&a, &b);
                         for(int64_t i = a->y; i <= b->y; ++i){
                              if(!buffer->lines[i][0]) continue;
                              unindent_line(buffer, &buffer_state->commit_tail, i, cursor);
                         }
                    }else{
                         unindent_line(buffer, &buffer_state->commit_tail, cursor->y, cursor);
                    }
                    break;
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
               case 't':
                    if(config_state->tab_current->next){
                         config_state->tab_current = config_state->tab_current->next;
                    }else{
                         config_state->tab_current = config_state->tab_head;
                    }
                    break;
               case 'T':
                    if(config_state->tab_current == config_state->tab_head){
                         // find tail
                         TabView_t* itr = config_state->tab_head;
                         while(itr->next) itr = itr->next;
                         config_state->tab_current = itr;
                    }else{
                         TabView_t* itr = config_state->tab_head;
                         while(itr && itr->next != config_state->tab_current) itr = itr->next;

                         // what if we don't find our current tab and hit the end of the list?!
                         assert(itr);
                         config_state->tab_current = itr;
                    }
                    break;
               case 'f':
               {
                    if(!buffer->lines[cursor->y]) break;
                    // TODO: get word under the cursor and unify with '*' impl
                    int64_t word_len = ce_find_delta_to_end_of_word(buffer, cursor, true)+1;
                    if(buffer->lines[cursor->y][cursor->x+word_len] == '.'){
                         Point_t ext_start = {cursor->x+word_len, cursor->y};
                         int64_t extension_len = ce_find_delta_to_end_of_word(buffer, &ext_start, true);
                         if(extension_len != -1) word_len += extension_len+1;
                    }
                    char* filename = alloca(word_len+1);
                    strncpy(filename, &buffer->lines[cursor->y][cursor->x], word_len);
                    filename[word_len] = '\0';

                    Buffer_t* file_buffer = open_file_buffer(head, filename);
                    if(file_buffer) buffer_view->buffer = file_buffer;
               } break;
               }
          } break;
          case '%':
          {
               Point_t delta;
               if(ce_find_delta_to_match(buffer, cursor, &delta)){
                    ce_move_cursor(buffer, cursor, delta);
               }
          } break;
          case KEY_NPAGE:
          {
               half_page_up(config_state->tab_current->view_current);
          } break;
          case KEY_PPAGE:
          {
               half_page_down(config_state->tab_current->view_current);
          } break;
          case 8: // Ctrl + h
              if(config_state->input){
                   // dump input history on cursor line
                   InputHistory_t* cur_hist = history_from_input_key(config_state);
                   if(!cur_hist){
                        ce_message("no history to dump");
                        break;
                   }

                   // start the insertions after the cursor, unless the buffer is empty
                   assert(config_state->view_input->buffer->line_count);
                   int lines_added = 1;
                   bool empty_first_line = !config_state->view_input->buffer->lines[0][0];

                   // insert each line in history
                   InputHistoryNode_t* node = cur_hist->head;
                   while(node && node->entry){
                        if(ce_insert_line(config_state->view_input->buffer,
                                          config_state->view_input->cursor.y + lines_added,
                                          node->entry)){
                            lines_added += ce_count_string_lines(node->entry);
                        }else{
                             break;
                        }
                        node = node->next;
                   }

                   if(empty_first_line) ce_remove_line(config_state->view_input->buffer, 0);
              }else{
                   // TODO: consolidate into function for use with other window movement keys, and for use in insert mode?
                   Point_t point = {config_state->tab_current->view_current->top_left.x - 2, // account for window separator
                                  cursor->y - config_state->tab_current->view_current->top_row + config_state->tab_current->view_current->top_left.y};
                   if(point.x < 0) point.x += g_terminal_dimensions->x - 1;
                   switch_to_view_at_point(config_state, &point);
              }
          break;
          case 10: // Ctrl + j
               if(config_state->input){
                    input_end(config_state);
                    buffer = config_state->tab_current->view_current->buffer;
                    buffer_state = buffer->user_data;
                    buffer_view = config_state->tab_current->view_current;
                    cursor = &config_state->tab_current->view_current->cursor;

                    switch(config_state->input_key) {
                    default:
                         break;
                    case ':':
                    {
                         if(config_state->view_input->buffer->line_count){
                              int64_t line = atoi(config_state->view_input->buffer->lines[0]);
                              if(line > 0){
                                   config_state->tab_current->view_current->cursor = (Point_t){0, line - 1};
                                   ce_move_cursor_to_soft_beginning_of_line(config_state->tab_current->view_current->buffer,
                                                                            &config_state->tab_current->view_current->cursor);
                              }
                         }
                    } break;
                    case 6: // Ctrl + f
                         // just grab the first line and load it as a file
                         commit_input_to_history(config_state->view_input->buffer, &config_state->load_file_history);
                         for(int64_t i = 0; i < config_state->view_input->buffer->line_count; ++i){
                              Buffer_t* new_buffer = open_file_buffer(head, config_state->view_input->buffer->lines[i]);
                              if(i == 0 && new_buffer){
                                   config_state->tab_current->view_current->buffer = new_buffer;
                                   config_state->tab_current->view_current->cursor = (Point_t){0, 0};
                              }
                         }
                         break;
                    case '/':
                         if(config_state->view_input->buffer->line_count){
                              commit_input_to_history(config_state->view_input->buffer, &config_state->search_history);
                              add_yank(config_state, '/', strdup(config_state->view_input->buffer->lines[0]), YANK_NORMAL);
                         }
                         break;
                    case '?':
                         if(config_state->view_input->buffer->line_count){
                              commit_input_to_history(config_state->view_input->buffer, &config_state->search_history);
                              add_yank(config_state, '/', strdup(config_state->view_input->buffer->lines[0]), YANK_NORMAL);
                         }
                         break;
                    case 24: // Ctrl + x
                    {
                         if(config_state->view_input->buffer->line_count == 0){
                              break;
                         }

                         if(config_state->shell_command_thread){
                              pthread_cancel(config_state->shell_command_thread);
                              pthread_join(config_state->shell_command_thread, NULL);
                         }

                         commit_input_to_history(config_state->view_input->buffer, &config_state->shell_command_history);

                         // if we found an existing command buffer, clear it and use it
                         Buffer_t* command_buffer = config_state->shell_command_buffer;
                         command_buffer->cursor = (Point_t){0, 0};
                         config_state->last_command_buffer_jump = 0;
                         BufferView_t* command_view = ce_buffer_in_view(config_state->tab_current->view_head, command_buffer);

                         if(command_view){
                              command_view->cursor = (Point_t){0, 0};
                              command_view->top_row = 0;
                         }else{
                              config_state->tab_current->view_current->buffer = command_buffer;
                              config_state->tab_current->view_current->cursor = (Point_t){0, 0};
                              config_state->tab_current->view_current->top_row = 0;
                              command_view = config_state->tab_current->view_current;
                         }

                         shell_command_data.command_count = config_state->view_input->buffer->line_count;
                         shell_command_data.commands = malloc(shell_command_data.command_count * sizeof(char**));
                         if(!shell_command_data.commands){
                              ce_message("failed to allocate shell commands");
                              break;
                         }

                         bool failed_to_alloc = false;
                         for(int64_t i = 0; i < config_state->view_input->buffer->line_count; ++i){
                              shell_command_data.commands[i] = strdup(config_state->view_input->buffer->lines[i]);
                              if(!shell_command_data.commands[i]){
                                   ce_message("failed to allocate shell command %" PRId64, i + 1);
                                   failed_to_alloc = true;
                                   break;
                              }
                         }
                         if(failed_to_alloc) break; // leak !

                         shell_command_data.output_buffer = command_buffer;
                         shell_command_data.buffer_node_head = head;
                         shell_command_data.view_head = config_state->tab_current->view_head;
                         shell_command_data.view_current = config_state->tab_current->view_current;
                         shell_command_data.user_data = user_data;

                         assert(command_buffer->readonly);
                         pthread_mutex_lock(&shell_buffer_lock);
                         ce_clear_lines_readonly(command_buffer);
                         pthread_mutex_unlock(&shell_buffer_lock);

                         pthread_create(&config_state->shell_command_thread, NULL, run_shell_commands, user_data);
                    } break;
                    case 'R':
                    {
                         YankNode_t* yank = find_yank(config_state, '/');
                         if(!yank) break;
                         const char* search_str = yank->text;
                         // NOTE: allow empty string to replace search
                         int64_t search_len = strlen(search_str);
                         if(!search_len) break;
                         char* replace_str = ce_dupe_buffer(config_state->view_input->buffer);
                         int64_t replace_len = strlen(replace_str);
                         Point_t begin = {};
                         Point_t match = {};
                         int64_t replace_count = 0;
                         while(ce_find_string(buffer, &begin, search_str, &match, CE_DOWN)){
                              if(!ce_remove_string(buffer, &match, search_len)) break;
                              if(replace_len){
                                   if(!ce_insert_string(buffer, &match, replace_str)) break;
                              }
                              ce_commit_change_string(&buffer_state->commit_tail, &match, &match, &match, strdup(replace_str),
                                                      strdup(search_str));
                              begin = match;
                              replace_count++;
                         }
                         if(replace_count){
                              ce_message("replaced %" PRId64 " matches", replace_count);
                         }else{
                              ce_message("no matches found to replace");
                         }
                         *cursor = match;
                         center_view(config_state->tab_current->view_current);
                         free(replace_str);
                    } break;
                    case 1: // Ctrl + a
                    {
                         if(!config_state->view_input->buffer->lines ||
                            !config_state->view_input->buffer->lines[0][0]) break;

                         if(ce_save_buffer(buffer, config_state->view_input->buffer->lines[0])){
                              buffer->filename = strdup(config_state->view_input->buffer->lines[0]);
                         }
                    } break;
                    case 9: // Ctrl + i
                    {
                         if(!config_state->view_input->buffer->lines) break;
                         if(!config_state->shell_command_thread) break;
                         if(!shell_command_data.shell_command_input_fd) break;

                         if(config_state->shell_input_thread){
                              pthread_cancel(config_state->shell_input_thread);
                              pthread_join(config_state->shell_input_thread, NULL);
                         }

                         Buffer_t* input_buffer = config_state->view_input->buffer;
                         commit_input_to_history(input_buffer, &config_state->shell_input_history);

                         ShellInputData_t* shell_input_data = malloc(sizeof(*shell_input_data));
                         if(!shell_input_data){
                              ce_message("failed to allocate shell input data");
                              break;
                         }

                         shell_input_data->input_count = input_buffer->line_count;
                         shell_input_data->input = malloc(shell_input_data->input_count * sizeof(char**));
                         if(!shell_input_data->input){
                              ce_message("failed to allocate shell input");
                              break;
                         }

                         bool failed_to_alloc = false;
                         for(int64_t i = 0; i < shell_input_data->input_count; ++i){
                              shell_input_data->input[i] = strdup(input_buffer->lines[i]);
                              if(!shell_input_data->input[i]){
                                   ce_message("failed to allocate shell input line %" PRId64, i + 1);
                                   failed_to_alloc = true;
                                   break;
                              }
                         }

                         if(failed_to_alloc) break; // leak!

                         shell_input_data->shell_command_buffer = config_state->shell_command_buffer;
                         shell_input_data->shell_command_input_fd = shell_command_data.shell_command_input_fd;
                         shell_input_data->shell_command_output_fd = shell_command_data.shell_command_output_fd;

                         pthread_create(&config_state->shell_input_thread, NULL, send_shell_input, shell_input_data);
                    }break;
                    }
               }else if(config_state->tab_current->view_current->buffer == &config_state->buffer_list_buffer){
                    int64_t line = cursor->y - 1; // account for buffer list row header
                    if(line < 0) break;
                    BufferNode_t* itr = head;

                    while(line > 0){
                         itr = itr->next;
                         if(!itr) break;
                         line--;
                    }

                    if(!itr) break;

                    config_state->tab_current->view_current->buffer = itr->buffer;
                    config_state->tab_current->view_current->cursor = itr->buffer->cursor;
                    center_view(config_state->tab_current->view_current);
               }else if(config_state->tab_current->view_current->buffer == config_state->shell_command_buffer){
                    BufferView_t* view_to_change = config_state->tab_current->view_current;
                    if(config_state->tab_current->view_previous) view_to_change = config_state->tab_current->view_previous;

                    if(goto_file_destination_in_buffer(head, config_state->shell_command_buffer, cursor->y,
                                                       config_state->tab_current->view_head, view_to_change,
                                                       &config_state->last_command_buffer_jump)){
                         config_state->tab_current->view_current = view_to_change;
                    }
               }else{
                    Point_t point = {cursor->x - config_state->tab_current->view_current->left_column + config_state->tab_current->view_current->top_left.x,
                                   config_state->tab_current->view_current->bottom_right.y + 2}; // account for window separator
                    if(point.y >= g_terminal_dimensions->y - 1) point.y = 0;
                    switch_to_view_at_point(config_state, &point);
               }
               break;
          case 11: // Ctrl + k
          {
               if(config_state->input) break;
               Point_t point = {cursor->x - config_state->tab_current->view_current->left_column + config_state->tab_current->view_current->top_left.x,
                              config_state->tab_current->view_current->top_left.y - 2}; // account for window separator
               switch_to_view_at_point(config_state, &point);
          }
          break;
          case 12: // Ctrl + l
          {
               if(config_state->input) break;
               Point_t point = {config_state->tab_current->view_current->bottom_right.x + 2, // account for window separator
                              cursor->y - config_state->tab_current->view_current->top_row + config_state->tab_current->view_current->top_left.y};
               if(point.x >= g_terminal_dimensions->x - 1) point.x = 0;
               switch_to_view_at_point(config_state, &point);
          }
          break;
          case ':':
          {
               if(config_state->input) break;
               input_start(config_state, "Goto Line", key);
          }
          break;
          case '#':
          {
               if(!buffer->lines || !buffer->lines[cursor->y]) break;

               Point_t word_start, word_end;
               if(!ce_get_word_at_location(buffer, cursor, &word_start, &word_end)) break;
               char* search_str = ce_dupe_string(buffer, &word_start, &word_end);
               add_yank(config_state, '/', search_str, YANK_NORMAL);
               config_state->search_command.direction = CE_UP;
               goto search;
          } break;
          case '*':
          {
               if(!buffer->lines || !buffer->lines[cursor->y]) break;

               Point_t word_start, word_end;
               if(!ce_get_word_at_location(buffer, cursor, &word_start, &word_end)) break;
               char* search_str = ce_dupe_string(buffer, &word_start, &word_end);
               add_yank(config_state, '/', search_str, YANK_NORMAL);
               config_state->search_command.direction = CE_DOWN;
               goto search;
          } break;
          case '/':
          {
               if(config_state->input) break;
               config_state->search_command.direction = CE_DOWN;
               config_state->start_search = *cursor;
               input_start(config_state, "Search", key);
               break;
          }
          case '?':
          {
               if(config_state->input) break;
               config_state->search_command.direction = CE_UP;
               config_state->start_search = *cursor;
               input_start(config_state, "Reverse Search", key);
               break;
          }
          case 'n':
search:
          {
               YankNode_t* yank = find_yank(config_state, '/');
               if(yank){
                    assert(yank->mode == YANK_NORMAL);
                    Point_t match;
                    if(ce_find_string(buffer, cursor, yank->text, &match, config_state->search_command.direction)){
                         ce_set_cursor(buffer, cursor, &match);
                         center_view(config_state->tab_current->view_current);
                    }
               }
          } break;
          case 'N':
          {
               YankNode_t* yank = find_yank(config_state, '/');
               if(yank){
                    assert(yank->mode == YANK_NORMAL);
                    Point_t match;
                    if(ce_find_string(buffer, cursor, yank->text, &match, ce_reverse_direction(config_state->search_command.direction))){
                         ce_set_cursor(buffer, cursor, &match);
                         center_view(config_state->tab_current->view_current);
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
                    default:
                         clear_keys(config_state);
                         return true;
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

                         int ret __attribute__((unused)) = execlp("clang-format", "clang-format", line_arg, cursor_arg, (char *)NULL);
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
                                   Point_t new_cursor_location = {-cursor_position, i};
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
                    Point_t delete_begin = {0, cursor->y};
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
               input_start(config_state, "Load File", key);
          } break;
          case 20: // Ctrl + t
          {
               TabView_t* new_tab = tab_view_insert(config_state->tab_head);
               if(!new_tab) break;

               // copy view attributes from the current view
               *new_tab->view_head = *config_state->tab_current->view_current;
               new_tab->view_head->next_horizontal = NULL;
               new_tab->view_head->next_vertical = NULL;
               new_tab->view_current = new_tab->view_head;

               config_state->tab_current = new_tab;
          } break;
          case 'R':
               if(config_state->input) break;
               input_start(config_state, "Replace", key);
          break;
          case 5: // Ctrl + e
          {
               Buffer_t* new_buffer = new_buffer_from_string(head, "unnamed", NULL);
               ce_alloc_lines(new_buffer, 1);
               config_state->tab_current->view_current->buffer = new_buffer;
               *cursor = (Point_t){0, 0};
          } break;
          case 1: // Ctrl + a
               if(config_state->input) break;
               input_start(config_state, "Save Buffer As", key);
          break;
          case 9: // Ctrl + i
               if(config_state->input) break;
               input_start(config_state, "Shell Command Input", key);
          break;
          case 15: // Ctrl + o // NOTE: not the best keybinding, but what else is left?!
          {
               if(access(buffer->filename, R_OK) != 0){
                    ce_message("failed to read %s: %s", buffer->filename, strerror(errno));
                    break;
               }

               // reload file
               if(buffer->readonly){
                    // NOTE: maybe ce_clear_lines shouldn't care about readonly
                    ce_clear_lines_readonly(buffer);
               }else{
                    ce_clear_lines(buffer);
               }

               ce_load_file(buffer, buffer->filename);
               ce_clamp_cursor(buffer, &buffer_view->cursor);
          } break;
          }
     }

     // incremental search
     if(config_state->input && (config_state->input_key == '/' || config_state->input_key == '?')){
          if(config_state->view_input->buffer->lines == NULL){
               config_state->tab_current->view_input_save->cursor = config_state->start_search;
          }else{
               const char* search_str = config_state->view_input->buffer->lines[0];
               Point_t match = {};
               if(search_str[0] &&
                  ce_find_string(config_state->tab_current->view_input_save->buffer,
                                 &config_state->start_search, search_str, &match,
                                 config_state->search_command.direction)){
                    ce_set_cursor(config_state->tab_current->view_input_save->buffer,
                                  &config_state->tab_current->view_input_save->cursor, &match);
                    center_view(config_state->tab_current->view_input_save);
               }else{
                    config_state->tab_current->view_input_save->cursor = config_state->start_search;
               }
          }
     }

     // if we've made it to here, the command is complete
     clear_keys(config_state);

     return true;
}

void draw_view_statuses(BufferView_t* view, BufferView_t* current_view, VimMode_t vim_mode, int last_key)
{
     Buffer_t* buffer = view->buffer;
     if(view->next_horizontal) draw_view_statuses(view->next_horizontal, current_view, vim_mode, last_key);
     if(view->next_vertical) draw_view_statuses(view->next_vertical, current_view, vim_mode, last_key);

     // NOTE: mode names need space at the end for OCD ppl like me
     static const char* mode_names[] = {
          "NORMAL ",
          "INSERT ",
          "VISUAL ",
          "VISUAL LINE ",
          "VISUAL BLOCK ",
     };

     attron(COLOR_PAIR(S_BORDERS));
     move(view->bottom_right.y, view->top_left.x);
     for(int i = view->top_left.x; i < view->bottom_right.x; ++i) addch(ACS_HLINE);

     attron(COLOR_PAIR(S_VIEW_STATUS));
     mvprintw(view->bottom_right.y, view->top_left.x + 1, " %s%s%s%s ",
              view == current_view ? mode_names[vim_mode] : "",
              modified_string(buffer), buffer->filename, readonly_string(buffer));
#ifndef NDEBUG
     if(view == current_view) printw("%s %d ", keyname(last_key), last_key);
#endif
     int64_t line = view->cursor.y + 1;
     int64_t digits_in_line = count_digits(line);
     mvprintw(view->bottom_right.y, (view->bottom_right.x - (digits_in_line + 3)), " %"PRId64" ", line);
}

void view_drawer(const BufferNode_t* head, void* user_data)
{
     // grab the draw lock so we can draw
     if(pthread_mutex_trylock(&draw_lock) != 0) return;

     // and the shell_buffer_lock so we know it won't change on us
     if(pthread_mutex_trylock(&shell_buffer_lock) != 0){
          pthread_mutex_unlock(&draw_lock);
          return;
     }

     // clear all lines in the terminal
     erase();

     (void)(head);
     ConfigState_t* config_state = user_data;
     Buffer_t* buffer = config_state->tab_current->view_current->buffer;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Point_t* cursor = &config_state->tab_current->view_current->cursor;

     Point_t top_left;
     Point_t bottom_right;
     get_terminal_view_rect(config_state->tab_head, &top_left, &bottom_right);
     ce_calc_views(config_state->tab_current->view_head, &top_left, &bottom_right);

     if(ce_buffer_in_view(config_state->tab_current->view_head, &config_state->buffer_list_buffer)){
          update_buffer_list_buffer(config_state, head);
     }

     int64_t input_view_height = 0;
     Point_t input_top_left = {};
     Point_t input_bottom_right = {};
     if(config_state->input){
          input_view_height = config_state->view_input->buffer->line_count;
          if(input_view_height) input_view_height--;
          input_top_left = (Point_t){config_state->tab_current->view_input_save->top_left.x,
                                   (config_state->tab_current->view_input_save->bottom_right.y - input_view_height) - 1};
          if(input_top_left.y < 1) input_top_left.y = 1; // clamp to growing to 1, account for input message
          input_bottom_right = config_state->tab_current->view_input_save->bottom_right;
          if(input_bottom_right.y == g_terminal_dimensions->y - 2){
               input_top_left.y++;
               input_bottom_right.y++; // account for bottom status bar
          }
          ce_calc_views(config_state->view_input, &input_top_left, &input_bottom_right);
     }

     view_follow_cursor(buffer_view);

     // setup highlight
     if(config_state->vim_mode == VM_VISUAL_RANGE){
          const Point_t* start = &config_state->visual_start;
          const Point_t* end = &config_state->tab_current->view_current->cursor;

          ce_sort_points(&start, &end);

          buffer->highlight_start = *start;
          buffer->highlight_end = *end;
     }else if(config_state->vim_mode == VM_VISUAL_LINE){
          int64_t start_line = config_state->visual_start.y;
          int64_t end_line = config_state->tab_current->view_current->cursor.y;

          if(start_line > end_line){
               int64_t tmp = start_line;
               start_line = end_line;
               end_line = tmp;
          }

          buffer->highlight_start = (Point_t){0, start_line};
          buffer->highlight_end = (Point_t){strlen(config_state->tab_current->view_current->buffer->lines[end_line]), end_line};
     }else{
          buffer->highlight_start = (Point_t){0, 0};
          buffer->highlight_end = (Point_t){-1, 0};
     }

     const char* search = NULL;
     if(config_state->input && (config_state->input_key == '/' || config_state->input_key == '?') &&
        config_state->view_input->buffer->lines && config_state->view_input->buffer->lines[0][0]){
          search = config_state->view_input->buffer->lines[0];
     }else{
          YankNode_t* yank = find_yank(config_state, '/');
          if(yank) search = yank->text;
     }

     // NOTE: always draw from the head
     ce_draw_views(config_state->tab_current->view_head, search);

     if(config_state->input){
          move(input_top_left.y - 1, input_top_left.x);

          attron(COLOR_PAIR(S_BORDERS));
          for(int i = input_top_left.x; i < input_bottom_right.x; ++i) addch(ACS_HLINE);
          // if we are at the edge of the terminal, draw the inclusing horizontal line. We
          if(input_bottom_right.x == g_terminal_dimensions->x - 1) addch(ACS_HLINE);

          attron(COLOR_PAIR(S_INPUT_STATUS));
          mvprintw(input_top_left.y - 1, input_top_left.x + 1, " %s ", config_state->input_message);

          standend();
          // clear input buffer section
          for(int y = input_top_left.y; y <= input_bottom_right.y; ++y){
               move(y, input_top_left.x);
               for(int x = input_top_left.x; x <= input_bottom_right.x; ++x){
                    addch(' ');
               }
          }

          ce_draw_views(config_state->view_input, NULL);
     }

     draw_view_statuses(config_state->tab_current->view_head, config_state->tab_current->view_current,
                        config_state->vim_mode, config_state->last_key);

     if(config_state->input){
          draw_view_statuses(config_state->view_input, config_state->tab_current->view_current,
                             config_state->vim_mode, config_state->last_key);
     }

     // draw tab line
     if(config_state->tab_head->next){
          // clear tab line with inverses
          move(0, 0);
          attron(COLOR_PAIR(S_BORDERS));
          for(int i = 0; i < g_terminal_dimensions->x; ++i) addch(ACS_HLINE);
          for(int i = 0; i < g_terminal_dimensions->x; ++i){
               Point_t p = {i, 0};
               ce_connect_border_lines(&p);
          }

          TabView_t* tab_itr = config_state->tab_head;
          move(0, 1);

          // draw before current tab
          attron(COLOR_PAIR(S_TAB_NAME));
          addch(' ');
          while(tab_itr && tab_itr != config_state->tab_current){
               printw(tab_itr->view_current->buffer->name);
               addch(' ');
               tab_itr = tab_itr->next;
          }

          // draw current tab
          assert(tab_itr == config_state->tab_current);
          attron(COLOR_PAIR(S_CURRENT_TAB_NAME));
          printw(tab_itr->view_current->buffer->name);
          addch(' ');

          // draw rest of tabs
          attron(COLOR_PAIR(S_TAB_NAME));
          tab_itr = tab_itr->next;
          while(tab_itr){
               printw(tab_itr->view_current->buffer->name);
               addch(' ');
               tab_itr = tab_itr->next;
          }
     }

     // reset the cursor
     Point_t terminal_cursor = get_cursor_on_terminal(cursor, buffer_view);
     move(terminal_cursor.y, terminal_cursor.x);

     // update the screen with what we drew
     refresh();

     pthread_mutex_unlock(&shell_buffer_lock);
     pthread_mutex_unlock(&draw_lock);
}
