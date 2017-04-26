#pragma once

#include "ce.h"

typedef struct TabView_t{
     BufferView_t* view_head;
     BufferView_t* view_current;
     BufferView_t* view_previous;
     BufferView_t* view_input_save;
     Point_t view_input_save_cursor;
     int64_t view_input_save_top_row;
     int64_t view_input_save_left_column;
     Buffer_t* overriden_buffer;
     struct TabView_t* next;
}TabView_t;

TabView_t* tab_view_insert(TabView_t* head);
void tab_view_remove(TabView_t** head, TabView_t* view);
void tab_view_input_save(TabView_t* view);
void tab_view_input_restore(TabView_t* view);
