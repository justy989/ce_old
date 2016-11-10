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
#include <dirent.h>
#include <sys/ioctl.h>

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
pthread_mutex_t view_input_save_lock;

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

typedef struct KeyNode_t{
     int key;
     struct KeyNode_t* next;
} KeyNode_t;

KeyNode_t* keys_push(KeyNode_t** head, int key)
{
     KeyNode_t* new_node = malloc(sizeof(*new_node));
     if(!new_node){
          ce_message("%s() failed to malloc node", __FUNCTION__);
          return NULL;
     }

     KeyNode_t* tail = *head;

     if(tail){
          while(tail->next){
               tail = tail->next;
          }
     }

     new_node->key = key;
     new_node->next = NULL;

     if(tail){
          tail->next = new_node;
     }else{
          *head = new_node;
     }

     return new_node;
}

// string is allocated and returned, it is the user's responsibility to free it
int* keys_get_string(KeyNode_t* head)
{
     int64_t len = 0;
     KeyNode_t* itr = head;
     while(itr){
          len++;
          itr = itr->next;
     }

     int* str = malloc((len + 1) * sizeof(*str));
     if(!str){
          ce_message("%s() failed to alloc string", __FUNCTION__);
          return NULL;
     }

     int64_t s = 0;
     itr = head;
     while(itr){
          str[s] = itr->key;
          s++;
          itr = itr->next;
     }

     str[len] = 0;
     return str;
}

void keys_free(KeyNode_t** head)
{
     while(*head){
          KeyNode_t* tmp = *head;
          *head = (*head)->next;
          free(tmp);
     }
}

char* command_string_to_char_string(const int* int_str)
{
     // build length
     size_t len = 1; // account for NULL terminator
     const int* int_itr = int_str;
     while(*int_itr){
          if(isprint(*int_itr)){
               len++;
          }else{
               switch(*int_itr){
               default:
                    len++; // going to fill in with '~' for now
                    break;
               case KEY_BACKSPACE:
               case KEY_ESCAPE:
               case KEY_ENTER:
               case KEY_TAB:
                    len += 2;
                    break;
               }
          }

          int_itr++;
     }

     char* char_str = malloc(len);
     if(!char_str) return NULL;

     char* char_itr = char_str;
     int_itr = int_str;
     while(*int_itr){
          if(isprint(*int_itr)){
               *char_itr = *int_itr;
               char_itr++;
          }else{
               switch(*int_itr){
               default:
                    *char_itr = '~';
                    char_itr++;
                    break;
               case KEY_BACKSPACE:
                    *char_itr = '\\'; char_itr++;
                    *char_itr = 'b'; char_itr++;
                    break;
               case KEY_ESCAPE:
                    *char_itr = '\\'; char_itr++;
                    *char_itr = 'e'; char_itr++;
                    break;
               case KEY_ENTER:
                    *char_itr = '\\'; char_itr++;
                    *char_itr = 'r'; char_itr++;
                    break;
               case KEY_TAB:
                    *char_itr = '\\'; char_itr++;
                    *char_itr = 't'; char_itr++;
                    break;
               case '\\':
                    *char_itr = '\\'; char_itr++;
                    *char_itr = '\\'; char_itr++;
                    break;
               }
          }

          int_itr++;
     }

     char_str[len - 1] = 0;

     return char_str;
}

int* char_string_to_command_string(const char* char_str)
{
     // we can just use the strlen, and it'll be over allocated because the command string will always be
     // the same size or small than the char string
     size_t str_len = strlen(char_str);

     int* int_str = malloc((str_len + 1) * sizeof(*int_str));
     if(!int_str) return NULL;

     int* int_itr = int_str;
     const char* char_itr = char_str;
     while(*char_itr){
          if(!isprint(*char_itr)){
               free(int_str);
               return NULL;
          }

          if(*char_itr == '\\'){
               char_itr++;
               switch(*char_itr){
               default:
                    free(int_str);
                    return NULL;
               case 'b':
                    *int_itr = KEY_BACKSPACE;
                    break;
               case 'e':
                    *int_itr = KEY_ESCAPE;
                    break;
               case 'r':
                    *int_itr = KEY_ENTER;
                    break;
               case 't':
                    *int_itr = KEY_TAB;
                    break;
               case '\\':
                    *int_itr = '\\';
                    break;
               }
               char_itr++;
               int_itr++;
          }else{
               *int_itr = *char_itr;
               char_itr++;
               int_itr++;
          }
     }

     *int_itr = 0; // NULL terminate

     return int_str;
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

int isnotsinglequote(int c)
{
     return c != '\'';
}

typedef struct{
     BufferCommitNode_t* commit_tail;
     int64_t cursor_save_column;
     struct MarkNode_t* mark_head;
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
     }else{
          YankNode_t* new_yank = malloc(sizeof(*new_yank));
          new_yank->reg_char = reg_char;
          new_yank->next = *head;
          node = new_yank;
          *head = new_yank;
     }

     node->text = yank_text;
     node->mode = mode;
}

void free_yanks(YankNode_t** head)
{
     YankNode_t* itr = *head;
     while(itr){
          YankNode_t* tmp = itr;
          itr = itr->next;

          free((void*)(tmp->text));
          free(tmp);
     }

     *head = NULL;
}

typedef struct MacroNode_t{
     char reg;
     int* command;
     struct MacroNode_t* next;
} MacroNode_t;

MacroNode_t* macro_find(MacroNode_t* head, char reg)
{
     MacroNode_t* itr = head;

     while(itr != NULL){
          if(itr->reg == reg) return itr;
          itr = itr->next;
     }

     return NULL;
}

// for now the yanked string is user allocated. eventually will probably
// want to change this interface so that everything is hidden
void macro_add(MacroNode_t** head, char reg, int* command)
{
     MacroNode_t* node = macro_find(*head, reg);

     if(node != NULL){
          free((void*)node->command);
     }else{
          MacroNode_t* new_yank = malloc(sizeof(*new_yank));
          new_yank->reg = reg;
          new_yank->next = *head;
          node = new_yank;
          *head = new_yank;
     }

     node->command = command;
}

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

#define VIM_COMMENT_STRING "//"

typedef enum{
     VM_NORMAL,
     VM_INSERT,
     VM_VISUAL_RANGE,
     VM_VISUAL_LINE,
     VM_VISUAL_BLOCK,
} VimMode_t;

typedef enum{
     VCT_NONE,
     VCT_MOTION,
     VCT_INSERT,
     VCT_DELETE,
     VCT_CHANGE_CHAR,
     VCT_PASTE_BEFORE,
     VCT_PASTE_AFTER,
     VCT_YANK,
     VCT_INDENT,
     VCT_UNINDENT,
     VCT_COMMENT,
     VCT_UNCOMMENT,
     VCT_FLIP_CASE,
     VCT_JOIN_LINE,
     VCT_OPEN_ABOVE, // NOTE: using the vim cheat sheet terminalogy for 'O' and 'o'
     VCT_OPEN_BELOW,
     VCT_RECORD_MACRO,
     VCT_PLAY_MACRO,
} VimChangeType_t;

