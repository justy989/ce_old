#include "layout.h"

static bool layout_list_append(LayoutList_t* layout_list)
{
     int64_t new_count = layout_list->layouts + 1;
     layout_list->layouts = realloc(layout_list->layouts, new_count * sizeof(*layout_list->layouts));
     if(!layout_list->layouts) return false;
     layout_list->layouts[layout_list->count] = layout_list->layouts[0];
     layout_list->count = new_count;
     return true;
}

void layout_calculate_even_layout(Layout_t* root, Rect_t rect)
{

}

Layout_t* layout_find_view(Layout_t* root, Point_t loc)
{
     return NULL;
}

bool layout_split(Layout_t* layout)
{
     switch(layout->type){
     case LAYOUT_HORIZONTAL:
     case LAYOUT_VERTICAL:
          return layout_list_append(&layout->list);
     case LAYOUT_VIEW_SPLIT_HORIZONTAL:
     case LAYOUT_VIEW_SPLIT_VERTICAL:
          break;
     }

     return false;
}

void layout_remove(Layout_t* root, Layout_t* target)
{

}
