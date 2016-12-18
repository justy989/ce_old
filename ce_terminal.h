#ifndef CE_TERM_H
#define CE_TERM_H

#include <pthread.h>
#include <semaphore.h>

#include "ce.h"

typedef struct TerminalColorNode_t{
     int index;
     int fg;
     int bg;
     struct TerminalColorNode_t* next;
}TerminalColorNode_t;

// ce's virtual terminal
typedef struct{
     bool is_alive;
     sem_t updated;

     Point_t cursor;

     int64_t width;
     int64_t height;

     pthread_t reader_thread;

     int fd;

     Buffer_t buffer;

     TerminalColorNode_t* color_lines;
}Terminal_t;

bool terminal_init(Terminal_t* term, int64_t width, int64_t height);
void terminal_free(Terminal_t* term);

bool terminal_resize(Terminal_t* term, int64_t width, int64_t height);
bool terminal_send_key(Terminal_t* term, int key);

typedef struct{
     Terminal_t* terminal;
     int unique_color_id;
     int last_fg;
     int last_bg;
} TerminalHighlight_t;

void terminal_highlight(const Buffer_t* buffer, Point_t top_left, Point_t bottom_right, Point_t cursor, Point_t loc,
                        const regex_t* highlight_regex, LineNumberType_t line_number_type, HighlightLineType_t highlight_line_type,
                        void* user_data, bool first_call);

#endif