typedef struct{
     VimChangeType_t type;
     union{
          char* insert_string;
          char reg;
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
     VMT_VISUAL_SWAP_WITH_CURSOR,
     VMT_MATCHING_PAIR,
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

typedef enum{
     VKH_UNHANDLED_KEY,
     VKH_HANDLED_KEY,
     VKH_COMPLETED_ACTION,
} VimKeyHandlerResultType_t;

typedef struct{
     VimKeyHandlerResultType_t type;
     VimAction_t completed_action;
} VimKeyHandlerResult_t;

typedef struct{
     VimMode_t mode;

     KeyNode_t* command_head;

     VimAction_t last_action;
     int* last_insert_command;

     char recording_macro; // holds the register we are recording, is 0 if we aren't recording
     char playing_macro;
     KeyNode_t* record_macro_head;
     MacroNode_t* macro_head;

     YankNode_t* yank_head;

     FindState_t find_state;

     Point_t visual_start;

     Direction_t search_direction;
     Point_t start_search;
} VimState_t;

VimKeyHandlerResult_t vim_key_handler(int key, VimState_t* vim_state, BufferView_t* buffer_view,
                                      AutoComplete_t* auto_complete, bool repeating);

int itoi(int* str)
{
     int value = 0;

     while(*str){
          if(*str < 0 || *str > 255 || !isdigit(*str)) return 0;
          value = value * 10 + (*str - '0');
          str++;
     }

     return value;
}

VimCommandState_t vim_action_from_string(const int* string, VimAction_t* action, VimMode_t vim_mode,
                                         Buffer_t* buffer, Point_t* cursor, Point_t* visual_start,
                                         FindState_t* find_state, bool recording_macro)
{
     int tmp[BUFSIZ];
     bool visual_mode = false;
     bool get_motion = true;
     VimAction_t built_action = {};

     built_action.multiplier = 1;
     built_action.motion.multiplier = 1;

     // get multiplier if there is one
     const int* itr = string;
     while(*itr && *itr >= 0 && *itr <= 255 && isdigit(*itr)) itr++;
     if(itr != string){
          int64_t len = itr - string;
          memcpy(tmp, string, len * sizeof(*tmp));
          tmp[len + 1] = 0;
          built_action.multiplier = itoi(tmp);

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
          built_action.motion.visual_start_after = ce_point_after(*visual_start, *cursor);
     }

     int change_char = *itr;

     // check for yank registers
     if(*itr == '"'){
          itr++;

          if(*itr == '\0'){
               return VCS_CONTINUE;
          } else if(!isprint(*itr) || *itr == '?'){
               return VCS_INVALID;
          }

          built_action.change.reg = *itr;
          itr++;
     }

     // get the change
     change_char = *itr;
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
          if(!isprint(built_action.change.change_char)){
               return VCS_INVALID;
          }
          get_motion = false;
          break;
     case 'g':
     {
          built_action.end_in_vim_mode = vim_mode;
          char next_ch = *(itr + 1);
          if(next_ch == 'c'){
               built_action.change.type = VCT_COMMENT;
          }else if(next_ch == 'u'){
               built_action.change.type = VCT_UNCOMMENT;
          }else if(next_ch == 0){
               return VCS_CONTINUE;
          }else{
               built_action.change.type = VCT_MOTION;
               if(visual_mode) get_motion = true;
          }
     } break;
     case 'p':
          built_action.change.type = VCT_PASTE_AFTER;
          get_motion = false;
          break;
     case 'P':
          built_action.change.type = VCT_PASTE_BEFORE;
          get_motion = false;
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
     case '~':
          built_action.change.type = VCT_FLIP_CASE;
          break;
     case ';':
          built_action.change.type = VCT_MOTION;
          built_action.motion.type = find_state->motion_type;
          built_action.motion.match_char = find_state->ch;
          get_motion = false;
          break;
     case ',':
          built_action.change.type = VCT_MOTION;
          built_action.motion.match_char = find_state->ch;

          // reverse the motion
          switch(find_state->motion_type){
          default:
               break;
          case VMT_FIND_NEXT_MATCHING_CHAR:
               built_action.motion.type = VMT_FIND_PREV_MATCHING_CHAR;
               break;
          case VMT_FIND_PREV_MATCHING_CHAR:
               built_action.motion.type = VMT_FIND_NEXT_MATCHING_CHAR;
               break;
          case VMT_TO_NEXT_MATCHING_CHAR:
               built_action.motion.type = VMT_TO_PREV_MATCHING_CHAR;
               break;
          case VMT_TO_PREV_MATCHING_CHAR:
               built_action.motion.type = VMT_TO_NEXT_MATCHING_CHAR;
               break;
          }
          get_motion = false;
          break;
     case 'J':
          built_action.motion.type = VMT_END_OF_LINE_HARD;
          built_action.change.type = VCT_JOIN_LINE;
          get_motion = false;
          break;
     case 'O':
          if(visual_mode){
               built_action.motion.type = VMT_VISUAL_SWAP_WITH_CURSOR;
               built_action.end_in_vim_mode = vim_mode;
          }else{
               built_action.change.type = VCT_OPEN_ABOVE;
               built_action.end_in_vim_mode = VM_INSERT;
          }
          get_motion = false;
          break;
     case 'o':
          if(visual_mode){
               built_action.motion.type = VMT_VISUAL_SWAP_WITH_CURSOR;
               built_action.end_in_vim_mode = vim_mode;
          }else{
               built_action.change.type = VCT_OPEN_BELOW;
               built_action.end_in_vim_mode = VM_INSERT;
          }
          get_motion = false;
          break;
     case 'q':
          built_action.change.type = VCT_RECORD_MACRO;
          if(!recording_macro){
               built_action.change.reg = *(++itr);
               if(!built_action.change.reg){
                    return VCS_CONTINUE;
               }
               if(!isprint(built_action.change.reg)){
                    return VCS_INVALID;
               }
          }
          get_motion = false;
          break;
     case '@':
          built_action.change.type = VCT_PLAY_MACRO;
          built_action.change.reg = *(++itr);
          if(!built_action.change.reg){
               return VCS_CONTINUE;
          }
          if(!isprint(built_action.change.reg)){
               return VCS_INVALID;
          }
          get_motion = false;
          break;
     case '%':
          built_action.change.type = VCT_MOTION;
          built_action.motion.type = VMT_MATCHING_PAIR;
          get_motion = false;
          break;
     }

     if(get_motion){
          // get the motion multiplier
          itr++;
          const int* start_itr = itr;
          while(*itr && isdigit(*itr)) itr++;
          if(itr != start_itr){
               int64_t len = itr - start_itr;
               memcpy(tmp, start_itr, len * sizeof(*tmp));
               tmp[len + 1] = 0;
               built_action.motion.multiplier = itoi(tmp);

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
               if(built_action.change.type == VCT_MOTION){
                    built_action.motion.type = VMT_DOWN;
               }else{
                    built_action.motion.type = VMT_LINE_DOWN;
               }
               break;
          case 'k':
               if(built_action.change.type == VCT_MOTION){
                    built_action.motion.type = VMT_UP;
               }else{
                    built_action.motion.type = VMT_LINE_UP;
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
               built_action.motion.type = VMT_WORD_BEGINNING_LITTLE;
               break;
          case 'B':
               built_action.motion.type = VMT_WORD_BEGINNING_BIG;
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
               case '\'':
               case ')':
               case '(':
               case '}':
               case '{':
               case '[':
               case ']':
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
               case '\'':
               case ')':
               case '(':
               case '}':
               case '{':
               case '[':
               case ']':
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
          case 'u':
               if(change_char == 'g') {
                    built_action.motion.type = VMT_LINE;
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
               }else if(change_char == 'g') {
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
     ce_insert_string(buffer, loc, TAB_STRING);
     ce_commit_insert_string(commit_tail, loc, *cursor, *cursor, strdup(TAB_STRING), BCC_KEEP_GOING);
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
          ce_remove_string(buffer, loc, whitespace_count);
          ce_commit_remove_string(commit_tail, loc, *cursor, *cursor, strdup(TAB_STRING), BCC_KEEP_GOING);
     }
}

typedef struct {
     Point_t start;
     Point_t end;
     const Point_t* sorted_start;
     const Point_t* sorted_end;
     YankMode_t yank_mode;
} VimActionRange_t;

bool vim_action_get_range(VimAction_t* action, Buffer_t* buffer, Point_t* cursor, FindState_t* find_state,
                          Point_t* visual_start, VimActionRange_t* action_range)
{
     action_range->start = *cursor;
     action_range->end = action_range->start;

     action_range->yank_mode = YANK_NORMAL;

     // setup the start and end if we are in visual mode
     if(action->motion.type == VMT_VISUAL_RANGE){
          Point_t calc_visual_start = *cursor;
          int64_t visual_length = action->motion.visual_length;
          if(!action->motion.visual_start_after) visual_length = -visual_length;
          ce_advance_cursor(buffer, &calc_visual_start, visual_length);
          action_range->start = *cursor;
          action_range->end = calc_visual_start;
     }else if(action->motion.type == VMT_VISUAL_LINE){
          Point_t calc_visual_start = *cursor;

          calc_visual_start.y += action->motion.visual_lines;

          // in visual line mode, we need to figure out which points are first/last so that we can set the
          // start/end 'x's accordingly, to the beginning and end of line
          const Point_t* a = cursor;
          const Point_t* b = &calc_visual_start;

          ce_sort_points(&a, &b);

          action_range->start = *a;
          action_range->end = *b;
          action_range->start.x = 0;
          action_range->end.x = strlen(buffer->lines[action_range->end.y]);
          action_range->yank_mode = YANK_LINE;
     }else if(buffer->line_count){ // can't do motions without a buffer !
          int64_t multiplier = action->multiplier * action->motion.multiplier;

          for(int64_t i = 0; i < multiplier; ++i){
               // get range based on motion
               switch(action->motion.type){
               default:
               case VMT_NONE:
                    break;
               case VMT_LEFT:
                    action_range->end.x--;
                    if(action_range->end.x < 0) action_range->end.x = 0;
                    break;
               case VMT_RIGHT:
               {
                    int64_t line_len = strlen(buffer->lines[action_range->end.y]);
                    action_range->end.x++;
                    if(action_range->end.x > line_len) action_range->end.x = line_len;
               } break;
               case VMT_UP:
                    action_range->end.y--;
                    if(action_range->end.y < 0) action_range->end.y = 0;
                    break;
               case VMT_DOWN:
                    action_range->end.y++;
                    if(action_range->end.y >= buffer->line_count) action_range->end.y = buffer->line_count - 1;
                    if(action_range->end.y < 0) action_range->end.y = 0;
                    break;
               case VMT_WORD_LITTLE:
                    ce_move_cursor_to_next_word(buffer, &action_range->end, true);
                    break;
               case VMT_WORD_BIG:
                    ce_move_cursor_to_next_word(buffer, &action_range->end, false);
                    break;
               case VMT_WORD_BEGINNING_LITTLE:
                    ce_move_cursor_to_beginning_of_word(buffer, &action_range->end, true);
                    break;
               case VMT_WORD_BEGINNING_BIG:
                    ce_move_cursor_to_beginning_of_word(buffer, &action_range->end, false);
                    break;
               case VMT_WORD_END_LITTLE:
                    ce_move_cursor_to_end_of_word(buffer, &action_range->end, true);
                    break;
               case VMT_WORD_END_BIG:
                    ce_move_cursor_to_end_of_word(buffer, &action_range->end, false);
                    break;
               case VMT_LINE:
                    action_range->start.x = 0;
                    action_range->end.x = strlen(buffer->lines[action_range->end.y]);
                    action_range->yank_mode = YANK_LINE;
                    break;
               case VMT_LINE_UP:
                    action_range->start.x = 0;
                    action_range->start.y--;
                    if(action_range->start.y < 0) action_range->start.y = 0;
                    action_range->end.x = strlen(buffer->lines[action_range->end.y]);
                    action_range->yank_mode = YANK_LINE;
                    break;
               case VMT_LINE_DOWN:
                    action_range->start.x = 0;
                    action_range->end.y++;
                    if(action_range->end.y >= buffer->line_count) action_range->end.y = buffer->line_count - 1;
                    if(action_range->end.y < 0) action_range->end.y = 0;
                    action_range->end.x = strlen(buffer->lines[action_range->end.y]);
                    action_range->yank_mode = YANK_LINE;
                    break;
               case VMT_FIND_NEXT_MATCHING_CHAR:
                    if(ce_move_cursor_forward_to_char(buffer, &action_range->end, action->motion.match_char)){
                         find_state->motion_type = action->motion.type;
                         find_state->ch = action->motion.match_char;
                    }
                    break;
               case VMT_FIND_PREV_MATCHING_CHAR:
                    if(ce_move_cursor_backward_to_char(buffer, &action_range->end, action->motion.match_char)){
                         find_state->motion_type = action->motion.type;
                         find_state->ch = action->motion.match_char;
                    }
                    break;
               case VMT_TO_NEXT_MATCHING_CHAR:
                    action_range->end.x++;
                    if(ce_move_cursor_forward_to_char(buffer, &action_range->end, action->motion.match_char)){
                         find_state->motion_type = action->motion.type;
                         find_state->ch = action->motion.match_char;
                         action_range->end.x--;
                         if(action_range->end.x < 0) action_range->end.x = 0;
                    }else{
                         action_range->end.x--;
                    }
                    break;
               case VMT_TO_PREV_MATCHING_CHAR:
               {
                    action_range->end.x--;
                    if(ce_move_cursor_backward_to_char(buffer, &action_range->end, action->motion.match_char)){
                         find_state->motion_type = action->motion.type;
                         find_state->ch = action->motion.match_char;
                         action_range->end.x++;
                         int64_t line_len = strlen(buffer->lines[action_range->end.y]);
                         if(action_range->end.x > line_len) action_range->end.x = line_len;
                    }else{
                         action_range->end.x++;
                    }
               }break;
               case VMT_BEGINNING_OF_FILE:
                    ce_move_cursor_to_beginning_of_file(buffer, &action_range->end);
                    break;
               case VMT_BEGINNING_OF_LINE_HARD:
                    ce_move_cursor_to_beginning_of_line(buffer, &action_range->end);
                    break;
               case VMT_BEGINNING_OF_LINE_SOFT:
                    ce_move_cursor_to_soft_beginning_of_line(buffer, &action_range->end);
                    break;
               case VMT_END_OF_LINE_PASSED:
                    ce_move_cursor_to_end_of_line(buffer, &action_range->end);
                    action_range->end.x++;
                    break;
               case VMT_END_OF_LINE_HARD:
                    ce_move_cursor_to_end_of_line(buffer, &action_range->end);
                    break;
               case VMT_END_OF_LINE_SOFT:
                    ce_move_cursor_to_soft_end_of_line(buffer, &action_range->end);
                    break;
               case VMT_END_OF_FILE:
                    ce_move_cursor_to_end_of_file(buffer, &action_range->end);
                    break;
               case VMT_INSIDE_PAIR:
                    switch(action->motion.inside_pair){
                    case '"':
                         if(!ce_get_homogenous_adjacents(buffer, &action_range->start, &action_range->end, isnotquote)) return false;
                         if(action_range->start.x == 0) return false;
                         break;
                    case '\'':
                         if(!ce_get_homogenous_adjacents(buffer, &action_range->start, &action_range->end, isnotsinglequote)) return false;
                         if(action_range->start.x == 0) return false;
                         break;
                    case '(':
                    case ')':
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->start, ')')) return false;
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->end, '(')) return false;
                         ce_advance_cursor(buffer, &action_range->start, 1);
                         ce_advance_cursor(buffer, &action_range->end, -1);
                         break;
                    case '{':
                    case '}':
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->start, '}')) return false;
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->end, '{')) return false;
                         ce_advance_cursor(buffer, &action_range->start, 1);
                         ce_advance_cursor(buffer, &action_range->end, -1);
                         break;
                    case '[':
                    case ']':
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->start, ']')) return false;
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->end, '[')) return false;
                         ce_advance_cursor(buffer, &action_range->start, 1);
                         ce_advance_cursor(buffer, &action_range->end, -1);
                         break;
                    default:
                         return false;
                    }

                    if(action_range->start.x == action_range->end.x && action_range->start.y == action_range->end.y) return false;
                    break;
               case VMT_INSIDE_WORD_LITTLE:
                    ce_get_word_at_location(buffer, *cursor, &action_range->start, &action_range->end);
                    break;
               case VMT_INSIDE_WORD_BIG:
               {
                    char curr_char;
                    if(!ce_get_char(buffer, action_range->start, &curr_char)) return false;

                    if(isblank(curr_char)){
                         ce_get_homogenous_adjacents(buffer, &action_range->start, &action_range->end, isblank);
                    }else{
                         assert(ispunct_or_iswordchar(curr_char));
                         ce_get_homogenous_adjacents(buffer, &action_range->start, &action_range->end, ispunct_or_iswordchar);
                    }
               } break;
               case VMT_AROUND_PAIR:
                    switch(action->motion.inside_pair){
                    case '"':
                         if(!ce_get_homogenous_adjacents(buffer, &action_range->start, &action_range->end, isnotquote)) return false;
                         ce_advance_cursor(buffer, &action_range->start, -1);
                         ce_advance_cursor(buffer, &action_range->end, 1);
                         break;
                    case '\'':
                         if(!ce_get_homogenous_adjacents(buffer, &action_range->start, &action_range->end, isnotsinglequote)) return false;
                         ce_advance_cursor(buffer, &action_range->start, 1);
                         ce_advance_cursor(buffer, &action_range->end, -1);
                         break;
                    case '(':
                    case ')':
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->start, ')')) return false;
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->end, '(')) return false;
                         break;
                    case '{':
                    case '}':
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->start, '}')) return false;
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->end, '{')) return false;
                         break;
                    case '[':
                    case ']':
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->start, ']')) return false;
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->end, '[')) return false;
                         break;
                    default:
                         return false;
                    }

                    if(action_range->start.x == action_range->end.x && action_range->start.y == action_range->end.y) return false;
                    break;
                    // TIME TO SLURP
#define SLURP_RIGHT(condition)                                                              \
                    do{ action_range->end.x++; if(!ce_get_char(buffer, action_range->end, &c)) break; }while(condition(c)); \
                    action_range->end.x--;

#define SLURP_LEFT(condition)                                                                   \
                    do{ action_range->start.x--; if(!ce_get_char(buffer, action_range->start, &c)) break; }while(condition(c)); \
                    action_range->start.x++;

               case VMT_AROUND_WORD_LITTLE:
               {
                    char c;
                    if(!ce_get_char(buffer, action_range->start, &c)) return false;

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
                    if(!ce_get_char(buffer, action_range->start, &c)) return false;

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
               case VMT_VISUAL_SWAP_WITH_CURSOR:
               {
                    Point_t tmp = *cursor;
                    *cursor = *visual_start;
                    *visual_start = tmp;
               } break;
               case VMT_MATCHING_PAIR:
               {
                    char matchee;
                    if(!ce_get_char(buffer, action_range->end, &matchee)) break;
                    ce_move_cursor_to_matching_pair(buffer, &action_range->end, matchee);
               } break;
               }
          }
     }

     // when we are not executing a motion delete up to the next word
     if(action->change.type != VCT_MOTION){
          if(action->motion.type == VMT_WORD_LITTLE || action->motion.type == VMT_WORD_BIG){
               action_range->end.x--;
               if(action_range->end.x < 0) action_range->end.x = 0;
          }else if(action->motion.type == VMT_WORD_BEGINNING_LITTLE || action->motion.type == VMT_WORD_BEGINNING_BIG){
               action_range->start.x--;
               if(action_range->start.x < 0) action_range->start.x = 0;
          }else if(action->motion.type == VMT_RIGHT){
               action_range->end.x--;
               if(action_range->end.x < 0) action_range->end.x = 0;
          }else if(action->motion.type == VMT_LEFT){
               if(action_range->end.x + 1 <= ce_last_index(buffer->lines[action_range->start.y])){
                    action_range->end.x++;
               }
          }
     }

     action_range->sorted_start = &action_range->start;
     action_range->sorted_end = &action_range->end;

     ce_sort_points(&action_range->sorted_start, &action_range->sorted_end);

     return true;
}

