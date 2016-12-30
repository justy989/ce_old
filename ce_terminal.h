#ifndef CE_TERM_H
#define CE_TERM_H

#include <pthread.h>
#include <semaphore.h>

#include "ce.h"
#include "ce_syntax.h"

#define TERM_START_COLOR (S_AUTO_COMPLETE + 1)

typedef struct TerminalColorNode_t{
     int index;
     int fg;
     int bg;
     struct TerminalColorNode_t* next;
}TerminalColorNode_t;

typedef struct TerminalColorPairNode_t{
     int fg;
     int bg;
     struct TerminalColorPairNode_t* next;
}TerminalColorPairNode_t;

// ce's virtual terminal
typedef struct{
     bool is_alive;
     sem_t* updated;

     Point_t cursor;

     int64_t width;
     int64_t height;

     pthread_t reader_thread;

     pid_t pid;
     int fd;

     Buffer_t* buffer;

     TerminalColorNode_t* color_lines; // array of nodes that lead to linked lists, size is buffer->line_count
}Terminal_t;

extern TerminalColorPairNode_t* terminal_color_pairs_head; // exposed for cleanup if necessary

bool terminal_init(Terminal_t* term, int64_t width, int64_t height, Buffer_t* buffer);
void terminal_free(Terminal_t* term);

bool terminal_resize(Terminal_t* term, int64_t width, int64_t height);
bool terminal_send_key(Terminal_t* term, int key);
char* terminal_get_current_directory(Terminal_t* term); // string returned must be free'd

typedef struct{
     Terminal_t* terminal;
     int last_fg;
     int last_bg;
     HighlightType_t highlight_type;
}TerminalHighlight_t;

void terminal_highlight(SyntaxHighlighterData_t* data, void* user_data);

#endif
