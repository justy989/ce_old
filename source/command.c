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

#define RELOAD_BUFFER_HELP "usage: reload_buffer"

static void command_reload_buffer(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message(RELOAD_BUFFER_HELP);
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Buffer_t* buffer = buffer_view->buffer;

     if(access(buffer->filename, R_OK) != 0){
          ce_message("failed to read %s: %s", buffer->filename, strerror(errno));
          return;
     }

     // reload file
     if(buffer->status == BS_READONLY){
          // NOTE: maybe ce_clear_lines shouldn't care about readonly
          ce_clear_lines_readonly(buffer);
     }else{
          ce_clear_lines(buffer);
     }

     ce_load_file(buffer, buffer->filename);
     ce_clamp_cursor(buffer, &buffer_view->cursor);
}

static void command_syntax_help()
{
     ce_message("usage: syntax [string]");
     ce_message(" supported styles: 'c', 'cpp', 'python', 'config', 'diff', 'plain'");
}

static void command_syntax(Command_t* command, void* user_data)
{
     if(command->arg_count != 1){
          command_syntax_help();
          return;
     }

     if(command->args[0].type != CAT_STRING){
          command_syntax_help();
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Buffer_t* buffer = buffer_view->buffer;

     if(strcmp(command->args[0].string, "c") == 0){
          ce_message("syntax 'c' now on %s", buffer->filename);
          buffer->syntax_fn = syntax_highlight_c;
          free(buffer->syntax_user_data);
          buffer->syntax_user_data = malloc(sizeof(SyntaxC_t));
          buffer->type = BFT_C;
     }else if(strcmp(command->args[0].string, "cpp") == 0){
          ce_message("syntax 'cpp' now on %s", buffer->filename);
          buffer->syntax_fn = syntax_highlight_cpp;
          free(buffer->syntax_user_data);
          buffer->syntax_user_data = malloc(sizeof(SyntaxCpp_t));
          buffer->type = BFT_CPP;
     }else if(strcmp(command->args[0].string, "python") == 0){
          ce_message("syntax 'python' now on %s", buffer->filename);
          buffer->syntax_fn = syntax_highlight_python;
          free(buffer->syntax_user_data);
          buffer->syntax_user_data = malloc(sizeof(SyntaxPython_t));
          buffer->type = BFT_PYTHON;
     }else if(strcmp(command->args[0].string, "java") == 0){
          ce_message("syntax 'java' now on %s", buffer->filename);
          buffer->syntax_fn = syntax_highlight_java;
          free(buffer->syntax_user_data);
          buffer->syntax_user_data = malloc(sizeof(SyntaxJava_t));
          buffer->type = BFT_JAVA;
     }else if(strcmp(command->args[0].string, "config") == 0){
          ce_message("syntax 'config' now on %s", buffer->filename);
          buffer->syntax_fn = syntax_highlight_config;
          free(buffer->syntax_user_data);
          buffer->syntax_user_data = malloc(sizeof(SyntaxConfig_t));
          buffer->type = BFT_CONFIG;
     }else if(strcmp(command->args[0].string, "diff") == 0){
          ce_message("syntax 'diff' now on %s", buffer->filename);
          buffer->syntax_fn = syntax_highlight_diff;
          free(buffer->syntax_user_data);
          buffer->syntax_user_data = malloc(sizeof(SyntaxDiff_t));
          buffer->type = BFT_DIFF;
     }else if(strcmp(command->args[0].string, "plain") == 0){
          ce_message("syntax 'plain' now on %s", buffer->filename);
          buffer->syntax_fn = syntax_highlight_plain;
          free(buffer->syntax_user_data);
          buffer->syntax_user_data = malloc(sizeof(SyntaxPlain_t));
          buffer->type = BFT_PLAIN;
     }else{
          ce_message("unknown syntax '%s'", command->args[0].string);
     }
}

#define QUIT_ALL_HELP "usage: quit_all"

static void command_quit_all(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message(QUIT_ALL_HELP);
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     if(strchr(command->name, '!')) {
          config_state->quit = true;
     }else{
          misc_quit_and_prompt_if_unsaved(config_state, *command_data->head);
     }
}


#define SPLIT_HELP "usage: [v]split"

static void command_split(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message(SPLIT_HELP);
          return;
     }

     bool vertical;
     if(command->name[0] == 'v'){
          vertical = true;
     }else{
          vertical = false;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     view_split(config_state->tab_current->view_head, config_state->tab_current->view_current, vertical, config_state->line_number_type);
     terminal_resize_if_in_view(config_state->tab_current->view_head, config_state->terminal_head);

     // TODO: open file if specified as an argument
}

#define CSCOPE_GOTO_DEFINITION_HELP "usage: cscope_goto_definition <symbol>"
static void command_cscope_goto_definition(Command_t* command, void* user_data)
{
     if(command->arg_count != 1 || command->args[0].type != CAT_STRING){
          ce_message(CSCOPE_GOTO_DEFINITION_HELP);
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     dest_cscope_goto_definition(config_state->tab_current->view_current, command_data->head, command->args[0].string);
}

#define NOH_HELP "usage: noh"

static void command_noh(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message(NOH_HELP);
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     config_state->do_not_highlight_search = true;
}

static void command_line_number_help()
{
     ce_message("usage: line_number [string]");
     ce_message(" supported modes: 'none', 'absolute', 'relative', 'both'");
}

static void command_line_number(Command_t* command, void* user_data)
{
     if(command->arg_count != 1){
          command_line_number_help();
          return;
     }

     if(command->args[0].type != CAT_STRING){
          command_line_number_help();
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     if(strcmp(command->args[0].string, "none") == 0){
          config_state->line_number_type = LNT_NONE;
     }else if(strcmp(command->args[0].string, "absolute") == 0){
          config_state->line_number_type = LNT_ABSOLUTE;
     }else if(strcmp(command->args[0].string, "relative") == 0){
          config_state->line_number_type = LNT_RELATIVE;
     }else if(strcmp(command->args[0].string, "both") == 0){
          config_state->line_number_type = LNT_RELATIVE_AND_ABSOLUTE;
     }
}

static void command_highlight_line_help()
{
     ce_message("usage: highlight_line [string]");
     ce_message(" supported modes: 'none', 'text', 'entire'");
}

static void command_highlight_line(Command_t* command, void* user_data)
{
     if(command->arg_count != 1){
          command_highlight_line_help();
          return;
     }

     if(command->args[0].type != CAT_STRING){
          command_highlight_line_help();
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     if(strcmp(command->args[0].string, "none") == 0){
          config_state->highlight_line_type = HLT_NONE;
     }else if(strcmp(command->args[0].string, "text") == 0){
          config_state->highlight_line_type = HLT_TO_END_OF_TEXT;
     }else if(strcmp(command->args[0].string, "entire") == 0){
          config_state->highlight_line_type = HLT_ENTIRE_LINE;
     }
}

#define NEW_BUFFER_HELP "usage: new_buffer [optional filename]"

static void command_new_buffer(Command_t* command, void* user_data)
{
     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     if(command->arg_count == 0){
          BufferNode_t* new_buffer_node = buffer_create_empty(command_data->head, "unnamed");
          if(new_buffer_node){
               config_state->tab_current->view_current->buffer = new_buffer_node->buffer;
               config_state->tab_current->view_current->cursor = (Point_t){0, 0};
          }
     }else if(command->arg_count == 1){
          BufferNode_t* new_buffer_node = buffer_create_empty(command_data->head, command->args[0].string);
          if(new_buffer_node){
               config_state->tab_current->view_current->buffer = new_buffer_node->buffer;
               config_state->tab_current->view_current->cursor = (Point_t){0, 0};
          }
     }else{
          ce_message(NEW_BUFFER_HELP);
          return;
     }
}

#define RENAME_HELP "usage: rename [string]"

static void command_rename(Command_t* command, void* user_data)
{
     if(command->arg_count != 1){
          ce_message(RENAME_HELP);
          return;
     }

     if(command->args[0].type != CAT_STRING){
          ce_message(RENAME_HELP);
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Buffer_t* buffer = buffer_view->buffer;

     if(buffer->name) free(buffer->name);
     buffer->name = strdup(command->args[0].string);
     if(buffer->status != BS_READONLY){
          buffer->status = BS_MODIFIED;
     }
}

#define BUFFERS_HELP "usage: buffers"

static void command_buffers(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message(BUFFERS_HELP);
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     view_switch_to_buffer_list(&command_data->config_state->buffer_list_buffer,
                                command_data->config_state->tab_current->view_current,
                                command_data->config_state->tab_current->view_head,
                                *command_data->head);
     info_update_buffer_list_buffer(&command_data->config_state->buffer_list_buffer, *command_data->head);
}

#define MACRO_BACKSLASHES_HELP "usage: macro_backslashes"

static void command_macro_backslashes(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message(MACRO_BACKSLASHES_HELP);
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Buffer_t* buffer = buffer_view->buffer;
     BufferState_t* buffer_state = buffer->user_data;
     Point_t* cursor = &buffer_view->cursor;

     if(config_state->vim_state.mode != VM_VISUAL_LINE) return;

     int64_t start_line = 0;
     int64_t end_line = 0;

     // sort points
     if(cursor->y > config_state->vim_state.visual_start.y){
          start_line = config_state->vim_state.visual_start.y;
          end_line = cursor->y;
     }else{
          start_line = cursor->y;
          end_line = config_state->vim_state.visual_start.y;
     }

     // figure out longest line TODO: after slurping spaces and backslashes
     int64_t line_count = end_line - start_line + 1;
     int64_t longest_line = 0;

     for(int64_t i = 0; i < line_count; ++i){
          int64_t line_len = strlen(buffer->lines[i + start_line]);
          if(line_len > longest_line) longest_line = line_len;
     }

     // insert whitespace and backslash on every line to make it the same length
     for(int64_t i = 0; i < line_count; ++i){
          int64_t line = i + start_line;
          int64_t line_len = strlen(buffer->lines[line]);
          int64_t space_len = longest_line - line_len + 1;
          Point_t loc = {line_len, line};
          for(int64_t s = 0; s < space_len; ++s){
               ce_insert_char(buffer, loc, ' ');
               ce_commit_insert_string(&buffer_state->commit_tail, loc, *cursor, *cursor, strdup(" "), BCC_KEEP_GOING);
               loc.x++;
          }
          ce_insert_char(buffer, loc, '\\');
          ce_commit_insert_string(&buffer_state->commit_tail, loc, *cursor, *cursor, strdup("\\"), BCC_KEEP_GOING);
     }
}

CommandEntry_t* commands_init(int64_t* command_entry_count)
{
     // create a stack array so we can have the compiler track the number of elements
     CommandEntry_t command_entries[] = {
          {command_buffers, "buffers", false},
          {command_highlight_line, "highlight_line", false},
          {command_line_number, "line_number", false},
          {command_macro_backslashes, "macro_backslashes", false},
          {command_new_buffer, "new_buffer", false},
          {command_noh, "noh", false},
          {command_reload_buffer, "reload_buffer", false},
          {command_rename, "rename", false},
          {command_syntax, "syntax", false},
          {command_quit_all, "quit_all", false},
          {command_quit_all, "qa", true}, // hidden vim-compatible shortcut
          {command_quit_all, "qa!", true}, // hidden vim-compatible shortcut
          {command_split, "split", false},
          {command_split, "vsplit", false},
          {command_cscope_goto_definition, "cscope_goto_definition", false},
     };

     // init and copy from our stack array
     *command_entry_count = sizeof(command_entries) / sizeof(command_entries[0]);
     CommandEntry_t* new_command_entries = malloc(*command_entry_count * sizeof(*command_entries));
     for(int64_t i = 0; i < *command_entry_count; ++i){
          new_command_entries[i] = command_entries[i];
     }

     return new_command_entries;
}
