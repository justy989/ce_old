#pragma once

#include <inttypes.h>
#include <stdbool.h>

typedef enum{
     CAT_INTEGER,
     CAT_DECIMAL,
     CAT_STRING,
     CAT_COUNT
}CommandArgType_t;

typedef struct{
     CommandArgType_t type;

     union{
          int64_t integer;
          double decimal;
          char* string;
     };
}CommandArg_t;

#define COMMAND_NAME_MAX_LEN 128

typedef struct{
     char name[COMMAND_NAME_MAX_LEN];
     CommandArg_t* args;
     int64_t arg_count;
}Command_t;

typedef void ce_command (Command_t*, void*);

typedef struct{
     ce_command* func;
     const char* name;
     bool hidden;
}CommandEntry_t;

bool command_parse(Command_t* command, const char* string);
void command_free(Command_t* command);

CommandEntry_t* commands_init(int64_t* command_entry_count);
