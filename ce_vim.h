#ifndef CE_VIM_H
#define CE_VIM_H

#include "ce.h"
#include "ce_auto_complete.h" // TODO: take out auto complete as a dependency

#define VIM_COMMENT_STRING "//"
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
     VMT_SEARCH_WORD_UNDER_CURSOR,
     VMT_SEARCH,
     VMT_MATCHING_PAIR,
     VMT_NEXT_BLANK_LINE,
     VMT_PREV_BLANK_LINE,
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
          Direction_t search_direction;
     };
     bool visual_start_after; // false means after !
     bool search_forward;
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
     VKH_COMPLETED_ACTION,
} VimKeyHandlerResultType_t;

typedef struct{
     VimKeyHandlerResultType_t type;
     VimAction_t completed_action;
} VimKeyHandlerResult_t;


// marks
typedef struct MarkNode_t{
     char reg_char;
     Point_t location;
     struct MarkNode_t* next;
} MarkNode_t;

Point_t* mark_find(MarkNode_t* mark_head, char mark_char);
void mark_add(MarkNode_t** head, char mark_char, const Point_t* location);
void marks_free(MarkNode_t** head);


// yanks
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

YankNode_t* yank_find(YankNode_t* head, char reg_char);
void yank_add(YankNode_t** head, char reg_char, const char* yank_text, YankMode_t mode);
void yanks_free(YankNode_t** head);


// macros
typedef struct MacroNode_t{
     char reg;
     int* command;
     struct MacroNode_t* next;
} MacroNode_t;

MacroNode_t* macro_find(MacroNode_t* head, char reg);
void macro_add(MacroNode_t** head, char reg, int* command);
void macros_free(MacroNode_t** head);

typedef struct MacroCommitNode_t{
     KeyNode_t* command_begin;
     KeyNode_t* command_copy;
     bool chain;
     struct MacroCommitNode_t* next;
     struct MacroCommitNode_t* prev;
} MacroCommitNode_t;

void macro_commits_free(MacroCommitNode_t** macro_commit);
void macro_commits_init(MacroCommitNode_t** macro_commit);
void macro_commit_push(MacroCommitNode_t** macro_commit, KeyNode_t* last_command_begin, bool chain);
void macro_commits_dump(const MacroCommitNode_t* macro_commit);


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
     MacroCommitNode_t* macro_commit_current;
     MacroNode_t* macro_head;
     BufferCommitNode_t* record_start_commit_tail;

     YankNode_t* yank_head;

     VimFindCharState_t find_char_state;

     Point_t visual_start;
     Point_t insert_start;

     Direction_t search_direction;
     Point_t start_search;
} VimState_t;

typedef struct {
     Point_t start;
     Point_t end;
     const Point_t* sorted_start;
     const Point_t* sorted_end;
     YankMode_t yank_mode;
} VimActionRange_t;

VimKeyHandlerResult_t vim_key_handler(int key, VimState_t* vim_state, Buffer_t* buffer, Point_t* cursor,
                                      BufferCommitNode_t** commit_tail, AutoComplete_t* auto_complete, bool repeating);

VimCommandState_t vim_action_from_string(const int* string, VimAction_t* action, VimMode_t vim_mode,
                                         Buffer_t* buffer, Point_t* cursor, Point_t* visual_start,
                                         VimFindCharState_t* find_char_in_line_state, bool recording_macro);

bool vim_action_get_range(VimAction_t* action, Buffer_t* buffer, Point_t* cursor, VimState_t* vim_state,
                          VimActionRange_t* action_range);

void vim_action_apply(VimAction_t* action, Buffer_t* buffer, Point_t* cursor, VimState_t* vim_state,
                      BufferCommitNode_t** commit_tail, AutoComplete_t* auto_complete);

void vim_enter_normal_mode(VimState_t* vim_state);
bool vim_enter_insert_mode(VimState_t* vim_state, Buffer_t* buffer);
void vim_enter_visual_range_mode(VimState_t* vim_state, Point_t cursor);
void vim_enter_visual_line_mode(VimState_t* vim_state, Point_t cursor);

void vim_stop_recording_macro(VimState_t* vim_state);

#endif