void vim_action_apply(VimAction_t* action, BufferView_t* buffer_view, Point_t* cursor, VimState_t* vim_state,
                      AutoComplete_t* auto_complete)
{
     Buffer_t* buffer = buffer_view->buffer;
     BufferState_t* buffer_state = buffer->user_data;
     VimActionRange_t action_range;

     if(!vim_action_get_range(action, buffer, cursor, &vim_state->find_state, &vim_state->visual_start, &action_range) ) return;

     // perform action on range
     switch(action->change.type){
     default:
          break;
     case VCT_MOTION:
          *cursor = action_range.end;

          if(action->motion.type == VMT_UP ||
             action->motion.type == VMT_DOWN){
               cursor->x = buffer_state->cursor_save_column;
          }

          if(vim_state->mode == VM_VISUAL_RANGE){
               // expand the selection for some motions
               if(ce_point_after(vim_state->visual_start, *cursor) &&
                  ce_point_after(*action_range.sorted_end, vim_state->visual_start)){
                     vim_state->visual_start = *action_range.sorted_end;
               }else if(ce_point_after(*cursor, vim_state->visual_start) &&
                        ce_point_after(vim_state->visual_start, *action_range.sorted_start)){
                     vim_state->visual_start = *action_range.sorted_start;
               }
          }
          break;
     case VCT_DELETE:
     {
          *cursor = *action_range.sorted_start;

          char* commit_string = ce_dupe_string(buffer, *action_range.sorted_start, *action_range.sorted_end);
          int64_t len = ce_compute_length(buffer, *action_range.sorted_start, *action_range.sorted_end);

          if(!ce_remove_string(buffer, *action_range.sorted_start, len)){
               free(commit_string);
               break;
          }

          if(action->yank){
               char* yank_string = strdup(commit_string);
               if(action_range.yank_mode == YANK_LINE && yank_string[len-1] == NEWLINE) yank_string[len-1] = 0;
               add_yank(&vim_state->yank_head, action->change.reg ? action->change.reg : '"', yank_string, action_range.yank_mode);
          }

          ce_commit_remove_string(&buffer_state->commit_tail, *action_range.sorted_start, *cursor, *action_range.sorted_start, commit_string, BCC_STOP);
     } break;
     case VCT_PASTE_BEFORE:
     {
          YankNode_t* yank = find_yank(vim_state->yank_head, action->change.reg ? action->change.reg : '"');

          if(!yank) break;

          switch(yank->mode){
          default:
               break;
          case YANK_NORMAL:
          {
               if(ce_insert_string(buffer, *action_range.sorted_start, yank->text)){
                    ce_commit_insert_string(&buffer_state->commit_tail,
                                            *action_range.sorted_start, *action_range.sorted_start, *action_range.sorted_start,
                                            strdup(yank->text), BCC_STOP);
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

                    if(ce_insert_string(buffer, insert_loc, save_str)){
                         ce_commit_insert_string(&buffer_state->commit_tail,
                                                 insert_loc, *cursor, cursor_loc,
                                                 save_str, BCC_STOP);
                    }
          } break;
          }
     } break;
     case VCT_PASTE_AFTER:
     {
          YankNode_t* yank = find_yank(vim_state->yank_head, action->change.reg ? action->change.reg : '"');

          if(!yank) break;

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

               if(ce_insert_string(buffer, insert_cursor, yank->text)){
                    ce_commit_insert_string(&buffer_state->commit_tail,
                                            insert_cursor, *action_range.sorted_start, *action_range.sorted_start,
                                            strdup(yank->text), BCC_STOP);
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

               if(ce_insert_string(buffer, insert_loc, save_str)){
                    ce_commit_insert_string(&buffer_state->commit_tail,
                                            insert_loc, *cursor, cursor_loc,
                                            save_str, BCC_STOP);
                    *cursor = cursor_loc;
               }
          } break;
          }
     } break;
     case VCT_CHANGE_CHAR:
     {
          char prev_char;

          if(!ce_get_char(buffer, *action_range.sorted_start, &prev_char)) break;
          if(!ce_set_char(buffer, *action_range.sorted_start, action->change.change_char)) break;

          ce_commit_change_char(&buffer_state->commit_tail, *action_range.sorted_start, *cursor, *action_range.sorted_start,
                                action->change.change_char, prev_char, BCC_STOP);
     } break;
     case VCT_YANK:
     {
          char* save_zero = ce_dupe_string(buffer, *action_range.sorted_start, *action_range.sorted_end);
          char* save_quote = ce_dupe_string(buffer, *action_range.sorted_start, *action_range.sorted_end);

          if(action_range.yank_mode == YANK_LINE){
               int64_t last_index = strlen(save_zero) - 1;
               if(last_index >= 0 && save_zero[last_index] == NEWLINE){
                    save_zero[last_index] = 0;
                    save_quote[last_index] = 0;
               }
          }

          add_yank(&vim_state->yank_head, '0', save_zero, action_range.yank_mode);
          add_yank(&vim_state->yank_head, action->change.reg ? action->change.reg : '"', save_quote,
                   action_range.yank_mode);
     } break;
     case VCT_INDENT:
     {
          if(action->motion.type == VMT_LINE || action->motion.type == VMT_LINE_UP ||
             action->motion.type == VMT_LINE_DOWN || action->motion.type == VMT_VISUAL_RANGE ||
             action->motion.type == VMT_VISUAL_LINE){
               for(int i = action_range.sorted_start->y; i <= action_range.sorted_end->y; ++i){
                    if(strlen(buffer->lines[i]) == 0) continue;
                    Point_t loc = {0, i};
                    ce_insert_string(buffer, loc, TAB_STRING);
                    ce_commit_insert_string(&buffer_state->commit_tail, loc, *cursor, *cursor, strdup(TAB_STRING), BCC_KEEP_GOING);
               }

               if(buffer_state->commit_tail) buffer_state->commit_tail->commit.chain = BCC_STOP;
          }
     } break;
     case VCT_UNINDENT:
     {
          if(action->motion.type == VMT_LINE || action->motion.type == VMT_LINE_UP ||
             action->motion.type == VMT_LINE_DOWN || action->motion.type == VMT_VISUAL_RANGE ||
             action->motion.type == VMT_VISUAL_LINE){
               for(int l = action_range.sorted_start->y; l <= action_range.sorted_end->y; ++l){
                    int64_t whitespace_count = 0;
                    const int64_t tab_len = strlen(TAB_STRING);
                    for(int i = 0; i < tab_len; ++i){
                         if(isblank(buffer->lines[l][i])){
                              whitespace_count++;
                         }else{
                              break;
                         }
                    }

                    if(whitespace_count){
                         Point_t loc = {0, l};
                         ce_remove_string(buffer, loc, whitespace_count);
                         ce_commit_remove_string(&buffer_state->commit_tail, loc, *cursor, *cursor, strdup(TAB_STRING), BCC_KEEP_GOING);
                    }
               }

               if(buffer_state->commit_tail) buffer_state->commit_tail->commit.chain = BCC_STOP;
          }
     } break;
     case VCT_COMMENT:
     {
          for(int64_t i = action_range.sorted_start->y; i <= action_range.sorted_end->y; ++i){
               if(!strlen(buffer->lines[i])) continue;

               Point_t soft_beginning = {0, i};
               ce_move_cursor_to_soft_beginning_of_line(buffer, &soft_beginning);

               if(ce_insert_string(buffer, soft_beginning, VIM_COMMENT_STRING)){
                    ce_commit_insert_string(&buffer_state->commit_tail, soft_beginning, *cursor, *cursor,
                                            strdup(VIM_COMMENT_STRING), BCC_KEEP_GOING);
               }
          }

          if(buffer_state->commit_tail) buffer_state->commit_tail->commit.chain = BCC_STOP;
     } break;
     case VCT_UNCOMMENT:
     {
          for(int64_t i = action_range.sorted_start->y; i <= action_range.sorted_end->y; ++i){
               Point_t soft_beginning = {0, i};
               ce_move_cursor_to_soft_beginning_of_line(buffer, &soft_beginning);

               if(strncmp(buffer->lines[i] + soft_beginning.x, VIM_COMMENT_STRING,
                          strlen(VIM_COMMENT_STRING)) != 0) continue;

               if(ce_remove_string(buffer, soft_beginning, strlen(VIM_COMMENT_STRING))){
                    ce_commit_remove_string(&buffer_state->commit_tail, soft_beginning, *cursor, *cursor,
                                            strdup(VIM_COMMENT_STRING), BCC_KEEP_GOING);
               }
          }

          if(buffer_state->commit_tail) buffer_state->commit_tail->commit.chain = BCC_STOP;
     } break;
     case VCT_FLIP_CASE:
     {
          Point_t itr = *action_range.sorted_start;

          do{
               char prev_char = 0;
               if(!ce_get_char(buffer, itr, &prev_char)) assert(0);

               if(isalpha(prev_char)) {
                    char new_char = 0;
                    if(isupper(prev_char)){
                         new_char = tolower(prev_char);
                    }else{
                         new_char = toupper(prev_char);
                    }
                    if(!ce_set_char(buffer, itr, new_char)) break;
                    ce_commit_change_char(&buffer_state->commit_tail, itr, itr, itr, new_char, prev_char, BCC_STOP);
               }

               ce_advance_cursor(buffer, &itr, 1);
          } while(!ce_point_after(itr, *action_range.sorted_end));
     } break;
     case VCT_JOIN_LINE:
     {
          if(action_range.sorted_start->y >= buffer->line_count - 1) break; // nothing to join

          Point_t next_line_start = {0, action_range.sorted_start->y + 1};
          Point_t next_line_join = next_line_start;
          ce_move_cursor_to_soft_beginning_of_line(buffer, &next_line_join);

          int64_t whitespace_to_delete = next_line_join.x - next_line_start.x;
          if(whitespace_to_delete){
               next_line_join.x--;
               char* save_whitespace = ce_dupe_string(buffer, next_line_start, next_line_join);
               if(ce_remove_string(buffer, next_line_start, whitespace_to_delete)){
                    ce_commit_remove_string(&buffer_state->commit_tail, next_line_start, *action_range.sorted_start, next_line_start,
                                            save_whitespace, BCC_KEEP_GOING);
               }else{
                    break;
               }
          }

          char* save_str = strdup(buffer->lines[next_line_start.y]);
          char* save_line = ce_dupe_line(buffer, next_line_start.y);
          Point_t join_loc = {strlen(buffer->lines[action_range.sorted_start->y]), action_range.sorted_start->y};

          if(ce_join_line(buffer, action_range.sorted_start->y)){
               ce_commit_insert_string(&buffer_state->commit_tail, join_loc, *action_range.sorted_start, join_loc,
                                       save_str, BCC_KEEP_GOING);
               ce_commit_remove_string(&buffer_state->commit_tail, next_line_start, *action_range.sorted_start, next_line_start,
                                       save_line, BCC_KEEP_GOING);
               *cursor = join_loc;
               if(ce_insert_string(buffer, *cursor, " ")){
                    ce_commit_insert_string(&buffer_state->commit_tail, join_loc, *action_range.sorted_start, join_loc,
                                            strdup(" "), BCC_STOP);
               }
          }else{
               free(save_str);
               free(save_line);
          }
     } break;
     case VCT_OPEN_ABOVE:
     {
          Point_t begin_line = {0, cursor->y};

          // indent if necessary
          int64_t indent_len = ce_get_indentation_for_next_line(buffer, *cursor, strlen(TAB_STRING));
          char* indent_nl = malloc(sizeof '\n' + indent_len + sizeof '\0');
          memset(&indent_nl[0], ' ', indent_len);
          indent_nl[indent_len] = '\n';
          indent_nl[indent_len + 1] = '\0';

          if(ce_insert_string(buffer, begin_line, indent_nl)){
               *cursor = (Point_t){indent_len, cursor->y};
               ce_commit_insert_string(&buffer_state->commit_tail, begin_line, *cursor, *cursor, indent_nl, BCC_KEEP_GOING);
          }
     } break;
     case VCT_OPEN_BELOW:
     {
          Point_t end_of_line = *cursor;
          end_of_line.x = strlen(buffer->lines[cursor->y]);

          // indent if necessary
          int64_t indent_len = ce_get_indentation_for_next_line(buffer, *cursor, strlen(TAB_STRING));
          char* nl_indent = malloc(sizeof '\n' + indent_len + sizeof '\0');
          nl_indent[0] = '\n';
          memset(&nl_indent[1], ' ', indent_len);
          nl_indent[1 + indent_len] = '\0';

          if(ce_insert_string(buffer, end_of_line, nl_indent)){
               Point_t save_cursor = *cursor;
               *cursor = (Point_t){indent_len, cursor->y + 1};
               ce_commit_insert_string(&buffer_state->commit_tail, end_of_line, save_cursor, *cursor, nl_indent, BCC_KEEP_GOING);
          }
     } break;
     case VCT_RECORD_MACRO:
          if(vim_state->recording_macro){
               int* built_macro = keys_get_string(vim_state->record_macro_head);

               if(built_macro[0]){
                    macro_add(&vim_state->macro_head, vim_state->recording_macro, built_macro);
               }else{
                    free(built_macro);
               }

               vim_state->recording_macro = 0;
          }else{
               vim_state->recording_macro = action->change.reg;
               keys_free(&vim_state->record_macro_head);
          }
          break;
     case VCT_PLAY_MACRO:
     {
          if(vim_state->playing_macro == action->change.reg){
               ce_message("attempted to play macro in register '%c' inside itself", action->change.reg);
               break;
          }

          MacroNode_t* macro = macro_find(vim_state->macro_head, action->change.reg);
          if(!macro){
               ce_message("no macro defined in register '%c'", action->change.reg);
               break;
          }

          KeyNode_t* save_command_head = vim_state->command_head;
          vim_state->command_head = NULL;
          vim_state->playing_macro = action->change.reg;

          for(int64_t i = 0; i < action->multiplier; ++i){

               bool unhandled_key = false;
               int* macro_itr = macro->command;
               while(*macro_itr){
                    VimKeyHandlerResult_t vkh_result =  vim_key_handler(*macro_itr, vim_state, buffer_view, auto_complete, true);

                    if(vkh_result.type == VKH_UNHANDLED_KEY){
                         unhandled_key = true;
                         break;
                    }

                    macro_itr++;
               }

               keys_free(&vim_state->command_head);

               if(unhandled_key) break;
          }

          vim_state->playing_macro = 0;
          vim_state->command_head = save_command_head;
     } break;
     }

     vim_state->mode = action->end_in_vim_mode;

     if(action->end_in_vim_mode != VM_INSERT){
          Point_t old_cursor = *cursor;
          ce_clamp_cursor(buffer, cursor);

          if(old_cursor.x == cursor->x &&
             old_cursor.y == cursor->y){
               buffer_state->cursor_save_column = cursor->x;
          }
     }else{
          if(action->change.type != VCT_NONE && buffer_state->commit_tail) buffer_state->commit_tail->commit.chain = BCC_KEEP_GOING;
     }
}

