#include "tab_view.h"

TabView_t* tab_view_insert(TabView_t* head)
{
     // find the tail
     TabView_t* itr = head; // if we use tab_current, we *may* be closer to the tail !
     while(itr->next) itr = itr->next;

     // create the new tab
     TabView_t* new_tab = calloc(1, sizeof(*new_tab));
     if(!new_tab){
          ce_message("failed to allocate tab");
          return NULL;
     }

     // attach to the end of the tail
     itr->next = new_tab;

     // allocate the view
     new_tab->view_head = calloc(1, sizeof(*new_tab->view_head));
     if(!new_tab->view_head){
          ce_message("failed to allocate new view for tab");
          return NULL;
     }

     return new_tab;
}

void tab_view_remove(TabView_t** head, TabView_t* view)
{
     if(!*head || !view) return;

     // TODO: free any open views
     TabView_t* tmp = *head;
     if(*head == view){
          *head = view->next;
          free(tmp);
          return;
     }

     while(tmp->next != view) tmp = tmp->next;
     tmp->next = view->next;
     free(view);
}

// TODO: remove
#if 0
void tab_view_input_save(TabView_t* view)
{
     view->view_input_save = view->view_current;
     view->view_input_save_cursor = view->view_current->cursor;
     view->view_input_save_top_row = view->view_current->top_row;
     view->view_input_save_left_column = view->view_current->left_column;
}

void tab_view_input_restore(TabView_t* view)
{
     view->view_current = view->view_input_save;
     view->view_current->cursor = view->view_input_save_cursor;
     view->view_current->top_row = view->view_input_save_top_row;
     view->view_current->left_column = view->view_input_save_left_column;
}
#endif
