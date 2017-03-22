#ifndef CE_H
#define CE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ncurses.h>
#include <errno.h>
#include <regex.h>
#include <dlfcn.h>

#define CE_CONFIG "ce_config.so"
#define MESSAGE_FILE "messages"

#define NEWLINE 10
#define COLOR_BACKGROUND -1
#define COLOR_FOREGROUND -1
#define COLOR_BRIGHT_BLACK 8
#define COLOR_BRIGHT_RED 9
#define COLOR_BRIGHT_GREEN 10
#define COLOR_BRIGHT_YELLOW 11
#define COLOR_BRIGHT_BLUE 12
#define COLOR_BRIGHT_MAGENTA 13
#define COLOR_BRIGHT_CYAN 14
#define COLOR_BRIGHT_WHITE 15

#define KEY_ESCAPE 27
#define KEY_TAB '\t'

#ifdef __APPLE__
#define RE_WORD_BOUNDARY_START "[[:<:]]"
#define RE_WORD_BOUNDARY_END   "[[:>:]]"
#else
#define RE_WORD_BOUNDARY_START "\\b"
#define RE_WORD_BOUNDARY_END   "\\b"
#endif

typedef enum {
     LNT_NONE,
     LNT_ABSOLUTE,
     LNT_RELATIVE,
     LNT_RELATIVE_AND_ABSOLUTE,
}LineNumberType_t;

typedef enum {
     HLT_NONE,
     HLT_TO_END_OF_TEXT,
     HLT_ENTIRE_LINE,
}HighlightLineType_t;

#define CE_MAX(a,b)\
     ({ __typeof__ (a) _a = (a); \
        __typeof__ (b) _b = (b); \
        _a > _b? _a : _b; })
#define CE_MIN(a,b) \
     ({ __typeof__ (a) _a = (a); \
        __typeof__ (b) _b = (b); \
        _a < _b? _a : _b; })

typedef struct{
     int64_t x;
     int64_t y;
}Point_t;

struct Buffer_t;

typedef enum{
     SS_INITIALIZING,
     SS_BEGINNING_OF_LINE,
     SS_CHARACTER,
     SS_END_OF_LINE,
}SyntaxState_t;

typedef struct{
     const struct Buffer_t* buffer;
     Point_t top_left;
     Point_t bottom_right;
     Point_t cursor;
     Point_t loc;
     const regex_t* highlight_regex;
     LineNumberType_t line_number_type;
     HighlightLineType_t highlight_line_type;
     SyntaxState_t state;
}SyntaxHighlighterData_t;

typedef void syntax_highlighter(SyntaxHighlighterData_t*, void*);

typedef enum{
     CE_UP = -1,
     CE_DOWN = 1
} Direction_t;

Direction_t ce_reverse_direction(Direction_t to_reverse);

typedef enum{
     BS_NONE,
     BS_MODIFIED,
     BS_READONLY,
     BS_NEW_FILE,
} BufferStatus_t;

typedef enum{
     BFT_PLAIN,
     BFT_C,
     BFT_PYTHON,
     BFT_BASH,
     BFT_CONFIG,
     BFT_DIFF,
     BFT_TERMINAL,
} BufferFileType_t;

typedef struct Buffer_t{
     char** lines; // '\0' terminated, does not contain newlines, NULL if empty
     int64_t line_count;

     BufferStatus_t status;
     BufferFileType_t type;

     Point_t cursor;

     // TODO: these are used for drawing, consider passing them as arguments?
     Point_t highlight_start;
     Point_t highlight_end;
     Point_t mark;

     union {
          char* filename;
          char* name;
     };

     void* user_data;

     syntax_highlighter* syntax_fn;
     void* syntax_user_data;

     bool absolutely_no_line_numbers_under_any_circumstances; // NOTE: I can't stop laughing
} Buffer_t;

typedef struct BufferNode_t {
     Buffer_t* buffer;
     struct BufferNode_t* next;
} BufferNode_t;

typedef enum {
     BCT_NONE,
     BCT_INSERT_CHAR,
     BCT_INSERT_STRING,
     BCT_REMOVE_CHAR,
     BCT_REMOVE_STRING,
     BCT_CHANGE_CHAR,
     BCT_CHANGE_STRING,
} BufferCommitType_t;

