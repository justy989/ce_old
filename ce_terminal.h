#ifndef CE_TERM_H
#define CE_TERM_H

#include <pthread.h>

#include "ce.h"

// ce's virtual terminal

typedef struct{
     bool alive;

     Point_t cursor;

     int64_t width;
     int64_t height;

     pthread_t reader_thread;

     int fd;

     Buffer_t buffer;
}Terminal_t;

bool term_init(Terminal_t* term, int64_t width, int64_t height);
void term_free(Terminal_t* term);

//bool term_resize(Terminal_t* term, int64_t width, int64_t height);
bool term_send_key(Terminal_t* term, int key);

#endif
