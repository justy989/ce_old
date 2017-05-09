#pragma once

#include "ce.h"

typedef struct TabView_t{
     BufferView_t* view_head;
     BufferView_t* view_current;
     BufferView_t* view_previous;
     struct TabView_t* next;
}TabView_t;

TabView_t* tab_view_insert(TabView_t* head);
void tab_view_remove(TabView_t** head, TabView_t* view);
