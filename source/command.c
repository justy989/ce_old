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
          ce_message("usage: view");
          ce_message("descr: split the currently selected window into 2 windows");
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

void command_cscope_goto_definition(Command_t* command, void* user_data)
{
     if(command->arg_count != 1 || command->args[0].type != CAT_STRING){
          ce_message("usage: cscope_goto_definition <symbol>");
          ce_message("descr: jump to the definition of the specified symbol");
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     dest_cscope_goto_definition(config_state->tab_current->view_current, command_data->head, command->args[0].string);
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

void command_buffers(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: buffers");
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     view_switch_to_buffer_list(&command_data->config_state->buffer_list_buffer,
                                command_data->config_state->tab_current->view_current,
                                command_data->config_state->tab_current->view_head,
                                *command_data->head);
     info_update_buffer_list_buffer(&command_data->config_state->buffer_list_buffer, *command_data->head);
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

void command_cancel_dialogue(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message("usage: switch_buffer_dialogue");
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     if(config_state->input.type > INPUT_NONE) input_cancel(&config_state->input, &config_state->tab_current->view_current, &config_state->vim_state);
}
