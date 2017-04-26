#pragma once

#include <linux/limits.h>
#include "ce.h"

#define JUMP_LIST_MAX 32

typedef struct{
     char filepath[PATH_MAX];
     Point_t location;
}Jump_t;

typedef struct{
     Jump_t jumps[JUMP_LIST_MAX];
     int64_t jump_current;
}JumpArray_t;

void jump_insert(JumpArray_t* jump_array, const char* filepath, Point_t location);
const Jump_t* jump_to_previous(JumpArray_t* jump_array);
const Jump_t* jump_to_next(JumpArray_t* jump_array);
