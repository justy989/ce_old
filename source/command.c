#include "command.h"
#include "ce_config.h"
#include "view.h"
#include "buffer.h"
#include "destination.h"
#include "info.h"
#include "terminal_helper.h"
#include "misc.h"
#include "completion.h"

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
               if(*string == '"' && prev != '\\'){
                    const char* end = string + 1;
                    if(*end) return end;
                    return NULL;
               }
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
          command->args = NULL;
          command->arg_count = 0;

          int64_t len = strlen(start);
          if(len >= COMMAND_NAME_MAX_LEN){
               ce_message("error: in command '%s' command name is greater than max %d characters", string, COMMAND_NAME_MAX_LEN);
               return false;
          }

          strcpy(command->name, start);
          return true;
     }

     // copy the command name
     int64_t len = end - start;
     if(len >= COMMAND_NAME_MAX_LEN){
          ce_message("error: in command '%s' command name is greater than max %d characters", string, COMMAND_NAME_MAX_LEN);
          return false;
     }

     strncpy(command->name, start, len);
     command->name[len] = 0;

     // exit early if there are no arguments
     if(arg_count == 0){
          command->args = NULL;
          command->arg_count = 0;
          return true;
     }

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
               buffer[arg_len] = 0;

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
               buffer[arg_len] = 0;
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

void command_log(Command_t* command)
{
     ce_message("command: '%s', %ld args", command->name, command->arg_count);

     for(int64_t i = 0; i < command->arg_count; ++i){
          CommandArg_t* arg = command->args + i;
          switch(arg->type){
          default:
               break;
          case CAT_INTEGER:
               ce_message("  %ld", arg->integer);
               break;
          case CAT_DECIMAL:
               ce_message("  %f", arg->decimal);
               break;
          case CAT_STRING:
               ce_message("  '%s'", arg->string);
               break;
          }
     }
}

void command_reload_buffer(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: reload_buffer");
          ce_message("descr: re-load the file that backs the buffer, overwriting any changes");
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
     ce_message("usage: syntax [style]");
     ce_message("descr: change the syntax mode of the current buffer");
     ce_message("style: 'c', 'cpp', 'python', 'java', 'config', 'diff', 'plain'");
}

void command_syntax(Command_t* command, void* user_data)
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

