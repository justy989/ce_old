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
#define MESSAGE_FILE "messages.txt"

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
     S_KEYWORD = 1,
     S_COMMENT,
     S_STRING,
     S_CONSTANT,
     S_PREPROC,
} Syntax;

#define CE_CHECK_PTR_ARG(arg)                                                 \
     if(!arg){                                                                \
          ce_message("%s() received NULL argument %s\n", __FUNCTION__, #arg); \
          return false;                                                       \
     }

typedef struct {
     int64_t x;
     int64_t y;
} Point;

// TODO: ce_offset_point(Point* p, int64_t dx, int64_t dy);

typedef struct {
     char** lines; // '\0' terminated, does not contain newlines, NULL if empty
     int64_t line_count;
     Point cursor;
     union {
          char* filename;
          char* name;
     };
     void* user_data;
} Buffer;

typedef struct BufferNode {
     Buffer* buffer;
     struct BufferNode* next;
} BufferNode;

typedef enum {
     BCT_NONE,
     BCT_INSERT_CHAR,
     BCT_INSERT_STRING,
     BCT_REMOVE_CHAR,
     BCT_REMOVE_STRING,
     BCT_CHANGE_CHAR,
     BCT_CHANGE_STRING,
} BufferCommitType;

typedef struct {
     BufferCommitType type;
     Point start;
     Point undo_cursor;
     Point redo_cursor;
     union {
          char c;
          char* str;
     };
     union {
          char prev_c;
          char* prev_str;
     };
} BufferCommit;

typedef struct BufferCommitNode {
     BufferCommit commit;
     struct BufferCommitNode* prev;
     struct BufferCommitNode* next;
} BufferCommitNode;

// horizontal split []|[]

// vertical split
// []
// --
// []
typedef struct BufferView {
     Point cursor;
     Point top_left;
     Point bottom_right;
     int64_t top_row;
     int64_t left_column;
     BufferNode* buffer_node;
     struct BufferView* next_horizontal;
     struct BufferView* next_vertical;
} BufferView;

typedef bool ce_initializer(BufferNode*, Point*, int, char**, void**);
typedef void ce_destroyer(BufferNode*, void*);
typedef bool ce_key_handler(int, BufferNode*, void*);
typedef void ce_view_drawer(const BufferNode*, void*);

extern Buffer* g_message_buffer;
extern Point* g_terminal_dimensions;

int64_t ce_count_string_lines(const char* string);
bool ce_alloc_lines(Buffer* buffer, int64_t line_count);
bool ce_load_string(Buffer* buffer, const char* string);
bool ce_load_file(Buffer* buffer, const char* filename);
bool ce_save_buffer(const Buffer* buffer, const char* filename);
void ce_free_buffer(Buffer* buffer);
bool ce_point_on_buffer(const Buffer* buffer, const Point* location);
bool ce_insert_char(Buffer* buffer, const Point* location, char c);
bool ce_insert_string(Buffer* buffer, const Point* location, const char* string);
bool ce_append_string(Buffer* buffer, int64_t line, const char* string);
bool ce_remove_string(Buffer* buffer, const Point* location, int64_t length);
bool ce_remove_char(Buffer* buffer, const Point* location);
void ce_clear_lines(Buffer* buffer);
char* ce_dupe_string(Buffer* buffer, const Point* start, const Point* end);
char* ce_dupe_line(Buffer* buffer, int64_t line);
bool ce_get_char(const Buffer* buffer, const Point* location, char* c);
bool ce_set_char(Buffer* buffer, const Point* location, char c);
int64_t ce_find_delta_to_end_of_line(const Buffer* buffer, Point* cursor);
int64_t ce_find_delta_to_char_forward_in_line(Buffer* buffer, const Point* location, char c);
int64_t ce_find_delta_to_char_backward_in_line(Buffer* buffer, const Point* location, char c);
int64_t ce_find_delta_to_beginning_of_word(Buffer* buffer, const Point* location, bool punctuation_word_boundaries);
int64_t ce_find_delta_to_end_of_word(Buffer* buffer, const Point* location, bool punctuation_word_boundaries);
int64_t ce_find_next_word(Buffer* buffer, const Point* location, bool punctuation_word_boundaries);
bool ce_find_match(Buffer* buffer, const Point* location, Point* delta);
bool ce_find_string(Buffer* buffer, const Point* location, const char* search_str, Point* match);
bool ce_move_cursor_to_soft_beginning_of_line(Buffer* buffer, Point* cursor);
int ce_ispunct(int c);
int ce_iswordchar(int c);
bool ce_get_homogenous_adjacents(const Buffer* buffer, Point* start, Point* end, int (*is_homogenous)(int));
bool ce_get_word_at_location(Buffer* buffer, const Point* location, Point* word_start, Point* word_end);

// NOTE: passing NULL to string causes an empty line to be inserted
bool ce_insert_line(Buffer* buffer, int64_t line, const char* string);
bool ce_append_line(Buffer* buffer, const char* string);
bool ce_insert_newline(Buffer* buffer, int64_t line);
bool ce_join_line(Buffer* buffer, int64_t line);
bool ce_remove_line(Buffer* buffer, int64_t line);
bool ce_set_line(Buffer* buffer, int64_t line, const char* string);

bool ce_draw_buffer(const Buffer* buffer, const Point* term_top_left, const Point* term_bottom_right,
                    const Point* buffer_top_left);

bool ce_message(const char* format, ...);

// NOTE: we may want to consider taking tail rather than head
BufferNode* ce_append_buffer_to_list(BufferNode* head, Buffer* buffer);
bool ce_remove_buffer_from_list(BufferNode* head, BufferNode** node);

Point* ce_clamp_cursor(const Buffer* buffer, Point* cursor);
bool ce_move_cursor(const Buffer* buffer, Point* cursor, const Point* delta);
bool ce_set_cursor(const Buffer* buffer, Point* cursor, const Point* location);
bool ce_follow_cursor(const Point* cursor, int64_t* left_column, int64_t* top_left, int64_t view_width, int64_t view_height,
                      bool at_terminal_width_edge, bool at_terminal_height_edge);
bool ce_advance_cursor(const Buffer* buffer, Point* cursor, int64_t delta);
bool ce_move_cursor_to_end_of_file(const Buffer* buffer, Point* cursor);

bool ce_commit_insert_char(BufferCommitNode** tail, const Point* start, const Point* undo_cursor, const Point* redo_cursor, char c);
bool ce_commit_insert_string(BufferCommitNode** tail, const Point* start, const Point* undo_cursor, const Point* redo_cursor, char* string);
bool ce_commit_remove_char(BufferCommitNode** tail, const Point* start, const Point* undo_cursor, const Point* redo_cursor, char c);
bool ce_commit_remove_string(BufferCommitNode** tail, const Point* start, const Point* undo_cursor, const Point* redo_cursor, char* string);
bool ce_commit_change_char(BufferCommitNode** tail, const Point* start, const Point* undo_cursor, const Point* redo_cursor, char c, char prev_c);
bool ce_commit_change_string(BufferCommitNode** tail, const Point* start, const Point* undo_cursor, const Point* redo_cursor, char* new_string, char* prev_string);
bool ce_commit_change(BufferCommitNode** tail, const BufferCommit* change);
bool ce_commit_undo(Buffer* buffer, BufferCommitNode** tail, Point* cursor);
bool ce_commit_redo(Buffer* buffer, BufferCommitNode** tail, Point* cursor);
bool ce_commits_free(BufferCommitNode* tail);

BufferView* ce_split_view(BufferView* view, BufferNode* buffer_node, bool horizontal);
bool ce_remove_view(BufferView** head, BufferView* view);
bool ce_calc_views(BufferView* head, const Point* top_left, const Point* top_right);
bool ce_draw_views(const BufferView* head);
bool ce_free_views(BufferView** view);
BufferView* ce_find_view_at_point(BufferView* head, const Point* point);

void* ce_memrchr(const void* s, int c, size_t n);
int64_t ce_compute_length(Point* start, Point* end);

#endif
