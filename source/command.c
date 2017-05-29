#include "command.h"
#include "ce_config.h"
#include "view.h"
#include "buffer.h"
#include "destination.h"
#include "info.h"
#include "terminal_helper.h"
#include "misc.h"

#include <ctype.h>
#include <unistd.h>

static const char* eat_blanks(const char* string)
{
     while(*string){
          if(!isblank(*string)) break;
          string++;
     }

     return string;
}

static const char* find_end_of_arg(const char* string)
{
     bool quote = (*string == '"');

     if(quote){
          // find the next quote, ignoring backslashed quotes
          char prev = 0;
          while(*string){
               prev = *string;
               string++;
               if(*string == '"' && prev != '\\') return string + 1;
          }
          return NULL;
     }else{
          return strchr(string, ' ');
     }
}

static bool parse_arg(CommandArg_t* arg, const char* string)
{
     if(*string == 0) return false;

     bool digits_only = true;
     bool decimal = false;
     const char* itr = string;

     while(*itr){
          if(!isdigit(*itr)){
               if(*itr == '.'){
                    if(decimal) return false;
                    decimal = true;
               }else{
                    digits_only = false;
               }
          }
          itr++;
     }

     if(digits_only){
          if(decimal){
               arg->type = CAT_DECIMAL;
               arg->decimal = atof(string);
          }else{
               arg->type = CAT_INTEGER;
               arg->integer = atoi(string);
          }
     }else{
          // skip over the first quote if one is there
          if(string[0] == '"') string++;

          arg->type = CAT_STRING;
          arg->string = strdup(string);
          int64_t len = strlen(arg->string);

          // overwrite last quote with null terminator
          if(arg->string[len - 1] == '"') arg->string[len - 1] = 0;
     }

     return true;
}

bool command_parse(Command_t* command, const char* string)
{
     if(*string == 0) return false;

     const char* start = NULL;
     const char* end = NULL;

     int64_t arg_count = 0;
     start = eat_blanks(string);

     // count the args
     while(*start){
          end = find_end_of_arg(start);
          arg_count++;
          if(!end) break;
          start = eat_blanks(end + 1);
     }

     // account for command at the beginning
     arg_count--;

     start = eat_blanks(string);
     end = find_end_of_arg(start);

     if(!end){
          strcpy(command->name, start);
          return true;
     }

     // copy the command name
     strncpy(command->name, start, end - start);

     start = eat_blanks(end + 1);

     // allocate the arguments
     command->args = malloc(arg_count * sizeof(*command->args));
     if(!command->args) return false;
     command->arg_count = arg_count;

     CommandArg_t* arg = command->args;

     // parse the individual args
     while(*start){
          end = find_end_of_arg(start);
          if(end){
               int64_t arg_len = end - start;
               char buffer[arg_len + 1];
               memset(buffer, 0, arg_len + 1);

               strncpy(buffer, start, arg_len);
               if(!parse_arg(arg, buffer)){
                    command_free(command);
                    return false;
               }
          }else{
               int64_t arg_len = strlen(start);
               char buffer[arg_len + 1];
               memset(buffer, 0, arg_len + 1);
               strcpy(buffer, start);
               if(!parse_arg(arg, buffer)){
                    command_free(command);
                    return false;
               }
               break;
          }

          start = eat_blanks(end + 1);
          arg++;
     }

     return true;
}

void command_free(Command_t* command)
{
     memset(command, 0, COMMAND_NAME_MAX_LEN);

     for(int64_t i = 0; i < command->arg_count; ++i){
          if(command->args[i].type == CAT_STRING){
               free(command->args[i].string);
          }
     }

     free(command->args);
     command->args = NULL;
     command->arg_count = 0;
}