void command_quit_all(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: quit_all");
          ce_message("descr: exit ce");
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

void command_view_split(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: [v]split");
          ce_message("descr: split the currently selected window into 2 windows");
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

void command_view_close(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: view_close");
          ce_message("descr: close the current view");
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Point_t* cursor = &buffer_view->cursor;

     if(config_state->input.type > INPUT_NONE){
          input_cancel(&config_state->input, &config_state->tab_current->view_current, &config_state->vim_state);
          return;
     }

     // NOTE: probably not exactly where we want this?
     if(config_state->vim_state.recording_macro){
          vim_stop_recording_macro(&config_state->vim_state);
          return;
     }

     // if this is the only view left, don't kill it !
     if(config_state->tab_current == config_state->tab_head &&
        config_state->tab_current->next == NULL &&
        config_state->tab_current->view_current == config_state->tab_current->view_head &&
        config_state->tab_current->view_current->next_horizontal == NULL &&
        config_state->tab_current->view_current->next_vertical == NULL ){
          return;
     }

     Point_t save_cursor_on_terminal = misc_get_cursor_on_user_terminal(cursor, buffer_view, config_state->line_number_type);
     config_state->tab_current->view_current->buffer->cursor = config_state->tab_current->view_current->cursor;

     if(ce_remove_view(&config_state->tab_current->view_head, config_state->tab_current->view_current)){
          // if head is NULL, then we have removed the view head, and there were no other views, head is NULL
          if(!config_state->tab_current->view_head){
               if(config_state->tab_current->next){
                    config_state->tab_current->next = config_state->tab_current->next;
                    TabView_t* tmp = config_state->tab_current;
                    config_state->tab_current = config_state->tab_current->next;
                    tab_view_remove(&config_state->tab_head, tmp);
                    return;
               }else{

                    TabView_t* itr = config_state->tab_head;
                    while(itr && itr->next != config_state->tab_current) itr = itr->next;
                    tab_view_remove(&config_state->tab_head, config_state->tab_current);
                    config_state->tab_current = itr;
                    return;
               }
          }

          if(config_state->tab_current->view_current == config_state->tab_current->view_previous){
               config_state->tab_current->view_previous = NULL;
          }

          Point_t top_left;
          Point_t bottom_right;
          misc_get_user_terminal_view_rect(config_state->tab_head, &top_left, &bottom_right);

          ce_calc_views(config_state->tab_current->view_head, top_left, bottom_right);
          BufferView_t* new_view = ce_find_view_at_point(config_state->tab_current->view_head, save_cursor_on_terminal);
          if(new_view){
               config_state->tab_current->view_current = new_view;
          }else{
               config_state->tab_current->view_current = config_state->tab_current->view_head;
          }

          terminal_resize_if_in_view(config_state->tab_current->view_head, config_state->terminal_head);
     }
}

static void command_cscope_goto_definition_help()
{
     ce_message("usage: cscope_goto_definition <symbol>");
     ce_message("descr: jump to the definition of the specified symbol. If no symbol is specified, use word under cursor.");
}

void command_cscope_goto_definition(Command_t* command, void* user_data)
{
     if(command->arg_count > 1){
          command_cscope_goto_definition_help();
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     if(command->arg_count == 0){
          BufferView_t* buffer_view = config_state->tab_current->view_current;
          Buffer_t* buffer = buffer_view->buffer;
          Point_t* cursor = &buffer_view->cursor;

          Point_t word_start, word_end;
          if(!ce_get_word_at_location(buffer, *cursor, &word_start, &word_end)) return;
          int len = (word_end.x - word_start.x) + 1;
          char* search_word = strndupa(buffer->lines[cursor->y] + word_start.x, len);
          dest_cscope_goto_definition(config_state->tab_current->view_current, command_data->head, search_word);
          return;
     }else if(command->args[0].type != CAT_STRING){
          command_cscope_goto_definition_help();
          return;
     }

     dest_cscope_goto_definition(config_state->tab_current->view_current, command_data->head, command->args[0].string);
}

void command_goto_file_under_cursor(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: goto_file_under_cursor");
          ce_message("descr: checks the word under the cursor for a valid file, if valid opens that file");
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Buffer_t* buffer = buffer_view->buffer;
     Point_t* cursor = &buffer_view->cursor;

     if(!buffer->lines[cursor->y]) return;
     Point_t word_start;
     Point_t word_end;
     if(!ce_get_word_at_location(buffer, *cursor, &word_start, &word_end)) return;

     // expand left to pick up the beginning of a path
     char check_word_char = 0;
     while(true){
          Point_t save_word_start = word_start;

          if(!ce_move_cursor_to_beginning_of_word(buffer, &word_start, true)) break;
          if(!ce_get_char(buffer, word_start, &check_word_char)) break;
          // TODO: probably need more rules for matching filepaths
          if(isalpha(check_word_char) || isdigit(check_word_char) || check_word_char == '/'){
               continue;
          }else{
               word_start = save_word_start;
               break;
          }
     }

     // expand right to pick up the full path
     while(true){
          Point_t save_word_end = word_end;

          if(!ce_move_cursor_to_end_of_word(buffer, &word_end, true)) break;
          if(!ce_get_char(buffer, word_end, &check_word_char)) break;
          // TODO: probably need more rules for matching filepaths
          if(isalpha(check_word_char) || isdigit(check_word_char) || check_word_char == '/'){
               continue;
          }else{
               word_end = save_word_end;
               break;
          }
     }

     word_end.x++;

     char period = 0;
     if(!ce_get_char(buffer, word_end, &period)) return;
     if(period != '.') return;
     Point_t extension_start;
     Point_t extension_end;
     if(!ce_get_word_at_location(buffer, (Point_t){word_end.x + 1, word_end.y}, &extension_start, &extension_end)) return;
     extension_end.x++;
     char filename[PATH_MAX];
     snprintf(filename, PATH_MAX, "%.*s.%.*s",
              (int)(word_end.x - word_start.x), buffer->lines[word_start.y] + word_start.x,
              (int)(extension_end.x - extension_start.x), buffer->lines[extension_start.y] + extension_start.x);

     if(access(filename, F_OK) != 0){
          ce_message("no such file: '%s' to go to", filename);
          return;
     }

     BufferNode_t* node = buffer_create_from_file(command_data->head, filename);
     if(node){
          buffer_view->buffer = node->buffer;
          buffer_view->cursor = (Point_t){0, 0};
     }
}

void command_noh(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: noh");
          ce_message("descr: toggle off search highlighting (until the next search)");
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     config_state->do_not_highlight_search = true;
}

static void command_line_number_help()
{
     ce_message("usage: line_number [style]");
     ce_message("descr: change the global mode in which line number are drawn");
     ce_message(" mode: 'none', 'absolute', 'relative', 'both'");
}

void command_line_number(Command_t* command, void* user_data)
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
     ce_message("usage: highlight_line [style]");
     ce_message("descr: change the global mode in which the current line is highlighted");
     ce_message(" mode: 'none', 'text', 'entire'");
}

void command_highlight_line(Command_t* command, void* user_data)
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

void command_new_buffer(Command_t* command, void* user_data)
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
          ce_message("usage: new_buffer [optional filename]");
          return;
     }
}