typedef enum {
     BCC_STOP,
     BCC_KEEP_GOING,
} BufferCommitChain_t;

typedef struct {
     BufferCommitType_t type;
     BufferCommitChain_t chain;

     Point_t start;

     Point_t undo_cursor;
     Point_t redo_cursor;

     union {
          char c;
          char* str;
     };

     union {
          char prev_c;
          char* prev_str;
     };
} BufferCommit_t;

typedef struct BufferCommitNode_t {
     BufferCommit_t commit;
     struct BufferCommitNode_t* prev;
     struct BufferCommitNode_t* next;
} BufferCommitNode_t;

// horizontal split []|[]

// vertical split
// []
// --
// []
typedef struct BufferView_t {
     Point_t cursor;

     Point_t top_left;
     Point_t bottom_right;

     int64_t top_row;
     int64_t left_column;

     Buffer_t* buffer;

     void* user_data; // NOTE: free'd by ce_free_views(), TODO: allow user to free, it's just painful to iterate over them manually

     struct BufferView_t* next_horizontal;
     struct BufferView_t* next_vertical;
} BufferView_t;

typedef enum {
     CT_NONE,
     CT_SINGLE_LINE,
     CT_BEGIN_MULTILINE,
     CT_END_MULTILINE,
} CommentType_t;

// NOTE: temporary, we probably want something like CeRC_t type of thing?
typedef enum {
     LF_DOES_NOT_EXIST,
     LF_IS_DIRECTORY,
     LF_SUCCESS,
} LoadFileResult_t;

typedef struct KeyNode_t{
     int key;
     struct KeyNode_t* next;
} KeyNode_t;

typedef enum{
     CAT_INTEGER,
     CAT_DECIMAL,
     CAT_STRING,
     CAT_COUNT
}CommandArgType_t;

typedef struct{
     CommandArgType_t type;

     union{
          int64_t integer;
          double decimal;
          char* string;
     };
}CommandArg_t;

#define COMMAND_NAME_MAX_LEN 128

typedef struct{
     char name[COMMAND_NAME_MAX_LEN];
     CommandArg_t* args;
     int64_t arg_count;
}Command_t;

extern Point_t* g_terminal_dimensions;

// CE Configuration-Defined Functions
typedef bool ce_initializer (BufferNode_t**, Point_t*, int, char**, void**);
typedef void ce_destroyer   (BufferNode_t**, void*);
typedef bool ce_key_handler (int, BufferNode_t**, void*);

typedef void ce_command (Command_t*, void*);


// BufferList Manipulation Functions
BufferNode_t* ce_append_buffer_to_list (BufferNode_t* head, Buffer_t* buffer); // NOTE: we may want to consider taking tail rather than head
bool ce_remove_buffer_from_list        (BufferNode_t** head, Buffer_t* buffer);


// BufferView Manipulation Functions
BufferView_t* ce_split_view         (BufferView_t* view, Buffer_t* buffer, bool horizontal);
bool ce_remove_view                 (BufferView_t** head, BufferView_t* view);
bool ce_calc_views                  (BufferView_t* head, Point_t top_left, Point_t top_right);
bool ce_draw_views                  (const BufferView_t* head, const regex_t* highlight_regex, LineNumberType_t line_number_type,
                                     HighlightLineType_t highlight_line_type);
bool ce_change_buffer_in_views      (BufferView_t* head, Buffer_t* match, Buffer_t* new);
bool ce_free_views                  (BufferView_t** view);
BufferView_t* ce_find_view_at_point (BufferView_t* head, Point_t point);
BufferView_t* ce_buffer_in_view     (BufferView_t* head, const Buffer_t* buffer);


// Buffer_t Manipulation Functions
// NOTE: readonly functions will modify readonly buffers, this is useful for
//       output-only buffers
void ce_free_buffer             (Buffer_t* buffer);

bool ce_alloc_lines             (Buffer_t* buffer, int64_t line_count);
void ce_clear_lines             (Buffer_t* buffer);
void ce_clear_lines_readonly    (Buffer_t* buffer);

bool ce_load_string             (Buffer_t* buffer, const char* string);
LoadFileResult_t ce_load_file   (Buffer_t* buffer, const char* filename);

