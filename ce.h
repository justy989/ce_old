#ifndef CE_H
#define CE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ncurses.h>
#include <errno.h>

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

typedef enum {
     S_NORMAL = 1,
     S_KEYWORD,
     S_TYPE,
     S_CONTROL,
     S_COMMENT,
     S_STRING,
     S_CONSTANT,
     S_PREPROCESSOR,
     S_FILEPATH,
     S_DIFF_ADD,
     S_DIFF_REMOVE,

     S_NORMAL_HIGHLIGHTED,
     S_KEYWORD_HIGHLIGHTED,
     S_TYPE_HIGHLIGHTED,
     S_CONTROL_HIGHLIGHTED,
     S_COMMENT_HIGHLIGHTED,
     S_STRING_HIGHLIGHTED,
     S_CONSTANT_HIGHLIGHTED,
     S_PREPROCESSOR_HIGHLIGHTED,
     S_FILEPATH_HIGHLIGHTED,
     S_DIFF_ADD_HIGHLIGHTED,
     S_DIFF_REMOVE_HIGHLIGHTED,

     S_TRAILING_WHITESPACE,

     S_BORDERS,

     S_TAB_NAME,
     S_CURRENT_TAB_NAME,

     S_VIEW_STATUS,
     S_INPUT_STATUS,

     S_AUTO_COMPLETE,
} Syntax_t;