static void command_rename_help()
{
     ce_message("usage: rename [string]");
     ce_message("descr: rename the current buffer");
}

void command_rename(Command_t* command, void* user_data)
{
     if(command->arg_count != 1){
          command_rename_help();
          return;
     }

     if(command->args[0].type != CAT_STRING){
          command_rename_help();
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

void command_save(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: save");
          ce_message("descr: save the current buffer");
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     Buffer_t* buffer = config_state->tab_current->view_current->buffer;

     ce_save_buffer(buffer, buffer->filename);
}

void command_save_and_close_view(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: save");
          ce_message("descr: save the current buffer");
          return;
     }

     command_save(command, user_data);
     command_view_close(command, user_data);
}

void command_show_buffers(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: buffers");
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     view_switch_to_buffer_list(&config_state->buffer_list_buffer,
                                config_state->tab_current->view_current,
                                config_state->tab_current->view_head,
                                *command_data->head);
     info_update_buffer_list_buffer(&config_state->buffer_list_buffer, *command_data->head);
}

void command_show_marks(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: show_marks");
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     Buffer_t* buffer = config_state->tab_current->view_current->buffer;

     info_update_mark_list_buffer(&config_state->mark_list_buffer, buffer);
     view_override_with_buffer(config_state->tab_current->view_current, &config_state->mark_list_buffer, &config_state->buffer_before_query);
}

void command_show_macros(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: show_macros");
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     info_update_macro_list_buffer(&config_state->macro_list_buffer, &config_state->vim_state);
     view_override_with_buffer(config_state->tab_current->view_current, &config_state->macro_list_buffer, &config_state->buffer_before_query);
}

void command_show_yanks(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: show_yanks");
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     info_update_yank_list_buffer(&config_state->yank_list_buffer, config_state->vim_state.yank_head);
     view_override_with_buffer(config_state->tab_current->view_current, &config_state->yank_list_buffer, &config_state->buffer_before_query);
}

void command_macro_backslashes(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: macro_backslashes");
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

static void command_view_scroll_help()
{
     ce_message("usage: view_scroll [mode]");
     ce_message("descr: scroll the view keeping the cursor on screen based on the mode specified.");
     ce_message(" mode: 'top', 'center', 'bottom'");
}

void command_view_scroll(Command_t* command, void* user_data)
{
     if(command->arg_count != 1){
          command_view_scroll_help();
          return;
     }

     if(command->args[0].type != CAT_STRING){
          command_view_scroll_help();
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Point_t* cursor = &buffer_view->cursor;

     if(strcmp(command->args[0].string, "top") == 0){
          Point_t location = (Point_t){0, cursor->y};
          view_scroll_to_location(buffer_view, &location);
     }else if(strcmp(command->args[0].string, "center") == 0){
          view_center(buffer_view);
     }else if(strcmp(command->args[0].string, "bottom") == 0){
          Point_t location = (Point_t){0, cursor->y - buffer_view->bottom_right.y};
          view_scroll_to_location(buffer_view, &location);
     }
}

static void command_view_switch_help()
{
     ce_message("usage: view_switch [mode]");
     ce_message("descr: switch to an adjacent view in the direction specified.");
     ce_message(" mode: 'up', 'down', 'left', 'right'");
}

void command_view_switch(Command_t* command, void* user_data)
{
     if(command->arg_count != 1){
          command_view_switch_help();
          return;
     }

     if(command->args[0].type != CAT_STRING){
          command_view_switch_help();
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Point_t* cursor = &buffer_view->cursor;
     Point_t point = {};

     if(strcmp(command->args[0].string, "up") == 0){
          point = (Point_t){cursor->x - config_state->tab_current->view_current->left_column + config_state->tab_current->view_current->top_left.x,
                            config_state->tab_current->view_current->top_left.y - 2};
     }else if(strcmp(command->args[0].string, "down") == 0){
          point = (Point_t){cursor->x - config_state->tab_current->view_current->left_column + config_state->tab_current->view_current->top_left.x,
                            config_state->tab_current->view_current->bottom_right.y + 2}; // account for window separator
     }else if(strcmp(command->args[0].string, "left") == 0){
          point = (Point_t){config_state->tab_current->view_current->top_left.x - 2, // account for window separator
                            cursor->y - config_state->tab_current->view_current->top_row + config_state->tab_current->view_current->top_left.y};
     }else if(strcmp(command->args[0].string, "right") == 0){
          point = (Point_t){config_state->tab_current->view_current->bottom_right.x + 2, // account for window separator
                            cursor->y - config_state->tab_current->view_current->top_row + config_state->tab_current->view_current->top_left.y};
     }else{
          command_view_switch_help();
          return;
     }

     view_switch_to_point(config_state->input.type > INPUT_NONE, config_state->input.view, &config_state->vim_state,
                             config_state->tab_current, config_state->terminal_head, &config_state->terminal_current, point);
}

void command_tab_new(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: tab_new");
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     TabView_t* new_tab = tab_view_insert(config_state->tab_head);
     if(!new_tab) return;

     // copy view attributes from the current view
     *new_tab->view_head = *config_state->tab_current->view_current;
     new_tab->view_head->next_horizontal = NULL;
     new_tab->view_head->next_vertical = NULL;
     new_tab->view_current = new_tab->view_head;

     config_state->tab_current = new_tab;
}

void command_tab_next(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: tab_next");
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     if(config_state->tab_current->next){
          config_state->tab_current = config_state->tab_current->next;
     }else{
          config_state->tab_current = config_state->tab_head;
     }
}

void command_tab_previous(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: tab_previous");
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     if(config_state->tab_current == config_state->tab_head){
          // find tail
          TabView_t* itr = config_state->tab_head;
          while(itr->next) itr = itr->next;
          config_state->tab_current = itr;
     }else{
          TabView_t* itr = config_state->tab_head;
          while(itr && itr->next != config_state->tab_current) itr = itr->next;

          // what if we don't find our current tab and hit the end of the list?!
          config_state->tab_current = itr;
     }
}

static void command_move_on_screen_help()
{
     ce_message("usage: move_on_screen [mode]");
     ce_message("descr: move the cursor to the part of the view specified by the mode.");
     ce_message(" mode: 'top', 'center', 'bottom'");
}

void command_move_on_screen(Command_t* command, void* user_data)
{
     if(command->arg_count != 1){
          command_move_on_screen_help();
          return;
     }

     if(command->args[0].type != CAT_STRING){
          command_move_on_screen_help();
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Buffer_t* buffer = buffer_view->buffer;
     Point_t* cursor = &buffer_view->cursor;

     if(strcmp(command->args[0].string, "top") == 0){
          Point_t location = {cursor->x, buffer_view->top_row};
          ce_set_cursor(buffer, cursor, location);
     }else if(strcmp(command->args[0].string, "center") == 0){
          int64_t view_height = buffer_view->bottom_right.y - buffer_view->top_left.y;
          Point_t location = {cursor->x, buffer_view->top_row + (view_height/2)};
          ce_set_cursor(buffer, cursor, location);
     }else if(strcmp(command->args[0].string, "bottom") == 0){
          int64_t view_height = buffer_view->bottom_right.y - buffer_view->top_left.y;
          Point_t location = {cursor->x, buffer_view->top_row + view_height};
          ce_set_cursor(buffer, cursor, location);
     }
}

static void command_move_half_page_help()
{
     ce_message("usage: move_half_page [dir]");
     ce_message("descr: move the cursor up or down a half page specified by the dir.");
     ce_message("  dir: 'up', 'down'");
}

void command_move_half_page(Command_t* command, void* user_data)
{
     if(command->arg_count != 1){
          command_move_half_page_help();
          return;
     }

     if(command->args[0].type != CAT_STRING){
          command_move_half_page_help();
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     if(strcmp(command->args[0].string, "up") == 0){
          view_move_cursor_half_page_up(config_state->tab_current->view_current);
     }else if(strcmp(command->args[0].string, "down") == 0){
          view_move_cursor_half_page_down(config_state->tab_current->view_current);
     }
}

void command_switch_buffer_dialogue(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: switch_buffer_dialogue");
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     if(config_state->input.type > INPUT_NONE &&
        config_state->tab_current->view_current == config_state->input.view){
          // pass
     }else{
          pthread_mutex_lock(&completion_lock);
          auto_complete_free(&config_state->auto_complete);
          BufferNode_t* itr = *command_data->head;
          while(itr){
               auto_complete_insert(&config_state->auto_complete, itr->buffer->name, NULL);
               itr = itr->next;
          }
          auto_complete_start(&config_state->auto_complete, ACT_OCCURANCE, (Point_t){0, 0});
          completion_update_buffer(config_state->completion_buffer, &config_state->auto_complete, NULL);
          pthread_mutex_unlock(&completion_lock);

          input_start(&config_state->input, &config_state->tab_current->view_current, &config_state->vim_state,
                      "Switch Buffer", INPUT_SWITCH_BUFFER);
     }
}

void command_command_dialogue(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: command_dialogue");
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     pthread_mutex_lock(&completion_lock);
     auto_complete_free(&config_state->auto_complete);
     for(int64_t i = 0; i < config_state->command_entry_count; ++i){
          if(config_state->command_entries[i].hidden) continue;
          auto_complete_insert(&config_state->auto_complete, config_state->command_entries[i].name, NULL);
     }
     auto_complete_start(&config_state->auto_complete, ACT_OCCURANCE, (Point_t){0, 0});
     completion_update_buffer(config_state->completion_buffer, &config_state->auto_complete, NULL);
     pthread_mutex_unlock(&completion_lock);
     input_start(&config_state->input, &config_state->tab_current->view_current, &config_state->vim_state,
                 "Command", INPUT_COMMAND);
}

static void command_search_dialogue_help()
{
     ce_message("usage: search [dir]");
     ce_message("descr: open dialogue to start an incremental search.");
     ce_message("  dir: 'up', 'down'");
}

void command_search_dialogue(Command_t* command, void* user_data)
{
     if(command->arg_count != 1){
          command_search_dialogue_help();
          return;
     }

     if(command->args[0].type != CAT_STRING){
          command_search_dialogue_help();
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Buffer_t* buffer = buffer_view->buffer;
     Point_t* cursor = &buffer_view->cursor;

     if(strcmp(command->args[0].string, "up") == 0){
          input_start(&config_state->input, &config_state->tab_current->view_current, &config_state->vim_state,
                      "Reverse Regex Search", INPUT_SEARCH);
          config_state->vim_state.search.direction = CE_UP;
     }else if(strcmp(command->args[0].string, "down") == 0){
          input_start(&config_state->input, &config_state->tab_current->view_current, &config_state->vim_state,
                      "Regex Search", INPUT_REVERSE_SEARCH);
          config_state->vim_state.search.direction = CE_DOWN;
     }

     config_state->vim_state.search.start = *cursor;

     JumpArray_t* jump_array = &((BufferViewState_t*)(buffer_view->user_data))->jump_array;
     jump_insert(jump_array, buffer->filename, *cursor);
}

void command_load_file_dialogue(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: load_file_dialogue");
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Buffer_t* buffer = buffer_view->buffer;
     Point_t* cursor = &buffer_view->cursor;

     buffer->cursor = buffer_view->cursor;

     input_start(&config_state->input, &config_state->tab_current->view_current, &config_state->vim_state,
                 "Load File", INPUT_LOAD_FILE);

     // when searching for a file, see if we would like to use a path other than the one ce was run at.
     TerminalNode_t* terminal_node = is_terminal_buffer(config_state->terminal_head, buffer);
     if(terminal_node){
          // if we are looking at a terminal, use the terminal's cwd
          config_state->input.load_file_search_path = terminal_get_current_directory(&terminal_node->terminal);
     }else{
          // if our file has a relative path in it, use that
          char* last_slash = strrchr(buffer->filename, '/');
          if(last_slash){
               int64_t path_len = last_slash - buffer->filename;
               config_state->input.load_file_search_path = malloc(path_len + 1);
               strncpy(config_state->input.load_file_search_path, buffer->filename, path_len);
               config_state->input.load_file_search_path[path_len] = 0;
          }
     }

     completion_calc_start_and_path(&config_state->auto_complete,
                                    config_state->input.buffer.lines[0],
                                    *cursor,
                                    config_state->completion_buffer,
                                    config_state->input.load_file_search_path);
}

void command_replace_dialogue(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: replace_dialogue");
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     if(config_state->vim_state.mode == VM_VISUAL_RANGE || config_state->vim_state.mode == VM_VISUAL_LINE){
          input_start(&config_state->input, &config_state->tab_current->view_current, &config_state->vim_state,
                      "Visual Replace", INPUT_REPLACE);
     }else{
          input_start(&config_state->input, &config_state->tab_current->view_current, &config_state->vim_state,
                      "Replace", INPUT_REPLACE);
     }
}

void command_cancel_dialogue(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: cancel_dialogue");
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     if(config_state->input.type > INPUT_NONE) input_cancel(&config_state->input, &config_state->tab_current->view_current, &config_state->vim_state);
}

void command_terminal_goto(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: terminal_goto");
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Buffer_t* buffer = buffer_view->buffer;

     if(config_state->terminal_current){
          // revive terminal if it is dead !
          if(!config_state->terminal_current->terminal.is_alive){
               pthread_cancel(config_state->terminal_current->terminal.reader_thread);
               pthread_join(config_state->terminal_current->terminal.reader_thread, NULL);

               if(!terminal_start_in_view(buffer_view, config_state->terminal_current, config_state)){
                    return;
               }
          }

          BufferView_t* terminal_view = ce_buffer_in_view(config_state->tab_current->view_head,
                                                          config_state->terminal_current->buffer);
          if(terminal_view){
               // if terminal is already in view
               config_state->tab_current->view_previous = config_state->tab_current->view_current; // save previous view
               config_state->tab_current->view_current = terminal_view;
               buffer_view = terminal_view;
          }else{
               // otherwise use the current view
               buffer->cursor = buffer_view->cursor; // save cursor before switching
               buffer_view->buffer = config_state->terminal_current->buffer;
          }

          buffer_view->cursor = config_state->terminal_current->terminal.cursor;
          buffer_view->top_row = 0;
          buffer_view->left_column = 0;
          view_follow_cursor(buffer_view, config_state->line_number_type);
          config_state->vim_state.mode = VM_INSERT;
          return;
     }else{
          command_terminal_new(command, user_data);
     }
}

void command_terminal_new(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: terminal_new");
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Buffer_t* buffer = buffer_view->buffer;

     TerminalNode_t* node = calloc(1, sizeof(*node));
     if(!node) return;

     // setup terminal buffer buffer_state
     BufferState_t* terminal_buffer_state = calloc(1, sizeof(*terminal_buffer_state));
     if(!terminal_buffer_state){
          ce_message("failed to allocate buffer state.");
          return;
     }

     node->buffer = calloc(1, sizeof(*node->buffer));
     node->buffer->absolutely_no_line_numbers_under_any_circumstances = true;
     node->buffer->user_data = terminal_buffer_state;

     TerminalHighlight_t* terminal_highlight_data = calloc(1, sizeof(TerminalHighlight_t));
     terminal_highlight_data->terminal = &node->terminal;
     node->buffer->syntax_fn = terminal_highlight;
     node->buffer->syntax_user_data = terminal_highlight_data;

     BufferNode_t* new_buffer_node = ce_append_buffer_to_list(command_data->head, node->buffer);
     if(!new_buffer_node){
          ce_message("failed to add shell command buffer to list");
          return;
     }

     if(!terminal_start_in_view(buffer_view, node, config_state)){
          return;
     }

     buffer->cursor = buffer_view->cursor; // save cursor before switching
     buffer_view->buffer = node->buffer;

     // append the node to the list
     int64_t id = 1;
     if(config_state->terminal_head){
          TerminalNode_t* itr = config_state->terminal_head;
          while(itr->next){
               itr = itr->next;
               id++;
          }
          id++;
          itr->next = node;
     }else{
          config_state->terminal_head = node;
     }

     // name terminal
     char buffer_name[64];
     snprintf(buffer_name, 64, "[terminal %" PRId64 "]", id);
     node->buffer->name = strdup(buffer_name);

     config_state->terminal_current = node;
     config_state->vim_state.mode = VM_INSERT;
}

static void command_terminal_jump_to_dest_help()
{
     ce_message("usage: terminal_jump_to_dest [dir]");
     ce_message("descr: jump the current view to the next destination found in the current terminal's output.");
     ce_message("  dir: 'up', 'down'");
}

void command_terminal_jump_to_dest(Command_t* command, void* user_data)
{
     if(command->arg_count != 1){
          command_terminal_jump_to_dest_help();
          return;
     }

     if(command->args[0].type != CAT_STRING){
          command_terminal_jump_to_dest_help();
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     bool down = false;

     if(strcmp(command->args[0].string, "up") == 0){
          // pass
     }else if(strcmp(command->args[0].string, "down") == 0){
          down = true;
     }else{
          command_terminal_jump_to_dest_help();
          return;
     }

     if(config_state->input.type == INPUT_NONE){
          dest_jump_to_next_in_terminal(command_data->head, config_state->terminal_head, &config_state->terminal_current,
                                        config_state->tab_current->view_head, config_state->tab_current->view_current,
                                        down);
     }
}

static void command_terminal_run_man_help()
{
     ce_message("usage: terminal_run_man <symbol>");
     ce_message("descr: run 'man -Pcat <symbol>' in current terminal. If no symbol is specified, word under cursor is used.");
}

void command_terminal_run_man(Command_t* command, void* user_data)
{
     if(command->arg_count > 1){
          command_terminal_run_man_help();
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     if(command->arg_count == 0){
          BufferView_t* buffer_view = config_state->tab_current->view_current;
          Buffer_t* buffer = buffer_view->buffer;
          Point_t* cursor = &buffer_view->cursor;
          Point_t word_start;
          Point_t word_end;
          if(!ce_get_word_at_location(buffer, *cursor, &word_start, &word_end)) return;
          int len = (word_end.x - word_start.x) + 1;

          char cmd_string[BUFSIZ];
          snprintf(cmd_string, BUFSIZ, "man --pager=cat %*.*s", len, len, buffer->lines[cursor->y] + word_start.x);
          terminal_in_view_run_command(config_state->terminal_head, config_state->tab_current->view_head, cmd_string);
          return;
     }else if(command->args[0].type != CAT_STRING){
          command_terminal_run_man_help();
          return;
     }

     char cmd_string[BUFSIZ];
     snprintf(cmd_string, BUFSIZ, "man --pager=cat %s", command->args[0].string);
     terminal_in_view_run_command(config_state->terminal_head, config_state->tab_current->view_head, cmd_string);
}

static void command_terminal_run_command_help()
{
     ce_message("usage: terminal_run_command [command]");
     ce_message("descr: run specified command in current terminal");
}

void command_terminal_run_command(Command_t* command, void* user_data)
{
     if(command->arg_count != 1){
          command_log(command);
          command_terminal_run_command_help();
          return;
     }

     if(command->args[0].type != CAT_STRING){
          command_log(command);
          command_terminal_run_command_help();
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     terminal_in_view_run_command(config_state->terminal_head, config_state->tab_current->view_head, command->args[0].string);
}

void command_jump_next(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: jump_next");
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Buffer_t* buffer = buffer_view->buffer;
     Point_t* cursor = &buffer_view->cursor;

     JumpArray_t* jump_array = &((BufferViewState_t*)(buffer_view->user_data))->jump_array;
     if(jump_array->jump_current){
          if(!jump_array->jumps[jump_array->jump_current].filepath[0]){
               jump_insert(jump_array, buffer->filename, *cursor);
               jump_to_previous(jump_array);
          }
          const Jump_t* jump = jump_to_previous(jump_array);
          if(jump){
               BufferNode_t* new_buffer_node = buffer_create_from_file(command_data->head, jump->filepath);
               if(new_buffer_node){
                    buffer_view->buffer = new_buffer_node->buffer;
                    buffer_view->cursor = jump->location;
                    view_center(buffer_view);
               }
          }
     }
}

void command_jump_previous(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: jump_previous");
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     BufferView_t* buffer_view = config_state->tab_current->view_current;

     JumpArray_t* jump_array = &((BufferViewState_t*)(buffer_view->user_data))->jump_array;
     const Jump_t* jump = jump_to_next(jump_array);
     if(jump){
          BufferNode_t* new_buffer_node = buffer_create_from_file(command_data->head, jump->filepath);
          if(new_buffer_node){
               buffer_view->buffer = new_buffer_node->buffer;
               buffer_view->cursor = jump->location;
               view_center(buffer_view);
          }
     }
}

void command_redraw(Command_t* command, void* user_data)
{
     // NOTE: how silly this seems to the average on-looker
     (void)(command);
     (void)(user_data);
     clear();
}