// location is {left_column, top_line} for the view
void scroll_view_to_location(BufferView_t* buffer_view, const Point_t* location){
     // TODO: should we be able to scroll the view above our first line?
     buffer_view->left_column = (location->x >= 0) ? location->x : 0;
     buffer_view->top_row = (location->y >= 0) ? location->y : 0;
}

void center_view(BufferView_t* view)
{
     int64_t view_height = view->bottom_right.y - view->top_left.y;
     Point_t location = (Point_t) {0, view->cursor.y - (view_height / 2)};
     scroll_view_to_location(view, &location);
}

typedef struct TabView_t{
     BufferView_t* view_head;
     BufferView_t* view_current;
     BufferView_t* view_previous;
     BufferView_t* view_input_save;
     BufferView_t* view_overrideable;
     Buffer_t* overriden_buffer;
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

void tab_view_save_overrideable(TabView_t* tab)
{
     tab->overriden_buffer = tab->view_overrideable->buffer;
     tab->overriden_buffer->cursor = tab->view_overrideable->cursor;
}

void tab_view_restore_overrideable(TabView_t* tab)
{
     tab->view_overrideable->buffer = tab->overriden_buffer;
     tab->view_overrideable->cursor = tab->overriden_buffer->cursor;
     center_view(tab->view_overrideable);
}

typedef struct{
     bool input;
     const char* input_message;
     int input_key;

     Buffer_t* shell_command_buffer; // Allocate so it can be part of the buffer list and get free'd at the end
     Buffer_t* completion_buffer;    // same as shell_command_buffer

     Buffer_t input_buffer;
     Buffer_t buffer_list_buffer;
     Buffer_t mark_list_buffer;
     Buffer_t yank_list_buffer;
     Buffer_t macro_list_buffer;
     Buffer_t* buffer_before_query;

     VimState_t vim_state;

     int64_t last_command_buffer_jump;
     int last_key;

     TabView_t* tab_head;
     TabView_t* tab_current;

     BufferView_t* view_input;

     InputHistory_t shell_command_history;
     InputHistory_t shell_input_history;
     InputHistory_t search_history;
     InputHistory_t load_file_history;

     pthread_t shell_command_thread;
     pthread_t shell_input_thread;

     AutoComplete_t auto_complete;

     LineNumberType_t line_number_type;

     bool quit;
} ConfigState_t;

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

void free_marks(MarkNode_t** head)
{
     MarkNode_t* itr = *head;
     while(itr){
          MarkNode_t* tmp = itr;
          itr = itr->next;
          free(tmp);
     }

     *head = NULL;
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

void vim_enter_normal_mode(VimState_t* vim_state)
{
     vim_state->mode = VM_NORMAL;
}

bool vim_enter_insert_mode(VimState_t* vim_state, BufferView_t* buffer_view)
{
     if(buffer_view->buffer->readonly) return false;

     vim_state->mode = VM_INSERT;
     return true;
}

void vim_enter_visual_range_mode(VimState_t* vim_state, BufferView_t* buffer_view)
{
     vim_state->mode = VM_VISUAL_RANGE;
     vim_state->visual_start = buffer_view->cursor;
}

void vim_enter_visual_line_mode(VimState_t* vim_state, BufferView_t* buffer_view)
{
     vim_state->mode = VM_VISUAL_LINE;
     vim_state->visual_start = buffer_view->cursor;
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

void input_start(ConfigState_t* config_state, const char* input_message, int input_key)
{
     ce_clear_lines(config_state->view_input->buffer);
     ce_alloc_lines(config_state->view_input->buffer, 1);
     config_state->input = true;
     config_state->view_input->cursor = (Point_t){0, 0};
     config_state->input_message = input_message;
     config_state->input_key = input_key;
     pthread_mutex_lock(&view_input_save_lock);
     config_state->tab_current->view_input_save = config_state->tab_current->view_current;
     pthread_mutex_unlock(&view_input_save_lock);
     config_state->tab_current->view_current = config_state->view_input;

     vim_enter_insert_mode(&config_state->vim_state, config_state->tab_current->view_current);

     // reset input history back to tail
     InputHistory_t* history = history_from_input_key(config_state);
     if(history) history->cur = history->tail;
}

void input_end(ConfigState_t* config_state)
{
     config_state->input = false;
     config_state->tab_current->view_current = config_state->tab_current->view_input_save;
     vim_enter_normal_mode(&config_state->vim_state);
}

void input_cancel(ConfigState_t* config_state)
{
     if(config_state->input_key == '/' || config_state->input_key == '?'){
          pthread_mutex_lock(&view_input_save_lock);
          config_state->tab_current->view_input_save->cursor = config_state->vim_state.start_search;
          pthread_mutex_unlock(&view_input_save_lock);
          center_view(config_state->tab_current->view_input_save);
     }else if(config_state->input_key == 6){
          if(config_state->tab_current->view_overrideable){
               tab_view_restore_overrideable(config_state->tab_current);
          }else{
               pthread_mutex_lock(&view_input_save_lock);
               config_state->tab_current->view_input_save->buffer = config_state->buffer_before_query;
               pthread_mutex_unlock(&view_input_save_lock);
               config_state->tab_current->view_input_save->cursor = config_state->buffer_before_query->cursor;
               center_view(config_state->tab_current->view_input_save);
          }
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

void free_buffer_state(BufferState_t* buffer_state)
{
     BufferCommitNode_t* itr = buffer_state->commit_tail;
     while(itr->prev) itr = itr->prev;
     ce_commits_free(itr);
     free_marks(&buffer_state->mark_head);
     free(buffer_state);
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

bool delete_buffer_at_index(BufferNode_t** head, TabView_t* tab_head, int64_t buffer_index)
{
     // find which buffer the user wants to delete
     Buffer_t* delete_buffer = NULL;
     BufferNode_t* itr = *head;
     while(itr){
          if(buffer_index == 0){
               delete_buffer = itr->buffer;
               break;
          }
          buffer_index--;
          itr = itr->next;
     }

     // remove buffer
     ce_remove_buffer_from_list(head, delete_buffer);

     // change views that are showing this buffer for all tabs
     if(head){
          TabView_t* tab_itr = tab_head;
          while(tab_itr){
               ce_change_buffer_in_views(tab_itr->view_head, delete_buffer, (*head)->buffer);
               tab_itr = tab_itr->next;
          }
     }else{
          return false;
     }

     // free the buffer and it's state
     free_buffer_state(delete_buffer->user_data);
     ce_free_buffer(delete_buffer);

     return true;
}

bool initializer(BufferNode_t** head, Point_t* terminal_dimensions, int argc, char** argv, void** user_data)
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
     config_state->input_buffer.name = strdup("[input]");
     config_state->view_input->buffer = &config_state->input_buffer;

     // setup buffer list buffer
     config_state->buffer_list_buffer.name = strdup("[buffers]");
     initialize_buffer(&config_state->buffer_list_buffer);
     config_state->buffer_list_buffer.readonly = true;

     config_state->mark_list_buffer.name = strdup("[marks]");
     initialize_buffer(&config_state->mark_list_buffer);
     config_state->mark_list_buffer.readonly = true;

     config_state->yank_list_buffer.name = strdup("[yanks]");
     initialize_buffer(&config_state->yank_list_buffer);
     config_state->yank_list_buffer.readonly = true;

     config_state->macro_list_buffer.name = strdup("[macros]");
     initialize_buffer(&config_state->macro_list_buffer);
     config_state->macro_list_buffer.readonly = true;

     // if we reload, the shell command buffer may already exist, don't recreate it
     BufferNode_t* itr = *head;
     while(itr){
          if(strcmp(itr->buffer->name, "[shell_output]") == 0){
               config_state->shell_command_buffer = itr->buffer;
          }
          if(strcmp(itr->buffer->name, "[completions]") == 0){
               config_state->completion_buffer = itr->buffer;
          }
          itr = itr->next;
     }

     if(!config_state->shell_command_buffer){
          config_state->shell_command_buffer = calloc(1, sizeof(*config_state->shell_command_buffer));
          config_state->shell_command_buffer->name = strdup("[shell_output]");
          config_state->shell_command_buffer->readonly = true;
          ce_alloc_lines(config_state->shell_command_buffer, 1);
          BufferNode_t* new_buffer_node = ce_append_buffer_to_list(*head, config_state->shell_command_buffer);
          if(!new_buffer_node){
               ce_message("failed to add shell command buffer to list");
               return false;
          }
     }

     if(!config_state->completion_buffer){
          config_state->completion_buffer = calloc(1, sizeof(*config_state->shell_command_buffer));
          config_state->completion_buffer->name = strdup("[completions]");
          config_state->completion_buffer->readonly = true;
          BufferNode_t* new_buffer_node = ce_append_buffer_to_list(*head, config_state->completion_buffer);
          if(!new_buffer_node){
               ce_message("failed to add shell command buffer to list");
               return false;
          }
     }

     *user_data = config_state;

     // setup state for each buffer
     itr = *head;
     while(itr){
          initialize_buffer(itr->buffer);
          itr = itr->next;
     }

     config_state->tab_current->view_head->buffer = (*head)->buffer;
     config_state->tab_current->view_current = config_state->tab_current->view_head;

     for(int i = 0; i < argc; ++i){
          BufferNode_t* node = new_buffer_from_file(*head, argv[i]);

          // if we loaded a file, set the view to point at the file
          if(i == 0 && node) config_state->tab_current->view_current->buffer = node->buffer;
     }

     config_state->line_number_type = LNT_RELATIVE;

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

     init_pair(S_NORMAL_CURRENT_LINE, COLOR_FOREGROUND, COLOR_BRIGHT_BLACK);
     init_pair(S_KEYWORD_CURRENT_LINE, COLOR_BLUE, COLOR_BRIGHT_BLACK);
     init_pair(S_TYPE_CURRENT_LINE, COLOR_BRIGHT_BLUE, COLOR_BRIGHT_BLACK);
     init_pair(S_CONTROL_CURRENT_LINE, COLOR_YELLOW, COLOR_BRIGHT_BLACK);
     init_pair(S_COMMENT_CURRENT_LINE, COLOR_GREEN, COLOR_BRIGHT_BLACK);
     init_pair(S_STRING_CURRENT_LINE, COLOR_RED, COLOR_BRIGHT_BLACK);
     init_pair(S_CONSTANT_CURRENT_LINE, COLOR_MAGENTA, COLOR_BRIGHT_BLACK);
     init_pair(S_PREPROCESSOR_CURRENT_LINE, COLOR_BRIGHT_MAGENTA, COLOR_BRIGHT_BLACK);
     init_pair(S_FILEPATH_CURRENT_LINE, COLOR_BLUE, COLOR_BRIGHT_BLACK);
     init_pair(S_DIFF_ADD_CURRENT_LINE, COLOR_GREEN, COLOR_BRIGHT_BLACK);
     init_pair(S_DIFF_REMOVE_CURRENT_LINE, COLOR_RED, COLOR_BRIGHT_BLACK);

     init_pair(S_LINE_NUMBERS, COLOR_WHITE, COLOR_BACKGROUND);

     init_pair(S_TRAILING_WHITESPACE, COLOR_FOREGROUND, COLOR_RED);

     init_pair(S_BORDERS, COLOR_WHITE, COLOR_BACKGROUND);

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

bool destroyer(BufferNode_t** head, void* user_data)
{
     BufferNode_t* itr = *head;
     while(itr){
          free_buffer_state(itr->buffer->user_data);
          itr->buffer->user_data = NULL;
          itr = itr->next;
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
          free_buffer_state(config_state->input_buffer.user_data);
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

     free_buffer_state(config_state->buffer_list_buffer.user_data);
     ce_free_buffer(&config_state->buffer_list_buffer);

     free_buffer_state(config_state->mark_list_buffer.user_data);
     ce_free_buffer(&config_state->mark_list_buffer);

     free_buffer_state(config_state->yank_list_buffer.user_data);
     ce_free_buffer(&config_state->yank_list_buffer);

     free_buffer_state(config_state->macro_list_buffer.user_data);
     ce_free_buffer(&config_state->macro_list_buffer);

     // history
     input_history_free(&config_state->shell_command_history);
     input_history_free(&config_state->shell_input_history);
     input_history_free(&config_state->search_history);
     input_history_free(&config_state->load_file_history);

     pthread_mutex_destroy(&draw_lock);
     pthread_mutex_destroy(&shell_buffer_lock);

     auto_complete_clear(&config_state->auto_complete);

     free(config_state->vim_state.last_insert_command);

     keys_free(&config_state->vim_state.command_head);
     keys_free(&config_state->vim_state.record_macro_head);

     free_yanks(&config_state->vim_state.yank_head);

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
          ce_set_cursor(new_buffer, &view->cursor, dst);

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
                    ce_set_cursor(new_buffer, &view->cursor, dst);
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

void commit_input_to_history(Buffer_t* input_buffer, InputHistory_t* history)
{
     if(!input_buffer->line_count) return;

     char* saved = ce_dupe_buffer(input_buffer);

     if(!history->tail->prev || (history->tail->prev && strcmp(saved, history->tail->prev->entry) != 0)){
          history->tail->entry = saved;
          input_history_commit_current(history);
     }
}

void view_follow_cursor(BufferView_t* current_view, LineNumberType_t line_number_type)
{
     ce_follow_cursor(current_view->cursor, &current_view->left_column, &current_view->top_row,
                      current_view->bottom_right.x - current_view->top_left.x,
                      current_view->bottom_right.y - current_view->top_left.y,
                      current_view->bottom_right.x == (g_terminal_dimensions->x - 1),
                      current_view->bottom_right.y == (g_terminal_dimensions->y - 2),
                      line_number_type, current_view->buffer->line_count);
}

void split_view(BufferView_t* head_view, BufferView_t* current_view, bool horizontal, LineNumberType_t line_number_type)
{
     BufferView_t* new_view = ce_split_view(current_view, current_view->buffer, horizontal);
     if(new_view){
          Point_t top_left = {0, 0};
          Point_t bottom_right = {g_terminal_dimensions->x - 1, g_terminal_dimensions->y - 1};
          ce_calc_views(head_view, top_left, bottom_right);
          view_follow_cursor(current_view, line_number_type);
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
     config_state->vim_state.recording_macro = 0;
     if(!next_view) next_view = ce_find_view_at_point(config_state->tab_current->view_head, point);

     if(next_view){
          // save view and cursor
          config_state->tab_current->view_previous = config_state->tab_current->view_current;
          config_state->tab_current->view_current->buffer->cursor = config_state->tab_current->view_current->cursor;
          config_state->tab_current->view_current = next_view;
          vim_enter_normal_mode(&config_state->vim_state);
     }
}

void handle_mouse_event(ConfigState_t* config_state, Buffer_t* buffer, BufferView_t* buffer_view, Point_t* cursor)
{
     MEVENT event;
     if(getmouse(&event) == OK){
          bool enter_insert;
          if((enter_insert = config_state->vim_state.mode == VM_INSERT)){
               ce_clamp_cursor(buffer, cursor);
               vim_enter_normal_mode(&config_state->vim_state);
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
                             click);
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
          if(enter_insert && config_state->tab_current->view_current == buffer_view){
               vim_enter_insert_mode(&config_state->vim_state, config_state->tab_current->view_current);
          }
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

bool iterate_history_input(ConfigState_t* config_state, bool previous)
{
     BufferState_t* buffer_state = config_state->view_input->buffer->user_data;
     InputHistory_t* history = history_from_input_key(config_state);
     if(!history) return false;

#if 0
     // update the current history node if we are at the tail to save what the user typed
     // skip this if they haven't typed anything
     if(history->tail == history->cur && config_state->view_input->buffer->line_count){
          history->tail->entry = ce_dupe_buffer(config_state->view_input->buffer);
     }
#endif

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
     snprintf(format_string, BUFSIZ, "+ %%5s %%-%"PRId64"s %%-%"PRId64"s", max_name_len,
              max_buffer_lines_digits);
     snprintf(buffer_info, BUFSIZ, format_string, "flags", "buffer name", "lines");
     ce_append_line(&config_state->buffer_list_buffer, buffer_info);

     // build buffer info
     snprintf(format_string, BUFSIZ, "  %%5s %%-%"PRId64"s %%%"PRId64 PRId64, max_name_len, max_buffer_lines_digits);

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

void update_mark_list_buffer(ConfigState_t* config_state, const Buffer_t* buffer)
{
     char buffer_info[BUFSIZ];
     config_state->mark_list_buffer.readonly = false;
     ce_clear_lines(&config_state->mark_list_buffer);

     snprintf(buffer_info, BUFSIZ, "+ reg line");
     ce_append_line(&config_state->mark_list_buffer, buffer_info);

     int max_digits = 1;
     const MarkNode_t* itr = ((BufferState_t*)(buffer->user_data))->mark_head;
     while(itr){
          int digits = count_digits(itr->location.y);
          if(digits > max_digits) max_digits = digits;
          itr = itr->next;
     }

     itr = ((BufferState_t*)(buffer->user_data))->mark_head;
     while(itr){
          snprintf(buffer_info, BUFSIZ, "  '%c' %*"PRId64" %s",
                   itr->reg_char, max_digits, itr->location.y,
                   itr->location.y < buffer->line_count ? buffer->lines[itr->location.y] : "");
          ce_append_line(&config_state->mark_list_buffer, buffer_info);
          itr = itr->next;
     }

     config_state->mark_list_buffer.modified = false;
     config_state->mark_list_buffer.readonly = true;
}

void update_yank_list_buffer(ConfigState_t* config_state)
{
     char buffer_info[BUFSIZ];
     config_state->yank_list_buffer.readonly = false;
     ce_clear_lines(&config_state->yank_list_buffer);

     const YankNode_t* itr = config_state->vim_state.yank_head;
     while(itr){
          snprintf(buffer_info, BUFSIZ, "+ reg '%c'", itr->reg_char);
          ce_append_line(&config_state->yank_list_buffer, buffer_info);
          ce_append_line(&config_state->yank_list_buffer, itr->text);
          itr = itr->next;
     }

     config_state->yank_list_buffer.modified = false;
     config_state->yank_list_buffer.readonly = true;
}

void update_macro_list_buffer(ConfigState_t* config_state)
{
     char buffer_info[BUFSIZ];
     config_state->macro_list_buffer.readonly = false;
     ce_clear_lines(&config_state->macro_list_buffer);

     ce_append_line(&config_state->macro_list_buffer, "+ reg actions");

     const MacroNode_t* itr = config_state->vim_state.macro_head;
     while(itr){
          char* char_string = command_string_to_char_string(itr->command);
          snprintf(buffer_info, BUFSIZ, "  '%c' %s", itr->reg, char_string);
          ce_append_line(&config_state->macro_list_buffer, buffer_info);
          free(char_string);
          itr = itr->next;
     }

     if(config_state->vim_state.recording_macro){
          ce_append_line(&config_state->macro_list_buffer, "");
          ce_append_line(&config_state->macro_list_buffer, "+ recording:");

          int* int_cmd = keys_get_string(config_state->vim_state.record_macro_head);

          if(int_cmd[0]){
               char* char_cmd = command_string_to_char_string(int_cmd);
               if(char_cmd[0]){
                    ce_append_line(&config_state->macro_list_buffer, char_cmd);
               }

               free(char_cmd);
          }

          free(int_cmd);
     }

     ce_append_line(&config_state->macro_list_buffer, "");
     ce_append_line(&config_state->macro_list_buffer, "+ escape conversions");
     ce_append_line(&config_state->macro_list_buffer, "+ \\b -> KEY_BACKSPACE");
     ce_append_line(&config_state->macro_list_buffer, "+ \\e -> KEY_ESCAPE");
     ce_append_line(&config_state->macro_list_buffer, "+ \\r -> KEY_ENTER");
     ce_append_line(&config_state->macro_list_buffer, "+ \\t -> KEY_TAB");
     ce_append_line(&config_state->macro_list_buffer, "+ \\\\ -> \\"); // HAHAHAHAHA

     config_state->macro_list_buffer.modified = false;
     config_state->macro_list_buffer.readonly = true;
}

Point_t get_cursor_on_terminal(const Point_t* cursor, const BufferView_t* buffer_view, LineNumberType_t line_number_type)
{
     Point_t p = {cursor->x - buffer_view->left_column + buffer_view->top_left.x,
                  cursor->y - buffer_view->top_row + buffer_view->top_left.y};
     p.x += ce_get_line_number_column_width(line_number_type, buffer_view->buffer->line_count, buffer_view->top_left.y,
                                            buffer_view->bottom_right.y);
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
     int64_t line_count = 0;
     CompleteNode_t* itr = auto_complete->head;
     while(itr){
          if(strncmp(itr->option, match, match_len) == 0){
               ce_append_line_readonly(completion_buffer, itr->option);
               line_count++;

               if(itr == auto_complete->current){
                    int64_t last_index = line_count - 1;
                    completion_buffer->highlight_start = (Point_t){0, last_index};
                    completion_buffer->highlight_end = (Point_t){strlen(completion_buffer->lines[last_index]), last_index};
               }
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
     if(cursor.x > 0){
          if(*path_begin != '\0') return false;

          while(path_begin >= line){
               if(!last_slash && *path_begin == '/') last_slash = path_begin;
               if(isblank(*path_begin)) break;
               path_begin--;
          }

          path_begin++; // account for iterating 1 too far
     }

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
          case KEY_CLOSE: // Ctrl + q
               if(!config_state->view_input->buffer->line_count) break;

               if(tolower(config_state->view_input->buffer->lines[0][0]) == 'y'){
                    config_state->quit = true;
               }
               break;
          case ':':
          {
               if(!config_state->view_input->buffer->line_count) break;

               int64_t line = atoi(config_state->view_input->buffer->lines[0]);
               if(line > 0){
                    *cursor = (Point_t){0, line - 1};
                    ce_move_cursor_to_soft_beginning_of_line(buffer, cursor);
               }
          } break;
          case 6: // Ctrl + f
               commit_input_to_history(config_state->view_input->buffer, &config_state->load_file_history);

               for(int64_t i = 0; i < config_state->view_input->buffer->line_count; ++i){
                    Buffer_t* new_buffer = open_file_buffer(head, config_state->view_input->buffer->lines[i]);
                    if(i == 0 && new_buffer){
                         config_state->tab_current->view_current->buffer = new_buffer;
                         config_state->tab_current->view_current->cursor = (Point_t){0, 0};
                    }
               }

               if(config_state->tab_current->overriden_buffer){
                    tab_view_restore_overrideable(config_state->tab_current);
               }
               break;
          case '/':
               if(!config_state->view_input->buffer->line_count) break;

               commit_input_to_history(config_state->view_input->buffer, &config_state->search_history);
               add_yank(&config_state->vim_state.yank_head, '/', strdup(config_state->view_input->buffer->lines[0]), YANK_NORMAL);
               break;
          case '?':
               if(!config_state->view_input->buffer->line_count) break;

               commit_input_to_history(config_state->view_input->buffer, &config_state->search_history);
               add_yank(&config_state->vim_state.yank_head, '/', strdup(config_state->view_input->buffer->lines[0]), YANK_NORMAL);
               break;
          case 24: // Ctrl + x
          {
               if(!config_state->view_input->buffer->line_count) break;

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
                    if(config_state->tab_current->view_overrideable){
                         tab_view_save_overrideable(config_state->tab_current);
                         config_state->tab_current->view_overrideable->buffer = command_buffer;
                         config_state->tab_current->view_overrideable->cursor = (Point_t){0, 0};
                         config_state->tab_current->view_overrideable->top_row = 0;
                    }else{
                         // save the cursor before switching buffers
                         buffer_view->buffer->cursor = buffer_view->cursor;
                         buffer_view->buffer = command_buffer;
                         buffer_view->cursor = (Point_t){0, 0};
                         buffer_view->top_row = 0;
                         command_view = buffer_view;
                    }
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
               if(!config_state->view_input->buffer->line_count) break; // NOTE: unsure if this is correct

               YankNode_t* yank = find_yank(config_state->vim_state.yank_head, '/');
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
                    if(!ce_remove_string(buffer, match, search_len)) break;
                    if(replace_len){
                         if(!ce_insert_string(buffer, match, replace_str)) break;
                    }
                    ce_commit_change_string(&buffer_state->commit_tail, match, match, match, strdup(replace_str),
                                            strdup(search_str), BCC_STOP);
                    begin = match;
                    replace_count++;
               }

               if(replace_count){
                    ce_message("replaced %" PRId64 " matches", replace_count);
               }else{
                    ce_message("no matches found to replace");
               }

               *cursor = begin;
               center_view(buffer_view);
               free(replace_str);
          } break;
          case 1: // Ctrl + a
          {
               if(!config_state->view_input->buffer->lines) break;

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
               for(int64_t i = 0; i < input_buffer->line_count; ++i){
                    const char* input = input_buffer->lines[i];

#if 0
                    pthread_mutex_lock(&shell_buffer_lock);
                    ce_append_string_readonly(config_state->shell_command_buffer,
                                              config_state->shell_command_buffer->line_count - 1,
                                              input);
                    ce_append_char_readonly(config_state->shell_command_buffer, NEWLINE);
                    pthread_mutex_unlock(&shell_buffer_lock);
#endif

                    // send the input to the shell command
                    write(shell_command_data.shell_command_input_fd, input, strlen(input));
                    write(shell_command_data.shell_command_input_fd, "\n", 1);
               }
          } break;
          case '@':
          {
               if(!config_state->view_input->buffer->lines) break;

               int64_t line = config_state->tab_current->view_input_save->cursor.y - 1; // account for buffer list row header
               if(line < 0) return;
               MacroNode_t* itr = config_state->vim_state.macro_head;

               while(line > 0){
                    itr = itr->next;
                    if(!itr) return;
                    line--;
               }

               if(!itr) return;

               free(itr->command);
               int* new_macro_string = char_string_to_command_string(config_state->view_input->buffer->lines[0]);

               if(new_macro_string){
                    itr->command = new_macro_string;
               }else{
                    ce_message("invalid editted macro string");
               }
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
          buffer_view->cursor = itr->buffer->cursor;
          *cursor = itr->buffer->cursor;
          center_view(buffer_view);
     }else if(buffer_view->buffer == config_state->shell_command_buffer){
          BufferView_t* view_to_change = buffer_view;
          if(config_state->tab_current->view_previous) view_to_change = config_state->tab_current->view_previous;

          if(goto_file_destination_in_buffer(head, config_state->shell_command_buffer, cursor->y,
                                             config_state->tab_current->view_head, view_to_change,
                                             &config_state->last_command_buffer_jump)){
               config_state->tab_current->view_current = view_to_change;
          }
     }else if(buffer_view->buffer == &config_state->mark_list_buffer){
          int64_t line = cursor->y - 1; // account for buffer list row header
          if(line < 0) return;
          MarkNode_t* itr = ((BufferState_t*)(config_state->buffer_before_query->user_data))->mark_head;

          while(line > 0){
               itr = itr->next;
               if(!itr) return;
               line--;
          }

          if(!itr) return;

          buffer_view->buffer = config_state->buffer_before_query;
          buffer_view->cursor.y = itr->location.y;
          center_view(buffer_view);

          if(config_state->tab_current->view_overrideable){
               tab_view_restore_overrideable(config_state->tab_current);
          }
     }else if(buffer_view->buffer == &config_state->macro_list_buffer){
          int64_t line = cursor->y - 1; // account for buffer list row header
          if(line < 0) return;
          MacroNode_t* itr = config_state->vim_state.macro_head;

          while(line > 0){
               itr = itr->next;
               if(!itr) return;
               line--;
          }

          if(!itr) return;

          input_start(config_state, "Edit Macro", '@');
          vim_enter_normal_mode(&config_state->vim_state);
          char* char_command = command_string_to_char_string(itr->command);
          ce_insert_string(config_state->view_input->buffer, (Point_t){0,0}, char_command);
          free(char_command);
     }
}

VimKeyHandlerResult_t vim_key_handler(int key, VimState_t* vim_state, BufferView_t* buffer_view,
                                      AutoComplete_t* auto_complete, bool repeating)
{
     Buffer_t* buffer = buffer_view->buffer;
     BufferState_t* buffer_state = buffer->user_data;
     Point_t* cursor = &buffer_view->cursor;
     char recording_macro = vim_state->recording_macro;

     VimKeyHandlerResult_t result;
     result.type = VKH_UNHANDLED_KEY;

     switch(vim_state->mode){
     default:
          assert(!"vim mode not handled");
          return result;
     case VM_INSERT:
          switch(key){
          default:
               if(isprint(key)){
                    if(ce_insert_char(buffer, *cursor, key)){
                         keys_push(&vim_state->command_head, key);
                         Point_t undo_cursor = *cursor;
                         cursor->x++;
                         ce_commit_insert_char(&buffer_state->commit_tail, undo_cursor, undo_cursor, *cursor, key, BCC_KEEP_GOING);
                    }
               }else{
                    return result;
               }
               break;
          case KEY_ESCAPE:
          {
               if(!repeating){
                    int* built_command = keys_get_string(vim_state->command_head);
                    if(vim_state->last_insert_command) free(vim_state->last_insert_command);
                    vim_state->last_insert_command = built_command;
               }

               keys_free(&vim_state->command_head);

               vim_enter_normal_mode(vim_state);
               ce_clamp_cursor(buffer, cursor);
               if(buffer_state->commit_tail) buffer_state->commit_tail->commit.chain = BCC_STOP;
          } break;
          case KEY_ENTER:
               key = NEWLINE;

               if(ce_insert_char(buffer, *cursor, key)){
                    Point_t undo_cursor = *cursor;

                    cursor->y++;
                    cursor->x = 0;

                    ce_commit_insert_char(&buffer_state->commit_tail, undo_cursor, undo_cursor, *cursor, key, BCC_KEEP_GOING);
                    keys_push(&vim_state->command_head, KEY_ENTER);

                    // indent if necessary
                    Point_t prev_line = {0, cursor->y-1};
                    int64_t indent_len = ce_get_indentation_for_next_line(buffer, prev_line, strlen(TAB_STRING));
                    if(indent_len > 0){
                         char* indent = malloc(indent_len + 1);
                         memset(indent, ' ', indent_len);
                         indent[indent_len] = '\0';

                         if(ce_insert_string(buffer, *cursor, indent)){
                              Point_t pre_insert = *cursor;
                              cursor->x += indent_len;
                              ce_commit_insert_string(&buffer_state->commit_tail, pre_insert, pre_insert, *cursor, indent, BCC_KEEP_GOING);
                         }
                    }
               }
               break;
          case KEY_BACKSPACE:
          {
               Point_t before_cursor = {cursor->x - 1, cursor->y};
               if(before_cursor.x < 0){
                    if(!before_cursor.y) break; // don't do anything if the user tries to backspace at 0, 0
                    before_cursor.x = strlen(buffer->lines[cursor->y - 1]);
                    before_cursor.y--;
               }

               char ch;
               if(ce_get_char(buffer, before_cursor, &ch)){
                    if(ce_remove_char(buffer, before_cursor)){
                         ce_commit_remove_char(&buffer_state->commit_tail, before_cursor, *cursor, before_cursor, ch, BCC_KEEP_GOING);
                         *cursor = before_cursor;
                         keys_push(&vim_state->command_head, key);
                    }
               }
           } break;
          case KEY_TAB:
          {
               if(auto_completing(auto_complete)){
                    int64_t offset = cursor->x - auto_complete->start.x;
                    const char* complete = auto_complete->current->option + offset;
                    int64_t complete_len = strlen(complete);
                    if(ce_insert_string(buffer, *cursor, complete)){
                         Point_t save_cursor = *cursor;
                         ce_move_cursor(buffer, cursor, (Point_t){complete_len, cursor->y});
                         cursor->x++;
                         ce_commit_insert_string(&buffer_state->commit_tail, save_cursor, save_cursor, *cursor, strdup(TAB_STRING), BCC_KEEP_GOING);
                    }
               }else{
                    if(ce_insert_string(buffer, *cursor, TAB_STRING)){
                         Point_t save_cursor = *cursor;
                         ce_move_cursor(buffer, cursor, (Point_t){strlen(TAB_STRING) - 1, 0});
                         cursor->x++; // we want to be after the tabs
                         ce_commit_insert_string(&buffer_state->commit_tail, save_cursor, save_cursor, *cursor, strdup(TAB_STRING), BCC_KEEP_GOING);
                         keys_push(&vim_state->command_head, key);
                    }
               }
          } break;
          case KEY_UP:
          case KEY_DOWN:
          {
               ce_move_cursor(buffer, cursor, (Point_t){0, (key == KEY_DOWN) ? 1 : -1});
               if(buffer_state->commit_tail){
                    buffer_state->commit_tail->commit.chain = BCC_STOP;
               }

               int* built_command = keys_get_string(vim_state->command_head);
               if(vim_state->last_insert_command) free(vim_state->last_insert_command);
               vim_state->last_insert_command = built_command;
               keys_free(&vim_state->command_head);

          } break;
          case KEY_LEFT:
          case KEY_RIGHT:
          {
               cursor->x += key == KEY_RIGHT? 1 : -1;
               if(cursor->x > (int64_t) strlen(buffer->lines[cursor->y])) cursor->x--;
               if(cursor->x < 0) cursor->x++;
               if(buffer_state->commit_tail){
                    buffer_state->commit_tail->commit.chain = BCC_STOP;
               }

               int* built_command = keys_get_string(vim_state->command_head);
               if(vim_state->last_insert_command) free(vim_state->last_insert_command);
               vim_state->last_insert_command = built_command;
               keys_free(&vim_state->command_head);
          } break;
          case '}':
          {
               if(ce_insert_char(buffer, *cursor, key)){
                    Point_t next_cursor = {cursor->x + 1, cursor->y};
                    ce_commit_insert_char(&buffer_state->commit_tail, *cursor, *cursor, next_cursor, key, BCC_KEEP_GOING);

                    bool do_indentation = true;
                    for(int i = 0; i < cursor->x; i++){
                         char blank_c;
                         Point_t itr = {i, cursor->y};
                         if(ce_get_char(buffer, itr, &blank_c)){
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

                         char matchee;
                         if(!ce_get_char(buffer, match, &matchee)) break;

                         if(ce_move_cursor_to_matching_pair(buffer, &match, matchee) && match.y != cursor->y){
                              // get the match's sbol (that's the indentation we're matching)
                              Point_t sbol_match = {0, match.y};
                              ce_move_cursor_to_soft_beginning_of_line(buffer, &sbol_match);

                              if(cursor->x < sbol_match.x){
                                   // we are adding spaces
                                   int64_t n_spaces = sbol_match.x - cursor->x;
                                   for(int64_t i = 0; i < n_spaces; i++){
                                        Point_t itr = {cursor->x + i, cursor->y};
                                        if(!ce_insert_char(buffer, itr, ' ')) assert(0);
                                        ce_commit_insert_char(&buffer_state->commit_tail, itr, *cursor, itr, ' ', BCC_KEEP_GOING);
                                   }
                                   cursor->x = sbol_match.x;
                              }else{
                                   int64_t n_deletes = CE_MIN((int64_t) strlen(TAB_STRING), cursor->x - sbol_match.x);

                                   bool can_unindent = true;
                                   for(Point_t iter = {0, cursor->y}; ce_point_on_buffer(buffer, iter) && iter.x < n_deletes; iter.x++){
                                        if(!isblank(ce_get_char_raw(buffer, iter))){
                                             can_unindent = false;
                                             break;
                                        }
                                   }

                                   if(can_unindent){
                                        Point_t end_of_delete = *cursor;
                                        end_of_delete.x--;
                                        cursor->x -= n_deletes;
                                        char* duped_str = ce_dupe_string(buffer, *cursor, end_of_delete);
                                        if(ce_remove_string(buffer, *cursor, n_deletes)){
                                             ce_commit_remove_string(&buffer_state->commit_tail, *cursor, end_of_delete, *cursor, duped_str, BCC_KEEP_GOING);
                                        }
                                   }
                              }
                         }
                    }

                    cursor->x++;
               }
          } break;
          }
          break;
     case VM_VISUAL_RANGE:
     case VM_VISUAL_LINE:
         if(key == KEY_ESCAPE){
               vim_enter_normal_mode(vim_state);
               break;
         }else if(key == 'v'){
               vim_enter_visual_range_mode(vim_state, buffer_view);
               break;
         }else if(key == 'V'){
               vim_enter_visual_line_mode(vim_state, buffer_view);
               break;
          }
     case VM_NORMAL:
     {
          keys_push(&vim_state->command_head, key);
          int* built_command = keys_get_string(vim_state->command_head);

          VimAction_t vim_action;
          VimCommandState_t command_state = vim_action_from_string(built_command, &vim_action, vim_state->mode, buffer,
                                                                   cursor, &vim_state->visual_start, &vim_state->find_state,
                                                                   vim_state->recording_macro);
          free(built_command);

          switch(command_state){
          default:
          case VCS_INVALID:
               // allow command to be cleared
               keys_free(&vim_state->command_head);
               return result; // did not handle key
          case VCS_CONTINUE:
               break;
          case VCS_COMPLETE:
          {
               VimMode_t original_mode = vim_state->mode;
               vim_action_apply(&vim_action, buffer_view, cursor, vim_state, auto_complete);

               if(vim_state->mode != original_mode){
                    switch(vim_state->mode){
                    default:
                         break;
                    case VM_INSERT:
                         vim_enter_insert_mode(vim_state, buffer_view);
                         break;
                    case VM_NORMAL:
                         vim_enter_normal_mode(vim_state);
                         break;
                    case VM_VISUAL_RANGE:
                         vim_enter_visual_range_mode(vim_state, buffer_view);
                         break;
                    case VM_VISUAL_LINE:
                         vim_enter_visual_line_mode(vim_state, buffer_view);
                         break;
                    }
               }

               if(vim_action.change.type != VCT_MOTION || vim_action.end_in_vim_mode == VM_INSERT){
                    if(!vim_state->playing_macro){
                         vim_state->last_action = vim_action;

                         if(!vim_state->recording_macro && vim_action.change.type == VCT_RECORD_MACRO){
                              vim_state->last_action.change.type = VCT_PLAY_MACRO;
                              vim_state->last_action.change.reg = recording_macro;
                         }
                    }

                    // always use the cursor as the start of the visual selection
                    vim_state->last_action.motion.visual_start_after = true;
               }

               keys_free(&vim_state->command_head);

               if(recording_macro && recording_macro == vim_state->recording_macro){
                    keys_push(&vim_state->record_macro_head, key);
               }

               result.type = VKH_COMPLETED_ACTION;
               result.completed_action = vim_action;

               return result;
          } break;
          }
     } break;
     }

     if(recording_macro && recording_macro == vim_state->recording_macro){
          keys_push(&vim_state->record_macro_head, key);
     }

     result.type = VKH_HANDLED_KEY;

     return result;
}

bool key_handler(int key, BufferNode_t** head, void* user_data)
{
     ConfigState_t* config_state = user_data;
     Buffer_t* buffer = config_state->tab_current->view_current->buffer;
     BufferState_t* buffer_state = buffer->user_data;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Point_t* cursor = &config_state->tab_current->view_current->cursor;

     bool handled_key = false;

     if(config_state->vim_state.mode != VM_INSERT){
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
               case 'r': // reload file
               {
                    ce_message("reloading %s", buffer->filename);
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

                    Buffer_t* file_buffer = open_file_buffer(*head, filename);
                    if(file_buffer) buffer_view->buffer = file_buffer;
               } break;
               case 'v':
                    config_state->tab_current->view_overrideable = config_state->tab_current->view_current;
                    config_state->tab_current->overriden_buffer = NULL;
                    break;
               case 'l':
                    config_state->line_number_type++;
                    config_state->line_number_type %= (LNT_RELATIVE_AND_ABSOLUTE + 1);
                    break;
               }

               if(handled_key) keys_free(&config_state->vim_state.command_head);
          } break;
          case 'm':
          {
               if(!isprint(key)) break;

               handled_key = true;

               if(key == '?'){
                    update_mark_list_buffer(config_state, buffer);

                    if(config_state->tab_current->view_overrideable){
                         tab_view_save_overrideable(config_state->tab_current);
                         config_state->tab_current->view_overrideable->buffer = &config_state->mark_list_buffer;
                         config_state->tab_current->view_overrideable->top_row = 0;
                         config_state->tab_current->view_overrideable->cursor = (Point_t){0, 1};
                    }else{
                         config_state->buffer_before_query = config_state->tab_current->view_current->buffer;
                         config_state->tab_current->view_current->buffer->cursor = *cursor;
                         config_state->tab_current->view_current->buffer = &config_state->mark_list_buffer;
                         config_state->tab_current->view_current->top_row = 0;
                         config_state->tab_current->view_current->cursor = (Point_t){0, 1};
                    }
               }else{
                    char mark = key;
                    add_mark(buffer_state, mark, cursor);
               }
          } break;
          case '"':
               if(!isprint(key)) break;

               if(key == '?'){
                    update_yank_list_buffer(config_state);

                    if(config_state->tab_current->view_overrideable){
                         tab_view_save_overrideable(config_state->tab_current);
                         config_state->tab_current->view_overrideable->buffer = &config_state->yank_list_buffer;
                         config_state->tab_current->view_overrideable->top_row = 0;
                         config_state->tab_current->view_overrideable->cursor = (Point_t){0, 1};
                    }else{
                         config_state->buffer_before_query = config_state->tab_current->view_current->buffer;
                         config_state->tab_current->view_current->buffer->cursor = *cursor;
                         config_state->tab_current->view_current->buffer = &config_state->yank_list_buffer;
                         config_state->tab_current->view_current->top_row = 0;
                         config_state->tab_current->view_current->cursor = (Point_t){0, 1};
                    }

                    handled_key = true;
               }
               break;
          case 'y':
               if(!isprint(key)) break;

               if(key == '?'){
                    update_yank_list_buffer(config_state);

                    if(config_state->tab_current->view_overrideable){
                         tab_view_save_overrideable(config_state->tab_current);
                         config_state->tab_current->view_overrideable->buffer = &config_state->yank_list_buffer;
                         config_state->tab_current->view_overrideable->top_row = 0;
                         config_state->tab_current->view_overrideable->cursor = (Point_t){0, 1};
                    }else{
                         config_state->buffer_before_query = config_state->tab_current->view_current->buffer;
                         config_state->tab_current->view_current->buffer->cursor = *cursor;
                         config_state->tab_current->view_current->buffer = &config_state->yank_list_buffer;
                         config_state->tab_current->view_current->top_row = 0;
                         config_state->tab_current->view_current->cursor = (Point_t){0, 1};
                    }

                    handled_key = true;
               }
               break;
          case '\'':
          {
               handled_key = true;
               Point_t* marked_location;
               char mark = key;
               marked_location = find_mark(buffer_state, mark);
               if(marked_location) {
                    cursor->y = marked_location->y;
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
               break;
          case 'q':
               if(key == '?'){
                    update_macro_list_buffer(config_state);

                    if(config_state->tab_current->view_overrideable){
                         tab_view_save_overrideable(config_state->tab_current);
                         config_state->tab_current->view_overrideable->buffer = &config_state->macro_list_buffer;
                         config_state->tab_current->view_overrideable->top_row = 0;
                         config_state->tab_current->view_overrideable->cursor = (Point_t){0, 1};
                    }else{
                         config_state->buffer_before_query = config_state->tab_current->view_current->buffer;
                         config_state->tab_current->view_current->buffer->cursor = *cursor;
                         config_state->tab_current->view_current->buffer = &config_state->macro_list_buffer;
                         config_state->tab_current->view_current->top_row = 0;
                         config_state->tab_current->view_current->cursor = (Point_t){0, 1};
                    }

                    handled_key = true;
               }
               break;
#if 0 // useful for debugging commit history
          case '!':
               ce_commits_dump(buffer_state->commit_tail);
               break;
#endif
          case '@':
          {
               if(key == '?'){
                    update_macro_list_buffer(config_state);

                    if(config_state->tab_current->view_overrideable){
                         tab_view_save_overrideable(config_state->tab_current);
                         config_state->tab_current->view_overrideable->buffer = &config_state->macro_list_buffer;
                         config_state->tab_current->view_overrideable->top_row = 0;
                         config_state->tab_current->view_overrideable->cursor = (Point_t){0, 1};
                    }else{
                         config_state->buffer_before_query = config_state->tab_current->view_current->buffer;
                         config_state->tab_current->view_current->buffer->cursor = *cursor;
                         config_state->tab_current->view_current->buffer = &config_state->macro_list_buffer;
                         config_state->tab_current->view_current->top_row = 0;
                         config_state->tab_current->view_current->cursor = (Point_t){0, 1};
                    }

                    handled_key = true;
               }
          } break;
          }
     }

     if(!handled_key){
          VimKeyHandlerResult_t vkh_result = vim_key_handler(key, &config_state->vim_state, config_state->tab_current->view_current,
                                                             &config_state->auto_complete, false);
          if(vkh_result.type == VKH_HANDLED_KEY){
               if(config_state->vim_state.mode == VM_INSERT && config_state->input && config_state->input_key == 6){
                    calc_auto_complete_start_and_path(&config_state->auto_complete,
                                                      buffer->lines[cursor->y],
                                                      *cursor,
                                                      config_state->completion_buffer);
               }
          }else if(vkh_result.type == VKH_COMPLETED_ACTION){
               if(vkh_result.completed_action.change.type == VCT_DELETE && config_state->tab_current->view_current->buffer == &config_state->buffer_list_buffer){
                    VimActionRange_t action_range;
                    if(vim_action_get_range(&vkh_result.completed_action, buffer, cursor, &config_state->vim_state.find_state,
                                             &config_state->vim_state.visual_start, &action_range)){
                         int64_t delete_index = action_range.sorted_start->y - 1;
                         int64_t buffers_to_delete = (action_range.sorted_end->y - action_range.sorted_start->y) + 1;
                         for(int64_t b = 0; b < buffers_to_delete; ++b){
                              if(!delete_buffer_at_index(head, config_state->tab_head, delete_index)){
                                   return false; // quit !
                              }
                         }

                         update_buffer_list_buffer(config_state, *head);

                         if(cursor->y >= config_state->buffer_list_buffer.line_count) cursor->y = config_state->buffer_list_buffer.line_count - 1;
                         vim_enter_normal_mode(&config_state->vim_state);
                         keys_free(&config_state->vim_state.command_head);
                    }
               }
          }else if(vkh_result.type == VKH_UNHANDLED_KEY){
               switch(key){
               case KEY_MOUSE:
                    handle_mouse_event(config_state, buffer, buffer_view, cursor);
                    break;
               case KEY_DC:
                    // TODO: with our current insert mode undo implementation we can't support this
                    // ce_remove_char(buffer, cursor);
                    break;
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
                              if(buffer->line_count && buffer->lines[cursor->y][0]) cursor->x++;
                              vim_enter_normal_mode(&config_state->vim_state);
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
                              if(buffer->line_count && buffer->lines[cursor->y][0]) cursor->x++;
                              vim_enter_normal_mode(&config_state->vim_state);
                         }
                    }
                    break;
               case 25: // Ctrl + y
                    confirm_action(config_state, *head);
                    keys_free(&config_state->vim_state.command_head);
                    break;
               }

               if(config_state->vim_state.mode != VM_INSERT){
                    switch(key){
                    default:
                         break;
                    case '.':
                    {
                         vim_action_apply(&config_state->vim_state.last_action, buffer_view, cursor, &config_state->vim_state,
                                          &config_state->auto_complete);

                         if(config_state->vim_state.mode != VM_INSERT || !config_state->vim_state.last_insert_command ||
                            config_state->vim_state.last_action.change.type == VCT_PLAY_MACRO) break;

                         vim_enter_insert_mode(&config_state->vim_state, config_state->tab_current->view_current);

                         int* cmd_itr = config_state->vim_state.last_insert_command;
                         while(*cmd_itr){
                              vim_key_handler(*cmd_itr, &config_state->vim_state, config_state->tab_current->view_current,
                                              &config_state->auto_complete, true);
                              cmd_itr++;
                         }

                         vim_enter_normal_mode(&config_state->vim_state);
                         keys_free(&config_state->vim_state.command_head);
                         if(buffer_state->commit_tail) buffer_state->commit_tail->commit.chain = BCC_STOP;
                    } break;
                    case KEY_ESCAPE:
                    {
                         if(config_state->input){
                              input_cancel(config_state);
                         }
                    } break;
                    case KEY_SAVE:
                         ce_save_buffer(buffer, buffer->filename);
                         break;
                    case 7: // Ctrl + g
                    {
                         split_view(config_state->tab_current->view_head, config_state->tab_current->view_current, false, config_state->line_number_type);
                    } break;
                    case 22: // Ctrl + v
                    {
                         split_view(config_state->tab_current->view_head, config_state->tab_current->view_current, true, config_state->line_number_type);
                    } break;
                    case KEY_CLOSE: // Ctrl + q
                    {
                         if(config_state->input){
                              input_cancel(config_state);
                              break;
                         }

                         // try to quit if there is nothing left to do!
                         if(config_state->tab_current == config_state->tab_head &&
                            config_state->tab_current->next == NULL &&
                            config_state->tab_current->view_current == config_state->tab_current->view_head &&
                            config_state->tab_current->view_current->next_horizontal == NULL &&
                            config_state->tab_current->view_current->next_vertical == NULL ){
                              uint64_t unsaved_buffers = 0;
                              BufferNode_t* itr = *head;
                              while(itr){
                                   if(!itr->buffer->readonly && itr->buffer->modified) unsaved_buffers++;
                                   itr = itr->next;
                              }

                              if(unsaved_buffers){
                                   input_start(config_state, "Unsaved buffers... Quit anyway? (y/n)", key);
                                   break;
                              }

                              return false;
                         }

                         Point_t save_cursor_on_terminal = get_cursor_on_terminal(cursor, buffer_view, config_state->line_number_type);
                         config_state->tab_current->view_current->buffer->cursor = config_state->tab_current->view_current->cursor;

                         if(ce_remove_view(&config_state->tab_current->view_head, config_state->tab_current->view_current)){
                              if(config_state->tab_current->view_current == config_state->tab_current->view_overrideable){
                                   config_state->tab_current->view_overrideable = NULL;
                              }

                              // if head is NULL, then we have removed the view head, and there were no other views, head is NULL
                              if(!config_state->tab_current->view_head){
                                   if(config_state->tab_current->next){
                                        config_state->tab_current->next = config_state->tab_current->next;
                                        TabView_t* tmp = config_state->tab_current;
                                        config_state->tab_current = config_state->tab_current->next;
                                        tab_view_remove(&config_state->tab_head, tmp);
                                        break;
                                   }else{

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
                              // try to find a better place to put the cursor to start
                              BufferNode_t* itr = *head;
                              int64_t buffer_index = 1;
                              bool found_good_buffer = false;
                              while(itr){
                                   if(!itr->buffer->readonly && !ce_buffer_in_view(config_state->tab_current->view_head, itr->buffer)){
                                        config_state->tab_current->view_current->cursor.y = buffer_index;
                                        found_good_buffer = true;
                                        break;
                                   }
                                   itr = itr->next;
                                   buffer_index++;
                              }

                              update_buffer_list_buffer(config_state, *head);
                              config_state->tab_current->view_current->buffer->cursor = *cursor;
                              config_state->tab_current->view_current->buffer = &config_state->buffer_list_buffer;
                              config_state->tab_current->view_current->top_row = 0;
                              config_state->tab_current->view_current->cursor = (Point_t){0, found_good_buffer ? buffer_index : 1};

                         }
                         break;
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
                         if(ce_get_char(buffer, *cursor, &c) && ce_remove_char(buffer, *cursor)){
                              ce_commit_remove_char(&buffer_state->commit_tail, *cursor, *cursor, *cursor, c, BCC_STOP);
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
                    case 'H':
                    {
                         // move cursor to top line of view
                         Point_t location = {cursor->x, buffer_view->top_row};
                         ce_set_cursor(buffer, cursor, location);
                    } break;
                    case 'M':
                    {
                         // move cursor to middle line of view
                         int64_t view_height = buffer_view->bottom_right.y - buffer_view->top_left.y;
                         Point_t location = {cursor->x, buffer_view->top_row + (view_height/2)};
                         ce_set_cursor(buffer, cursor, location);
                    } break;
                    case 'L':
                    {
                         // move cursor to bottom line of view
                         int64_t view_height = buffer_view->bottom_right.y - buffer_view->top_left.y;
                         Point_t location = {cursor->x, buffer_view->top_row + view_height};
                         ce_set_cursor(buffer, cursor, location);
                    } break;
                    case 'z':
                    break;
                    case KEY_NPAGE:
                    {
                         half_page_up(config_state->tab_current->view_current);
                    } break;
                    case KEY_PPAGE:
                    {
                         half_page_down(config_state->tab_current->view_current);
                    } break;
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
                         add_yank(&config_state->vim_state.yank_head, '/', search_str, YANK_NORMAL);
                         config_state->vim_state.search_direction = CE_UP;
                         goto search;
                    } break;
                    case '*':
                    {
                         if(!buffer->lines || !buffer->lines[cursor->y]) break;

                         Point_t word_start, word_end;
                         if(!ce_get_word_at_location(buffer, *cursor, &word_start, &word_end)) break;
                         char* search_str = ce_dupe_string(buffer, word_start, word_end);
                         add_yank(&config_state->vim_state.yank_head, '/', search_str, YANK_NORMAL);
                         config_state->vim_state.search_direction = CE_DOWN;
                         goto search;
                    } break;
                    case '/':
                    {
                         input_start(config_state, "Search", key);
                         config_state->vim_state.search_direction = CE_DOWN;
                         config_state->vim_state.start_search = *cursor;
                         break;
                    }
                    case '?':
                    {
                         input_start(config_state, "Reverse Search", key);
                         config_state->vim_state.search_direction = CE_UP;
                         config_state->vim_state.start_search = *cursor;
                         break;
                    }
                    case 'n':
          search:
                    {
                         YankNode_t* yank = find_yank(config_state->vim_state.yank_head, '/');
                         if(yank){
                              assert(yank->mode == YANK_NORMAL);
                              Point_t match;
                              if(ce_find_string(buffer, *cursor, yank->text, &match, config_state->vim_state.search_direction)){
                                   ce_set_cursor(buffer, cursor, match);
                                   center_view(config_state->tab_current->view_current);
                              }
                         }
                    } break;
                    case 'N':
                    {
                         YankNode_t* yank = find_yank(config_state->vim_state.yank_head, '/');
                         if(yank){
                              assert(yank->mode == YANK_NORMAL);
                              Point_t match;
                              if(ce_find_string(buffer, *cursor, yank->text, &match, ce_reverse_direction(config_state->vim_state.search_direction))){
                                   ce_set_cursor(buffer, cursor, match);
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
#endif
                    } break;
                    case 24: // Ctrl + x
                    {
                         input_start(config_state, "Shell Command", key);
                    } break;
                    case 14: // Ctrl + n
                         if(config_state->input) break;

                         jump_to_next_shell_command_file_destination(*head, config_state, true);
                         break;
                    case 16: // Ctrl + p
                         if(config_state->input) break;

                         jump_to_next_shell_command_file_destination(*head, config_state, false);
                         break;
                    case 6: // Ctrl + f
                    {
                         input_start(config_state, "Load File", key);
                         calc_auto_complete_start_and_path(&config_state->auto_complete,
                                                           config_state->view_input->buffer->lines[0],
                                                           *cursor,
                                                           config_state->completion_buffer);
                         if(config_state->tab_current->view_overrideable){
                              tab_view_save_overrideable(config_state->tab_current);

                              config_state->tab_current->view_overrideable->buffer = config_state->completion_buffer;
                              config_state->tab_current->view_overrideable->cursor = (Point_t){0, 0};
                              center_view(config_state->tab_current->view_overrideable);
                         }else{
                              config_state->buffer_before_query = config_state->tab_current->view_input_save->buffer;
                              config_state->buffer_before_query->cursor = config_state->tab_current->view_input_save->cursor;
                              config_state->tab_current->view_input_save->buffer = config_state->completion_buffer;
                              config_state->tab_current->view_input_save->cursor = (Point_t){0, 0};
                              config_state->tab_current->view_input_save->top_row = 0;
                         }
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
                         if(config_state->vim_state.mode == VM_VISUAL_RANGE || config_state->vim_state.mode == VM_VISUAL_LINE){
                              input_start(config_state, "Visual Replace", key);
                         }else{
                              input_start(config_state, "Replace", key);
                         }
                    break;
                    case 5: // Ctrl + e
                    {
                         Buffer_t* new_buffer = new_buffer_from_string(*head, "unnamed", NULL);
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
                    }
               }
          }
     }

     // incremental search
     if(config_state->input && (config_state->input_key == '/' || config_state->input_key == '?')){
          if(config_state->view_input->buffer->lines == NULL){
               pthread_mutex_lock(&view_input_save_lock);
               config_state->tab_current->view_input_save->cursor = config_state->vim_state.start_search;
               pthread_mutex_unlock(&view_input_save_lock);
          }else{
               const char* search_str = config_state->view_input->buffer->lines[0];
               Point_t match = {};
               if(search_str[0] &&
                  ce_find_string(config_state->tab_current->view_input_save->buffer,
                                 config_state->vim_state.start_search, search_str, &match,
                                 config_state->vim_state.search_direction)){
                    pthread_mutex_lock(&view_input_save_lock);
                    ce_set_cursor(config_state->tab_current->view_input_save->buffer,
                                  &config_state->tab_current->view_input_save->cursor, match);
                    pthread_mutex_unlock(&view_input_save_lock);
                    center_view(config_state->tab_current->view_input_save);
               }else{
                    pthread_mutex_lock(&view_input_save_lock);
                    config_state->tab_current->view_input_save->cursor = config_state->vim_state.start_search;
                    pthread_mutex_unlock(&view_input_save_lock);
                    center_view(config_state->tab_current->view_input_save);
               }
          }
     }

     if(config_state->vim_state.mode != VM_INSERT){
          auto_complete_end(&config_state->auto_complete);
     }

     if(config_state->tab_current->view_overrideable && buffer_view->buffer == &config_state->mark_list_buffer){
          int64_t line = cursor->y - 1; // account for buffer list row header
          if(line >= 0){
               MarkNode_t* itr = ((BufferState_t*)(config_state->buffer_before_query->user_data))->mark_head;

               while(line > 0){
                    itr = itr->next;
                    if(!itr) break;
                    line--;
               }

               if(itr) {
                    config_state->tab_current->view_overrideable->buffer = config_state->buffer_before_query;
                    config_state->tab_current->view_overrideable->cursor.y = itr->location.y;
                    center_view(config_state->tab_current->view_overrideable);
               }
          }
     }

     if(config_state->quit) return false;

     config_state->last_key = key;
     return true;
}

void draw_view_statuses(BufferView_t* view, BufferView_t* current_view, BufferView_t* overrideable_view, VimMode_t vim_mode, int last_key,
                        char recording_macro)
{
     Buffer_t* buffer = view->buffer;
     if(view->next_horizontal) draw_view_statuses(view->next_horizontal, current_view, overrideable_view, vim_mode, last_key, recording_macro);
     if(view->next_vertical) draw_view_statuses(view->next_vertical, current_view, overrideable_view, vim_mode, last_key, recording_macro);

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
     if(view == overrideable_view) printw("^ ");
     if(view == current_view && recording_macro) printw("RECORDING %c ", recording_macro);
     int64_t row = view->cursor.y + 1;
     int64_t column = view->cursor.x + 1;
     int64_t digits_in_line = count_digits(row);
     digits_in_line += count_digits(column);
     mvprintw(view->bottom_right.y, (view->bottom_right.x - (digits_in_line + 5)), " %"PRId64", %"PRId64" ", column, row);
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

     if(config_state->tab_current->view_current->buffer != &config_state->mark_list_buffer &&
        ce_buffer_in_view(config_state->tab_current->view_head, &config_state->mark_list_buffer)){
          update_mark_list_buffer(config_state, buffer);
     }

     if(ce_buffer_in_view(config_state->tab_current->view_head, &config_state->yank_list_buffer)){
          update_yank_list_buffer(config_state);
     }

     if(ce_buffer_in_view(config_state->tab_current->view_head, &config_state->macro_list_buffer)){
          update_macro_list_buffer(config_state);
     }

     int64_t input_view_height = 0;
     Point_t input_top_left = {};
     Point_t input_bottom_right = {};
     if(config_state->input){
          input_view_height = config_state->view_input->buffer->line_count;
          if(input_view_height) input_view_height--;
          pthread_mutex_lock(&view_input_save_lock);
          input_top_left = (Point_t){config_state->tab_current->view_input_save->top_left.x,
                                   (config_state->tab_current->view_input_save->bottom_right.y - input_view_height) - 1};
          input_bottom_right = config_state->tab_current->view_input_save->bottom_right;
          pthread_mutex_unlock(&view_input_save_lock);
          if(input_top_left.y < 1) input_top_left.y = 1; // clamp to growing to 1, account for input message
          if(input_bottom_right.y == g_terminal_dimensions->y - 2){
               input_top_left.y++;
               input_bottom_right.y++; // account for bottom status bar
          }
          ce_calc_views(config_state->view_input, input_top_left, input_bottom_right);
          pthread_mutex_lock(&view_input_save_lock);
          config_state->tab_current->view_input_save->bottom_right.y = input_top_left.y - 1;
          pthread_mutex_unlock(&view_input_save_lock);
     }

     view_follow_cursor(buffer_view, config_state->line_number_type);

     // setup highlight
     if(config_state->vim_state.mode == VM_VISUAL_RANGE){
          const Point_t* start = &config_state->vim_state.visual_start;
          const Point_t* end = &config_state->tab_current->view_current->cursor;

          ce_sort_points(&start, &end);

          buffer->highlight_start = *start;
          buffer->highlight_end = *end;
     }else if(config_state->vim_state.mode == VM_VISUAL_LINE){
          int64_t start_line = config_state->vim_state.visual_start.y;
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
          YankNode_t* yank = find_yank(config_state->vim_state.yank_head, '/');
          if(yank) search = yank->text;
     }

     // NOTE: always draw from the head
     ce_draw_views(config_state->tab_current->view_head, search, config_state->line_number_type);

     draw_view_statuses(config_state->tab_current->view_head, config_state->tab_current->view_current,
                        config_state->tab_current->view_overrideable, config_state->vim_state.mode, config_state->last_key,
                        config_state->vim_state.recording_macro);

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

          ce_draw_views(config_state->view_input, NULL, LNT_NONE);
          draw_view_statuses(config_state->view_input, config_state->tab_current->view_current,
                             NULL, config_state->vim_state.mode, config_state->last_key,
                             config_state->vim_state.recording_macro);
     }

     // draw auto complete
     // TODO: don't draw over borders!
     Point_t terminal_cursor = get_cursor_on_terminal(cursor, buffer_view,
                                                      buffer_view == config_state->view_input ? LNT_NONE :
                                                                                                config_state->line_number_type);
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

     // reset the cursor
     move(terminal_cursor.y, terminal_cursor.x);

     // update the screen with what we drew
     refresh();

     pthread_mutex_unlock(&shell_buffer_lock);
     pthread_mutex_unlock(&draw_lock);
}