#define CE_CHECK_PTR_ARG(arg)                                                 \
     if(!arg){                                                                \
          ce_message("%s() received NULL argument %s\n", __FUNCTION__, #arg); \
          return false;                                                       \
     }

#define CE_MAX(a,b)\
     ({ __typeof__ (a) _a = (a); \
        __typeof__ (b) _b = (b); \
        _a > _b? _a : _b; })
#define CE_MIN(a,b) \
     ({ __typeof__ (a) _a = (a); \
        __typeof__ (b) _b = (b); \
        _a < _b? _a : _b; })

typedef struct {
     int64_t x;
     int64_t y;
} Point_t;

typedef enum{
     CE_UP = -1,
     CE_DOWN = 1
} Direction_t;

Direction_t ce_reverse_direction(Direction_t to_reverse);

typedef struct {
     char** lines; // '\0' terminated, does not contain newlines, NULL if empty
     int64_t line_count;
     Point_t cursor;
     Point_t highlight_start;
     Point_t highlight_end;
     bool modified;
     bool readonly;
     uint16_t network_id; // TODO: move this into user data as a NetworkId_t
     union {
          char* filename;
          char* name;
     };
     void* user_data;
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

typedef struct {
     BufferCommitType_t type;
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
     struct BufferView_t* next_horizontal;
     struct BufferView_t* next_vertical;
} BufferView_t;

typedef enum {
     CT_NONE,
     CT_SINGLE_LINE,
     CT_BEGIN_MULTILINE,
     CT_END_MULTILINE,
} CommentType_t;

extern Point_t* g_terminal_dimensions;

// CE Configuration-Defined Functions
typedef bool ce_initializer (bool, bool, BufferNode_t*, Point_t*, int, char**, void**);
typedef void ce_destroyer   (BufferNode_t*, void*);
typedef bool ce_key_handler (int, BufferNode_t*, void*);
typedef void ce_view_drawer (const BufferNode_t*, void*);


// BufferList Manipulation Functions
BufferNode_t* ce_append_buffer_to_list (BufferNode_t* head, Buffer_t* buffer); // NOTE: we may want to consider taking tail rather than head
bool ce_remove_buffer_from_list        (BufferNode_t* head, BufferNode_t** node);


// BufferView Manipulation Functions
BufferView_t* ce_split_view       (BufferView_t* view, Buffer_t* buffer, bool horizontal);
bool ce_remove_view               (BufferView_t** head, BufferView_t* view);
bool ce_calc_views                (BufferView_t* head, const Point_t* top_left, const Point_t* top_right);
bool ce_draw_views                (const BufferView_t* head, const char* highlight_word);
bool ce_free_views                (BufferView_t** view);
BufferView_t* ce_find_view_at_point (BufferView_t* head, const Point_t* point);
BufferView_t* ce_buffer_in_view(BufferView_t* head, const Buffer_t* buffer);


// Buffer_t Manipulation Functions
// NOTE: readonly functions will modify readonly buffers, this is useful for
//       output-only buffers
void ce_free_buffer             (Buffer_t* buffer);

bool ce_alloc_lines             (Buffer_t* buffer, int64_t line_count);
void ce_clear_lines             (Buffer_t* buffer);
void ce_clear_lines_readonly    (Buffer_t* buffer);

bool ce_load_string             (Buffer_t* buffer, const char* string);
bool ce_load_file               (Buffer_t* buffer, const char* filename);

bool ce_insert_char             (Buffer_t* buffer, const Point_t* location, char c);
bool ce_append_char             (Buffer_t* buffer, char c);
bool ce_remove_char             (Buffer_t* buffer, const Point_t* location);
bool ce_set_char                (Buffer_t* buffer, const Point_t* location, char c);
bool ce_insert_char_readonly    (Buffer_t* buffer, const Point_t* location, char c);
bool ce_append_char_readonly    (Buffer_t* buffer, char c);

bool ce_insert_string           (Buffer_t* buffer, const Point_t* location, const char* string);
bool ce_insert_string_readonly  (Buffer_t* buffer, const Point_t* location, const char* string);
bool ce_remove_string           (Buffer_t* buffer, const Point_t* location, int64_t length);
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
bool    ce_draw_buffer                   (const Buffer_t* buffer, const Point_t* cursor,const Point_t* term_top_left,
                                          const Point_t* term_bottom_right, const Point_t* buffer_top_left,
                                          const char* highlight_word);
bool    ce_save_buffer                   (Buffer_t* buffer, const char* filename);
bool    ce_point_on_buffer               (const Buffer_t* buffer, const Point_t* location);
bool    ce_get_char                      (const Buffer_t* buffer, const Point_t* location, char* c);
char    ce_get_char_raw                  (const Buffer_t* buffer, const Point_t* location);
int64_t ce_compute_length                (const Buffer_t* buffer, const Point_t* start, const Point_t* end);
char*   ce_dupe_string                   (const Buffer_t* buffer, const Point_t* start, const Point_t* end);
char*   ce_dupe_buffer                   (const Buffer_t* buffer);
char*   ce_dupe_line                     (const Buffer_t* buffer, int64_t line);
char*   ce_dupe_lines                    (const Buffer_t* buffer, int64_t start_line, int64_t end_line);
int64_t ce_get_indentation_for_next_line (const Buffer_t* buffer, const Point_t* location, int64_t tab_len);


// Find Delta Functions
int64_t ce_find_delta_to_soft_end_of_line       (const Buffer_t* buffer, const Point_t* location);
int64_t ce_find_delta_to_soft_beginning_of_line (const Buffer_t* buffer, const Point_t* location);
int64_t ce_find_delta_to_char_forward_in_line   (const Buffer_t* buffer, const Point_t* location, char c);
int64_t ce_find_delta_to_char_backward_in_line  (const Buffer_t* buffer, const Point_t* location, char c);
int64_t ce_find_delta_to_end_of_word            (const Buffer_t* buffer, const Point_t* location, bool punctuation_word_boundaries);
int64_t ce_find_delta_to_next_word              (const Buffer_t* buffer, const Point_t* location, bool punctuation_word_boundaries);
bool    ce_find_delta_to_match                  (const Buffer_t* buffer, const Point_t* location, Point_t* delta);

// Find Point_t Functions
bool ce_find_match               (const Buffer_t* buffer, const Point_t* location, Point_t* delta);
bool ce_find_string              (const Buffer_t* buffer, const Point_t* location, const char* search_str, Point_t* match, Direction_t direction);
bool ce_get_word_at_location     (const Buffer_t* buffer, const Point_t* location, Point_t* word_start, Point_t* word_end); // TODO: Is location necessary?
bool ce_get_homogenous_adjacents (const Buffer_t* buffer, Point_t* start, Point_t* end, int (*is_homogenous)(int));


// Cursor Movement Functions
Point_t* ce_clamp_cursor                          (const Buffer_t* buffer, Point_t* cursor);
bool     ce_advance_cursor                        (const Buffer_t* buffer, Point_t* cursor, int64_t delta);
bool     ce_move_cursor                           (const Buffer_t* buffer, Point_t* cursor, Point_t delta);
bool     ce_set_cursor                            (const Buffer_t* buffer, Point_t* cursor, const Point_t* location);
bool     ce_move_cursor_to_beginning_of_word      (const Buffer_t* buffer, Point_t* cursor, bool punctuation_word_boundaries);
bool     ce_move_cursor_to_end_of_line            (const Buffer_t* buffer, Point_t* cursor);
void     ce_move_cursor_to_beginning_of_line      (const Buffer_t* buffer, Point_t* cursor);
bool     ce_move_cursor_to_soft_end_of_line       (const Buffer_t* buffer, Point_t* cursor);
bool     ce_move_cursor_to_soft_beginning_of_line (const Buffer_t* buffer, Point_t* cursor);
bool     ce_move_cursor_to_end_of_file            (const Buffer_t* buffer, Point_t* cursor);
bool     ce_move_cursor_to_beginning_of_file      (const Buffer_t* buffer, Point_t* cursor);
bool     ce_follow_cursor                         (const Point_t* cursor, int64_t* left_column, int64_t* top_row, int64_t view_width, int64_t view_height,
                                                   bool at_terminal_width_edge, bool at_terminal_height_edge);

// Undo/Redo Functions
bool ce_commit_insert_char   (BufferCommitNode_t** tail, const Point_t* start, const Point_t* undo_cursor, const Point_t* redo_cursor, char c);
bool ce_commit_remove_char   (BufferCommitNode_t** tail, const Point_t* start, const Point_t* undo_cursor, const Point_t* redo_cursor, char c);
bool ce_commit_change_char   (BufferCommitNode_t** tail, const Point_t* start, const Point_t* undo_cursor, const Point_t* redo_cursor, char c, char prev_c);

bool ce_commit_insert_string (BufferCommitNode_t** tail, const Point_t* start, const Point_t* undo_cursor, const Point_t* redo_cursor, char* string);
bool ce_commit_remove_string (BufferCommitNode_t** tail, const Point_t* start, const Point_t* undo_cursor, const Point_t* redo_cursor, char* string);
bool ce_commit_change_string (BufferCommitNode_t** tail, const Point_t* start, const Point_t* undo_cursor, const Point_t* redo_cursor, char* new_string, char* prev_string);

bool ce_commit_undo          (Buffer_t* buffer, BufferCommitNode_t** tail, Point_t* cursor);
bool ce_commit_redo          (Buffer_t* buffer, BufferCommitNode_t** tail, Point_t* cursor);
bool ce_commit_change        (BufferCommitNode_t** tail, const BufferCommit_t* change);

bool ce_commits_free         (BufferCommitNode_t* tail);

// Syntax
int64_t ce_is_c_keyword     (const char* line, int64_t start_offset);
int64_t ce_is_c_contrl      (const char* line, int64_t start_offset);
int64_t ce_is_preprocessor  (const char* line, int64_t start_offset);
int64_t ce_is_c_typename    (const char* line, int64_t start_offset);
CommentType_t ce_is_comment (const char* line, int64_t start_offset);
void ce_is_string_literal   (const char* line, int64_t start_offset, int64_t line_len, bool* inside_string, char* last_quote_char);
int64_t ce_is_caps_var      (const char* line, int64_t start_offset);

// Logging Functions
#define ce_message(...) ({fprintf(stderr,__VA_ARGS__);\
                          fprintf(stderr,"\n");})

// Misc. Utility Functions
int64_t ce_count_string_lines   (const char* string);
void    ce_sort_points          (const Point_t** a, const Point_t** b);
int     ce_ispunct              (int c);
int     ce_iswordchar           (int c);
void*   ce_memrchr              (const void* s, int c, size_t n);
bool    ce_point_in_range       (const Point_t* p, const Point_t* start, const Point_t* end);
int64_t ce_last_index           (const char* string);
bool    ce_connect_border_lines (const Point_t* location);

#endif
