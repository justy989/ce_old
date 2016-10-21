#include "ce.h"
#include "ce_client.h"
#include "ce_network.h"
#include "ce_server.h"
#include <assert.h>
#include <ctype.h>
#include <ftw.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/ioctl.h>

void view_drawer(const BufferNode_t* head, void* user_data);

typedef struct CompleteNode_t{
     char* option;
     struct CompleteNode_t* next;
     struct CompleteNode_t* prev;
} CompleteNode_t;

typedef struct{
     CompleteNode_t* head;
     CompleteNode_t* tail;
     CompleteNode_t* current;
     Point_t start;
} AutoComplete_t;

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

typedef struct TabView_t{
     BufferView_t* view_head;
     BufferView_t* view_current;
     BufferView_t* view_previous;
     BufferView_t* view_input_save;
     struct TabView_t* next;
} TabView_t;

typedef enum{
     VM_NORMAL,
     VM_INSERT,
     VM_VISUAL_RANGE,
     VM_VISUAL_LINE,
     VM_VISUAL_BLOCK,
} VimMode_t;

typedef enum{
     VCT_MOTION,
     VCT_INSERT,
     VCT_DELETE,
     VCT_CHANGE_CHAR,
     VCT_PASTE_BEFORE,
     VCT_PASTE_AFTER,
     VCT_YANK,
     VCT_INDENT,
     VCT_UNINDENT,
} VimChangeType_t;

typedef struct{
     VimChangeType_t type;
     union{
          char* insert_string;
          char yank_register;
          char paste_register;
          char change_char;
          char* change_string;
     };
} VimChange_t;

typedef enum{
     VMT_NONE,
     VMT_LEFT,
     VMT_RIGHT,
     VMT_UP,
     VMT_DOWN,
     VMT_WORD_LITTLE,
     VMT_WORD_BIG,
     VMT_WORD_BEGINNING_LITTLE,
     VMT_WORD_BEGINNING_BIG,
     VMT_WORD_BEGINNING_LITTLE_PRE_CURSOR,
     VMT_WORD_BEGINNING_BIG_PRE_CURSOR,
     VMT_WORD_END_LITTLE,
     VMT_WORD_END_BIG,
     VMT_LINE,
     VMT_LINE_UP,
     VMT_LINE_DOWN,
     VMT_FIND_NEXT_MATCHING_CHAR,
     VMT_FIND_PREV_MATCHING_CHAR,
     VMT_TO_NEXT_MATCHING_CHAR,
     VMT_TO_PREV_MATCHING_CHAR,
     VMT_BEGINNING_OF_FILE,
     VMT_BEGINNING_OF_LINE_HARD,
     VMT_BEGINNING_OF_LINE_SOFT,
     VMT_END_OF_LINE_PASSED,
     VMT_END_OF_LINE_HARD,
     VMT_END_OF_LINE_SOFT,
     VMT_END_OF_FILE,
     VMT_INSIDE_PAIR,
     VMT_INSIDE_WORD_LITTLE,
     VMT_INSIDE_WORD_BIG,
     VMT_AROUND_PAIR,
     VMT_AROUND_WORD_LITTLE,
     VMT_AROUND_WORD_BIG,
     VMT_VISUAL_RANGE,
     VMT_VISUAL_LINE,
} VimMotionType_t;

typedef struct{
     VimMotionType_t type;
     int32_t multiplier;
     union{
          char match_char;
          char inside_pair;
          char around_pair;
          int64_t visual_length;
          int64_t visual_lines;
     };
     bool visual_start_after; // false means after !
} VimMotion_t;

typedef struct{
     int64_t multiplier;
     Point_t start;
     VimMotion_t motion;
     VimChange_t change;
     VimMode_t end_in_vim_mode;
     bool yank;
} VimAction_t;

typedef enum{
    VCS_INVALID,
    VCS_CONTINUE,
    VCS_COMPLETE,
} VimCommandState_t;

typedef struct{
     VimMotionType_t motion_type;
     char ch;
} FindState_t;

typedef struct{
     Point_t entered;
     Point_t leftmost;
     int64_t backspaces;
     char* string;
     bool used_arrow_key;
} InsertModeState_t;

#define VIM_COMMAND_MAX 128

typedef struct{
     VimMode_t vim_mode;
     bool input;
     const char* input_message;
     char input_key;
     Buffer_t* shell_command_buffer; // Allocate so it can be part of the buffer list and get free'd at the end
     Buffer_t* completion_buffer; // same as shell_command_buffer (let's see how quickly this comment gets out of date!)
     Buffer_t input_buffer;
     Buffer_t buffer_list_buffer;
     int64_t last_command_buffer_jump;
     int last_key;
     char command[VIM_COMMAND_MAX];
     int64_t command_len;
     FindState_t find_state;
     struct {
          Direction_t direction;
     } search_command;
     InsertModeState_t insert_state;
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
     AutoComplete_t auto_complete;
     ClientState_t client_state;
     ServerState_t server_state;
     VimAction_t last_vim_action;
} ConfigState_t;

// TODO: try not to let justin kill me ;)
ConfigState_t* g_config_state;

bool config_free_buffer            (ConfigState_t* config_state, Buffer_t* buffer);
bool config_alloc_lines            (ConfigState_t* config_state, Buffer_t* buffer, int64_t line_count);
bool config_clear_lines            (ConfigState_t* config_state, Buffer_t* buffer);
bool config_clear_lines_readonly   (ConfigState_t* config_state, Buffer_t* buffer);
bool config_load_string            (ConfigState_t* config_state, Buffer_t* buffer, const char* string);
bool config_insert_char            (ConfigState_t* config_state, Buffer_t* buffer, Point_t location, char c);
bool config_insert_char_readonly   (ConfigState_t* config_state, Buffer_t* buffer, Point_t location, char c);
bool config_append_char            (ConfigState_t* config_state, Buffer_t* buffer, char c);
bool config_append_char_readonly   (ConfigState_t* config_state, Buffer_t* buffer, char c);
bool config_remove_char            (ConfigState_t* config_state, Buffer_t* buffer, Point_t location);
bool config_set_char               (ConfigState_t* config_state, Buffer_t* buffer, Point_t location, char c);
bool config_insert_string          (ConfigState_t* config_state, Buffer_t* buffer, Point_t location, const char* string);
bool config_insert_string_readonly (ConfigState_t* config_state, Buffer_t* buffer, Point_t location, const char* string);
bool config_remove_string          (ConfigState_t* config_state, Buffer_t* buffer, Point_t location, int64_t length);
bool config_prepend_string         (ConfigState_t* config_state, Buffer_t* buffer, int64_t line, const char* string);
bool config_append_string          (ConfigState_t* config_state, Buffer_t* buffer, int64_t line, const char* string);
bool config_append_string_readonly (ConfigState_t* config_state, Buffer_t* buffer, int64_t line, const char* string);
bool config_insert_line            (ConfigState_t* config_state, Buffer_t* buffer, int64_t line, const char* string);
bool config_insert_line_readonly   (ConfigState_t* config_state, Buffer_t* buffer, int64_t line, const char* string);
bool config_remove_line            (ConfigState_t* config_state, Buffer_t* buffer, int64_t line);
bool config_append_line            (ConfigState_t* config_state, Buffer_t* buffer, const char* string);
bool config_append_line_readonly   (ConfigState_t* config_state, Buffer_t* buffer, const char* string);
bool config_join_line              (ConfigState_t* config_state, Buffer_t* buffer, int64_t line);
bool config_insert_newline         (ConfigState_t* config_state, Buffer_t* buffer, int64_t line);
bool config_save_buffer            (ConfigState_t* config_state, Buffer_t* buffer, const char* filename);
bool config_set_cursor             (ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor, Point_t location);
bool config_move_cursor            (ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor, Point_t delta);
bool config_move_cursor_to_beginning_of_word       (ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor, bool punctuation_word_boundaries);
bool config_move_cursor_to_end_of_word             (ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor, bool punctuation_word_boundaries);
bool config_move_cursor_to_next_word               (ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor, bool punctuation_word_boundaries);
bool config_move_cursor_to_end_of_line             (ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor);
bool config_move_cursor_to_soft_end_of_line        (ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor);
bool config_move_cursor_to_beginning_of_line       (ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor);
bool config_move_cursor_to_soft_beginning_of_line  (ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor);
bool config_move_cursor_to_end_of_file             (ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor);
bool config_move_cursor_to_beginning_of_file       (ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor);
bool config_move_cursor_forward_to_char            (ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor, char c);
bool config_move_cursor_backward_to_char           (ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor, char c);
bool config_move_cursor_to_matching_pair           (ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor);
bool config_commit_undo            (ConfigState_t* config_state, Buffer_t* buffer, BufferCommitNode_t** tail, Point_t* cursor);

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

// TODO: should be replaced with ce_advance_cursor(buffer, cursor, -1), but it didn't work for me, and I'm not sure why!
Point_t previous_point(Buffer_t* buffer, Point_t point)
{
     Point_t previous_point = {point.x - 1, point.y};

     if(previous_point.x < 0){
          previous_point.y--;
          if(previous_point.y < 0){
               previous_point = (Point_t){0, 0};
          }else{
               previous_point.x = ce_last_index(buffer->lines[previous_point.y]);
          }
     }

     return previous_point;
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
     BT_LOCAL = 0,
     BT_NETWORK,
}BufferType_t;
typedef struct{
     BufferCommitNode_t* commit_tail;
     BackspaceNode_t* backspace_head;
     struct MarkNode_t* mark_head;
     BufferType_t type;
} BufferState_t;

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

YankNode_t* find_yank(YankNode_t* head, char reg_char){
     YankNode_t* itr = head;
     while(itr != NULL){
          if(itr->reg_char == reg_char) return itr;
          itr = itr->next;
     }
     return NULL;
}

// for now the yanked string is user allocated. eventually will probably
// want to change this interface so that everything is hidden
void add_yank(YankNode_t** head, char reg_char, const char* yank_text, YankMode_t mode){
     YankNode_t* node = find_yank(*head, reg_char);
     if(node != NULL){
          free((void*)node->text);
     }
     else{
          YankNode_t* new_yank = malloc(sizeof(*new_yank));
          new_yank->reg_char = reg_char;
          new_yank->next = *head;
          node = new_yank;
          *head = new_yank;
     }

     node->text = yank_text;
     node->mode = mode;
}

void yank_visual_range(Buffer_t* buffer, Point_t* cursor, Point_t* visual_start, YankNode_t** yank_head)
{
     Point_t start = *visual_start;
     Point_t end = {cursor->x, cursor->y};

     const Point_t* a = &start;
     const Point_t* b = &end;

     ce_sort_points(&a, &b);

     add_yank(yank_head, '0', ce_dupe_string(buffer, *a, *b), YANK_NORMAL);
     add_yank(yank_head, '"', ce_dupe_string(buffer, *a, *b), YANK_NORMAL);
}

void yank_visual_lines(Buffer_t* buffer, Point_t* cursor, Point_t* visual_start, YankNode_t** yank_head)
{
     int64_t start_line = visual_start->y;
     int64_t end_line = cursor->y;

     if(start_line > end_line){
          int64_t tmp = start_line;
          start_line = end_line;
          end_line = tmp;
     }

     Point_t start = {0, start_line};
     Point_t end = {ce_last_index(buffer->lines[end_line]), end_line};

     add_yank(yank_head, '0', ce_dupe_string(buffer, start, end), YANK_LINE);
     add_yank(yank_head, '"', ce_dupe_string(buffer, start, end), YANK_LINE);
}

void remove_visual_range(Buffer_t* buffer, Point_t* cursor, Point_t* visual_start)
{
     BufferState_t* buffer_state = buffer->user_data;
     Point_t start = *visual_start;
     Point_t end = {cursor->x, cursor->y};

     const Point_t* a = &start;
     const Point_t* b = &end;

     ce_sort_points(&a, &b);

     char* removed_str = ce_dupe_string(buffer, *a, *b);
     int64_t remove_len = ce_compute_length(buffer, *a, *b);
     if(config_remove_string(g_config_state, buffer, *a, remove_len)){
          ce_commit_remove_string(&buffer_state->commit_tail, *a, *cursor, *a, removed_str);
          config_set_cursor(g_config_state, buffer, cursor, *a);
     }else{
          free(removed_str);
     }
}

void remove_visual_lines(Buffer_t* buffer, Point_t* cursor, Point_t* visual_start)
{
     BufferState_t* buffer_state = buffer->user_data;
     int64_t start_line = visual_start->y;
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
     if(config_remove_string(g_config_state, buffer, start, remove_len)){
          ce_commit_remove_string(&buffer_state->commit_tail, start, *cursor, start,
                                  removed_str);
          config_set_cursor(g_config_state, buffer, cursor, start);
     }else{
          free(removed_str);
     }
}

