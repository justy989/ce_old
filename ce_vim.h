#ifndef CE_VIM_H
#define CE_VIM_H

// ce's vimplementation: obviously only a subset of vim's features + some customization

#include "ce.h"

#define VIM_C_COMMENT_STRING "//"
#define VIM_PYTHON_COMMENT_STRING "#"
#define VIM_CONFIG_COMMENT_STRING "#"
#define TAB_STRING "     "

typedef enum{
     VM_NORMAL,
     VM_INSERT,
     VM_VISUAL_RANGE,
     VM_VISUAL_LINE,
     // TODO: VM_VISUAL_BLOCK,
} VimMode_t;

typedef enum{
     VCT_NONE,
     VCT_MOTION,
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
     VCT_SET_MARK,
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
     VMT_LINE_SOFT,
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
     VMT_SEARCH_WORD_UNDER_CURSOR,
     VMT_SEARCH,
     VMT_MATCHING_PAIR,
     VMT_NEXT_BLANK_LINE,
     VMT_PREV_BLANK_LINE,
     VMT_GOTO_MARK,
} VimMotionType_t;

typedef struct{
     VimMotionType_t type;
     int32_t multiplier;
     union{
          char reg;
          char match_char;
          char inside_pair;
          char around_pair;
          int64_t visual_length;
          int64_t visual_lines;
          Direction_t search_direction;
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
} VimFindCharState_t;

typedef enum{
     VKH_UNHANDLED_KEY,
     VKH_HANDLED_KEY,
     VKH_COMPLETED_ACTION_FAILURE,
     VKH_COMPLETED_ACTION_SUCCESS,
} VimKeyHandlerResultType_t;

typedef struct{
     VimKeyHandlerResultType_t type;
     VimAction_t completed_action;
} VimKeyHandlerResult_t;

typedef struct{
     Direction_t direction;
     Point_t start;
     regex_t regex;
     bool valid_regex;
} VimSearch_t;


// marks
typedef struct VimMarkNode_t{
     char reg_char;
     Point_t location;
     struct VimMarkNode_t* next;
} VimMarkNode_t;

Point_t* vim_mark_find(VimMarkNode_t* mark_head, char mark_char);
void vim_mark_add(VimMarkNode_t** head, char mark_char, const Point_t* location);
void vim_marks_free(VimMarkNode_t** head);


// yanks
typedef enum{
     YANK_NORMAL,
     YANK_LINE,
} VimYankMode_t;

typedef struct VimYankNode_t{
     char reg_char;
     const char* text;
     VimYankMode_t mode;
     struct VimYankNode_t* next;
} VimYankNode_t;

VimYankNode_t* vim_yank_find(VimYankNode_t* head, char reg_char);
void vim_yank_add(VimYankNode_t** head, char reg_char, const char* yank_text, VimYankMode_t mode);
void vim_yanks_free(VimYankNode_t** head);


// macros
typedef struct VimMacroNode_t{
     char reg;
     int* command;
     struct VimMacroNode_t* next;
} VimMacroNode_t;

VimMacroNode_t* vim_macro_find(VimMacroNode_t* head, char reg);
void vim_macro_add(VimMacroNode_t** head, char reg, int* command);
void vim_macros_free(VimMacroNode_t** head);

typedef struct VimMacroCommitNode_t{
     KeyNode_t* command_begin;
     KeyNode_t* command_copy;
     bool chain;
     struct VimMacroCommitNode_t* next;
     struct VimMacroCommitNode_t* prev;
} VimMacroCommitNode_t;

void vim_macro_commits_free(VimMacroCommitNode_t** macro_commit);
void vim_macro_commits_init(VimMacroCommitNode_t** macro_commit);
void vim_macro_commit_push(VimMacroCommitNode_t** macro_commit, KeyNode_t* last_command_begin, bool chain);
void vim_macro_commits_dump(const VimMacroCommitNode_t* macro_commit);


// vim state
typedef struct{
     VimMode_t mode;

     KeyNode_t* command_head;

     VimAction_t last_action;
     int* last_insert_command;

     char recording_macro; // holds the register we are recording, is 0 if we aren't recording
     char playing_macro;
     KeyNode_t* record_macro_head;
     KeyNode_t* last_macro_command_begin;
     VimMacroCommitNode_t* macro_commit_current;
     VimMacroNode_t* macro_head;
     BufferCommitNode_t* record_start_commit_tail;

     VimYankNode_t* yank_head;

     VimFindCharState_t find_char_state;

     Point_t visual_start;
     Point_t insert_start;

     VimSearch_t search;
} VimState_t;

// used to track info per buffer
typedef struct{
     VimMarkNode_t* mark_head;
     int64_t cursor_save_column;
} VimBufferState_t;

typedef struct {
     Point_t start;
     Point_t end;
     const Point_t* sorted_start;
     const Point_t* sorted_end;
     VimYankMode_t yank_mode;
} VimActionRange_t;

VimKeyHandlerResult_t vim_key_handler(int key, VimState_t* vim_state, Buffer_t* buffer, Point_t* cursor,
                                      BufferCommitNode_t** commit_tail, VimBufferState_t* vim_buffer_state,
                                      bool repeating);

VimCommandState_t vim_action_from_string(const int* string, VimAction_t* action, VimMode_t vim_mode,
                                         Buffer_t* buffer, Point_t* cursor, Point_t* visual_start,
                                         VimFindCharState_t* find_char_in_line_state, bool recording_macro);

bool vim_action_get_range(VimAction_t* action, Buffer_t* buffer, Point_t* cursor, VimState_t* vim_state,
                          VimBufferState_t* vim_buffer_state, VimActionRange_t* action_range);

bool vim_action_apply(VimAction_t* action, Buffer_t* buffer, Point_t* cursor, VimState_t* vim_state,
                      BufferCommitNode_t** commit_tail, VimBufferState_t* vim_buffer_state);

void vim_enter_normal_mode(VimState_t* vim_state);
bool vim_enter_insert_mode(VimState_t* vim_state, Buffer_t* buffer);
void vim_enter_visual_range_mode(VimState_t* vim_state, Point_t cursor);
void vim_enter_visual_line_mode(VimState_t* vim_state, Point_t cursor);

void vim_stop_recording_macro(VimState_t* vim_state);

char* vim_command_string_to_char_string(const int* int_str);
int* vim_char_string_to_command_string(const char* char_str);

#endif