bool ce_insert_char             (Buffer_t* buffer, Point_t location, char c);
bool ce_append_char             (Buffer_t* buffer, char c);
bool ce_remove_char             (Buffer_t* buffer, Point_t location);
bool ce_set_char                (Buffer_t* buffer, Point_t location, char c);
bool ce_remove_char_readonly    (Buffer_t* buffer, Point_t location);
bool ce_insert_char_readonly    (Buffer_t* buffer, Point_t location, char c);
bool ce_append_char_readonly    (Buffer_t* buffer, char c);
bool ce_set_char_readonly       (Buffer_t* buffer, Point_t location, char c);

bool ce_insert_string           (Buffer_t* buffer, Point_t location, const char* string);
bool ce_insert_string_readonly  (Buffer_t* buffer, Point_t location, const char* string);
bool ce_remove_string           (Buffer_t* buffer, Point_t location, int64_t length);
bool ce_prepend_string          (Buffer_t* buffer, int64_t line, const char* string);
bool ce_append_string           (Buffer_t* buffer, int64_t line, const char* string);
bool ce_append_string_readonly  (Buffer_t* buffer, int64_t line, const char* string);

bool ce_insert_line             (Buffer_t* buffer, int64_t line, const char* string);
bool ce_insert_line_readonly    (Buffer_t* buffer, int64_t line, const char* string);
bool ce_remove_line             (Buffer_t* buffer, int64_t line);
bool ce_append_line             (Buffer_t* buffer, const char* string);
bool ce_append_line_readonly    (Buffer_t* buffer, const char* string);
bool ce_join_line               (Buffer_t* buffer, int64_t line);

bool ce_insert_newline          (Buffer_t* buffer, int64_t line);


// Buffer Inspection Functions
bool    ce_draw_buffer              (const Buffer_t* buffer, const Point_t* cursor, const Point_t* term_top_left,
                                     const Point_t* term_bottom_right, const Point_t* buffer_top_left,
                                     const regex_t* highlight_regex, LineNumberType_t line_number_type,
                                     HighlightLineType_t highlight_line_type);
bool    ce_save_buffer              (Buffer_t* buffer, const char* filename);
bool    ce_point_on_buffer          (const Buffer_t* buffer, Point_t location);
bool    ce_get_char                 (const Buffer_t* buffer, Point_t location, char* c);
char    ce_get_char_raw             (const Buffer_t* buffer, Point_t location);
int64_t ce_compute_length           (const Buffer_t* buffer, Point_t start, Point_t end);
char*   ce_dupe_string              (const Buffer_t* buffer, Point_t start, Point_t end);
char*   ce_dupe_buffer              (const Buffer_t* buffer);
char*   ce_dupe_line                (const Buffer_t* buffer, int64_t line);
char*   ce_dupe_lines               (const Buffer_t* buffer, int64_t start_line, int64_t end_line);
int64_t ce_get_indentation_for_line (const Buffer_t* buffer, Point_t location, int64_t tab_len);


// Find Point_t Functions
bool ce_find_string              (const Buffer_t* buffer, Point_t location, const char* search_str, Point_t* match, Direction_t direction);
bool ce_find_regex               (const Buffer_t* buffer, Point_t location, const regex_t* regex, Point_t* match, int64_t* match_len, Direction_t direction);
bool ce_get_word_at_location     (const Buffer_t* buffer, Point_t location, Point_t* word_start, Point_t* word_end); // TODO: Is location necessary?
bool ce_get_homogenous_adjacents (const Buffer_t* buffer, Point_t* start, Point_t* end, int (*is_homogenous)(int));