VimCommandState_t vim_action_from_string(const char* string, VimAction_t* action, VimMode_t vim_mode,
                                         Buffer_t* buffer, Point_t* cursor, Point_t* visual_start)
{
     char tmp[BUFSIZ];
     bool visual_mode = false;
     bool get_motion = true;
     VimAction_t built_action = {};

     built_action.multiplier = 1;
     built_action.motion.multiplier = 1;

     // get multiplier if there is one
     const char* itr = string;
     while(*itr && isdigit(*itr)) itr++;
     if(itr != string){
          int64_t len = itr - string;
          strncpy(tmp, string, len);
          tmp[len + 1] = 0;
          built_action.multiplier = atoi(tmp);

          if(built_action.multiplier == 0){
               // it's actually just a motion to move to the beginning of the line!
               built_action.multiplier = 1;
               built_action.change.type = VCT_MOTION;
               built_action.motion.type = VMT_BEGINNING_OF_LINE_HARD;
               *action = built_action;
               return VCS_COMPLETE;
          }
     }

     // set motions early if visual mode, allowing them to be overriden by any action that wants to
     if(vim_mode == VM_VISUAL_RANGE){
          visual_mode = true;
          get_motion = false;
          built_action.motion.type = VMT_VISUAL_RANGE;
          built_action.motion.visual_length = ce_compute_length(buffer, *visual_start, *cursor) - 1;
          built_action.motion.visual_start_after = ce_point_after(*visual_start, *cursor);
     }else if(vim_mode == VM_VISUAL_LINE){
          visual_mode = true;
          get_motion = false;
          built_action.motion.type = VMT_VISUAL_LINE;
          built_action.motion.visual_lines = visual_start->y - cursor->y;
     }

     // get the change
     char change_char = *itr;
     switch(*itr){
     default:
          built_action.end_in_vim_mode = vim_mode;
          built_action.change.type = VCT_MOTION;
          itr--;
          if(visual_mode) get_motion = true; // if we are just executing a motion, use override the built motion
          break;
     case 'd':
          built_action.change.type = VCT_DELETE;
          built_action.yank = true;
          break;
     case 'D':
          built_action.change.type = VCT_DELETE;
          built_action.motion.type = VMT_END_OF_LINE_HARD;
          built_action.yank = true;
          get_motion = false;
          break;
     case 'c':
          built_action.change.type = VCT_DELETE;
          built_action.end_in_vim_mode = VM_INSERT;
          built_action.yank = true;
          break;
     case 'C':
          built_action.change.type = VCT_DELETE;
          built_action.motion.type = VMT_END_OF_LINE_HARD;
          built_action.end_in_vim_mode = VM_INSERT;
          built_action.yank = true;
          get_motion = false;
          break;
     case 'a':
          if(vim_mode == VM_VISUAL_RANGE) { // wait for aw in visual range mode
               built_action.end_in_vim_mode = vim_mode;
               itr--; // back up so this counts as a motion
               get_motion = true;
               break;
          }
          built_action.change.type = VCT_MOTION;
          built_action.motion.type = VMT_RIGHT;
          built_action.end_in_vim_mode = VM_INSERT;
          get_motion = false;
          break;
     case 'A':
          built_action.change.type = VCT_MOTION;
          built_action.motion.type = VMT_END_OF_LINE_PASSED;
          built_action.end_in_vim_mode = VM_INSERT;
          get_motion = false;
          break;
     case 's':
          built_action.change.type = VCT_DELETE;
          built_action.end_in_vim_mode = VM_INSERT;
          get_motion = false;
          break;
     case 'S':
          built_action.change.type = VCT_DELETE;
          built_action.motion.type = VMT_END_OF_LINE_HARD;
          built_action.end_in_vim_mode = VM_INSERT;
          get_motion = false;
          break;
     case 'i':
          if(vim_mode == VM_VISUAL_RANGE) { // wait for iw in visual range mode
               built_action.end_in_vim_mode = vim_mode;
               itr--; // back up so this counts as a motion
               get_motion = true;
               break;
          }
          built_action.end_in_vim_mode = VM_INSERT;
          get_motion = false;
          break;
     case 'v':
          built_action.end_in_vim_mode = VM_VISUAL_RANGE;
          get_motion = false;
          break;
     case 'V':
          built_action.end_in_vim_mode = VM_VISUAL_LINE;
          get_motion = false;
          break;
     case 'I':
          built_action.motion.type = VMT_BEGINNING_OF_LINE_SOFT;
          built_action.end_in_vim_mode = VM_INSERT;
          get_motion = false;
          break;
     case 'x':
          built_action.change.type = VCT_DELETE;
          get_motion = false;
          if(visual_mode) get_motion = false;
          break;
     case 'r':
          built_action.change.type = VCT_CHANGE_CHAR;
          built_action.change.change_char = *(++itr);
          if(!built_action.change.change_char){
               return VCS_CONTINUE;
          }
          get_motion = false;
          break;
     case 'p':
          built_action.change.type = VCT_PASTE_AFTER;
          get_motion = false;
          break;
     case 'P':
          built_action.change.type = VCT_PASTE_BEFORE;
          get_motion = false;
          break;
     case 'g':
          built_action.change.type = VCT_MOTION;
          break;
     case 'y':
          built_action.change.type = VCT_YANK;
          break;
     case 'Y':
          built_action.change.type = VCT_YANK;
          built_action.motion.type = VMT_END_OF_LINE_HARD;
          get_motion = false;
          break;
     case '>':
          built_action.change.type = VCT_INDENT;
          break;
     case '<':
          built_action.change.type = VCT_UNINDENT;
          break;
     }

     if(get_motion){
          // get the motion multiplier
          itr++;
          const char* start_itr = itr;
          while(*itr && isdigit(*itr)) itr++;
          if(itr != start_itr){
               int64_t len = itr - start_itr;
               strncpy(tmp, start_itr, len);
               tmp[len + 1] = 0;
               built_action.motion.multiplier = atoi(tmp);

               if(built_action.motion.multiplier == 0){
                    built_action.motion.multiplier = 1;
                    itr--;
               }
          }

          // get the motion
          switch(*itr){
          default:
               return VCS_INVALID;
          case '\0':
               return VCS_CONTINUE;
          case 'h':
               built_action.motion.type = VMT_LEFT;
               break;
          case 'j':
               if(change_char == 'd' || change_char == 'c' ||
                  change_char == 'D' || change_char == 'C' ||
                  change_char == '<' || change_char == '>'){
                    built_action.motion.type = VMT_LINE_DOWN;
               }else{
                    built_action.motion.type = VMT_DOWN;
               }
               break;
          case 'k':
               if(change_char == 'd' || change_char == 'c' ||
                  change_char == 'D' || change_char == 'C' ||
                  change_char == '<' || change_char == '>'){
                    built_action.motion.type = VMT_LINE_UP;
               }else{
                    built_action.motion.type = VMT_UP;
               }
               break;
          case 'l':
               built_action.motion.type = VMT_RIGHT;
               break;
          case 'w':
               built_action.motion.type = VMT_WORD_LITTLE;
               break;
          case 'W':
               built_action.motion.type = VMT_WORD_BIG;
               break;
          case 'b':
               if(change_char == 'd' || change_char == 'c'){
                    built_action.motion.type = VMT_WORD_BEGINNING_LITTLE_PRE_CURSOR;
               }else{
                    built_action.motion.type = VMT_WORD_BEGINNING_LITTLE;
               }
               break;
          case 'B':
               if(change_char == 'd' || change_char == 'c'){
                    built_action.motion.type = VMT_WORD_BEGINNING_BIG_PRE_CURSOR;
               }else{
                    built_action.motion.type = VMT_WORD_BEGINNING_BIG;
               }
               break;
          case 'e':
               built_action.motion.type = VMT_WORD_END_LITTLE;
               break;
          case 'E':
               built_action.motion.type = VMT_WORD_END_BIG;
               break;
          case 'f':
               built_action.motion.type = VMT_FIND_NEXT_MATCHING_CHAR;
               built_action.motion.match_char = *(++itr);
               if(!built_action.motion.match_char) return VCS_CONTINUE;
               break;
          case 'F':
               built_action.motion.type = VMT_FIND_PREV_MATCHING_CHAR;
               built_action.motion.match_char = *(++itr);
               if(!built_action.motion.match_char) return VCS_CONTINUE;
               break;
          case 't':
               built_action.motion.type = VMT_TO_NEXT_MATCHING_CHAR;
               built_action.motion.match_char = *(++itr);
               if(!built_action.motion.match_char) return VCS_CONTINUE;
               break;
          case 'T':
               built_action.motion.type = VMT_TO_PREV_MATCHING_CHAR;
               built_action.motion.match_char = *(++itr);
               if(!built_action.motion.match_char) return VCS_CONTINUE;
               break;
          case '$':
               built_action.motion.type = VMT_END_OF_LINE_HARD;
               break;
          case '0':
               built_action.motion.type = VMT_BEGINNING_OF_LINE_HARD;
               break;
          case '^':
               built_action.motion.type = VMT_BEGINNING_OF_LINE_SOFT;
               break;
          case 'i': // inside
          {
               char ch = *(++itr);

               switch(ch){
               default:
                    return VCS_INVALID;
               case '\0':
                    return VCS_CONTINUE;
               case 'w':
                    built_action.motion.type = VMT_INSIDE_WORD_LITTLE;
                    break;
               case 'W':
                    built_action.motion.type = VMT_INSIDE_WORD_BIG;
                    break;
               case '"':
                    built_action.motion.type = VMT_INSIDE_PAIR;
                    built_action.motion.inside_pair = ch;
                    break;
               }
          } break;
          case 'a': // around
          {
               char ch = *(++itr);

               switch(ch){
               default:
                    return VCS_INVALID;
               case '\0':
                    return VCS_CONTINUE;
               case 'w':
                    built_action.motion.type = VMT_AROUND_WORD_LITTLE;
                    break;
               case 'W':
                    built_action.motion.type = VMT_AROUND_WORD_BIG;
                    break;
               case '"':
                    built_action.motion.type = VMT_AROUND_PAIR;
                    built_action.motion.around_pair = ch;
                    break;
               }
          } break;
          case 'g':
               if(change_char == 'g') {
                    built_action.motion.type = VMT_BEGINNING_OF_FILE;
               }else{
                    return VCS_INVALID;
               }
          break;
          case 'G':
          built_action.motion.type = VMT_END_OF_FILE;
          break;
          case 'c':
               if(change_char == 'c') {
                    built_action.motion.type = VMT_LINE;
               }else{
                    return VCS_INVALID;
               }
               break;
          case 'd':
               if(change_char == 'd') {
                    built_action.motion.type = VMT_LINE;
               }else{
                    return VCS_INVALID;
               }
               break;
          case 'y':
               if(change_char == 'y') {
                    built_action.motion.type = VMT_LINE;
               }else{
                    return VCS_INVALID;
               }
               break;
          case '<':
               if(change_char == '<') {
                    built_action.motion.type = VMT_LINE;
               }else{
                    return VCS_INVALID;
               }
               break;
          case '>':
               if(change_char == '>') {
                    built_action.motion.type = VMT_LINE;
               }else{
                    return VCS_INVALID;
               }
               break;
          }
     }

     *action = built_action;
     return VCS_COMPLETE;
}

void indent_line(Buffer_t* buffer, BufferCommitNode_t** commit_tail, int64_t line, Point_t* cursor)
{
     if(line >= buffer->line_count) return;
     if(!buffer->lines[line][0]) return;
     Point_t loc = {0, line};
     config_insert_string(g_config_state, buffer, loc, TAB_STRING);
     ce_commit_insert_string(commit_tail, loc, *cursor, *cursor, strdup(TAB_STRING));
}

void unindent_line(Buffer_t* buffer, BufferCommitNode_t** commit_tail, int64_t line, Point_t* cursor)
{
     if(line >= buffer->line_count) return;
     if(!buffer->lines[line][0]) return;

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
          config_remove_string(g_config_state, buffer, loc, whitespace_count);
          ce_commit_remove_string(commit_tail, loc, *cursor, *cursor, strdup(TAB_STRING));
     }
}

