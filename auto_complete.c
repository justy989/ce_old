#include "auto_complete.h"

#include <assert.h>

bool auto_complete_insert(AutoComplete_t* auto_complete, const char* option, const char* description)
{
     CompleteNode_t* new_node = calloc(1, sizeof(*new_node));
     if(!new_node){
          ce_message("failed to allocate auto complete option");
          return false;
     }

     new_node->option = strdup(option);
     if(description) new_node->description = strdup(description);

     if(auto_complete->tail){
          auto_complete->tail->next = new_node;
          new_node->prev = auto_complete->tail;
     }

     auto_complete->tail = new_node;
     if(!auto_complete->head){
          auto_complete->head = new_node;
     }

     return true;
}

void auto_complete_start(AutoComplete_t* auto_complete, AutoCompleteType_t type, Point_t start)
{
     assert(start.x >= 0);
     auto_complete->start = start;
     auto_complete->type = type;
     auto_complete->current = auto_complete->head;
}

void auto_complete_end(AutoComplete_t* auto_complete)
{
     auto_complete->start.x = -1;
}

bool auto_completing(AutoComplete_t* auto_complete)
{
    return auto_complete->start.x >= 0;
}

static int64_t string_common_beginning(const char* a, const char* b)
{
     size_t common = 0;

     while(*a && *b){
          if(*a == *b){
               common++;
          }else{
               break;
          }

          a++;
          b++;
     }

     return common;
}

// NOTE: allocates the string that is returned to the user
char* auto_complete_get_completion(AutoComplete_t* auto_complete, int64_t x)
{
     int64_t offset = x - auto_complete->start.x;
     if(offset < 0) return NULL;

     CompleteNode_t* itr = auto_complete->current;
     int64_t complete_len = strlen(auto_complete->current->option + offset);
     int64_t min_complete_len = complete_len;

     do{
          itr = itr->next;
          if(!itr) itr = auto_complete->head;
          int64_t option_len = strlen(itr->option);

          if(option_len <= offset) continue;
          if(auto_complete->type == ACT_EXACT){
               if(strncmp(auto_complete->current->option, itr->option, offset) != 0) continue;

               int64_t check_complete_len = string_common_beginning(auto_complete->current->option + offset, itr->option + offset);
               if(check_complete_len < min_complete_len) min_complete_len = check_complete_len;
          }
     }while(itr != auto_complete->current);

     if(min_complete_len) complete_len = min_complete_len;

     char* completion = malloc(complete_len + 1);
     strncpy(completion, auto_complete->current->option + offset, complete_len);
     completion[complete_len] = 0;

     return completion;
}

void auto_complete_free(AutoComplete_t* auto_complete)
{
     auto_complete_end(auto_complete);

     CompleteNode_t* itr = auto_complete->head;
     while(itr){
          CompleteNode_t* tmp = itr;
          itr = itr->next;
          free(tmp->option);
          free(tmp->description);
          free(tmp);
     }

     auto_complete->head = NULL;
     auto_complete->tail = NULL;
     auto_complete->current = NULL;
}

bool auto_complete_next(AutoComplete_t* auto_complete, const char* match)
{
     int64_t match_len = 0;
     if(match){
          match_len = strlen(match);
     }else{
          match = "";
     }
     CompleteNode_t* initial_node = auto_complete->current;

     do{
          auto_complete->current = auto_complete->current->next;
          if(!auto_complete->current) auto_complete->current = auto_complete->head;
          if(auto_complete->type == ACT_EXACT){
               if(strncmp(auto_complete->current->option, match, match_len) == 0) return true;
          }else if(auto_complete->type == ACT_OCCURANCE){
               if(strstr(auto_complete->current->option, match)) return true;
          }
     }while(auto_complete->current != initial_node);

     auto_complete_end(auto_complete);
     return false;
}

bool auto_complete_prev(AutoComplete_t* auto_complete, const char* match)
{
     int64_t match_len = 0;
     if(match){
          match_len = strlen(match);
     }else{
          match = "";
     }
     CompleteNode_t* initial_node = auto_complete->current;

     do{
          auto_complete->current = auto_complete->current->prev;
          if(!auto_complete->current) auto_complete->current = auto_complete->tail;
          if(auto_complete->type == ACT_EXACT){
               if(strncmp(auto_complete->current->option, match, match_len) == 0) return true;
          }else if(auto_complete->type == ACT_OCCURANCE){
               if(strstr(auto_complete->current->option, match)) return true;
          }
     }while(auto_complete->current != initial_node);

     auto_complete_end(auto_complete);
     return false;
}

