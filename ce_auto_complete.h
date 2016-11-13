#ifndef CE_AUTOCOMPLETE_H
#define CE_AUTOCOMPLETE_H

#include "ce.h"

typedef struct CompleteNode_t{
     char* option;
     struct CompleteNode_t* next;
     struct CompleteNode_t* prev;
} CompleteNode_t;

typedef struct{
     CompleteNode_t* head;
     CompleteNode_t* tail;
     CompleteNode_t* current;
     Point_t start;
} AutoComplete_t;

bool auto_complete_insert          (AutoComplete_t* auto_complete, const char* option);
void auto_complete_start           (AutoComplete_t* auto_complete, Point_t start);
void auto_complete_end             (AutoComplete_t* auto_complete);
bool auto_completing               (AutoComplete_t* auto_complete);
char* auto_complete_get_completion (AutoComplete_t* auto_complete, int64_t x);
void auto_complete_free            (AutoComplete_t* auto_complete);
void auto_complete_next            (AutoComplete_t* auto_complete, const char* match);
void auto_complete_prev            (AutoComplete_t* auto_complete, const char* match);

#endif