bool vim_action_apply(VimAction_t* action, Buffer_t* buffer, Point_t* cursor, VimMode_t vim_mode,
                      YankNode_t** yank_head, VimMode_t* final_mode, Point_t* visual_start,
                      FindState_t* find_state)
{
     BufferState_t* buffer_state = buffer->user_data;
     Point_t start = *cursor;
     Point_t end = start;

     YankMode_t yank_mode = YANK_NORMAL;

     // setup the start and end if we are in visual mode
     if(action->motion.type == VMT_VISUAL_RANGE){
          Point_t calc_visual_start = *cursor;
          int64_t visual_length = action->motion.visual_length;
          if(!action->motion.visual_start_after) visual_length = -visual_length;
          ce_advance_cursor(buffer, &calc_visual_start, visual_length);
          start = *cursor;
          end = calc_visual_start;
     }else if(action->motion.type == VMT_VISUAL_LINE){
          Point_t calc_visual_start = {cursor->x, cursor->y + action->motion.visual_lines};

          // in visual line mode, we need to figure out which points are first/last so that we can set the
          // start/end 'x's accordingly, to the beginning and end of line
          const Point_t* a = cursor;
          const Point_t* b = &calc_visual_start;

          ce_sort_points(&a, &b);

          start = *a;
          end = *b;
          start.x = 0;
          end.x = strlen(buffer->lines[end.y]);
          yank_mode = YANK_LINE;
     }else{
          int64_t multiplier = action->multiplier * action->motion.multiplier;

          for(int64_t i = 0; i < multiplier; ++i){
               // get range based on motion
               switch(action->motion.type){
               default:
               case VMT_NONE:
                    break;
               case VMT_LEFT:
                    ce_move_cursor(buffer, &end, (Point_t){-1, 0}, MF_ALLOW_EOL);
                    break;
               case VMT_RIGHT:
                    ce_move_cursor(buffer, &end, (Point_t){1, 0}, MF_ALLOW_EOL);
                    break;
               case VMT_UP:
                    ce_move_cursor(buffer, &end, (Point_t){0, -1}, MF_ALLOW_EOL);
                    break;
               case VMT_DOWN:
                    ce_move_cursor(buffer, &end, (Point_t){0, 1}, MF_ALLOW_EOL);
                    break;
               case VMT_WORD_LITTLE:
                    ce_move_cursor_to_next_word(buffer, &end, true);

                    // when we are not executing a motion delete up to the next word
                    if(action->change.type != VCT_MOTION){
                         end.x--;
                         if(end.x < 0) end.x = 0;
                    }
                    break;
               case VMT_WORD_BIG:
                    ce_move_cursor_to_next_word(buffer, &end, false);

                    // when we are not executing a motion delete up to the next word
                    if(action->change.type != VCT_MOTION){
                         end.x--;
                         if(end.x < 0) end.x = 0;
                    }
                    break;
               case VMT_WORD_BEGINNING_LITTLE:
                    ce_move_cursor_to_beginning_of_word(buffer, &end, true);
                    break;
               case VMT_WORD_BEGINNING_LITTLE_PRE_CURSOR:
                    ce_move_cursor_to_beginning_of_word(buffer, &end, true);
                    start.x--;
                    if(start.x < 0) end.x = 0;
                    break;
               case VMT_WORD_BEGINNING_BIG:
                    ce_move_cursor_to_beginning_of_word(buffer, &end, false);
                    break;
               case VMT_WORD_BEGINNING_BIG_PRE_CURSOR:
                    ce_move_cursor_to_beginning_of_word(buffer, &end, false);
                    start.x--;
                    if(start.x < 0) end.x = 0;
                    break;
               case VMT_WORD_END_LITTLE:
                    ce_move_cursor_to_end_of_word(buffer, &end, true);
                    break;
               case VMT_WORD_END_BIG:
                    ce_move_cursor_to_end_of_word(buffer, &end, false);
                    break;
               case VMT_LINE:
                    start.x = 0;
                    end.x = strlen(buffer->lines[end.y]);
                    yank_mode = YANK_LINE;
                    break;
               case VMT_LINE_UP:
                    start.x = 0;
                    start.y--;
                    if(start.y < 0) start.y = 0;
                    end.x = strlen(buffer->lines[end.y]);
                    yank_mode = YANK_LINE;
                    break;
               case VMT_LINE_DOWN:
                    start.x = 0;
                    end.y++;
                    if(end.y >= buffer->line_count) end.y = buffer->line_count - 1;
                    if(end.y < 0) end.y = 0;
                    end.x = strlen(buffer->lines[end.y]);
                    yank_mode = YANK_LINE;
                    break;
               case VMT_FIND_NEXT_MATCHING_CHAR:
                    if(ce_move_cursor_forward_to_char(buffer, &end, action->motion.match_char)){
                         find_state->motion_type = action->motion.type;
                         find_state->ch = action->motion.match_char;
                    }
                    break;
               case VMT_FIND_PREV_MATCHING_CHAR:
                    if(ce_move_cursor_backward_to_char(buffer, &end, action->motion.match_char)){
                         find_state->motion_type = action->motion.type;
                         find_state->ch = action->motion.match_char;
                    }
                    break;
               case VMT_TO_NEXT_MATCHING_CHAR:
                    end.x++;
                    if(ce_move_cursor_forward_to_char(buffer, &end, action->motion.match_char)){
                         find_state->motion_type = action->motion.type;
                         find_state->ch = action->motion.match_char;
                         end.x--;
                         if(end.x < 0) end.x = 0;
                    }else{
                         end.x--;
                    }
                    break;
               case VMT_TO_PREV_MATCHING_CHAR:
               {
                    end.x--;
                    if(ce_move_cursor_backward_to_char(buffer, &end, action->motion.match_char)){
                         find_state->motion_type = action->motion.type;
                         find_state->ch = action->motion.match_char;
                         end.x++;
                         int64_t line_len = strlen(buffer->lines[end.y]);
                         if(end.x > line_len) end.x = line_len;
                    }else{
                         end.x++;
                    }
               }break;
               case VMT_BEGINNING_OF_FILE:
                    ce_move_cursor_to_beginning_of_file(&end);
                    break;
               case VMT_BEGINNING_OF_LINE_HARD:
                    ce_move_cursor_to_beginning_of_line(buffer, &end);
                    break;
               case VMT_BEGINNING_OF_LINE_SOFT:
                    ce_move_cursor_to_soft_beginning_of_line(buffer, &end);
                    break;
               case VMT_END_OF_LINE_PASSED:
                    ce_move_cursor_to_end_of_line(buffer, &end);
                    end.x++;
                    break;
               case VMT_END_OF_LINE_HARD:
                    ce_move_cursor_to_end_of_line(buffer, &end);
                    break;
               case VMT_END_OF_LINE_SOFT:
                    ce_move_cursor_to_soft_end_of_line(buffer, &end);
                    break;
               case VMT_END_OF_FILE:
                    ce_move_cursor_to_end_of_file(buffer, &end);
                    break;
               case VMT_INSIDE_PAIR:
                    if(!ce_get_homogenous_adjacents(buffer, &start, &end, isnotquote)) return false;
                    if(start.x == end.x && start.y == end.y) return false;
                    if(start.x == 0) return false;
                    break;
               case VMT_INSIDE_WORD_LITTLE:
                    ce_get_word_at_location(buffer, *cursor, &start, &end);
                    break;
               case VMT_INSIDE_WORD_BIG:
               {
                    char curr_char;
                    if(!ce_get_char(buffer, start, &curr_char)) return false;

                    if(isblank(curr_char)){
                         ce_get_homogenous_adjacents(buffer, &start, &end, isblank);
                    }else{
                         assert(ispunct_or_iswordchar(curr_char));
                         ce_get_homogenous_adjacents(buffer, &start, &end, ispunct_or_iswordchar);
                    }
               } break;
               case VMT_AROUND_PAIR:
                    if(!ce_get_homogenous_adjacents(buffer, &start, &end, isnotquote)) return false;
                    if(start.x == end.x && start.y == end.y) return false;
                    start.x--;
                    end.x++;
                    break;
                    // TIME TO SLURP
#define SLURP_RIGHT(condition)                                                              \
                    do{ end.x++; if(!ce_get_char(buffer, end, &c)) break; }while(condition(c)); \
                    end.x--;

#define SLURP_LEFT(condition)                                                                   \
                    do{ start.x--; if(!ce_get_char(buffer, start, &c)) break; }while(condition(c)); \
                    start.x++;

               case VMT_AROUND_WORD_LITTLE:
               {
                    char c;
                    if(!ce_get_char(buffer, start, &c)) return false;

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
               case VMT_AROUND_WORD_BIG:
               {
                    char c;
                    if(!ce_get_char(buffer, start, &c)) return false;

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
               }
          }
     }

     const Point_t* sorted_start = &start;
     const Point_t* sorted_end = &end;

     ce_sort_points(&sorted_start, &sorted_end);

     // perform action on range
     switch(action->change.type){
     default:
          break;
     case VCT_MOTION:
          config_set_cursor(g_config_state, buffer, cursor, end);
          if(vim_mode == VM_VISUAL_RANGE){
               // expand the selection for some motions
               if(ce_point_after(*visual_start, *cursor) &&
                  ce_point_after(*sorted_end, *visual_start)){
                     *visual_start = *sorted_end;
               }else if(ce_point_after(*cursor, *visual_start) &&
                        ce_point_after(*visual_start, *sorted_start)){
                     *visual_start = *sorted_start;
               }
          }
          break;
     case VCT_DELETE:
     {
          *cursor = *sorted_start;

          char* commit_string = ce_dupe_string(buffer, *sorted_start, *sorted_end);
          int64_t len = ce_compute_length(buffer, *sorted_start, *sorted_end);

          if(!config_remove_string(g_config_state, buffer, *sorted_start, len)){
               free(commit_string);
               return false;
          }

          if(action->yank){
               char* yank_string = strdup(commit_string);
               if(yank_mode == YANK_LINE && yank_string[len-1] == NEWLINE) yank_string[len-1] = 0;
               add_yank(yank_head, '"', yank_string, yank_mode);
          }

          ce_commit_remove_string(&buffer_state->commit_tail, *sorted_start, *cursor, *sorted_start, commit_string);
     } break;
     case VCT_PASTE_BEFORE:
     {
          YankNode_t* yank = find_yank(*yank_head, '"');

          if(!yank) return false;

          switch(yank->mode){
          default:
               break;
          case YANK_NORMAL:
          {
               if(config_insert_string(g_config_state, buffer, *sorted_start, yank->text)){
                    ce_commit_insert_string(&buffer_state->commit_tail,
                                            *sorted_start, *sorted_start, *sorted_start,
                                            strdup(yank->text));
               }
          } break;
          case YANK_LINE:
          {
                    size_t len = strlen(yank->text);
                    char* save_str = malloc(len + 2); // newline and '\0'
                    Point_t insert_loc = {0, cursor->y};
                    Point_t cursor_loc = {0, cursor->y};

                    save_str[len] = '\n'; // append a new line to create a line
                    save_str[len+1] = '\0';
                    memcpy(save_str, yank->text, len);

                    if(config_insert_string(g_config_state, buffer, insert_loc, save_str)){
                         ce_commit_insert_string(&buffer_state->commit_tail,
                                                 insert_loc, *cursor, cursor_loc,
                                                 save_str);
                    }
          } break;
          }
     } break;
     case VCT_PASTE_AFTER:
     {
          YankNode_t* yank = find_yank(*yank_head, '"');

          if(!yank) return false;

          switch(yank->mode){
          default:
               break;
          case YANK_NORMAL:
          {
               Point_t insert_cursor = *cursor;
               int64_t yank_len = strlen(yank->text);

               if(buffer->lines[cursor->y][0]){
                    insert_cursor.x++; // don't increment x for blank lines
               }else{
                    assert(cursor->x == 0);
                    // if the line is empty we cannot paste after the first character, so when we move the
                    // cursor at the end, we need to account for 1 less character
                    yank_len--;
               }

               if(config_insert_string(g_config_state, buffer, insert_cursor, yank->text)){
                    ce_commit_insert_string(&buffer_state->commit_tail,
                                            insert_cursor, *sorted_start, *sorted_start,
                                            strdup(yank->text));
                    ce_advance_cursor(buffer, cursor, yank_len);
               }
          } break;
          case YANK_LINE:
          {
               size_t len = strlen(yank->text);
               char* save_str = malloc(len + 2); // newline and '\0'
               Point_t cursor_loc = {0, cursor->y + 1};
               Point_t insert_loc = {strlen(buffer->lines[cursor->y]), cursor->y};

               save_str[0] = '\n'; // prepend a new line to create a line
               memcpy(save_str + 1, yank->text, len + 1); // also copy the '\0'

               if(config_insert_string(g_config_state, buffer, insert_loc, save_str)){
                    ce_commit_insert_string(&buffer_state->commit_tail,
                                            insert_loc, *cursor, cursor_loc,
                                            save_str);
                    config_set_cursor(g_config_state, buffer, cursor, cursor_loc);
               }
          } break;
          }
     } break;
     case VCT_CHANGE_CHAR:
     {
          char prev_char;

          if(!ce_get_char(buffer, *sorted_start, &prev_char)) return false;
          if(!config_set_char(g_config_state, buffer, *sorted_start, action->change.change_char)) return false;

          ce_commit_change_char(&buffer_state->commit_tail, *sorted_start, *cursor, *sorted_start,
                                action->change.change_char, prev_char);
     } break;
     case VCT_YANK:
     {
          char* save_zero = ce_dupe_string(buffer, *sorted_start, *sorted_end);
          char* save_quote = ce_dupe_string(buffer, *sorted_start, *sorted_end);

          if(yank_mode == YANK_LINE){
               int64_t last_index = strlen(save_zero) - 1;
               if(last_index >= 0 && save_zero[last_index] == NEWLINE){
                    save_zero[last_index] = 0;
                    save_quote[last_index] = 0;
               }
          }

          add_yank(yank_head, '0', save_zero, yank_mode);
          add_yank(yank_head, '"', save_quote, yank_mode);
     } break;
     case VCT_INDENT:
     {
          if(action->motion.type == VMT_LINE || action->motion.type == VMT_LINE_UP ||
             action->motion.type == VMT_LINE_DOWN || action->motion.type == VMT_VISUAL_RANGE ||
             action->motion.type == VMT_VISUAL_LINE){
               for(int i = sorted_start->y; i <= sorted_end->y; ++i){
                    indent_line(buffer, &buffer_state->commit_tail, i, cursor);
               }
          }else{
               return false;
          }
     } break;
     case VCT_UNINDENT:
     {
          if(action->motion.type == VMT_LINE || action->motion.type == VMT_LINE_UP ||
             action->motion.type == VMT_LINE_DOWN || action->motion.type == VMT_VISUAL_RANGE ||
             action->motion.type == VMT_VISUAL_LINE){
               for(int i = sorted_start->y; i <= sorted_end->y; ++i){
                    unindent_line(buffer, &buffer_state->commit_tail, i, cursor);
               }
          }else{
               return false;
          }
     } break;
     }

     *final_mode = action->end_in_vim_mode;

     return true;
}

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

bool auto_complete_insert(AutoComplete_t* auto_complete, const char* option)
{
     CompleteNode_t* new_node = calloc(1, sizeof(*new_node));
     if(!new_node){
          ce_message("failed to allocate auto complete option");
          return false;
     }

     new_node->option = strdup(option);

     if(auto_complete->tail){
          auto_complete->tail->next = new_node;
          new_node->prev = auto_complete->tail;
     }

     auto_complete->tail = new_node;
     if(!auto_complete->head){
          auto_complete->head = new_node;
     }

     return true;
}

void auto_complete_start(AutoComplete_t* auto_complete, Point_t start)
{
     assert(start.x >= 0);
     auto_complete->start = start;
     auto_complete->current = auto_complete->head;
}

void auto_complete_end(AutoComplete_t* auto_complete)
{
     auto_complete->start.x = -1;
}

bool auto_completing(AutoComplete_t* auto_complete)
{
    return auto_complete->start.x >= 0;
}

void auto_complete_clear(AutoComplete_t* auto_complete)
{
    CompleteNode_t* itr = auto_complete->head;
    while(itr){
         CompleteNode_t* tmp = itr;
         itr = itr->next;
         free(tmp->option);
         free(tmp);
    }

    auto_complete->head = NULL;
    auto_complete->tail = NULL;
    auto_complete->current = NULL;
}

void auto_complete_next(AutoComplete_t* auto_complete, const char* match)
{
     int64_t match_len = strlen(match);
     CompleteNode_t* initial_node = auto_complete->current;

     do{
          auto_complete->current = auto_complete->current->next;
          if(!auto_complete->current) auto_complete->current = auto_complete->head;
          if(strncmp(auto_complete->current->option, match, match_len) == 0) return;
     }while(auto_complete->current != initial_node);

     auto_complete_end(auto_complete);
}

void auto_complete_prev(AutoComplete_t* auto_complete, const char* match)
{
     int64_t match_len = strlen(match);
     CompleteNode_t* initial_node = auto_complete->current;

     do{
          auto_complete->current = auto_complete->current->prev;
          if(!auto_complete->current) auto_complete->current = auto_complete->tail;
          if(strncmp(auto_complete->current->option, match, match_len) == 0) return;
     }while(auto_complete->current != initial_node);

     auto_complete_end(auto_complete);
}


// BEGIN CONFIG VERSIONS OF LIBRARY FUNCTIONS

bool config_free_buffer(ConfigState_t* config_state, Buffer_t* buffer)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          ce_free_buffer(buffer);
          return true;
     }
     return client_free_buffer(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id);
}

bool config_alloc_lines(ConfigState_t* config_state, Buffer_t* buffer, int64_t line_count)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          return ce_alloc_lines(buffer, line_count);
     }
     return client_alloc_lines(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, line_count);
}

bool config_clear_lines(ConfigState_t* config_state, Buffer_t* buffer)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          ce_clear_lines(buffer);
          return true;
     }
     return client_clear_lines(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id);
}

bool config_clear_lines_readonly(ConfigState_t* config_state, Buffer_t* buffer)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          ce_clear_lines_readonly(buffer);
          return true;
     }
     return client_clear_lines_readonly(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id);
}

