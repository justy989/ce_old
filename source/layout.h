#pragma once

#include "ce.h"

typedef enum{
     LAYOUT_HORIZONTAL,
     LAYOUT_VERTICAL,
     LAYOUT_VIEW_SPLIT_HORIZONTAL,
     LAYOUT_VIEW_SPLIT_VERTICAL,
}LayoutType_t;

typedef struct{
     Point_t cursor;
     Point_t screen_top_left;
     Point_t screen_bottom_right;
     Point_t scroll;

     Buffer_t* buffer;
     void*     user_data;
}LayoutView_t;

typedef struct{
     Layout_t* layouts;
     int64_t count;
}LayoutList_t;

typedef struct{
     LayoutType_t type;

     union{
          LayoutList_t list;
          LayoutView_t view;
     };
}Layout_t;

void layout_calculate_even_layout(Layout_t* root, Rect_t rect);
Layout_t* layout_find_view(Layout_t* root, Point_t loc);
bool layout_split(Layout_t* layout);
void layout_remove(Layout_t* root, Layout_t* target);
