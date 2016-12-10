#ifndef CE_TERM_H
#define CE_TERM_H

#include <pthread.h>

#include "ce.h"

// ce's virtual terminal

typedef struct{
     bool is_alive;
     bool is_updated;

     Point_t cursor;

     int64_t width;
     int64_t height;

     pthread_t reader_thread;

     int fd;

     Buffer_t buffer;
}Terminal_t;

bool terminal_init(Terminal_t* term, int64_t width, int64_t height);
void terminal_free(Terminal_t* term);

//bool terminal_resize(Terminal_t* term, int64_t width, int64_t height);
bool terminal_send_key(Terminal_t* term, int key);

#endif