bool config_load_string(ConfigState_t* config_state, Buffer_t* buffer, const char* string)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          return ce_load_string(buffer, string);
     }
     return client_load_string(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, string);
}

bool config_insert_char(ConfigState_t* config_state, Buffer_t* buffer, Point_t location, char c)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          return ce_insert_char(buffer, location, c);
     }
     return client_insert_char(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, location, c);
}

bool config_insert_char_readonly(ConfigState_t* config_state, Buffer_t* buffer, Point_t location, char c)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          return ce_insert_char_readonly(buffer, location, c);
     }
     return client_insert_char_readonly(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, location, c);
}

bool config_append_char(ConfigState_t* config_state, Buffer_t* buffer, char c)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          return ce_append_char(buffer, c);
     }
     return client_append_char(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, c);
}

bool config_append_char_readonly(ConfigState_t* config_state, Buffer_t* buffer, char c)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          return ce_append_char_readonly(buffer, c);
     }
     return client_append_char_readonly(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, c);
}

bool config_remove_char(ConfigState_t* config_state, Buffer_t* buffer, Point_t location)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          return ce_remove_char(buffer, location);
     }
     return client_remove_char(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, location);
}

bool config_set_char(ConfigState_t* config_state, Buffer_t* buffer, Point_t location, char c)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          return ce_set_char(buffer, location, c);
     }
     return client_set_char(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, location, c);
}

bool config_insert_string(ConfigState_t* config_state, Buffer_t* buffer, Point_t location, const char* string)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          return ce_insert_string(buffer, location, string);
     }
     return client_insert_string(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, location, string);
}

bool config_insert_string_readonly(ConfigState_t* config_state, Buffer_t* buffer, Point_t location, const char* string)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          return ce_insert_string_readonly(buffer, location, string);
     }
     return client_insert_string_readonly(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, location, string);
}

bool config_remove_string(ConfigState_t* config_state, Buffer_t* buffer, Point_t location, int64_t length)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          return ce_remove_string(buffer, location, length);
     }
     return client_remove_string(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, location, length);
}

bool config_prepend_string(ConfigState_t* config_state, Buffer_t* buffer, int64_t line, const char* string)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          return ce_prepend_string(buffer, line, string);
     }
     return client_prepend_string(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, line, string);
}

bool config_append_string(ConfigState_t* config_state, Buffer_t* buffer, int64_t line, const char* string)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          return ce_append_string(buffer, line, string);
     }
     return client_append_string(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, line, string);
}

bool config_append_string_readonly(ConfigState_t* config_state, Buffer_t* buffer, int64_t line, const char* string)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          return ce_append_string_readonly(buffer, line, string);
     }
     return client_append_string_readonly(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, line, string);
}

bool config_insert_line(ConfigState_t* config_state, Buffer_t* buffer, int64_t line, const char* string)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          return ce_insert_line(buffer, line, string);
     }
     return client_insert_line(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, line, string);
}

bool config_insert_line_readonly(ConfigState_t* config_state, Buffer_t* buffer, int64_t line, const char* string)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          return ce_insert_line_readonly(buffer, line, string);
     }
     return client_insert_line_readonly(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, line, string);
}

bool config_remove_line(ConfigState_t* config_state, Buffer_t* buffer, int64_t line)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          return ce_remove_line(buffer, line);
     }
     return client_remove_line(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, line);
}

bool config_append_line(ConfigState_t* config_state, Buffer_t* buffer, const char* string)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          return ce_append_line(buffer, string);
     }
     return client_append_line(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, string);
}

bool config_append_line_readonly(ConfigState_t* config_state, Buffer_t* buffer, const char* string)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          return ce_append_line_readonly(buffer, string);
     }
     return client_append_line_readonly(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, string);
}

bool config_join_line(ConfigState_t* config_state, Buffer_t* buffer, int64_t line)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          return ce_join_line(buffer, line);
     }
     return client_join_line(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, line);
}

bool config_insert_newline(ConfigState_t* config_state, Buffer_t* buffer, int64_t line)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          return ce_insert_newline(buffer, line);
     }
     return client_insert_newline(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, line);
}

bool config_save_buffer(ConfigState_t* config_state, Buffer_t* buffer, const char* filename)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          return ce_save_buffer(buffer, filename);
     }
     return client_save_buffer(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, filename);
}

bool config_commit_undo(ConfigState_t* config_state, Buffer_t* buffer, BufferCommitNode_t** tail, Point_t* cursor)
{
     if(((BufferState_t*)buffer->user_data)->type == BT_LOCAL || pthread_equal(config_state->client_state.command_thread, pthread_self())){
          return ce_commit_undo(buffer, tail, cursor);
     }
     else return false;
}

// BEGIN CONFIG CURSOR MOVEMENT FUNCTIONS
bool config_set_cursor(ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor, Point_t location)
{
     if(!ce_set_cursor(buffer, cursor, location, (config_state->vim_mode == VM_INSERT) ? MF_ALLOW_EOL : MF_DEFAULT)) return false;
     if(((BufferState_t*)buffer->user_data)->type == BT_NETWORK){
          return client_set_cursor(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, *cursor);
     }
     return true;
}

bool config_move_cursor(ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor, Point_t delta)
{
     if(!ce_move_cursor(buffer, cursor, delta, (config_state->vim_mode == VM_INSERT) ? MF_ALLOW_EOL : MF_DEFAULT)) return false;
     if(((BufferState_t*)buffer->user_data)->type == BT_NETWORK){
          return client_set_cursor(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, *cursor);
     }
     return true;
}

bool config_move_cursor_to_beginning_of_word(ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor, bool punctuation_word_boundaries)
{
     if(!ce_move_cursor_to_beginning_of_word(buffer, cursor, punctuation_word_boundaries)) return false;
     if(((BufferState_t*)buffer->user_data)->type == BT_NETWORK){
          return client_set_cursor(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, *cursor);
     }
     return true;
}

bool config_move_cursor_to_end_of_word(ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor, bool punctuation_word_boundaries)
{
     if(!ce_move_cursor_to_end_of_word(buffer, cursor, punctuation_word_boundaries)) return false;
     if(((BufferState_t*)buffer->user_data)->type == BT_NETWORK){
          return client_set_cursor(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, *cursor);
     }
     return true;
}

bool config_move_cursor_to_next_word(ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor, bool punctuation_word_boundaries)
{
     if(!ce_move_cursor_to_next_word(buffer, cursor, punctuation_word_boundaries)) return false;
     if(((BufferState_t*)buffer->user_data)->type == BT_NETWORK){
          return client_set_cursor(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, *cursor);
     }
     return true;
}

bool config_move_cursor_to_end_of_line(ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor)
{
     if(!ce_move_cursor_to_end_of_line(buffer, cursor)) return false;
     if(((BufferState_t*)buffer->user_data)->type == BT_NETWORK){
          return client_set_cursor(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, *cursor);
     }
     return true;
}

bool config_move_cursor_to_soft_end_of_line(ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor)
{
     if(!ce_move_cursor_to_soft_end_of_line(buffer, cursor)) return false;
     if(((BufferState_t*)buffer->user_data)->type == BT_NETWORK){
          return client_set_cursor(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, *cursor);
     }
     return true;
}

bool config_move_cursor_to_beginning_of_line(ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor)
{
     ce_move_cursor_to_beginning_of_line(buffer, cursor);
     if(((BufferState_t*)buffer->user_data)->type == BT_NETWORK){
          return client_set_cursor(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, *cursor);
     }
     return true;
}

bool config_move_cursor_to_soft_beginning_of_line  (ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor)
{
     ce_move_cursor_to_soft_beginning_of_line(buffer, cursor);
     if(((BufferState_t*)buffer->user_data)->type == BT_NETWORK){
          return client_set_cursor(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, *cursor);
     }
     return true;
}

bool config_move_cursor_to_end_of_file(ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor)
{
     if(!ce_move_cursor_to_end_of_file(buffer, cursor)) return false;
     if(((BufferState_t*)buffer->user_data)->type == BT_NETWORK){
          return client_set_cursor(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, *cursor);
     }
     return true;
}

bool config_move_cursor_to_beginning_of_file(ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor)
{
     ce_move_cursor_to_beginning_of_file(cursor);
     if(((BufferState_t*)buffer->user_data)->type == BT_NETWORK){
          return client_set_cursor(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, *cursor);
     }
     return true;
}

bool config_move_cursor_forward_to_char(ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor, char c)
{
     if(!ce_move_cursor_forward_to_char(buffer, cursor, c)) return false;
     if(((BufferState_t*)buffer->user_data)->type == BT_NETWORK){
          return client_set_cursor(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, *cursor);
     }
     return true;
}

bool config_move_cursor_backward_to_char(ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor, char c)
{
     if(!ce_move_cursor_backward_to_char(buffer, cursor, c)) return false;
     if(((BufferState_t*)buffer->user_data)->type == BT_NETWORK){
          return client_set_cursor(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, *cursor);
     }
     return true;
}

bool config_move_cursor_to_matching_pair(ConfigState_t* config_state, Buffer_t* buffer, Point_t* cursor)
{
     if(!ce_move_cursor_to_matching_pair(buffer, cursor)) return false;
     if(((BufferState_t*)buffer->user_data)->type == BT_NETWORK){
          return client_set_cursor(&config_state->client_state, config_state->client_state.server_list_head, buffer->network_id, *cursor);
     }
     return true;
}

// END CONFIG CURSOR MOVEMENT FUNCTIONS

// END CONFIG VERSIONS OF LIBRARY FUNCTIONS

typedef struct MarkNode_t{
     char reg_char;
     Point_t location;
     struct MarkNode_t* next;
} MarkNode_t;

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
     auto_complete_end(&config_state->auto_complete);
}