// Cursor Movement Functions
Point_t* ce_clamp_cursor                          (const Buffer_t* buffer, Point_t* cursor);
bool     ce_advance_cursor                        (const Buffer_t* buffer, Point_t* cursor, int64_t delta);
bool     ce_move_cursor                           (const Buffer_t* buffer, Point_t* cursor, Point_t delta);
bool     ce_set_cursor                            (const Buffer_t* buffer, Point_t* cursor, Point_t location);
bool     ce_move_cursor_to_beginning_of_word      (const Buffer_t* buffer, Point_t* cursor, bool punctuation_word_boundaries);
bool     ce_move_cursor_to_end_of_word            (const Buffer_t* buffer, Point_t* cursor, bool punctuation_word_boundaries);
bool     ce_move_cursor_to_next_word              (const Buffer_t* buffer, Point_t* cursor, bool punctuation_word_boundaries);
bool     ce_move_cursor_to_end_of_line            (const Buffer_t* buffer, Point_t* cursor);
void     ce_move_cursor_to_beginning_of_line      (const Buffer_t* buffer, Point_t* cursor);
bool     ce_move_cursor_to_soft_end_of_line       (const Buffer_t* buffer, Point_t* cursor);
bool     ce_move_cursor_to_soft_beginning_of_line (const Buffer_t* buffer, Point_t* cursor);
bool     ce_move_cursor_to_end_of_file            (const Buffer_t* buffer, Point_t* cursor);
bool     ce_move_cursor_to_beginning_of_file      (const Buffer_t* buffer, Point_t* cursor);
bool     ce_move_cursor_forward_to_char           (const Buffer_t* buffer, Point_t* cursor, char c);
bool     ce_move_cursor_backward_to_char          (const Buffer_t* buffer, Point_t* cursor, char c);
bool     ce_move_cursor_to_matching_pair          (const Buffer_t* buffer, Point_t* cursor, char matchee);
bool     ce_follow_cursor                         (Point_t cursor, int64_t* left_column, int64_t* top_row, int64_t view_width, int64_t view_height,
                                                   bool at_terminal_width_edge, bool at_terminal_height_edge,
                                                   LineNumberType_t line_number_type, int64_t line_count);

// Undo/Redo Functions
bool ce_commit_insert_char   (BufferCommitNode_t** tail, Point_t start, Point_t undo_cursor, Point_t redo_cursor, char c, BufferCommitChain_t chain);
bool ce_commit_remove_char   (BufferCommitNode_t** tail, Point_t start, Point_t undo_cursor, Point_t redo_cursor, char c, BufferCommitChain_t chain);
bool ce_commit_change_char   (BufferCommitNode_t** tail, Point_t start, Point_t undo_cursor, Point_t redo_cursor, char c, char prev_c, BufferCommitChain_t chain);

bool ce_commit_insert_string (BufferCommitNode_t** tail, Point_t start, Point_t undo_cursor, Point_t redo_cursor, char* string, BufferCommitChain_t chain);
bool ce_commit_remove_string (BufferCommitNode_t** tail, Point_t start, Point_t undo_cursor, Point_t redo_cursor, char* string, BufferCommitChain_t chain);
bool ce_commit_change_string (BufferCommitNode_t** tail, Point_t start, Point_t undo_cursor, Point_t redo_cursor, char* new_string, char* prev_string, BufferCommitChain_t chain);

bool ce_commit_undo          (Buffer_t* buffer, BufferCommitNode_t** tail, Point_t* cursor);
bool ce_commit_redo          (Buffer_t* buffer, BufferCommitNode_t** tail, Point_t* cursor);
bool ce_commit_change        (BufferCommitNode_t** tail, const BufferCommit_t* change);

bool ce_commits_free         (BufferCommitNode_t* tail);
bool ce_commits_dump         (BufferCommitNode_t* tail);

// Line Numbers
int64_t ce_get_line_number_column_width(LineNumberType_t line_number_type, int64_t buffer_line_count, int64_t buffer_view_top, int64_t buffer_view_bottom);

// Logging Functions
#define ce_message(...) ({fprintf(stderr,__VA_ARGS__);\
                          fprintf(stderr,"\n");})

// Key Node
KeyNode_t* ce_keys_push(KeyNode_t** head, int key);
int* ce_keys_get_string(KeyNode_t* head);
void ce_keys_free(KeyNode_t** head);

// Commands
bool command_parse(Command_t* command, const char* string);
void command_free(Command_t* command);

// Misc. Utility Functions
int64_t ce_count_string_lines   (const char* string);
bool    ce_point_after          (Point_t a, Point_t b);
bool    ce_points_equal         (Point_t a, Point_t b);
void    ce_sort_points          (const Point_t** a, const Point_t** b);
int     ce_ispunct              (int c);
int     ce_iswordchar           (int c);
void*   ce_memrchr              (const void* s, int c, size_t n);
bool    ce_point_in_range       (Point_t p, Point_t start, Point_t end);
int64_t ce_last_index           (const char* string);
bool    ce_connect_border_lines (Point_t location);

#endif
