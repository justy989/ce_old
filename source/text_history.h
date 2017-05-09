#pragma once

#include <stdbool.h>

typedef struct TextHistoryNode_t{
     char* entry;
     struct TextHistoryNode_t* next;
     struct TextHistoryNode_t* prev;
}TextHistoryNode_t;

typedef struct{
     TextHistoryNode_t* head;
     TextHistoryNode_t* tail;
     TextHistoryNode_t* cur;
}TextHistory_t;

bool text_history_init(TextHistory_t* history);
void text_history_free(TextHistory_t* history);
bool text_history_commit_current(TextHistory_t* history);
bool text_history_next(TextHistory_t* history);
bool text_history_prev(TextHistory_t* history);