void enter_insert_mode(ConfigState_t* config_state, Point_t cursor)
{
     if(config_state->tab_current->view_current->buffer->readonly) return;
     config_state->vim_mode = VM_INSERT;
     config_state->insert_state.leftmost = cursor;
     config_state->insert_state.entered = cursor;
     config_state->insert_state.backspaces = 0;
     free(config_state->insert_state.string);
     config_state->insert_state.string = NULL;
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

void commit_insert_mode_changes(InsertModeState_t* insert_state, Buffer_t* buffer, BufferState_t* buffer_state, Point_t* cursor)
{
     if(insert_state->leftmost.x == cursor->x &&
        insert_state->leftmost.y == cursor->y &&
        insert_state->entered.x == cursor->x &&
        insert_state->entered.y == cursor->y){
          // pass no change
     }else{
          if(insert_state->leftmost.x == insert_state->entered.x &&
             insert_state->leftmost.y == insert_state->entered.y){
               // TODO: assert cursor is after start_insert
               // exclusively inserts
               Point_t last_inserted_char = previous_point(buffer, *cursor);
               char* inserted = ce_dupe_string(buffer, insert_state->leftmost, last_inserted_char);
               ce_commit_insert_string(&buffer_state->commit_tail,
                                       insert_state->leftmost,
                                       insert_state->entered,
                                       *cursor,
                                       inserted);
               insert_state->string = strdup(inserted);
               // NOTE: we could have added backspaces and just not used them
               backspace_free(&buffer_state->backspace_head);
          }else if(insert_state->leftmost.x < insert_state->entered.x ||
                   insert_state->leftmost.y < insert_state->entered.y){
               if(cursor->x == insert_state->leftmost.x &&
                  cursor->y == insert_state->leftmost.y){
                    // exclusively backspaces!
                    ce_commit_remove_string(&buffer_state->commit_tail,
                                            *cursor,
                                            insert_state->entered,
                                            *cursor,
                                            backspace_get_string(buffer_state->backspace_head));
                    backspace_free(&buffer_state->backspace_head);
               }else{
                    // mixture of inserts and backspaces
                    Point_t last_inserted_char = previous_point(buffer, *cursor);
                    char* inserted = ce_dupe_string(buffer,
                                                    insert_state->leftmost,
                                                    last_inserted_char);
                    ce_commit_change_string(&buffer_state->commit_tail,
                                            insert_state->leftmost,
                                            insert_state->entered,
                                            *cursor,
                                            inserted,
                                            backspace_get_string(buffer_state->backspace_head));
                    insert_state->string = strdup(inserted);
                    backspace_free(&buffer_state->backspace_head);
               }
          }
     }
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
     enter_insert_mode(config_state, config_state->view_input->cursor);

     // reset input history back to tail
     InputHistory_t* history = history_from_input_key(config_state);
     if(history) history->cur = history->tail;
}

void input_end(ConfigState_t* config_state)
{
     config_state->input = false;
     config_state->tab_current->view_current = config_state->tab_current->view_input_save;
     enter_normal_mode(config_state);
}

void input_cancel(ConfigState_t* config_state)
{
     if(config_state->input_key == '/' || config_state->input_key == '?'){
          config_state->tab_current->view_input_save->cursor = config_state->start_search;
          config_set_cursor(config_state,
                            config_state->tab_current->view_input_save->buffer,
                            &config_state->tab_current->view_input_save->cursor, config_state->start_search);
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

#define REMOTE_PREFIX "remote:"
Buffer_t* open_file_buffer(BufferNode_t* head, const char* filename)
{
     const char* remote_filename = NULL;
     if(!strncmp(REMOTE_PREFIX, filename, sizeof(REMOTE_PREFIX) - 1)){
          remote_filename = filename + (sizeof(REMOTE_PREFIX) - 1);
     }

     BufferNode_t* itr = head;
     while(itr){
          if(!strcmp(itr->buffer->name, (remote_filename) ? : filename)){
               return itr->buffer; // already open
          }
          itr = itr->next;
     }

     if(!remote_filename){
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
     else{
          // load remote file
          // NOTE: file will be populated when we receive the load file network command
          Buffer_t* buffer = new_buffer_from_string(head, remote_filename, "loading remote file...");

          // request that the file contents be loaded asynchronously over the network
          if(!client_load_file(&g_config_state->client_state,
                               g_config_state->client_state.server_list_head,
                               remote_filename)){
               ce_message("failed to load file");
               // TODO: error handling
          }
          BufferState_t* buffer_state = buffer->user_data;
          buffer_state->type = BT_NETWORK;
          return buffer;
     }
}

bool initializer(const char* server_addr, bool is_server, BufferNode_t* head, Point_t* terminal_dimensions, int argc, char** argv, void** user_data)
{
     // NOTE: need to set these in this module
     g_terminal_dimensions = terminal_dimensions;

     // setup the config's state
     ConfigState_t* config_state = calloc(1, sizeof(*config_state));
     if(!config_state){
          ce_message("failed to allocate config state");
          return false;
     }
     g_config_state = config_state;

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

     // if we reload, the shell command buffer may already exist, don't recreate it
     BufferNode_t* itr = head;
     while(itr){
          if(strcmp(itr->buffer->name, "shell_output") == 0){
               config_state->shell_command_buffer = itr->buffer;
          }
          if(strcmp(itr->buffer->name, "completions") == 0){
               config_state->completion_buffer = itr->buffer;
          }
          itr = itr->next;
     }

     if(!config_state->shell_command_buffer){
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

     if(!config_state->completion_buffer){
          config_state->completion_buffer = calloc(1, sizeof(*config_state->shell_command_buffer));
          config_state->completion_buffer->name = strdup("completions");
          config_state->completion_buffer->readonly = true;
          initialize_buffer(config_state->completion_buffer);
          BufferNode_t* new_buffer_node = ce_append_buffer_to_list(head, config_state->completion_buffer);
          if(!new_buffer_node){
               ce_message("failed to add shell command buffer to list");
               return false;
          }
     }

     *user_data = config_state;

     // client/server initialization
     if(is_server){
          ce_message("spawning server");
          config_state->server_state.buffer_list_head = head;
          ce_server_init(&config_state->server_state);
     }
     if(server_addr){
          ce_message("spawning client");
          config_state->client_state.config_user_data = config_state;
          config_state->client_state.buffer_list_head = head;
          if(!ce_client_init(&config_state->client_state, server_addr)){
               ce_message("failed to initialize client");
               return false;
          }
     }

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
          if(i == 0 && node){
               config_state->tab_current->view_current->buffer = node->buffer;
               if(server_addr){
                    config_state->tab_current->view_current->top_left = (Point_t){0, 0};
                    config_state->tab_current->view_current->bottom_right = (Point_t){g_terminal_dimensions->x - 1, g_terminal_dimensions->y - 1};
               }
          }
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
     init_pair(S_FILEPATH, COLOR_BLUE, COLOR_BACKGROUND);
     init_pair(S_DIFF_ADD, COLOR_GREEN, COLOR_BACKGROUND);
     init_pair(S_DIFF_REMOVE, COLOR_RED, COLOR_BACKGROUND);

     init_pair(S_NORMAL_HIGHLIGHTED, COLOR_FOREGROUND, COLOR_WHITE);
     init_pair(S_KEYWORD_HIGHLIGHTED, COLOR_BLUE, COLOR_WHITE);
     init_pair(S_TYPE_HIGHLIGHTED, COLOR_BRIGHT_BLUE, COLOR_WHITE);
     init_pair(S_CONTROL_HIGHLIGHTED, COLOR_YELLOW, COLOR_WHITE);
     init_pair(S_COMMENT_HIGHLIGHTED, COLOR_GREEN, COLOR_WHITE);
     init_pair(S_STRING_HIGHLIGHTED, COLOR_RED, COLOR_WHITE);
     init_pair(S_CONSTANT_HIGHLIGHTED, COLOR_MAGENTA, COLOR_WHITE);
     init_pair(S_PREPROCESSOR_HIGHLIGHTED, COLOR_BRIGHT_MAGENTA, COLOR_WHITE);
     init_pair(S_FILEPATH_HIGHLIGHTED, COLOR_BLUE, COLOR_WHITE);
     init_pair(S_DIFF_ADD_HIGHLIGHTED, COLOR_GREEN, COLOR_WHITE);
     init_pair(S_DIFF_REMOVE_HIGHLIGHTED, COLOR_RED, COLOR_WHITE);

     init_pair(S_TRAILING_WHITESPACE, COLOR_FOREGROUND, COLOR_RED);

     init_pair(S_BORDERS, COLOR_FOREGROUND, COLOR_BACKGROUND);

     init_pair(S_TAB_NAME, COLOR_WHITE, COLOR_BACKGROUND);
     init_pair(S_CURRENT_TAB_NAME, COLOR_CYAN, COLOR_BACKGROUND);

     init_pair(S_VIEW_STATUS, COLOR_CYAN, COLOR_BACKGROUND);
     init_pair(S_INPUT_STATUS, COLOR_RED, COLOR_BACKGROUND);
     init_pair(S_AUTO_COMPLETE, COLOR_WHITE, COLOR_BACKGROUND);

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
     define_key(NULL, KEY_ENTER);       // Blow away enter
     define_key("\x0D", KEY_ENTER);     // Enter       (13) (0x0D) ASCII "CR"  NL Carriage Return

     pthread_mutex_init(&draw_lock, NULL);
     pthread_mutex_init(&shell_buffer_lock, NULL);

     auto_complete_end(&config_state->auto_complete);

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

     pthread_mutex_destroy(&draw_lock);
     pthread_mutex_destroy(&shell_buffer_lock);

     auto_complete_clear(&config_state->auto_complete);

     free(config_state);
     return true;
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
          config_set_cursor(g_config_state, new_buffer, &view->cursor, dst);

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
                    config_set_cursor(g_config_state, new_buffer, &view->cursor, dst);
               }else{
                    config_move_cursor_to_soft_beginning_of_line(g_config_state, new_buffer, &view->cursor);
               }
          }else{
               config_move_cursor_to_soft_beginning_of_line(g_config_state, new_buffer, &view->cursor);
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
     ce_follow_cursor(current_view->cursor, &current_view->left_column, &current_view->top_row,
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
          Point_t bottom_right = {g_terminal_dimensions->x - 1, g_terminal_dimensions->y - 1};
          ce_calc_views(head_view, top_left, bottom_right);
          view_follow_cursor(current_view);
          new_view->cursor = current_view->cursor;
          new_view->top_row = current_view->top_row;
          new_view->left_column = current_view->left_column;
     }
}

void switch_to_view_at_point(ConfigState_t* config_state, Point_t point)
{
     BufferView_t* next_view = NULL;

     if(point.x < 0) point.x = g_terminal_dimensions->x - 1;
     if(point.y < 0) point.y = g_terminal_dimensions->y - 1;
     if(point.x >= g_terminal_dimensions->x) point.x = 0;
     if(point.y >= g_terminal_dimensions->y) point.y = 0;

     if(config_state->input) next_view = ce_find_view_at_point(config_state->view_input, point);
     if(!next_view) next_view = ce_find_view_at_point(config_state->tab_current->view_head, point);

     if(next_view){
          // save view and cursor
          config_state->tab_current->view_previous = config_state->tab_current->view_current;
          config_state->tab_current->view_current->buffer->cursor = config_state->tab_current->view_current->cursor;
          config_state->tab_current->view_current = next_view;
          enter_normal_mode(config_state);
     }
}

void handle_mouse_event(ConfigState_t* config_state, Buffer_t* buffer, BufferState_t* buffer_state, BufferView_t* buffer_view, Point_t* cursor)
{
     MEVENT event;
     if(getmouse(&event) == OK){
          bool enter_insert;
          if((enter_insert = config_state->vim_mode == VM_INSERT)){
               commit_insert_mode_changes(&config_state->insert_state, buffer, buffer_state, cursor);
               ce_clamp_cursor(buffer, cursor, MF_DEFAULT);
               enter_normal_mode(config_state);
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
               switch_to_view_at_point(config_state, click);
               click = (Point_t) {event.x - (config_state->tab_current->view_current->top_left.x - config_state->tab_current->view_current->left_column),
                                  event.y - (config_state->tab_current->view_current->top_left.y - config_state->tab_current->view_current->top_row)};
               ce_set_cursor(config_state->tab_current->view_current->buffer,
                             &config_state->tab_current->view_current->cursor,
                             click, MF_DEFAULT);
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
          if(enter_insert && config_state->tab_current->view_current == buffer_view) enter_insert_mode(config_state, *cursor);
     }
}

void half_page_up(BufferView_t* view)
{
     int64_t view_height = view->bottom_right.y - view->top_left.y;
     Point_t delta = { 0, -view_height / 2 };
     config_move_cursor(g_config_state, view->buffer, &view->cursor, delta);
}

void half_page_down(BufferView_t* view)
{
     int64_t view_height = view->bottom_right.y - view->top_left.y;
     Point_t delta = { 0, view_height / 2 };
     config_move_cursor(g_config_state, view->buffer, &view->cursor, delta);
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
          config_append_string(g_config_state, config_state->view_input->buffer, 0, history->cur->entry);
          config_state->view_input->cursor = (Point_t){0, 0};
          config_move_cursor_to_end_of_file(config_state, config_state->view_input->buffer, &config_state->view_input->cursor);
          reset_buffer_commits(&buffer_state->commit_tail);
     }

     return success;
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
     snprintf(format_string, BUFSIZ, "%%5s %%-%"PRId64"s %%-%"PRId64"s", max_name_len,
              max_buffer_lines_digits);
     snprintf(buffer_info, BUFSIZ, format_string, "flags", "buffer name", "lines");
     config_append_line(g_config_state, &config_state->buffer_list_buffer, buffer_info);

     // build buffer info
     snprintf(format_string, BUFSIZ, "%%5s %%-%"PRId64"s %%%"PRId64 PRId64, max_name_len, max_buffer_lines_digits);

     itr = head;
     while(itr){
          const char* buffer_flag_str = itr->buffer->readonly ? readonly_string(itr->buffer) :
                                                                modified_string(itr->buffer);
          snprintf(buffer_info, BUFSIZ, format_string, buffer_flag_str, itr->buffer->name,
                   itr->buffer->line_count);
          config_append_line(g_config_state, &config_state->buffer_list_buffer, buffer_info);
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

         // set stdin to be non-blocking
         int fd_flags = fcntl(input_fds[1], F_GETFL, 0);
         fcntl(input_fds[1], F_SETFL, fd_flags | O_NONBLOCK);

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

void run_shell_commands_cleanup(void* user_data)
{
     (void)(user_data);

     // release locks we could be holding
     pthread_mutex_unlock(&shell_buffer_lock);
     pthread_mutex_unlock(&draw_lock);

     // free memory we could be using
     for(int64_t i = 0; i < shell_command_data.command_count; ++i) free(shell_command_data.commands[i]);
     free(shell_command_data.commands);
}

// NOTE: runs N commands where each command is newline separated
void* run_shell_commands(void* user_data)
{
     char tmp[BUFSIZ];
     int in_fd;
     int out_fd;

     pthread_cleanup_push(run_shell_commands_cleanup, NULL);

     for(int64_t i = 0; i < shell_command_data.command_count; ++i){
          char* current_command = shell_command_data.commands[i];

          pid_t cmd_pid = bidirectional_popen(current_command, &in_fd, &out_fd);
          if(cmd_pid <= 0) pthread_exit(NULL);

          shell_command_data.shell_command_input_fd = in_fd;
          shell_command_data.shell_command_output_fd = out_fd;

          // append the command to the buffer
          snprintf(tmp, BUFSIZ, "+ %s", current_command);

          pthread_mutex_lock(&shell_buffer_lock);
          ce_append_line_readonly(shell_command_data.output_buffer, tmp);
          ce_append_char_readonly(shell_command_data.output_buffer, NEWLINE);
          pthread_mutex_unlock(&shell_buffer_lock);

          redraw_if_shell_command_buffer_in_view(shell_command_data.view_head,
                                                 shell_command_data.output_buffer,
                                                 shell_command_data.buffer_node_head,
                                                 user_data);

          // load one line at a time
          int exit_code = 0;
          while(true){
               // has the command generated any output we should read?
               int count = read(out_fd, tmp, 1);
               if(count <= 0){
                    // check if the pid has exitted
                    int status;
                    pid_t check_pid = waitpid(cmd_pid, &status, WNOHANG);
                    if(check_pid > 0){
                         exit_code = WEXITSTATUS(status);
                         break;
                    }
                    continue;
               }

               if(ioctl(out_fd, FIONREAD, &count) != -1){
                    if(count >= BUFSIZ) count = BUFSIZ - 1;
                    count = read(out_fd, tmp + 1, count);
               }

               tmp[count + 1] = 0;

               pthread_mutex_lock(&shell_buffer_lock);
               if(!ce_append_string_readonly(shell_command_data.output_buffer,
                                             shell_command_data.output_buffer->line_count - 1,
                                             tmp)){
                    pthread_exit(NULL);
               }
               pthread_mutex_unlock(&shell_buffer_lock);

               redraw_if_shell_command_buffer_in_view(shell_command_data.view_head,
                                                      shell_command_data.output_buffer,
                                                      shell_command_data.buffer_node_head,
                                                      user_data);
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

     pthread_cleanup_pop(NULL);
     return NULL;
}

void update_completion_buffer(Buffer_t* completion_buffer, AutoComplete_t* auto_complete, const char* match)
{
     assert(completion_buffer->readonly);
     ce_clear_lines_readonly(completion_buffer);

     int64_t match_len = strlen(match);
     CompleteNode_t* itr = auto_complete->head;
     while(itr){
          if(strncmp(itr->option, match, match_len) == 0){
               ce_append_line_readonly(completion_buffer, itr->option);
          }
          itr = itr->next;
     }
}

bool generate_auto_complete_files_in_dir(AutoComplete_t* auto_complete, const char* dir)
{
     struct dirent *node;
     DIR* os_dir = opendir(dir);
     if(!os_dir) return false;

     auto_complete_clear(auto_complete);

     char tmp[BUFSIZ];
     struct stat info;
     while((node = readdir(os_dir)) != NULL){
          snprintf(tmp, BUFSIZ, "%s/%s", dir, node->d_name);
          stat(tmp, &info);
          if(S_ISDIR(info.st_mode)){
               snprintf(tmp, BUFSIZ, "%s/", node->d_name);
          }else{
               strncpy(tmp, node->d_name, BUFSIZ);
          }
          auto_complete_insert(auto_complete, tmp);
     }

     closedir(os_dir);

     if(!auto_complete->head) return false;
     return true;
}

bool calc_auto_complete_start_and_path(AutoComplete_t* auto_complete, const char* line, Point_t cursor,
                                       Buffer_t* completion_buffer)
{
     // we only auto complete in the case where the cursor is up against path with directories
     // -pat|
     // -/dir/pat|
     // -/base/dir/pat|
     const char* path_begin = line + cursor.x;
     const char* last_slash = NULL;

     // if the cursor is not on the null terminator, skip
     if(*path_begin != '\0') return false;

     while(path_begin >= line){
          if(!last_slash && *path_begin == '/') last_slash = path_begin;
          if(isblank(*path_begin)) break;
          path_begin--;
     }

     path_begin++; // account for iterating 1 too far

     // generate based on the path
     bool rc = false;
     if(last_slash){
          int64_t path_len = (last_slash - path_begin) + 1;
          char* path = malloc(path_len + 1);
          if(!path){
               ce_message("failed to alloc path");
               return false;
          }

          memcpy(path, path_begin, path_len);
          path[path_len] = 0;

          rc = generate_auto_complete_files_in_dir(auto_complete, path);
          free(path);
     }else{
          rc = generate_auto_complete_files_in_dir(auto_complete, ".");
     }

     // set the start point if we generated files
     if(rc){
          if(last_slash){
               const char* completion = last_slash + 1;
               auto_complete_start(auto_complete, (Point_t){(last_slash - line) + 1, cursor.y});
               auto_complete_next(auto_complete, completion);
               update_completion_buffer(completion_buffer, auto_complete, completion);
          }else{
               auto_complete_start(auto_complete, (Point_t){(path_begin - line), cursor.y});
               auto_complete_next(auto_complete, path_begin);
               update_completion_buffer(completion_buffer, auto_complete, path_begin);
          }
     }

     return rc;
}

void confirm_action(ConfigState_t* config_state, BufferNode_t* head)
{
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Buffer_t* buffer = buffer_view->buffer;
     Point_t* cursor = &buffer_view->cursor;
     BufferState_t* buffer_state = buffer->user_data;

     if(config_state->input && buffer_view == config_state->view_input){
          input_end(config_state);

          // update convenience vars
          buffer_view = config_state->tab_current->view_current;
          buffer = config_state->tab_current->view_current->buffer;
          cursor = &config_state->tab_current->view_current->cursor;
          buffer_state = buffer->user_data;

          switch(config_state->input_key) {
          default:
               break;
          case ':':
          {
               if(config_state->view_input->buffer->line_count){
                    int64_t line = atoi(config_state->view_input->buffer->lines[0]);
                    if(line > 0){
                         *cursor = (Point_t){0, line - 1};
                         config_move_cursor_to_soft_beginning_of_line(config_state, buffer, cursor);
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
                    add_yank(&config_state->yank_head, '/', strdup(config_state->view_input->buffer->lines[0]), YANK_NORMAL);
               }
               break;
          case '?':
               if(config_state->view_input->buffer->line_count){
                    commit_input_to_history(config_state->view_input->buffer, &config_state->search_history);
                    add_yank(&config_state->yank_head, '/', strdup(config_state->view_input->buffer->lines[0]), YANK_NORMAL);
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
                    buffer_view->buffer = command_buffer;
                    buffer_view->cursor = (Point_t){0, 0};
                    buffer_view->top_row = 0;
                    command_view = buffer_view;
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
               shell_command_data.view_current = buffer_view;
               shell_command_data.user_data = config_state;

               assert(command_buffer->readonly);
               pthread_mutex_lock(&shell_buffer_lock);
               ce_clear_lines_readonly(command_buffer);
               pthread_mutex_unlock(&shell_buffer_lock);

               pthread_create(&config_state->shell_command_thread, NULL, run_shell_commands, config_state);
          } break;
          case 'R':
          {
               YankNode_t* yank = find_yank(config_state->yank_head, '/');
               if(!yank) break;
               const char* search_str = yank->text;
               // NOTE: allow empty string to replace search
               int64_t search_len = strlen(search_str);
               if(!search_len) break;
               char* replace_str = ce_dupe_buffer(config_state->view_input->buffer);
               int64_t replace_len = strlen(replace_str);
               Point_t begin = config_state->tab_current->view_input_save->buffer->highlight_start;
               Point_t end = config_state->tab_current->view_input_save->buffer->highlight_end;
               if(end.x < 0) ce_move_cursor_to_end_of_file(config_state->tab_current->view_input_save->buffer, &end);
               Point_t match = {};
               int64_t replace_count = 0;
               while(ce_find_string(buffer, begin, search_str, &match, CE_DOWN)){
                    if(ce_point_after(match, end)) break;
                    if(!config_remove_string(config_state, buffer, match, search_len)) break;
                    if(replace_len){
                         if(!config_insert_string(g_config_state, buffer, match, replace_str)) break;
                    }
                    ce_commit_change_string(&buffer_state->commit_tail, match, match, match, strdup(replace_str),
                                            strdup(search_str));
                    begin = match;
                    replace_count++;
               }
               if(replace_count){
                    ce_message("replaced %" PRId64 " matches", replace_count);
               }else{
                    ce_message("no matches found to replace");
               }
               config_set_cursor(config_state, buffer, cursor, begin);
               center_view(buffer_view);
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

               // put the line in the output buffer
               const char* input = input_buffer->lines[0];

               pthread_mutex_lock(&shell_buffer_lock);
               ce_append_string_readonly(config_state->shell_command_buffer,
                                         config_state->shell_command_buffer->line_count - 1,
                                         input);
               ce_append_char_readonly(config_state->shell_command_buffer, NEWLINE);
               pthread_mutex_unlock(&shell_buffer_lock);

               // send the input to the shell command
               write(shell_command_data.shell_command_input_fd, input, strlen(input));
               write(shell_command_data.shell_command_input_fd, "\n", 1);
          } break;
          }
     }else if(buffer_view->buffer == &config_state->buffer_list_buffer){
          int64_t line = cursor->y - 1; // account for buffer list row header
          if(line < 0) return;
          BufferNode_t* itr = head;

          while(line > 0){
               itr = itr->next;
               if(!itr) return;
               line--;
          }

          if(!itr) return;

          buffer_view->buffer = itr->buffer;
          config_set_cursor(config_state, buffer, cursor, itr->buffer->cursor);
          center_view(buffer_view);
     }else if(config_state->tab_current->view_current->buffer == config_state->shell_command_buffer){
          BufferView_t* view_to_change = buffer_view;
          if(config_state->tab_current->view_previous) view_to_change = config_state->tab_current->view_previous;

          if(goto_file_destination_in_buffer(head, config_state->shell_command_buffer, cursor->y,
                                             config_state->tab_current->view_head, view_to_change,
                                             &config_state->last_command_buffer_jump)){
               config_state->tab_current->view_current = view_to_change;
          }
     }
}

void repeat_insert_actions(InsertModeState_t* insert_state, Buffer_t* buffer, Point_t* cursor)
{
     BufferState_t* buffer_state = buffer->user_data;

     // remove any backspaces we made
     Point_t replay = *cursor;
     if(insert_state->backspaces){
          ce_advance_cursor(buffer, &replay, -insert_state->backspaces);
          Point_t previous = previous_point(buffer, *cursor);
          char* removed_string = ce_dupe_string(buffer, replay, previous);
          config_remove_string(g_config_state, buffer, replay, insert_state->backspaces);

          if(insert_state->string){
               config_insert_string(g_config_state, buffer, replay, insert_state->string);
               Point_t end = *cursor;
               ce_advance_cursor(buffer, &end, strlen(insert_state->string) -
                                 insert_state->backspaces);
               ce_commit_change_string(&buffer_state->commit_tail, replay, *cursor,
                                       end, strdup(insert_state->string),
                                       removed_string);
               *cursor = end;
          }else{
               ce_commit_remove_string(&buffer_state->commit_tail, replay, *cursor,
                                       replay, removed_string);
               *cursor = replay;
          }
     }else if(insert_state->string){
          config_insert_string(g_config_state, buffer, replay, insert_state->string);
          Point_t end = *cursor;
          ce_advance_cursor(buffer, &end, strlen(insert_state->string));
          ce_commit_insert_string(&buffer_state->commit_tail, replay, *cursor,
                                  end, strdup(insert_state->string));
          *cursor = end;
     }
}

bool key_handler(int key, BufferNode_t* head, void* user_data)
{
     ConfigState_t* config_state = user_data;
     Buffer_t* buffer = config_state->tab_current->view_current->buffer;
     BufferState_t* buffer_state = buffer->user_data;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Point_t* cursor = &config_state->tab_current->view_current->cursor;

     if(config_state->vim_mode == VM_INSERT){
          InsertModeState_t* insert_state = &config_state->insert_state;
          switch(key){
          case 27: // ESC
          {
               commit_insert_mode_changes(insert_state, buffer, buffer_state, cursor);
               enter_normal_mode(config_state);
               ce_clamp_cursor(buffer, cursor, MF_DEFAULT);
          } break;
          case KEY_MOUSE:
               handle_mouse_event(config_state, buffer, buffer_state, buffer_view, cursor);
               break;
          case KEY_BACKSPACE:
               if(insert_state->used_arrow_key){
                    enter_insert_mode(config_state, *cursor);
                    insert_state->used_arrow_key = false;
               }

               if(buffer->line_count){
                    if(cursor->x <= 0){
                         if(cursor->y){
                              int64_t prev_line_len = strlen(buffer->lines[cursor->y - 1]);
                              int64_t cur_line_len = strlen(buffer->lines[cursor->y]);

                              if(cur_line_len){
                                   config_append_string(g_config_state, buffer, cursor->y - 1, buffer->lines[cursor->y]);
                              }

                              if(config_remove_line(g_config_state, buffer, cursor->y)){
                                   backspace_push(&buffer_state->backspace_head, '\n');
                                   config_set_cursor(config_state, buffer, cursor, (Point_t){prev_line_len, cursor->y-1});
                                   if(insert_state->leftmost.y > cursor->y){
                                        insert_state->leftmost = *cursor;
                                        insert_state->backspaces++;
                                   }
                              }
                         }
                    }else{
                         // figure out if we can slurp up a tab
                         size_t n_backspaces = 1;
                         if(cursor->x % strlen(TAB_STRING) == 0){
                              n_backspaces = strlen(TAB_STRING);
                              char c = 0;
                              Point_t previous = *cursor;
                              for(size_t n=0; n<n_backspaces; n++){
                                  previous.x--;
                                  if(!ce_get_char(buffer, previous, &c) || c != ' ') n_backspaces = 1;
                              }
                         }
                         for(size_t n=0; n < n_backspaces; n++){
                              // perform backspace
                              Point_t previous = *cursor;
                              previous.x--;
                              char c = 0;
                              if(ce_get_char(buffer, previous, &c)){
                                   if(config_remove_char(g_config_state, buffer, previous)){
                                        if(previous.x < insert_state->leftmost.x){
                                             backspace_push(&buffer_state->backspace_head, c);
                                             insert_state->leftmost.x = previous.x;
                                             insert_state->backspaces++;
                                        }
                                        config_move_cursor(config_state, buffer, cursor, (Point_t){-1, 0});
                                   }
                              }
                         }
                    }

                    if(auto_completing(&config_state->auto_complete)){
                         calc_auto_complete_start_and_path(&config_state->auto_complete,
                                                           buffer->lines[cursor->y],
                                                           *cursor,
                                                           config_state->completion_buffer);
                    }
               }
               break;
          case KEY_DC:
               // TODO: with our current insert mode undo implementation we can't support this
               // config_remove_char(g_config_state, buffer, cursor);
               break;
          case '\t':
          {
               if(insert_state->used_arrow_key){
                    enter_insert_mode(config_state, *cursor);
                    insert_state->used_arrow_key = false;
               }

               if(auto_completing(&config_state->auto_complete)){
                    int64_t offset = cursor->x - config_state->auto_complete.start.x;
                    const char* complete = config_state->auto_complete.current->option + offset;
                    int64_t complete_len = strlen(complete);
                    if(config_insert_string(config_state, buffer, *cursor, complete)){
                         config_move_cursor(config_state, buffer, cursor, (Point_t){complete_len, cursor->y});

                         calc_auto_complete_start_and_path(&config_state->auto_complete,
                                                           buffer->lines[cursor->y],
                                                           *cursor,
                                                           config_state->completion_buffer);
                    }
               }else{
                    config_insert_string(config_state, buffer, *cursor, TAB_STRING);
                    config_move_cursor(config_state, buffer, cursor, (Point_t){strlen(TAB_STRING), 0});
               }
          } break;
          case KEY_ENTER: // return
          {
               if(insert_state->used_arrow_key){
                    enter_insert_mode(config_state, *cursor);
                    insert_state->used_arrow_key = false;
               }

               if(!buffer->lines) ce_alloc_lines(buffer, 1);
               char* start = buffer->lines[cursor->y] + cursor->x;
               int64_t to_end_of_line_len = strlen(start);

               if(config_insert_line(g_config_state, buffer, cursor->y + 1, start)){
                    if(to_end_of_line_len){
                         config_remove_string(g_config_state, buffer, *cursor, to_end_of_line_len);
                    }
                    config_set_cursor(config_state, buffer, cursor, (Point_t){0, cursor->y+1});

                    // indent if necessary
                    Point_t prev_line = {0, cursor->y-1};
                    int64_t indent_len = ce_get_indentation_for_next_line(buffer, prev_line, strlen(TAB_STRING));
                    if(indent_len > 0){
                         char* indent = malloc(indent_len + 1);
                         memset(indent, ' ', indent_len);
                         indent[indent_len] = '\0';

                         if(config_insert_string(config_state, buffer, *cursor, indent))
                              config_move_cursor(config_state, buffer, cursor, (Point_t){indent_len, 0});
                    }

                    if(auto_completing(&config_state->auto_complete)){
                         calc_auto_complete_start_and_path(&config_state->auto_complete,
                                                           buffer->lines[cursor->y],
                                                           *cursor,
                                                           config_state->completion_buffer);
                    }
               }
          } break;
          case KEY_UP:
          case KEY_DOWN:
               if(!insert_state->used_arrow_key){
                    commit_insert_mode_changes(insert_state, buffer, buffer_state, cursor);
               }
               insert_state->used_arrow_key = true;
               config_move_cursor(config_state, buffer, cursor, (Point_t){0, (key == KEY_DOWN) ? 1 : -1});
               break;
          case KEY_LEFT:
          case KEY_RIGHT:
               if(!insert_state->used_arrow_key){
                    commit_insert_mode_changes(insert_state, buffer, buffer_state, cursor);
               }
               insert_state->used_arrow_key = true;
               config_move_cursor(config_state, buffer, cursor, (Point_t){(key == KEY_RIGHT) ? 1 : -1, 0});
               break;
          case '}':
          {
               if(insert_state->used_arrow_key){
                    enter_insert_mode(config_state, *cursor);
                    insert_state->used_arrow_key = false;
               }

               if(config_insert_char(config_state, buffer, *cursor, key)){
                    bool do_indentation = true;
                    for(int i = 0; i < cursor->x; i++){
                         char blank_c;
                         if(ce_get_char(buffer, (Point_t){i, cursor->y}, &blank_c)){
                              // we only change the indentation if everything to the left of the cursor is blank
                              if(!isblank(blank_c)){
                                   do_indentation = false;
                                   break;
                              }
                         }
                         else assert(0);
                    }

                    if(do_indentation){
                         Point_t match = *cursor;
                         if(ce_move_cursor_to_matching_pair(buffer, &match) && match.y != cursor->y){

                              // get the match's sbol (that's the indentation we're matching)
                              Point_t sbol_match = {0, match.y};
                              ce_move_cursor_to_soft_beginning_of_line(buffer, &sbol_match);
                              if(cursor->x < sbol_match.x){
                                   // we are adding spaces
                                   int64_t n_spaces = sbol_match.x - cursor->x;
                                   for(int64_t i = 0; i < n_spaces; i++){
                                        if(!config_insert_char(config_state, buffer, (Point_t){cursor->x + i, cursor->y}, ' ')) assert(0);
                                   }

                                   config_set_cursor(config_state, buffer, cursor, (Point_t){sbol_match.x, cursor->y});
                              }
                              else{
                                   int64_t n_deletes = CE_MIN((int64_t) strlen(TAB_STRING), cursor->x - sbol_match.x);

                                   bool can_unindent = true;
                                   for(Point_t iter = {0, cursor->y}; ce_point_on_buffer(buffer, iter) && iter.x < n_deletes; iter.x++){
                                        if(!isblank(ce_get_char_raw(buffer, iter))){
                                             can_unindent = false;
                                             break;
                                        }
                                   }

                                   if(can_unindent){
                                        config_move_cursor(config_state, buffer, cursor, (Point_t){-n_deletes, 0});
                                        if(config_remove_string(g_config_state, buffer, *cursor, n_deletes)){
                                             if(insert_state->leftmost.y == cursor->y &&
                                                insert_state->leftmost.x > cursor->x){
                                                  insert_state->leftmost.x = cursor->x;
                                                  for(int i = 0; i < n_deletes; ++i){
                                                       backspace_push(&buffer_state->backspace_head, ' ');
                                                       insert_state->backspaces++;
                                                  }
                                             }
                                        }
                                   }
                              }
                         }

                    }

                    config_move_cursor(config_state, buffer, cursor, (Point_t){1, 0});
                    if(auto_completing(&config_state->auto_complete)){
                         calc_auto_complete_start_and_path(&config_state->auto_complete,
                                                           buffer->lines[cursor->y],
                                                           *cursor,
                                                           config_state->completion_buffer);
                    }
               }
          } break;
          case 14: // Ctrl + n
               if(auto_completing(&config_state->auto_complete)){
                    Point_t end = *cursor;
                    end.x--;
                    if(end.x < 0) end.x = 0;
                    char* match = ce_dupe_string(buffer, config_state->auto_complete.start, end);
                    auto_complete_next(&config_state->auto_complete, match);
                    update_completion_buffer(config_state->completion_buffer, &config_state->auto_complete,
                                             match);
                    free(match);
                    break;
               }

               if(config_state->input){
                    if(iterate_history_input(config_state, false)){
                         if(buffer->line_count && buffer->lines[cursor->y][0])
                              ce_move_cursor(buffer, cursor, (Point_t){1, 0}, MF_ALLOW_EOL);
                         enter_normal_mode(config_state);
                    }
               }
               break;
          case 16: // Ctrl + p
               if(auto_completing(&config_state->auto_complete)){
                    Point_t end = *cursor;
                    end.x--;
                    if(end.x < 0) end.x = 0;
                    char* match = ce_dupe_string(buffer, config_state->auto_complete.start, end);
                    auto_complete_prev(&config_state->auto_complete, match);
                    update_completion_buffer(config_state->completion_buffer, &config_state->auto_complete,
                                             match);
                    free(match);
                    break;
               }

               if(config_state->input){
                    if(iterate_history_input(config_state, true)){
                         if(buffer->line_count && buffer->lines[cursor->y][0])
                              ce_move_cursor(buffer, cursor, (Point_t){1, 0}, MF_ALLOW_EOL);
                         enter_normal_mode(config_state);
                    }
               }
               break;
          case 25: // Ctrl + y
               confirm_action(config_state, head);
               break;
          default:
               if(insert_state->used_arrow_key){
                    enter_insert_mode(config_state, *cursor);
                    insert_state->used_arrow_key = false;
               }

               if(config_insert_char(config_state, buffer, *cursor, key)){
                    config_move_cursor(config_state, buffer, cursor, (Point_t){1, 0});
                    if(auto_completing(&config_state->auto_complete)){
                         calc_auto_complete_start_and_path(&config_state->auto_complete,
                                                           buffer->lines[cursor->y],
                                                           *cursor,
                                                           config_state->completion_buffer);
                    }
               }
               break;
          }

          if(!auto_completing(&config_state->auto_complete) && config_state->input &&
             config_state->input_key == 6){
               calc_auto_complete_start_and_path(&config_state->auto_complete,
                                                 buffer->lines[cursor->y],
                                                 *cursor,
                                                 config_state->completion_buffer);
          }
     }else{
          bool clear_command = true;
          bool handled_key = false;

          switch(config_state->last_key){
          default:
               break;
          case 'z':
          {
               Point_t location;
               handled_key = true;
               switch(key){
               default:
                    handled_key = false;
                    break;
               case 't':
                    location = (Point_t){0, cursor->y};
                    scroll_view_to_location(buffer_view, &location);
                    break;
               case 'z': {
                    center_view(buffer_view);
                    // reset key so we don't come in here on the very next iteration
                    key = 0;
               } break;
               case 'b': {
                    // move current line to bottom of view
                    location = (Point_t){0, cursor->y - buffer_view->bottom_right.y};
                    scroll_view_to_location(buffer_view, &location);
               } break;
               }
          } break;
          case 'g':
          {
               handled_key = true;
               switch(key){
               default:
                    handled_key = false;
                    break;
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
                    Point_t word_end = *cursor;
                    ce_move_cursor_to_end_of_word(buffer, &word_end, true);
                    int64_t word_len = (word_end.x - cursor->x) + 1;
                    if(buffer->lines[word_end.y][word_end.x] == '.'){
                         Point_t ext_end = {cursor->x + word_len, cursor->y};
                         ce_move_cursor_to_end_of_word(buffer, &ext_end, true);
                         word_len += ext_end.x - word_end.x;
                    }
                    char* filename = alloca(word_len+1);
                    strncpy(filename, &buffer->lines[cursor->y][cursor->x], word_len);
                    filename[word_len] = '\0';

                    Buffer_t* file_buffer = open_file_buffer(head, filename);
                    if(file_buffer) buffer_view->buffer = file_buffer;
               } break;
               }
               if(handled_key) config_state->command_len = 0;
          } break;
          case 'm':
          {
               handled_key = true;
               char mark = key;
               add_mark(buffer_state, mark, cursor);
          } break;
          case '\'':
          {
               handled_key = true;
               Point_t* marked_location;
               char mark = key;
               marked_location = find_mark(buffer_state, mark);
               if(marked_location) {
                    config_set_cursor(config_state, buffer, cursor, (Point_t){cursor->x, marked_location->y});
                    center_view(buffer_view);
               }
          } break;
          case 'Z':
               switch(config_state->last_key){
               default:
                    break;
               case 'Z':
                    ce_save_buffer(buffer, buffer->filename);
                    return false; // quit
               }
          }

          if(!handled_key && isprint(key)){
               if(config_state->command_len >= VIM_COMMAND_MAX){
                    config_state->command_len = 0;
               }

               // insert the character into the command
               config_state->command[config_state->command_len] = key;
               config_state->command_len++;
               config_state->command[config_state->command_len] = 0;

               VimAction_t vim_action;
               VimCommandState_t command_state = vim_action_from_string(config_state->command, &vim_action,
                                                                        config_state->vim_mode, buffer,
                                                                        cursor, &config_state->visual_start);
               switch(command_state){
               default:
               case VCS_INVALID:
                    // allow command to be cleared
                    break;
               case VCS_CONTINUE:
                    // no! don't clear the command
                    clear_command = false;
                    handled_key = true;
                    break;
               case VCS_COMPLETE:
               {
                    VimMode_t final_mode = config_state->vim_mode;
                    VimMode_t original_mode = final_mode;
                    if(vim_action_apply(&vim_action, buffer, cursor, config_state->vim_mode,
                                        &config_state->yank_head, &final_mode, &config_state->visual_start,
                                        &config_state->find_state)){
                         if(final_mode != original_mode){
                              switch(final_mode){
                              default:
                                   break;
                              case VM_INSERT:
                                   enter_insert_mode(config_state, *cursor);
                                   break;
                              case VM_NORMAL:
                                   enter_normal_mode(config_state);
                                   break;
                              case VM_VISUAL_RANGE:
                                   enter_visual_range_mode(config_state, buffer_view);
                                   break;
                              case VM_VISUAL_LINE:
                                   enter_visual_line_mode(config_state, buffer_view);
                                   break;
                              }
                         }

                         if(vim_action.change.type != VCT_MOTION || vim_action.end_in_vim_mode == VM_INSERT){
                              config_state->last_vim_action = vim_action;
                         }
                         // allow the command to be cleared
                    }
                    handled_key = true;
               }    break;
               }
          }

          if(!handled_key){
               switch(key){
               default:
               {
               } break;
               case '.':
               {
                    if(config_state->insert_state.used_arrow_key){
                         repeat_insert_actions(&config_state->insert_state, buffer, cursor);
                    }else{
                         VimMode_t final_mode;
                         vim_action_apply(&config_state->last_vim_action, buffer, cursor, config_state->vim_mode,
                                          &config_state->yank_head, &final_mode, &config_state->visual_start,
                                          &config_state->find_state);

                         if(config_state->last_vim_action.end_in_vim_mode == VM_INSERT){
                              repeat_insert_actions(&config_state->insert_state, buffer, cursor);
                              // insert the string we added
                              enter_normal_mode(config_state);
                         }
                    }

               } break;
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
                    char* save_str = ce_dupe_string(buffer, join_loc, end_join_loc);
                    assert(save_str[0] == '\n');
                    if(config_remove_string(g_config_state, buffer, join_loc, ce_compute_length(buffer, join_loc, end_join_loc))){
                         config_insert_string(config_state, buffer, join_loc, " ");
                         ce_commit_change_string(&buffer_state->commit_tail, join_loc, *cursor, *cursor, strdup("\n"), save_str);
                    }
               } break;
               case 'O':
               {
                    Point_t begin_line = {0, cursor->y};

                    // indent if necessary
                    int64_t indent_len = ce_get_indentation_for_next_line(buffer, *cursor, strlen(TAB_STRING));
                    char* indent_nl = malloc(sizeof '\n' + indent_len + sizeof '\0');
                    memset(&indent_nl[0], ' ', indent_len);
                    indent_nl[indent_len] = '\n';
                    indent_nl[indent_len + 1] = '\0';

                    if(config_insert_string(config_state, buffer, begin_line, indent_nl)){
                         Point_t next_cursor = (Point_t){indent_len, cursor->y};
                         ce_commit_insert_string(&buffer_state->commit_tail, begin_line, next_cursor, next_cursor, indent_nl);
                         enter_insert_mode(config_state, next_cursor);
                         config_set_cursor(config_state, buffer, cursor, next_cursor);
                    }
               } break;
               case 'o':
               {
                    Point_t end_of_line = *cursor;
                    end_of_line.x = strlen(buffer->lines[cursor->y]);

                    // indent if necessary
                    int64_t indent_len = ce_get_indentation_for_next_line(buffer, *cursor, strlen(TAB_STRING));
                    char* nl_indent = malloc(sizeof '\n' + indent_len + sizeof '\0');
                    nl_indent[0] = '\n';
                    memset(&nl_indent[1], ' ', indent_len);
                    nl_indent[1 + indent_len] = '\0';

                    if(config_insert_string(config_state, buffer, end_of_line, nl_indent)){
                         Point_t next_cursor = *cursor;
                         ce_set_cursor(buffer, &next_cursor, (Point_t){indent_len, cursor->y + 1}, MF_ALLOW_EOL);
                         ce_commit_insert_string(&buffer_state->commit_tail, end_of_line, *cursor, next_cursor, nl_indent);
                         enter_insert_mode(config_state, next_cursor);
                         config_set_cursor(config_state, buffer, cursor, next_cursor);
                    }
               } break;
               case KEY_SAVE:
                    ce_save_buffer(buffer, buffer->filename);
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
                                   // Quit !
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

                         ce_calc_views(config_state->tab_current->view_head, top_left, bottom_right);
                         BufferView_t* new_view = ce_find_view_at_point(config_state->tab_current->view_head, save_cursor_on_terminal);
                         if(new_view){
                              config_state->tab_current->view_current = new_view;
                         }else{
                              config_state->tab_current->view_current = config_state->tab_current->view_head;
                         }
                    }
               } break;
               case 2: // Ctrl + b
                    if(config_state->input && config_state->tab_current->view_current == config_state->view_input){
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
                              if(config_insert_line(g_config_state, config_state->view_input->buffer,
                                                config_state->view_input->cursor.y + lines_added,
                                                node->entry)){
                                   lines_added += ce_count_string_lines(node->entry);
                              }else{
                                   break;
                              }
                              node = node->next;
                         }

                         if(empty_first_line) config_remove_line(g_config_state, config_state->view_input->buffer, 0);
                    }else{
                         update_buffer_list_buffer(config_state, head);
                         config_state->buffer_list_buffer.readonly = true;
                         config_state->tab_current->view_current->buffer->cursor = *cursor;
                         config_state->tab_current->view_current->buffer = &config_state->buffer_list_buffer;
                         config_state->tab_current->view_current->cursor = (Point_t){0, 1};
                         config_state->tab_current->view_current->top_row = 0;
                    }
                    break;
               case 'u':
                    if(buffer_state->commit_tail && buffer_state->commit_tail->commit.type != BCT_NONE){
                         config_commit_undo(config_state, buffer, &buffer_state->commit_tail, cursor);
                         if(buffer_state->commit_tail->commit.type == BCT_NONE){
                              buffer->modified = false;
                         }
                    }
                    break;
               case 'x':
               {
                    char c;
                    if(ce_get_char(buffer, *cursor, &c) && config_remove_char(g_config_state, buffer, *cursor)){
                         ce_commit_remove_char(&buffer_state->commit_tail, *cursor, *cursor, *cursor, c);
                         ce_clamp_cursor(buffer, cursor, MF_DEFAULT);
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
                    VimAction_t action = {};
                    FindState_t find_state; // dummy find state so our's doesn't get modified
                    VimMode_t final_mode;
                    action.multiplier = 1;
                    action.change.type = VCT_MOTION;
                    action.motion.type = config_state->find_state.motion_type;
                    action.motion.multiplier = 1;
                    action.motion.match_char = config_state->find_state.ch;
                    vim_action_apply(&action, buffer, cursor, config_state->vim_mode,
                                     &config_state->yank_head, &final_mode, &config_state->visual_start,
                                     &find_state);
               } break;
               case ',':
               {
                    VimAction_t action = {};
                    FindState_t find_state; // dummy find state so our's doesn't get modified
                    VimMode_t final_mode;
                    action.multiplier = 1;
                    action.change.type = VCT_MOTION;
                    action.motion.multiplier = 1;
                    action.motion.match_char = config_state->find_state.ch;
                    switch(config_state->find_state.motion_type){
                    default:
                         break;
                    case VMT_FIND_NEXT_MATCHING_CHAR:
                         action.motion.type = VMT_FIND_PREV_MATCHING_CHAR;
                         break;
                    case VMT_FIND_PREV_MATCHING_CHAR:
                         action.motion.type = VMT_FIND_NEXT_MATCHING_CHAR;
                         break;
                    case VMT_TO_NEXT_MATCHING_CHAR:
                         action.motion.type = VMT_TO_PREV_MATCHING_CHAR;
                         break;
                    case VMT_TO_PREV_MATCHING_CHAR:
                         action.motion.type = VMT_TO_NEXT_MATCHING_CHAR;
                         break;
                    }
                    vim_action_apply(&action, buffer, cursor, config_state->vim_mode,
                                     &config_state->yank_head, &final_mode, &config_state->visual_start,
                                     &find_state);
               } break;
               case 'H':
               {
                    // move cursor to top line of view
                    Point_t location = {cursor->x, buffer_view->top_row};
                    config_set_cursor(config_state, buffer, cursor, location);
               } break;
               case 'M':
               {
                    // move cursor to middle line of view
                    int64_t view_height = buffer_view->bottom_right.y - buffer_view->top_left.y;
                    Point_t location = {cursor->x, buffer_view->top_row + (view_height/2)};
                    config_set_cursor(config_state, buffer, cursor, location);
               } break;
               case 'L':
               {
                    // move cursor to bottom line of view
                    int64_t view_height = buffer_view->bottom_right.y - buffer_view->top_left.y;
                    Point_t location = {cursor->x, buffer_view->top_row + view_height};
                    config_set_cursor(config_state, buffer, cursor, location);
               } break;
               case 'z':
               break;
               case '%':
               {
                    config_move_cursor_to_matching_pair(config_state, buffer, cursor);
               } break;
               case KEY_NPAGE:
               {
                    half_page_up(config_state->tab_current->view_current);
               } break;
               case KEY_PPAGE:
               {
                    half_page_down(config_state->tab_current->view_current);
               } break;
               case 25: // Ctrl + y
                    confirm_action(config_state, head);
                    break;
               case 8: // Ctrl + h
               {
                   // TODO: consolidate into function for use with other window movement keys, and for use in insert mode?
                   Point_t point = {config_state->tab_current->view_current->top_left.x - 2, // account for window separator
                                    cursor->y - config_state->tab_current->view_current->top_row + config_state->tab_current->view_current->top_left.y};
                   switch_to_view_at_point(config_state, point);
               } break;
               case 10: // Ctrl + j
               {
                    Point_t point = {cursor->x - config_state->tab_current->view_current->left_column + config_state->tab_current->view_current->top_left.x,
                                     config_state->tab_current->view_current->bottom_right.y + 2}; // account for window separator
                    switch_to_view_at_point(config_state, point);
               } break;
               case 11: // Ctrl + k
               {
                    Point_t point = {cursor->x - config_state->tab_current->view_current->left_column + config_state->tab_current->view_current->top_left.x,
                                     config_state->tab_current->view_current->top_left.y - 2};
                    switch_to_view_at_point(config_state, point);
               } break;
               case 12: // Ctrl + l
               {
                    Point_t point = {config_state->tab_current->view_current->bottom_right.x + 2, // account for window separator
                                     cursor->y - config_state->tab_current->view_current->top_row + config_state->tab_current->view_current->top_left.y};
                    switch_to_view_at_point(config_state, point);
               } break;
               case ':':
               {
                    input_start(config_state, "Goto Line", key);
               }
               break;
               case '#':
               {
                    if(!buffer->lines || !buffer->lines[cursor->y]) break;

                    Point_t word_start, word_end;
                    if(!ce_get_word_at_location(buffer, *cursor, &word_start, &word_end)) break;
                    char* search_str = ce_dupe_string(buffer, word_start, word_end);
                    add_yank(&config_state->yank_head, '/', search_str, YANK_NORMAL);
                    config_state->search_command.direction = CE_UP;
                    goto search;
               } break;
               case '*':
               {
                    if(!buffer->lines || !buffer->lines[cursor->y]) break;

                    Point_t word_start, word_end;
                    if(!ce_get_word_at_location(buffer, *cursor, &word_start, &word_end)) break;
                    char* search_str = ce_dupe_string(buffer, word_start, word_end);
                    add_yank(&config_state->yank_head, '/', search_str, YANK_NORMAL);
                    config_state->search_command.direction = CE_DOWN;
                    goto search;
               } break;
               case '/':
               {
                    input_start(config_state, "Search", key);
                    config_state->search_command.direction = CE_DOWN;
                    config_state->start_search = *cursor;
                    break;
               }
               case '?':
               {
                    input_start(config_state, "Reverse Search", key);
                    config_state->search_command.direction = CE_UP;
                    config_state->start_search = *cursor;
                    break;
               }
               case 'n':
     search:
               {
                    YankNode_t* yank = find_yank(config_state->yank_head, '/');
                    if(yank){
                         assert(yank->mode == YANK_NORMAL);
                         Point_t match;
                         if(ce_find_string(buffer, *cursor, yank->text, &match, config_state->search_command.direction)){
                              config_set_cursor(config_state, buffer, cursor, match);
                              center_view(config_state->tab_current->view_current);
                         }
                    }
               } break;
               case 'N':
               {
                    YankNode_t* yank = find_yank(config_state->yank_head, '/');
                    if(yank){
                         assert(yank->mode == YANK_NORMAL);
                         Point_t match;
                         if(ce_find_string(buffer, *cursor, yank->text, &match, ce_reverse_direction(config_state->search_command.direction))){
                              config_set_cursor(config_state, buffer, cursor, match);
                              center_view(config_state->tab_current->view_current);
                         }
                    }
               } break;
               break;
               case '=':
               {
#if 0
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
                              config_remove_line(g_config_state, buffer, i);
                         }

                         for(int64_t i = 0; ; i++){
                              if(fgets(formatted_line_buf, BUFSIZ, child_stdout) == NULL) break;
                              size_t new_line_len = strlen(formatted_line_buf) - 1;
                              assert(formatted_line_buf[new_line_len] == '\n');
                              formatted_line_buf[new_line_len] = 0;
                              config_insert_line(g_config_state, buffer, i, formatted_line_buf);
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
                         ce_set_cursor(buffer, cursor, (Point_t){0, 0}, MF_DEFAULT);
                         if(!ce_advance_cursor(buffer, cursor, cursor_position-1))
                              ce_message("failed to advance cursor");

#if 0
                         // TODO: use -output-replacements-xml to support undo
                         char* formatted_line = strdup(formatted_line_buf);
                         // save the current line in undo history
                         Point_t delete_begin = {0, cursor->y};
                         char* save_string = ce_dupe_line(buffer, cursor->y);
                         if(!config_remove_line(g_config_state, buffer, cursor->y)){
                              ce_message("ce_remove_string failed");
                              return true;
                         }
                         config_insert_string(config_state, buffer, &delete_begin, formatted_line);
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
#endif
               } break;
               case 24: // Ctrl + x
               {
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
                    if(config_state->vim_mode == VM_VISUAL_RANGE || config_state->vim_mode == VM_VISUAL_LINE){
                         input_start(config_state, "Visual Replace", key);
                    }else{
                         input_start(config_state, "Replace", key);
                    }
               break;
               case 5: // Ctrl + e
               {
                    Buffer_t* new_buffer = new_buffer_from_string(head, "unnamed", NULL);
                    ce_alloc_lines(new_buffer, 1);
                    config_state->tab_current->view_current->buffer = new_buffer;
                    *cursor = (Point_t){0, 0};
               } break;
               case 1: // Ctrl + a
                    input_start(config_state, "Save Buffer As", key);
               break;
               case 9: // Ctrl + i
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
                    ce_clamp_cursor(buffer, &buffer_view->cursor, MF_DEFAULT);
               } break;
               }
          }

          if(clear_command){
               config_state->command_len = 0;
          }
     }

     // incremental search
     if(config_state->input && (config_state->input_key == '/' || config_state->input_key == '?')){
          if(config_state->view_input->buffer->lines == NULL){
               config_set_cursor(config_state,
                                 config_state->tab_current->view_input_save->buffer,
                                 &config_state->tab_current->view_input_save->cursor,
                                 config_state->start_search);
          }else{
               const char* search_str = config_state->view_input->buffer->lines[0];
               Point_t match = {};
               if(search_str[0] &&
                  ce_find_string(config_state->tab_current->view_input_save->buffer,
                                 config_state->start_search, search_str, &match,
                                 config_state->search_command.direction)){
                    config_set_cursor(config_state,
                                      config_state->tab_current->view_input_save->buffer,
                                      &config_state->tab_current->view_input_save->cursor, match);
                    center_view(config_state->tab_current->view_input_save);
               }else{
                    config_set_cursor(config_state,
                                      config_state->tab_current->view_input_save->buffer,
                                      &config_state->tab_current->view_input_save->cursor,
                                      config_state->start_search);
               }
          }
     }

     config_state->last_key = key;
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
     ce_calc_views(config_state->tab_current->view_head, top_left, bottom_right);

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
          ce_calc_views(config_state->view_input, input_top_left, input_bottom_right);
          config_state->tab_current->view_input_save->bottom_right.y = input_top_left.y - 1;
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
          YankNode_t* yank = find_yank(config_state->yank_head, '/');
          if(yank) search = yank->text;
     }

     // NOTE: always draw from the head
     ce_draw_views(config_state->tab_current->view_head, search);

     draw_view_statuses(config_state->tab_current->view_head, config_state->tab_current->view_current,
                        config_state->vim_mode, config_state->last_key);

     // draw input status
     if(config_state->input){
          if(config_state->view_input == config_state->tab_current->view_current){
               move(input_top_left.y - 1, input_top_left.x);

               attron(COLOR_PAIR(S_BORDERS));
               for(int i = input_top_left.x; i < input_bottom_right.x; ++i) addch(ACS_HLINE);
               // if we are at the edge of the terminal, draw the inclusing horizontal line. We
               if(input_bottom_right.x == g_terminal_dimensions->x - 1) addch(ACS_HLINE);

               attron(COLOR_PAIR(S_INPUT_STATUS));
               mvprintw(input_top_left.y - 1, input_top_left.x + 1, " %s ", config_state->input_message);
          }

          standend();
          // clear input buffer section
          for(int y = input_top_left.y; y <= input_bottom_right.y; ++y){
               move(y, input_top_left.x);
               for(int x = input_top_left.x; x <= input_bottom_right.x; ++x){
                    addch(' ');
               }
          }

          ce_draw_views(config_state->view_input, NULL);
          draw_view_statuses(config_state->view_input, config_state->tab_current->view_current,
                             config_state->vim_mode, config_state->last_key);
     }

     // draw auto complete
     // TODO: don't draw over borders!
     Point_t terminal_cursor = get_cursor_on_terminal(cursor, buffer_view);
     if(auto_completing(&config_state->auto_complete)){
          move(terminal_cursor.y, terminal_cursor.x);
          int64_t offset = cursor->x - config_state->auto_complete.start.x;
          if(offset < 0) offset = 0;
          const char* option = config_state->auto_complete.current->option + offset;
          attron(COLOR_PAIR(S_AUTO_COMPLETE));
          while(*option){
               addch(*option);
               option++;
          }
     }

     // draw tab line
     if(config_state->tab_head->next){
          // clear tab line with inverses
          move(0, 0);
          attron(COLOR_PAIR(S_BORDERS));
          for(int i = 0; i < g_terminal_dimensions->x; ++i) addch(ACS_HLINE);
          for(int i = 0; i < g_terminal_dimensions->x; ++i){
               Point_t p = {i, 0};
               ce_connect_border_lines(p);
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

     // draw remote cursors
     CursorNode_t* itr = config_state->client_state.cursor_list_head;
     while(itr){
          Buffer_t* cursor_buf = id_to_buffer(head, itr->network_id);
          if(cursor_buf){
               BufferView_t* cursor_view = ce_buffer_in_view(config_state->tab_current->view_head, cursor_buf);
               if(cursor_view){
                    Point_t draw_loc = get_cursor_on_terminal(&itr->cursor, cursor_view);
                    if(ce_point_after(*g_terminal_dimensions, draw_loc)
                       && ce_point_after(draw_loc, (Point_t){0, 0})){
                         chtype cur_char = mvinch(draw_loc.y, draw_loc.x);
                         cur_char |= A_REVERSE;
                         addch(cur_char);
                    }
#if 0
                    short color_pair = (short)(cur_type & A_COLOR);
                    short fg_color, bg_color;
                    pair_content(color_pair, &fg_color, &bg_color);
#endif
               }
          }

          itr = itr->next;
     }

     // reset the cursor
     move(terminal_cursor.y, terminal_cursor.x);

     // update the screen with what we drew
     doupdate();

     pthread_mutex_unlock(&shell_buffer_lock);
     pthread_mutex_unlock(&draw_lock);
}
