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

#define CE_CHECK_PTR_ARG(arg)                                                 \
     if(!arg){                                                                \
          ce_message("%s() received NULL argument %s\n", __FUNCTION__, #arg); \
          return false;                                                       \
     }

typedef struct {
     int64_t x;
     int64_t y;
} Point;

typedef struct {
     char** lines; // '\0' terminated, does not contain newlines, NULL if empty
     int64_t line_count;
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

typedef struct BufferChange {
     bool insertion; // insertion or deletion
     Point start;
     Point cursor;
     union{
          int64_t length; // insertion
          char c; // deletion
     };
} BufferChange;

typedef struct BufferChangeNode {
     BufferChange change;
     struct BufferChangeNode* prev;
} BufferChangeNode;

typedef bool ce_initializer(BufferNode*, Point*, int, char**, void**);
typedef void ce_destroyer(BufferNode*, void*);
typedef bool ce_key_handler(int, BufferNode*, void*);
typedef void ce_view_drawer(const BufferNode*, void*);

extern Buffer* g_message_buffer;
extern Point* g_terminal_dimensions;

bool ce_alloc_lines(Buffer* buffer, int64_t line_count);
void ce_load_string(Buffer* buffer, const char* str);
bool ce_load_file(Buffer* buffer, const char* filename);
bool ce_save_buffer(const Buffer* buffer, const char* filename);
void ce_free_buffer(Buffer* buffer);
bool ce_point_on_buffer(const Buffer* buffer, const Point* location);
bool ce_insert_char(Buffer* buffer, const Point* location, char c);
bool ce_insert_string(Buffer* buffer, const Point* location, const char* string);
bool ce_remove_char(Buffer* buffer, const Point* location);
bool ce_get_char(Buffer* buffer, const Point* location, char* c);
bool ce_set_char(Buffer* buffer, const Point* location, char c);
int64_t ce_find_end_of_line(const Buffer* buffer, Point* cursor);
int64_t ce_find_char_forward_in_line(Buffer* buffer, const Point* location, char c);
int64_t ce_find_char_backward_in_line(Buffer* buffer, const Point* location, char c);
int64_t ce_find_beginning_of_word(Buffer* buffer, const Point* location, bool punctuation_word_boundaries);
int64_t ce_find_end_of_word(Buffer* buffer, const Point* location, bool punctuation_word_boundaries);
int64_t ce_find_next_word(Buffer* buffer, const Point* location, bool punctuation_word_boundaries);
bool ce_find_match(Buffer* buffer, const Point* location, Point* delta);
bool ce_move_cursor_to_soft_beginning_of_line(Buffer* buffer, Point* cursor);

// NOTE: passing NULL to string causes an empty line to be inserted
bool ce_insert_line(Buffer* buffer, int64_t line, const char* string);
bool ce_append_line(Buffer* buffer, const char* string);
bool ce_insert_newline(Buffer* buffer, int64_t line);
bool ce_remove_line(Buffer* buffer, int64_t line);
bool ce_set_line(Buffer* buffer, int64_t line, const char* string);

bool ce_draw_buffer(const Buffer* buffer, const Point* term_top_left, const Point* term_bottom_right,
                    const Point* buffer_top_left);

bool ce_message(const char* format, ...);

BufferNode* ce_append_buffer_to_list(BufferNode* head, Buffer* buffer);
bool ce_remove_buffer_from_list(BufferNode* head, BufferNode** node);

bool ce_move_cursor(const Buffer* buffer, Point* cursor, const Point* delta);
bool ce_follow_cursor(const Point* cursor, int64_t* top_line, int64_t* left_column, int64_t view_height, int64_t view_width);

bool ce_buffer_change(BufferChangeNode** tail, const BufferChange* change);
bool ce_buffer_undo(Buffer* buffer, BufferChangeNode** tail);

void* ce_memrchr(const void* s, int c, size_t n);

#endif
