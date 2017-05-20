#include "jump.h"

void jump_insert(JumpArray_t* jump_array, const char* filepath, Point_t location)
{
     // update data
     int64_t next_index = jump_array->jump_current + 1;

     if(next_index < (JUMP_LIST_MAX - 1)){
          strncpy(jump_array->jumps[jump_array->jump_current].filepath, filepath, PATH_MAX);
          jump_array->jumps[jump_array->jump_current].location = location;

          // clear all jumps afterwards
          for(int64_t i = next_index; i < JUMP_LIST_MAX; ++i){
               jump_array->jumps[i].filepath[0] = 0;
          }

          // advance jump index
          jump_array->jump_current = next_index;
     }else{
          for(int64_t i = 1; i < JUMP_LIST_MAX; ++i){
               jump_array->jumps[i - 1] = jump_array->jumps[i];
          }

          strncpy(jump_array->jumps[jump_array->jump_current].filepath, filepath, PATH_MAX);
          jump_array->jumps[jump_array->jump_current].location = location;
     }
}

const Jump_t* jump_to_previous(JumpArray_t* jump_array)
{
     if(jump_array->jump_current == 0) return NULL; // history is empty

     int jump_index = jump_array->jump_current - 1;
     Jump_t* jump = jump_array->jumps + jump_index;
     if(!jump->filepath[0]) return NULL; // the jump is empty

     jump_array->jump_current = jump_index;

     return jump;
}

const Jump_t* jump_to_next(JumpArray_t* jump_array)
{
     if(jump_array->jump_current >= (JUMP_LIST_MAX - 1)) return NULL;

     int jump_index = jump_array->jump_current + 1;
     Jump_t* jump = jump_array->jumps + jump_index;
     if(!jump->filepath[0]) return NULL; // the jump is empty

     jump_array->jump_current = jump_index;

     return jump;
}
