#include <assert.h>
#include <ctype.h>
#include <ftw.h>
#include <inttypes.h>
#include <unistd.h>
#include <signal.h>

#include "ce_config.h"
#include "syntax.h"
#include "text_history.h"
#include "view.h"
#include "buffer.h"
#include "input.h"
#include "destination.h"
#include "completion.h"
#include "info.h"
#include "terminal_helper.h"
#include "misc.h"

#define SCROLL_LINES 1

int g_last_key = 0;

void sigint_handler(int signal)
{
     ce_message("recieved signal %d", signal);
}

void mouse_handle_event(BufferView_t* buffer_view, VimState_t* vim_state, Input_t* input, TabView_t* tab_current,
                        TerminalNode_t* terminal_head, TerminalNode_t** terminal_current,
                        LineNumberType_t line_number_type)
{
     MEVENT event;
     if(getmouse(&event) == OK){
          bool enter_insert = vim_state->mode == VM_INSERT;
          if(enter_insert){
               ce_clamp_cursor(buffer_view->buffer, &buffer_view->cursor);
               vim_enter_normal_mode(vim_state);
          }
          if(event.bstate & BUTTON1_PRESSED){ // Left click OSX
               Point_t click = {event.x, event.y};
               view_switch_to_point(input->type > INPUT_NONE, input->view, vim_state, tab_current, terminal_head, terminal_current, click);
               click = (Point_t) {event.x - (buffer_view->top_left.x - buffer_view->left_column),
                                  event.y - (buffer_view->top_left.y - buffer_view->top_row)};
               click.x -= ce_get_line_number_column_width(line_number_type, buffer_view->buffer->line_count,
                                                          buffer_view->top_left.y, buffer_view->bottom_right.y);
               if(click.x < 0) click.x = 0;
               ce_set_cursor(buffer_view->buffer, &buffer_view->cursor, click);
          }
#ifdef SCROLL_SUPPORT
          // This feature is currently unreliable and is only known to work for Ryan :)
          else if(event.bstate & (BUTTON_ALT | BUTTON2_CLICKED)){
               Point_t next_line = {0, cursor->y + SCROLL_LINES};
               if(ce_point_on_buffer(buffer, &next_line)){
                    Point_t scroll_location = {0, buffer_view->top_row + SCROLL_LINES};
                    view_scroll_to_location(buffer_view, &scroll_location);
                    if(buffer_view->cursor.y < buffer_view->top_row)
                         ce_move_cursor(buffer, cursor, (Point_t){0, SCROLL_LINES});
               }
          }else if(event.bstate & BUTTON4_TRIPLE_CLICKED){
               Point_t next_line = {0, cursor->y - SCROLL_LINES};
               if(ce_point_on_buffer(buffer, &next_line)){
                    Point_t scroll_location = {0, buffer_view->top_row - SCROLL_LINES};
                    view_scroll_to_location(buffer_view, &scroll_location);
                    if(buffer_view->cursor.y > buffer_view->top_row + (buffer_view->bottom_right.y - buffer_view->top_left.y))
                         ce_move_cursor(buffer, cursor, (Point_t){0, -SCROLL_LINES});
               }
          }
#endif
          // if we left insert and haven't switched views, enter insert mode
          if(enter_insert){
               vim_enter_insert_mode(vim_state, buffer_view->buffer);
          }
     }
}

static int int_strneq(int* a, int* b, size_t len)
{
     for(size_t i = 0; i < len; ++i){
          if(!*a) return false;
          if(!*b) return false;
          if(*a != *b) return false;
          a++;
          b++;
     }

     return true;
}

static void convert_bind_defs(KeyBinds_t* binds, KeyBindDef_t* bind_defs, int64_t bind_def_count)
{
     binds->count = bind_def_count;
     binds->binds = malloc(binds->count * sizeof(*binds->binds));

     for(int64_t i = 0; i < binds->count; ++i){
          command_parse(&binds->binds[i].command, bind_defs[i].command);
          binds->binds[i].key_count = 0;

          for(int k = 0; k < 4; ++k){
               if(bind_defs[i].keys[k] == 0) break;
               binds->binds[i].key_count++;
          }

          if(!binds->binds[i].key_count) continue;

          binds->binds[i].keys = malloc(binds->binds[i].key_count * sizeof(binds->binds[i].keys[0]));

          for(int k = 0; k < binds->binds[i].key_count; ++k){
               binds->binds[i].keys[k] = bind_defs[i].keys[k];
          }
     }
}

static bool confirm_action(ConfigState_t* config_state, BufferNode_t** head)
{
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Buffer_t* buffer = buffer_view->buffer;
     Point_t* cursor = &buffer_view->cursor;
     BufferState_t* buffer_state = buffer->user_data;

     if(config_state->input.type > INPUT_NONE && buffer_view == config_state->input.view){
          int input_type = config_state->input.type;
          input_end(&config_state->input, &config_state->vim_state);
          config_state->tab_current->view_current = config_state->input.view_save;

          // update convenience vars
          buffer_view = config_state->tab_current->view_current;
          buffer = config_state->tab_current->view_current->buffer;
          cursor = &config_state->tab_current->view_current->cursor;
          buffer_state = buffer->user_data;

          switch(input_type){
          default:
               break;
          case INPUT_QUIT:
               if(!config_state->input.buffer.line_count) break;

               if(tolower(config_state->input.buffer.lines[0][0]) == 'y'){
                    config_state->quit = true;
               }
               return true;
          case INPUT_SWITCH_BUFFER:
          {
               if(!config_state->input.buffer.line_count) break;

               // if auto complete has a current matching value, overwrite what the user wrote with that completion
               if(auto_completing(&config_state->auto_complete) && config_state->auto_complete.current){
                    int64_t len = strlen(config_state->input.buffer.lines[0]);
                    if(!ce_remove_string(&config_state->input.buffer, (Point_t){0, 0}, len)) break;
                    if(!ce_insert_string(&config_state->input.buffer, (Point_t){0, 0}, config_state->auto_complete.current->option)) break;
               }

               BufferNode_t* itr = *head;

               while(itr){
                    if(strcmp(itr->buffer->name, config_state->input.buffer.lines[0]) == 0){
                         config_state->tab_current->view_current->buffer = itr->buffer;
                         config_state->tab_current->view_current->cursor = itr->buffer->cursor;
                         view_center(config_state->tab_current->view_current);
                         break;
                    }
                    itr = itr->next;
               }

               terminal_resize_if_in_view(config_state->tab_current->view_head, config_state->terminal_head);

               // return whether we switched to a buffer or not
               return true;
          }
          case INPUT_LOAD_FILE:
          {
               if(!config_state->input.buffer.line_count) break;

               bool switched_to_open_file = false;

               // if auto complete has a current matching value, overwrite what the user wrote with that completion
               if(auto_completing(&config_state->auto_complete) && config_state->auto_complete.current){
                    char* last_slash = strrchr(config_state->input.buffer.lines[0], '/');
                    int64_t offset = 0;
                    if(last_slash) offset = (last_slash - config_state->input.buffer.lines[0]) + 1;

                    int64_t len = strlen(config_state->input.buffer.lines[0] + offset);
                    if(!ce_remove_string(&config_state->input.buffer, (Point_t){offset, 0}, len)) break;
                    if(!ce_insert_string(&config_state->input.buffer, (Point_t){offset, 0}, config_state->auto_complete.current->option)) break;
               }

               // load the buffer, either from the current working dir, or from another base filepath
               Buffer_t* new_buffer = NULL;
               if(config_state->input.load_file_search_path && config_state->input.buffer.lines[0][0] != '/'){
                    char path[BUFSIZ];
                    snprintf(path, BUFSIZ, "%s/%s", config_state->input.load_file_search_path, config_state->input.buffer.lines[0]);
                    BufferNode_t* new_buffer_node = buffer_create_from_file(head, path);
                    if(new_buffer_node) new_buffer = new_buffer_node->buffer;
               }else{
                    BufferNode_t* new_buffer_node  = buffer_create_from_file(head, config_state->input.buffer.lines[0]);
                    if(new_buffer_node) new_buffer = new_buffer_node->buffer;
               }

               if(new_buffer){
                    JumpArray_t* jump_array = &((BufferViewState_t*)(config_state->tab_current->view_current->user_data))->jump_array;
                    jump_insert(jump_array, config_state->tab_current->view_current->buffer->filename,
                                config_state->tab_current->view_current->buffer->cursor);
                    config_state->tab_current->view_current->buffer = new_buffer;
                    config_state->tab_current->view_current->cursor = (Point_t){0, 0};
                    switched_to_open_file = true;
               }

               // free the search path so we can re-use it
               free(config_state->input.load_file_search_path);
               config_state->input.load_file_search_path = NULL;

               if(!switched_to_open_file){
                    config_state->tab_current->view_current->buffer = (*head)->buffer; // message buffer
                    config_state->tab_current->view_current->cursor = (Point_t){0, 0};
               }

               return true;
          } break;
          case INPUT_SEARCH:
          case INPUT_REVERSE_SEARCH:
               if(!config_state->input.buffer.line_count) break;

               input_commit_to_history(&config_state->input.buffer, &config_state->input.search_history);
               vim_yank_add(&config_state->vim_state.yank_head, '/', strdup(config_state->input.buffer.lines[0]), YANK_NORMAL);
               return true;
          case INPUT_REPLACE:
          {
               if(!config_state->input.buffer.line_count) break;
               if(!config_state->vim_state.search.valid_regex) break;

               VimYankNode_t* yank = vim_yank_find(config_state->vim_state.yank_head, '/');
               if(!yank) break;

               const char* search_str = yank->text;
               // NOTE: allow empty string to replace search
               int64_t search_len = strlen(search_str);
               if(!search_len) break;

               char* replace_str = ce_dupe_buffer(&config_state->input.buffer);
               int64_t replace_len = strlen(replace_str);
               Point_t begin = config_state->input.view_save->buffer->highlight_start;
               Point_t end = config_state->input.view_save->buffer->highlight_end;
               if(end.x < 0) ce_move_cursor_to_end_of_file(config_state->input.view_save->buffer, &end);

               Point_t match = {};
               int64_t match_len = 0;
               int64_t replace_count = 0;
               while(ce_find_regex(buffer, begin, &config_state->vim_state.search.regex, &match, &match_len, CE_DOWN)){
                    if(ce_point_after(match, end)) break;
                    Point_t end_match = match;
                    ce_advance_cursor(buffer, &end_match, match_len - 1);
                    char* searched_dup = ce_dupe_string(buffer, match, end_match);
                    if(!ce_remove_string(buffer, match, match_len)) break;
                    if(replace_len){
                         if(!ce_insert_string(buffer, match, replace_str)) break;
                    }
                    ce_commit_change_string(&buffer_state->commit_tail, match, match, match, strdup(replace_str),
                                            searched_dup, BCC_KEEP_GOING);
                    begin = match;
                    replace_count++;
               }

               if(buffer_state->commit_tail) buffer_state->commit_tail->commit.chain = BCC_STOP;

               if(replace_count){
                    ce_message("replaced %" PRId64 " matches", replace_count);
               }else{
                    ce_message("no matches found to replace");
               }

               *cursor = begin;
               view_center(buffer_view);
               free(replace_str);
               return true;
          } break;
          case INPUT_EDIT_MACRO:
          {
               if(!config_state->input.buffer.lines) break;

               int64_t line = config_state->input.view_save->cursor.y - 1; // account for buffer list row header
               if(line < 0) break;

               VimMacroNode_t* macro = vim_macro_find(config_state->vim_state.macro_head, config_state->editting_register);
               if(!macro) break;

               free(macro->command);
               int* new_macro_string = vim_char_string_to_command_string(config_state->input.buffer.lines[0]);

               if(new_macro_string){
                    macro->command = new_macro_string;
               }else{
                    ce_message("invalid editted macro string");
               }

               config_state->editting_register = 0;
               return true;
          } break;
          case INPUT_EDIT_YANK:
          {
               int64_t line = config_state->input.view_save->cursor.y;
               if(line < 0) break;

               VimYankNode_t* yank = vim_yank_find(config_state->vim_state.yank_head, config_state->editting_register);
               if(!yank) break;

               char* new_yank = ce_dupe_buffer(&config_state->input.buffer);
               free((char*)(yank->text));
               yank->text = new_yank;
               config_state->editting_register = 0;
          } break;
          case INPUT_COMMAND:
          {
               if(!config_state->input.buffer.line_count) break;

               input_commit_to_history(&config_state->input.buffer, &config_state->input.command_history);

               bool alldigits = true;
               const char* itr = config_state->input.buffer.lines[0];
               while(*itr){
                    if(!isdigit(*itr)){
                         alldigits = false;
                         break;
                    }
                    itr++;
               }

               if(alldigits){
                    // goto line
                    int64_t line = atoi(config_state->input.buffer.lines[0]);
                    if(line > 0){
                         *cursor = (Point_t){0, line - 1};
                         ce_move_cursor_to_soft_beginning_of_line(buffer, cursor);
                         view_center(buffer_view);
                         JumpArray_t* jump_array = &((BufferViewState_t*)(buffer_view->user_data))->jump_array;
                         jump_insert(jump_array, buffer_view->buffer->filename, buffer_view->cursor);
                    }
               }else{
                    // if auto complete has a current matching value, overwrite what the user wrote with that completion
                    if(auto_completing(&config_state->auto_complete) && config_state->auto_complete.current){
                         int64_t len = strlen(config_state->input.buffer.lines[0]);
                         if(!ce_remove_string(&config_state->input.buffer, (Point_t){0, 0}, len)) break;
                         if(!ce_insert_string(&config_state->input.buffer, (Point_t){0, 0}, config_state->auto_complete.current->option)) break;
                    }

                    // run all commands in the input buffer
                    Command_t command = {};
                    if(!command_parse(&command, config_state->input.buffer.lines[0])){
                         ce_message("failed to parse command: '%s'", config_state->input.buffer.lines[0]);
                    }else{
                         ce_command* command_func = NULL;
                         for(int64_t i = 0; i < config_state->command_entry_count; ++i){
                              CommandEntry_t* entry = config_state->command_entries + i;
                              if(strcmp(entry->name, command.name) == 0){
                                   command_func = entry->func;
                                   break;
                              }
                         }

                         if(command_func){
                              CommandData_t command_data = {config_state, head};
                              command_func(&command, &command_data);
                         }else{
                              ce_message("unknown command: '%s'", command.name);
                         }
                    }
               }

               return true;
          } break; // NOTE: Ahhh the unreachable break in it's natural habitat
          }
     }else if(buffer_view->buffer == &config_state->buffer_list_buffer){
          int64_t line = cursor->y - 1; // account for buffer list row header
          if(line < 0) return false;
          BufferNode_t* itr = *head;

          while(line > 0){
               itr = itr->next;
               if(!itr) return false;
               line--;
          }

          if(!itr) return false;

          buffer_view->buffer = itr->buffer;
          buffer_view->cursor = itr->buffer->cursor;
          view_center(buffer_view);

          TerminalNode_t* terminal_node = is_terminal_buffer(config_state->terminal_head, itr->buffer);
          if(terminal_node){
               int64_t new_width = buffer_view->bottom_right.x - buffer_view->top_left.x;
               int64_t new_height = buffer_view->bottom_right.y - buffer_view->top_left.y;
               terminal_resize(&terminal_node->terminal, new_width, new_height);
               config_state->terminal_current = terminal_node;
          }

          return true;
     }else if(buffer_view->buffer == &config_state->mark_list_buffer){
          int64_t line = cursor->y - 1; // account for buffer list row header
          if(line < 0) return false;
          VimMarkNode_t* itr = ((BufferState_t*)(config_state->buffer_before_query->user_data))->vim_buffer_state.mark_head;

          while(line > 0){
               itr = itr->next;
               if(!itr) return false;
               line--;
          }

          if(!itr) return false;

          buffer_view->buffer = config_state->buffer_before_query;
          buffer_view->cursor.y = itr->location.y;
          ce_move_cursor_to_soft_beginning_of_line(buffer_view->buffer, &buffer_view->cursor);
          view_center(buffer_view);

          return true;
     }else if(buffer_view->buffer == &config_state->macro_list_buffer){
          int64_t line = cursor->y - 1; // account for buffer list row header
          if(line < 0) return false;
          VimMacroNode_t* itr = config_state->vim_state.macro_head;

          while(line > 0){
               itr = itr->next;
               if(!itr) return false;
               line--;
          }

          if(!itr) return false;

          input_start(&config_state->input, &config_state->tab_current->view_current, &config_state->vim_state,
                      "Edit Macro", '@');
          config_state->editting_register = itr->reg;
          vim_enter_normal_mode(&config_state->vim_state);
          char* char_command = vim_command_string_to_char_string(itr->command);
          ce_insert_string(&config_state->input.buffer, (Point_t){0,0}, char_command);
          free(char_command);
          return true;
     }else if(buffer_view->buffer == &config_state->yank_list_buffer){
          int64_t line = cursor->y;
          if(line < 0) return false;

          VimYankNode_t* itr = config_state->vim_state.yank_head;
          while(itr){
               line -= (ce_count_string_lines(itr->text) + 1);
               if(line < 0) break;

               itr = itr->next;
          }

          input_start(&config_state->input, &config_state->tab_current->view_current, &config_state->vim_state,
                      "Edit Yank", 'y');
          config_state->editting_register = itr->reg_char;
          vim_enter_normal_mode(&config_state->vim_state);
          ce_insert_string(&config_state->input.buffer, (Point_t){0,0}, itr->text);
          return true;
     }else{
          TerminalNode_t* terminal_node = is_terminal_buffer(config_state->terminal_head, buffer_view->buffer);
          if(terminal_node){
               BufferView_t* view_to_change = buffer_view;
               if(config_state->tab_current->view_previous) view_to_change = config_state->tab_current->view_previous;

               char* terminal_current_directory = terminal_get_current_directory(&terminal_node->terminal);
               if(dest_goto_file_location_in_buffer(head, buffer_view->buffer, cursor->y,
                                                    config_state->tab_current->view_head, view_to_change,
                                                    &config_state->terminal_current->last_jump_location,
                                                    terminal_current_directory)){
                    config_state->tab_current->view_current = view_to_change;
               }
               free(terminal_current_directory);
               return true;
          }
     }

     return false;
}

void draw_view_statuses(BufferView_t* view, BufferView_t* current_view, VimMode_t vim_mode,
                        char recording_macro, TerminalNode_t* terminal_current)
{
     // recursively call draw view for all statuses
     if(view->next_horizontal) draw_view_statuses(view->next_horizontal, current_view, vim_mode, recording_macro, terminal_current);
     if(view->next_vertical) draw_view_statuses(view->next_vertical, current_view, vim_mode, recording_macro, terminal_current);

     // NOTE: mode names need space at the end for OCD ppl like me
     static const char* mode_names[] = {
          "NORMAL ",
          "INSERT ",
          "VISUAL ",
          "VISUAL LINE ",
          "VISUAL BLOCK ",
     };

     Buffer_t* buffer = view->buffer;

     // put horizontal line at the bottom of the view
     attron(COLOR_PAIR(S_BORDERS));
     move(view->bottom_right.y, view->top_left.x);
     for(int i = view->top_left.x; i < view->bottom_right.x; ++i) addch(ACS_HLINE);
     int right_status_offset = 0;
     if(view->bottom_right.x == (g_terminal_dimensions->x - 1)){
          addch(ACS_HLINE);
          right_status_offset = 1;
     }

     // draw buffer flags and mode
     attron(COLOR_PAIR(S_VIEW_STATUS));
     mvprintw(view->bottom_right.y, view->top_left.x + 1, " %s%s",
              view == current_view ? mode_names[vim_mode] : "", misc_buffer_flag_string(buffer));
     int save_title_y, save_title_x;
     getyx(stdscr, save_title_y, save_title_x);

     // print column, row in bottom right of view
     int64_t row = view->cursor.y + 1;
     int64_t column = view->cursor.x + 1;
     int64_t digits_in_line = misc_count_digits(row);
     digits_in_line += misc_count_digits(column);
     int position_info_x = (view->bottom_right.x - (digits_in_line + 5)) + right_status_offset;
     mvprintw(view->bottom_right.y, position_info_x, " %"PRId64", %"PRId64" ", column, row);

     // draw filename next to mode, accounting for the fact that it may not fit
     int space_for_filename = (position_info_x - save_title_x) - 2; // account for separator and space
     int filename_len = strlen(buffer->filename);
     int filename_offset = 0;
     if(filename_len > space_for_filename) filename_offset = filename_len - space_for_filename;
     mvprintw(save_title_y, save_title_x, "%s ", buffer->filename + filename_offset);

     // finally print extra info for special modes, recording macro, or in terminal
     if(terminal_current && view->buffer == terminal_current->buffer) printw("$ ");
     if(view == current_view && recording_macro) printw("RECORDING %c ", recording_macro);

#if 0 // NOTE: useful to show key presses when debugging
     if(view == current_view) printw("%s %d ", keyname(g_last_key), g_last_key);
#endif
}

bool initializer(BufferNode_t** head, Point_t* terminal_dimensions, int argc, char** argv, void** user_data)
{
     // NOTE: need to set these in this module
     g_terminal_dimensions = terminal_dimensions;

     // setup the config's state
     ConfigState_t* config_state = calloc(1, sizeof(*config_state));
     if(!config_state){
          ce_message("failed to allocate config state");
          return false;
     }

     config_state->tab_head = calloc(1, sizeof(*config_state->tab_head));
     if(!config_state->tab_head){
          ce_message("failed to allocate tab");
          return false;
     }

     config_state->tab_current = config_state->tab_head;

     config_state->tab_head->view_head = calloc(1, sizeof(*config_state->tab_head->view_head));
     if(!config_state->tab_head->view_head){
          ce_message("failed to allocate buffer view");
          return false;
     }

     BufferViewState_t* buffer_view_state = calloc(1, sizeof(*buffer_view_state));
     if(!buffer_view_state){
          ce_message("failed to allocate buffer view state");
          return false;
     }

     config_state->tab_head->view_head->user_data = buffer_view_state;

     config_state->input.view = calloc(1, sizeof(*config_state->input.view));
     if(!config_state->input.view){
          ce_message("failed to allocate buffer view for input");
          return false;
     }

     config_state->view_auto_complete = calloc(1, sizeof(*config_state->view_auto_complete));
     if(!config_state->view_auto_complete){
          ce_message("failed to allocate buffer view for auto complete");
          return false;
     }

     // setup input buffer
     ce_alloc_lines(&config_state->input.buffer, 1);
     buffer_initialize(&config_state->input.buffer);
     config_state->input.buffer.name = strdup("[input]");
     config_state->input.buffer.absolutely_no_line_numbers_under_any_circumstances = true;
     config_state->input.view->buffer = &config_state->input.buffer;

     // setup clang completion buffer
     buffer_initialize(&config_state->clang_completion_buffer);
     config_state->clang_completion_buffer.name = strdup("[clang completion]");

     // setup buffer list buffer
     config_state->buffer_list_buffer.name = strdup("[buffers]");
     buffer_initialize(&config_state->buffer_list_buffer);
     config_state->buffer_list_buffer.status = BS_READONLY;
     config_state->buffer_list_buffer.absolutely_no_line_numbers_under_any_circumstances = true;
     config_state->buffer_list_buffer.syntax_fn = syntax_highlight_c;
     config_state->buffer_list_buffer.syntax_user_data = realloc(config_state->buffer_list_buffer.syntax_user_data, sizeof(SyntaxC_t));
     config_state->buffer_list_buffer.type = BFT_C;

     config_state->mark_list_buffer.name = strdup("[marks]");
     buffer_initialize(&config_state->mark_list_buffer);
     config_state->mark_list_buffer.status = BS_READONLY;
     config_state->mark_list_buffer.absolutely_no_line_numbers_under_any_circumstances = true;
     config_state->mark_list_buffer.syntax_fn = syntax_highlight_c;
     config_state->mark_list_buffer.syntax_user_data = realloc(config_state->mark_list_buffer.syntax_user_data, sizeof(SyntaxC_t));
     config_state->mark_list_buffer.type = BFT_C;

     config_state->yank_list_buffer.name = strdup("[yanks]");
     buffer_initialize(&config_state->yank_list_buffer);
     config_state->yank_list_buffer.status = BS_READONLY;
     config_state->yank_list_buffer.absolutely_no_line_numbers_under_any_circumstances = true;
     config_state->yank_list_buffer.syntax_fn = syntax_highlight_c;
     config_state->yank_list_buffer.syntax_user_data = realloc(config_state->yank_list_buffer.syntax_user_data, sizeof(SyntaxC_t));
     config_state->yank_list_buffer.type = BFT_C;

     config_state->macro_list_buffer.name = strdup("[macros]");
     buffer_initialize(&config_state->macro_list_buffer);
     config_state->macro_list_buffer.status = BS_READONLY;
     config_state->macro_list_buffer.absolutely_no_line_numbers_under_any_circumstances = true;
     config_state->macro_list_buffer.syntax_fn = syntax_highlight_c;
     config_state->macro_list_buffer.syntax_user_data = realloc(config_state->macro_list_buffer.syntax_user_data, sizeof(SyntaxC_t));
     config_state->macro_list_buffer.type = BFT_C;

     // if we reload, the completionbuffer may already exist, don't recreate it
     BufferNode_t* itr = *head;
     while(itr){
          if(strcmp(itr->buffer->name, "[completions]") == 0){
               config_state->completion_buffer = itr->buffer;
               break;
          }
          itr = itr->next;
     }

     if(!config_state->completion_buffer){
          config_state->completion_buffer = calloc(1, sizeof(*config_state->completion_buffer));
          config_state->completion_buffer->name = strdup("[completions]");
          config_state->completion_buffer->status = BS_READONLY;
          config_state->completion_buffer->absolutely_no_line_numbers_under_any_circumstances = true;
          config_state->completion_buffer->syntax_fn = syntax_highlight_c;
          config_state->completion_buffer->syntax_user_data = realloc(config_state->completion_buffer->syntax_user_data, sizeof(SyntaxC_t));
          config_state->completion_buffer->type = BFT_C;
          BufferNode_t* new_buffer_node = ce_append_buffer_to_list(head, config_state->completion_buffer);
          if(!new_buffer_node){
               ce_message("failed to add shell command buffer to list");
               return false;
          }
     }

     config_state->view_auto_complete->buffer = config_state->completion_buffer;

     *user_data = config_state;

     // setup state for each buffer
     itr = *head;
     while(itr){
          buffer_initialize(itr->buffer);
          itr = itr->next;
     }

     config_state->tab_current->view_head->buffer = (*head)->buffer;
     config_state->tab_current->view_current = config_state->tab_current->view_head;

     for(int i = 0; i < argc; ++i){
          BufferNode_t* node = buffer_create_from_file(head, argv[i]);

          // if we loaded a file, set the view to point at the file
          if(node){
               if(i == 0){
                    config_state->tab_current->view_current->buffer = node->buffer;
               }else{
                    BufferView_t* buffer_view = ce_split_view(config_state->tab_current->view_head, node->buffer, true);
                    BufferViewState_t* buffer_view_state = calloc(1, sizeof(*buffer_view_state));
                    if(!buffer_view_state){
                         ce_message("failed to allocate buffer view state");
                         return false;
                    }

                    buffer_view->user_data = buffer_view_state;
               }
          }
     }

     // update view boundaries now that we have split them
     Point_t top_left;
     Point_t bottom_right;
     misc_get_user_terminal_view_rect(config_state->tab_head, &top_left, &bottom_right);
     ce_calc_views(config_state->tab_current->view_head, top_left, bottom_right);

     config_state->line_number_type = LNT_NONE;
     config_state->highlight_line_type = HLT_ENTIRE_LINE;

     config_state->max_auto_complete_height = 10;

#if 0
     // enable mouse events
     mousemask(~((mmask_t)0), NULL);
     mouseinterval(0);
#endif

     text_history_init(&config_state->input.search_history);
     text_history_init(&config_state->input.command_history);

#define CHECK_PAIR(syntax, fg, bg)                 \
     if(init_pair(syntax, fg, bg) != 0){           \
          fprintf(stderr, "%s failed\n", #syntax); \
          return false;                            \
     }

     // setup colors for syntax highlighting
     CHECK_PAIR(S_NORMAL, COLOR_FOREGROUND, COLOR_BACKGROUND);
     CHECK_PAIR(S_KEYWORD, COLOR_BLUE, COLOR_BACKGROUND);
     CHECK_PAIR(S_TYPE, COLOR_BRIGHT_BLUE, COLOR_BACKGROUND);
     CHECK_PAIR(S_FUNC, COLOR_BRIGHT_GREEN, COLOR_BACKGROUND);
     CHECK_PAIR(S_VARIABLE_DECLARATION, COLOR_BRIGHT_WHITE, COLOR_BACKGROUND);
     CHECK_PAIR(S_CONTROL, COLOR_YELLOW, COLOR_BACKGROUND);
     CHECK_PAIR(S_COMMENT, COLOR_GREEN, COLOR_BACKGROUND);
     CHECK_PAIR(S_STRING, COLOR_RED, COLOR_BACKGROUND);
     CHECK_PAIR(S_CONSTANT, COLOR_MAGENTA, COLOR_BACKGROUND);
     CHECK_PAIR(S_CONSTANT_NUMBER, COLOR_MAGENTA, COLOR_BACKGROUND);
     CHECK_PAIR(S_MATCHING_PARENS, COLOR_BRIGHT_WHITE, COLOR_BACKGROUND);
     CHECK_PAIR(S_PREPROCESSOR, COLOR_BRIGHT_MAGENTA, COLOR_BACKGROUND);
     CHECK_PAIR(S_FILEPATH, COLOR_BLUE, COLOR_BACKGROUND);
     CHECK_PAIR(S_BLINK, COLOR_BRIGHT_WHITE, COLOR_BACKGROUND);
     CHECK_PAIR(S_DIFF_ADDED, COLOR_GREEN, COLOR_BACKGROUND);
     CHECK_PAIR(S_DIFF_REMOVED, COLOR_RED, COLOR_BACKGROUND);
     CHECK_PAIR(S_DIFF_HEADER, COLOR_BRIGHT_WHITE, COLOR_BACKGROUND);

     CHECK_PAIR(S_NORMAL_HIGHLIGHTED, COLOR_FOREGROUND, COLOR_WHITE);
     CHECK_PAIR(S_KEYWORD_HIGHLIGHTED, COLOR_BLUE, COLOR_WHITE);
     CHECK_PAIR(S_TYPE_HIGHLIGHTED, COLOR_BRIGHT_BLUE, COLOR_WHITE);
     CHECK_PAIR(S_FUNC_HIGHLIGHTED, COLOR_BRIGHT_GREEN, COLOR_WHITE);
     CHECK_PAIR(S_VARIABLE_DECLARATION_HIGHLIGHTED, COLOR_BRIGHT_WHITE, COLOR_WHITE);
     CHECK_PAIR(S_CONTROL_HIGHLIGHTED, COLOR_YELLOW, COLOR_WHITE);
     CHECK_PAIR(S_COMMENT_HIGHLIGHTED, COLOR_GREEN, COLOR_WHITE);
     CHECK_PAIR(S_STRING_HIGHLIGHTED, COLOR_RED, COLOR_WHITE);
     CHECK_PAIR(S_CONSTANT_HIGHLIGHTED, COLOR_MAGENTA, COLOR_WHITE);
     CHECK_PAIR(S_CONSTANT_NUMBER_HIGHLIGHTED, COLOR_MAGENTA, COLOR_WHITE);
     CHECK_PAIR(S_MATCHING_PARENS_HIGHLIGHTED, COLOR_BRIGHT_WHITE, COLOR_WHITE);
     CHECK_PAIR(S_PREPROCESSOR_HIGHLIGHTED, COLOR_BRIGHT_MAGENTA, COLOR_WHITE);
     CHECK_PAIR(S_FILEPATH_HIGHLIGHTED, COLOR_BLUE, COLOR_WHITE);
     CHECK_PAIR(S_BLINK_HIGHLIGHTED, COLOR_BRIGHT_WHITE, COLOR_WHITE);
     CHECK_PAIR(S_DIFF_ADDED_HIGHLIGHTED, COLOR_GREEN, COLOR_WHITE);
     CHECK_PAIR(S_DIFF_REMOVED_HIGHLIGHTED, COLOR_RED, COLOR_WHITE);
     CHECK_PAIR(S_DIFF_HEADER_HIGHLIGHTED, COLOR_BRIGHT_WHITE, COLOR_WHITE);

     CHECK_PAIR(S_NORMAL_CURRENT_LINE, COLOR_FOREGROUND, COLOR_BRIGHT_BLACK);
     CHECK_PAIR(S_KEYWORD_CURRENT_LINE, COLOR_BLUE, COLOR_BRIGHT_BLACK);
     CHECK_PAIR(S_TYPE_CURRENT_LINE, COLOR_BRIGHT_BLUE, COLOR_BRIGHT_BLACK);
     CHECK_PAIR(S_FUNC_CURRENT_LINE, COLOR_BRIGHT_GREEN, COLOR_BRIGHT_BLACK);
     CHECK_PAIR(S_VARIABLE_DECLARATION_CURRENT_LINE, COLOR_BRIGHT_WHITE, COLOR_BRIGHT_BLACK);
     CHECK_PAIR(S_CONTROL_CURRENT_LINE, COLOR_YELLOW, COLOR_BRIGHT_BLACK);
     CHECK_PAIR(S_COMMENT_CURRENT_LINE, COLOR_GREEN, COLOR_BRIGHT_BLACK);
     CHECK_PAIR(S_STRING_CURRENT_LINE, COLOR_RED, COLOR_BRIGHT_BLACK);
     CHECK_PAIR(S_CONSTANT_CURRENT_LINE, COLOR_MAGENTA, COLOR_BRIGHT_BLACK);
     CHECK_PAIR(S_CONSTANT_NUMBER_CURRENT_LINE, COLOR_MAGENTA, COLOR_BRIGHT_BLACK);
     CHECK_PAIR(S_MATCHING_PARENS_CURRENT_LINE, COLOR_BRIGHT_WHITE, COLOR_BRIGHT_BLACK);
     CHECK_PAIR(S_PREPROCESSOR_CURRENT_LINE, COLOR_BRIGHT_MAGENTA, COLOR_BRIGHT_BLACK);
     CHECK_PAIR(S_FILEPATH_CURRENT_LINE, COLOR_BLUE, COLOR_BRIGHT_BLACK);
     CHECK_PAIR(S_BLINK_CURRENT_LINE, COLOR_BRIGHT_WHITE, COLOR_BRIGHT_BLACK);
     CHECK_PAIR(S_DIFF_ADDED_CURRENT_LINE, COLOR_GREEN, COLOR_BRIGHT_BLACK);
     CHECK_PAIR(S_DIFF_REMOVED_CURRENT_LINE, COLOR_RED, COLOR_BRIGHT_BLACK);
     CHECK_PAIR(S_DIFF_HEADER_CURRENT_LINE, COLOR_BRIGHT_WHITE, COLOR_BRIGHT_BLACK);

     CHECK_PAIR(S_LINE_NUMBERS, COLOR_WHITE, COLOR_BACKGROUND);

     CHECK_PAIR(S_TRAILING_WHITESPACE, COLOR_FOREGROUND, COLOR_RED);

     CHECK_PAIR(S_BORDERS, COLOR_WHITE, COLOR_BACKGROUND);

     CHECK_PAIR(S_TAB_NAME, COLOR_WHITE, COLOR_BACKGROUND);
     CHECK_PAIR(S_CURRENT_TAB_NAME, COLOR_CYAN, COLOR_BACKGROUND);

     CHECK_PAIR(S_VIEW_STATUS, COLOR_CYAN, COLOR_BACKGROUND);
     CHECK_PAIR(S_INPUT_STATUS, COLOR_RED, COLOR_BACKGROUND);
     CHECK_PAIR(S_AUTO_COMPLETE, COLOR_WHITE, COLOR_BACKGROUND);

     define_key(NULL, KEY_BACKSPACE);   // Blow away backspace
     define_key("\x7F", KEY_BACKSPACE); // Backspace  (127) (0x7F) ASCII "DEL" Delete
     define_key("\x15", KEY_NPAGE);     // ctrl + d    (21) (0x15) ASCII "NAK" Negative Acknowledgement
     define_key("\x04", KEY_PPAGE);     // ctrl + u     (4) (0x04) ASCII "EOT" End of Transmission
     define_key("\x11", KEY_CLOSE);     // ctrl + q    (17) (0x11) ASCII "DC1" Device Control 1
     define_key("\x12", KEY_REDO);      // ctrl + r    (18) (0x12) ASCII "DC2" Device Control 2
     define_key("\x17", KEY_SAVE);      // ctrl + w    (23) (0x17) ASCII "ETB" End of Transmission Block
     define_key(NULL, KEY_ENTER);       // Blow away enter
     define_key("\x0D", KEY_ENTER);     // Enter       (13) (0x0D) ASCII "CR"  NL Carriage Return

     // default vim "leader" key
     #define KEY_LEADER '\\'

     pthread_mutex_init(&draw_lock, NULL);

     auto_complete_end(&config_state->auto_complete);
     config_state->vim_state.insert_start = (Point_t){-1, -1};

     // initialize commands
     {
          // create a stack array so we can have the compiler track the number of elements
          CommandEntry_t command_entries[] = {
               {command_help, "help", NULL, "print the commands and their descriptions in the message log", NULL},
               {command_quit_all, "quit_all", NULL, "exit ce, prompting if you have any unsaved buffers", NULL},
               {command_save, "save", NULL, "save the current buffer", NULL},
               {command_save_and_close_view, "save_and_close_view", NULL, "save the current buffer and close the view", NULL},
               {command_redraw, "redraw", NULL, "clear the screen so it will get fully redrawn", NULL},
               {command_highlight_line, "highlight_line", "[style]", "change the global style in which the current line is highlighted", "styles: none, text, entire"},
               {command_line_number, "line_number", "[style]", "change the global style in which line number are drawn", "styles: none, absolute, relative, both"},
               {command_noh, "noh", NULL, "turn off search highlighting", NULL},

               {command_buffer_rename, "buffer_rename", "[string]", "rename the current buffer", NULL},
               {command_buffer_reload, "buffer_reload", NULL, "reload the file that backs the buffer, overwriting any unsaved changes", NULL},
               {command_buffer_new, "buffer_new", "<string>", "creates a new buffer with an optional filename", NULL},
               {command_buffer_syntax, "buffer_syntax", "[style]", "change the syntax mode of the current buffer", "styles: c, cpp, python, java, config, diff, plain"},

               {command_move_on_screen, "move_on_screen", "[mode]", "move the cursor to the part of the view specified by the mode", "modes: top, center, bottom"},
               {command_move_half_page, "move_half_page", "[dir]", "move the cursor up or down a half page specified by the dir", "  dir: up, down"},

               {command_view_split, "view_split", "[dir]", "split the currently selected window into 2 windows", "dir: horizontal, vertical"},
               {command_view_close, "view_close", NULL, "close the current view", NULL},
               {command_view_scroll, "view_scroll", "[mode]", "scroll the view keeping the cursor on screen based on the mode specified", "modes: top, center, bottom"},
               {command_view_switch, "view_switch", "[mode]", "switch to an adjacent view in the direction specified", " mode: up, down, left, right"},

               {command_tab_new, "tab_new", NULL, "add a new tab", NULL},
               {command_tab_next, "tab_next", NULL, "switch to the next tab", NULL},
               {command_tab_previous, "tab_previous", NULL, "switch to the previous tab", NULL},

               {command_show_buffers, "show_buffers", NULL, "show all buffers in the current view", NULL},
               {command_show_marks, "show_marks", NULL, "show all marks in the current view", NULL},
               {command_show_macros, "show_macros", NULL, "show all macros in the current view", NULL},
               {command_show_yanks, "show_yanks", NULL, "show all yanks in the current view", NULL},

               {command_switch_buffer_dialogue, "switch_buffer_dialogue", NULL, "open a dialogue to switch buffers", NULL},
               {command_command_dialogue, "command_dialogue", NULL, "open a dialogue to run a ce command", NULL},
               {command_search_dialogue, "search_dialogue", "[dir]", "open a dialogue to search the current buffer for a regex", "dirs: up, down"},
               {command_load_file_dialogue, "load_file_dialogue", NULL, "open a dialogue to load a file", NULL},
               {command_replace_dialogue, "replace_dialogue", NULL, "open a dialogue to replace in the current buffer", NULL},
               {command_cancel_dialogue, "cancel_dialogue", NULL, "cancel any open dialogue", NULL},

               {command_terminal_goto, "terminal_goto", NULL, "switch to a terminal in view, if none is in view, switch the current view to an already open terminal. Start's a new terminal if none exists.", NULL},
               {command_terminal_new, "terminal_new", NULL, "start a new terminal and open it in the current view", NULL},
               {command_terminal_jump_to_dest, "terminal_jump_to_dest", "[dir]", "jump the current view to the next destination found in the current terminal's output", "dir: up, down"},
               {command_terminal_run_man, "terminal_run_man", "<symbol>", "run 'man -Pcat <symbol>' in current terminal. If no symbol is specified, word under cursor is used", NULL},
               {command_terminal_run_command, "terminal_run_command", "[command]", "run specified command in current terminal", NULL},

               {command_jump_next, "jump_next", NULL, "using the jump list, move the cursor to the next destination in the list", NULL},
               {command_jump_previous, "jump_previous", NULL, "using the jump list, move the cursor to the previous destination in the list", NULL},

               {command_completion_toggle, "completion_toggle", NULL, "toggle auto completion for c using clang", NULL},
               {command_completion_apply, "completion_apply", NULL, "if auto completion is running, insert the selection option at the cursor", NULL},
               {command_completion_next, "completion_next", NULL, "if auto completion is running, select the next completion in the list", NULL},
               {command_completion_previous, "completion_previous", NULL, "if auto completion is running, select the next completion in the list", NULL},

               {command_cscope_goto_definition, "cscope_goto_definition", "<symbol>", "jump to the definition of the specified symbol. If no symbol is specified, use word under cursor", NULL},
               {command_goto_file_under_cursor, "goto_file_under_cursor", NULL, "checks the word under the cursor for a valid file, if valid opens that file", NULL},
               {command_macro_backslashes, "macro_backslashes", NULL, "add formatted backslashes around a macro", NULL},
          };

          // init and copy from our stack array
          config_state->command_entry_count = sizeof(command_entries) / sizeof(command_entries[0]);
          config_state->command_entries = malloc(config_state->command_entry_count * sizeof(*command_entries));
          for(int64_t i = 0; i < config_state->command_entry_count; ++i){
               config_state->command_entries[i] = command_entries[i];
          }
     }

     // initialize keybinds
     {
          KeyBindDef_t normal_mode_bind_defs[] = {
               {{'\\', 'e'}, "show_buffers"},
               {{'\\', 'q'}, "quit_all"},
               {{'\\', 'b'}, "terminal_run_command make"},
               {{'\\', 'm'}, "terminal_run_command \"make clean\""},
               {{'z', 't'}, "view_scroll top"},
               {{'z', 'z'}, "view_scroll center"},
               {{'z', 'b'}, "view_scroll bottom"},
               {{2}, "switch_buffer_dialogue"}, // Ctrl + b
               {{KEY_SAVE}, "save"},
               {{KEY_ESCAPE}, "cancel_dialogue"},
               {{KEY_CLOSE}, "view_close"},
               {{20}, "tab_new"}, // Ctrl + t
               {{':'}, "command_dialogue"},
               {{6}, "load_file_dialogue"},
               {{'R'}, "replace_dialogue"},
               {{'H'}, "move_on_screen top"},
               {{'M'}, "move_on_screen center"},
               {{'L'}, "move_on_screen bottom"},
               {{KEY_NPAGE}, "move_half_page up"},
               {{KEY_PPAGE}, "move_half_page down"},
               {{'/'}, "search_dialogue down"},
               {{'?'}, "search_dialogue up"},
               {{24}, "terminal_goto"}, // Ctrl + x
               {{1}, "terminal_new"}, // Ctrl + a
               {{14}, "terminal_jump_to_dest down"}, // Ctrl + n
               {{16}, "terminal_jump_to_dest up"}, // Ctrl + p
               {{8}, "view_switch left"}, // Ctrl + h
               {{10}, "view_switch down"}, // Ctrl + j
               {{11}, "view_switch up"}, // Ctrl + k
               {{12}, "view_switch right"}, // Ctrl + l
               {{19}, "view_split horizontal"}, // Ctrl + s
               {{22}, "view_split vertical"}, // Ctrl + v
               {{15}, "jump_next"}, // Ctrl + o
               {{9}, "jump_previous"}, // Ctrl + i
               {{29}, "cscope_goto_definition"}, // Ctrl + ]
               {{'K'}, "terminal_run_man"},
               {{'g', 't'}, "tab_next"},
               {{'g', 'T'}, "tab_previous"},
               {{'g', 'f'}, "goto_file_under_cursor"},
               {{'\\', 'r'}, "redraw"},
               {{'m', '?'}, "show_marks"},
               {{'\"', '?'}, "show_marks"},
               {{'q', '?'}, "show_macros"},
               {{'@', '?'}, "show_macros"},
               {{'y', '?'}, "show_yanks"},
               {{'Z', 'Z'}, "save_and_close_view"},
               {{14}, "completion_next"},
               {{16}, "completion_previous"},
          };

          convert_bind_defs(config_state->binds + VM_NORMAL, normal_mode_bind_defs, sizeof(normal_mode_bind_defs) / sizeof(normal_mode_bind_defs[0]));

          KeyBindDef_t insert_mode_bind_defs[] = {
               {{KEY_TAB}, "completion_apply"},
               {{25}, "completion_toggle"}, // Ctrl + y
               {{14}, "completion_next"}, // Ctrl + n
               {{16}, "completion_previous"}, // Ctrl + p
          };

          convert_bind_defs(config_state->binds + VM_INSERT, insert_mode_bind_defs, sizeof(insert_mode_bind_defs) / sizeof(insert_mode_bind_defs[0]));
     }

     // read in state file if it exists
     // TODO: load this into a buffer instead of dealing with the freakin scanf nonsense
     {
          char path[128];
          snprintf(path, 128, "%s/%s", getenv("HOME"), ".ce");

          Buffer_t scrap_buffer = {};
          if(ce_load_file(&scrap_buffer, path) && scrap_buffer.line_count){
               ce_message("restoring state from %s", path);

               int yank_lines = atoi(scrap_buffer.lines[0]);

               Point_t start = {0, 1};
               Point_t end = {0, start.y + (yank_lines - 1)};

               if(yank_lines){
                    end.x = ce_last_index(scrap_buffer.lines[end.y]);

                    char* search_pattern = ce_dupe_string(&scrap_buffer, start, end);
                    vim_yank_add(&config_state->vim_state.yank_head, '/', search_pattern, YANK_NORMAL);
                    int rc = regcomp(&config_state->vim_state.search.regex, search_pattern, REG_EXTENDED);
                    if(rc != 0){
                         char error_buffer[BUFSIZ];
                         regerror(rc, &config_state->vim_state.search.regex, error_buffer, BUFSIZ);
                         ce_message("regcomp() failed: '%s'", error_buffer);
                    }else{
                         config_state->vim_state.search.valid_regex = true;
                         ce_message("  search pattern '%s'", search_pattern);
                    }
               }

               int64_t next_line = end.y + 1;

               if(next_line < scrap_buffer.line_count){
                    ce_message("  file cursor positions");
               }

               for(int64_t i = next_line; i < scrap_buffer.line_count; ++i){
                    char* space = scrap_buffer.lines[i];
                    while(*space && *space != ' ') space++;
                    if(*space != ' ') break;

                    char* filename = scrap_buffer.lines[i];
                    ssize_t filename_len = space - filename;
                    int line_number = atoi(space + 1);

                    BufferNode_t* itr = *head;
                    while(itr){
                         if(strncmp(itr->buffer->name, filename, filename_len) == 0){
                              ce_message(" '%s' at line %d", itr->buffer->name, line_number);
                              itr->buffer->cursor.y = line_number;
                              ce_move_cursor_to_soft_beginning_of_line(itr->buffer, &itr->buffer->cursor);
                              BufferView_t* view = ce_buffer_in_view(config_state->tab_current->view_head, itr->buffer);
                              if(view){
                                   view->cursor = view->buffer->cursor;
                                   view_center(view);
                              }
                              break;
                         }
                         itr = itr->next;
                    }
               }

               ce_free_buffer(&scrap_buffer);
          }
     }

     // register ctrl + c signal handler
     struct sigaction sa = {};
     sa.sa_handler = sigint_handler;
     sigemptyset(&sa.sa_mask);
     if(sigaction(SIGINT, &sa, NULL) == -1){
          ce_message("failed to register ctrl+c (SIGINT) signal handler.");
     }

     // ignore sigpipe for when things like clang completion error our
     signal(SIGPIPE, SIG_IGN);

     if(pthread_mutex_trylock(&draw_lock) == 0){
          view_drawer(*user_data);
          pthread_mutex_unlock(&draw_lock);
     }

     return true;
}

bool destroyer(BufferNode_t** head, void* user_data)
{
     ConfigState_t* config_state = user_data;

     // write out file with some state we can use to restore
     {
          char path[128];
          snprintf(path, 128, "%s/%s", getenv("HOME"), ".ce");

          FILE* out_file = fopen(path, "w");
          if(out_file){
               // write out last searched text
               VimYankNode_t* yank = vim_yank_find(config_state->vim_state.yank_head, '/');
               if(yank){
                    int64_t line_count = ce_count_string_lines(yank->text);
                    fprintf(out_file, "%"PRId64"\n", line_count);
                    fprintf(out_file, "%s\n", yank->text);
               }else{
                    fprintf(out_file, "0\n");
               }

               // TODO: vim last command

               // write out all buffers and cursor positions
               BufferNode_t* itr = *head;
               while(itr){
                    if(itr->buffer->status != BS_READONLY){
                         // TODO: handle all tabs
                         BufferView_t* view = ce_buffer_in_view(config_state->tab_current->view_head, itr->buffer);
                         if(view){
                              fprintf(out_file, "%s %"PRId64"\n", itr->buffer->name, view->cursor.y);
                         }else{
                              fprintf(out_file, "%s %"PRId64"\n", itr->buffer->name, itr->buffer->cursor.y);
                         }
                    }
                    itr = itr->next;
               }

               fclose(out_file);
          }
     }

     if(config_state->terminal_head){
          TerminalColorPairNode_t* color_itr = terminal_color_pairs_head;

          while(color_itr){
               TerminalColorPairNode_t* tmp = color_itr;
               color_itr = color_itr->next;
               free(tmp);
          }
     }

     TerminalNode_t* term_itr = config_state->terminal_head;
     while(term_itr){
          if(term_itr->check_update_thread){
               pthread_cancel(term_itr->check_update_thread);
               pthread_join(term_itr->check_update_thread, NULL);
               terminal_free(&term_itr->terminal);
          }

          TerminalNode_t* tmp = term_itr;
          term_itr = term_itr->next;
          free(tmp);
     }

     config_state->terminal_head = NULL;

     BufferNode_t* itr = *head;
     while(itr){
          buffer_state_free(itr->buffer->user_data);
          itr->buffer->user_data = NULL;

          free(itr->buffer->syntax_user_data);
          itr->buffer->syntax_user_data = NULL;

          itr = itr->next;
     }

     TabView_t* tab_itr = config_state->tab_head;
     while(tab_itr){
          if(tab_itr->view_head){
               ce_free_views(&tab_itr->view_head);
          }
          tab_itr = tab_itr->next;
     }

     tab_itr = config_state->tab_head;
     while(tab_itr){
          TabView_t* tmp = tab_itr;
          tab_itr = tab_itr->next;
          free(tmp);
     }

     // input buffer
     {
          buffer_state_free(config_state->input.buffer.user_data);
          free(config_state->input.buffer.syntax_user_data);
          ce_free_buffer(&config_state->input.buffer);
          free(config_state->input.view);
     }

     free(config_state->view_auto_complete);

     buffer_state_free(config_state->clang_completion_buffer.user_data);
     free(config_state->clang_completion_buffer.syntax_user_data);
     ce_free_buffer(&config_state->clang_completion_buffer);

     buffer_state_free(config_state->buffer_list_buffer.user_data);
     free(config_state->buffer_list_buffer.syntax_user_data);
     ce_free_buffer(&config_state->buffer_list_buffer);

     buffer_state_free(config_state->mark_list_buffer.user_data);
     free(config_state->mark_list_buffer.syntax_user_data);
     ce_free_buffer(&config_state->mark_list_buffer);

     buffer_state_free(config_state->yank_list_buffer.user_data);
     free(config_state->yank_list_buffer.syntax_user_data);
     ce_free_buffer(&config_state->yank_list_buffer);

     buffer_state_free(config_state->macro_list_buffer.user_data);
     free(config_state->macro_list_buffer.syntax_user_data);
     ce_free_buffer(&config_state->macro_list_buffer);

     free(config_state->command_entries);

     // history
     text_history_free(&config_state->input.search_history);
     text_history_free(&config_state->input.command_history);

     pthread_mutex_destroy(&draw_lock);

     auto_complete_free(&config_state->auto_complete);

     free(config_state->vim_state.last_insert_command);

     ce_keys_free(&config_state->vim_state.command_head);
     ce_keys_free(&config_state->vim_state.record_macro_head);

     if(config_state->vim_state.search.valid_regex){
          regfree(&config_state->vim_state.search.regex);
     }

     // key binds
     for(int64_t i = 0; i < config_state->binds[VM_NORMAL].count; ++i){
          free(config_state->binds[VM_NORMAL].binds[i].keys);
     }
     free(config_state->binds[VM_NORMAL].binds);

     vim_yanks_free(&config_state->vim_state.yank_head);
     vim_macros_free(&config_state->vim_state.macro_head);

     VimMacroCommitNode_t* macro_commit_head = config_state->vim_state.macro_commit_current;
     while(macro_commit_head && macro_commit_head->prev) macro_commit_head = macro_commit_head->prev;
     vim_macro_commits_free(&macro_commit_head);

     free(config_state);
     return true;
}

bool key_handler(int key, BufferNode_t** head, void* user_data)
{
     ConfigState_t* config_state = user_data;
     Buffer_t* buffer = config_state->tab_current->view_current->buffer;
     BufferState_t* buffer_state = buffer->user_data;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Point_t* cursor = &config_state->tab_current->view_current->cursor;

     bool handled_key = false;

     // handle non-keypress events
     switch(key){
     default:
          break;
     case KEY_RESIZE:
     {
          Point_t top_left = {0, 0};
          Point_t bottom_right = {g_terminal_dimensions->x - 1, g_terminal_dimensions->y - 1};
          ce_calc_views(config_state->tab_current->view_head, top_left, bottom_right);
          terminal_resize_if_in_view(buffer_view, config_state->terminal_head);
          handled_key = true;
     } break;
     case KEY_MOUSE:
          mouse_handle_event(buffer_view, &config_state->vim_state, &config_state->input, config_state->tab_current,
                             config_state->terminal_head, &config_state->terminal_current, config_state->line_number_type);
          break;
     }

     config_state->save_buffer_head = head;
     buffer->check_left_for_pair = false;

     // as long as vim isn't in the middle of handling keys, in insert mode vim returns VKH_HANDLED_KEY TODO: is that what we want?
     if(config_state->last_vim_result_type != VKH_HANDLED_KEY || config_state->last_vim_mode == VM_INSERT){
          // append to keys
          if(config_state->keys){
               config_state->key_count++;
               config_state->keys = realloc(config_state->keys, config_state->key_count * sizeof(config_state->keys[0]));
          }else{
               config_state->key_count = 1;
               config_state->keys = malloc(config_state->key_count * sizeof(config_state->keys[0]));
          }

          config_state->keys[config_state->key_count - 1] = key;

          // if matches a key bind
          bool no_matches = true;
          for(int64_t i = 0; i < config_state->binds[config_state->vim_state.mode].count; ++i){
               if(int_strneq(config_state->binds[config_state->vim_state.mode].binds[i].keys, config_state->keys, config_state->key_count)){
                    no_matches = false;

                    // if we have matches, but don't completely match, then wait for more keypresses,
                    // otherwise, execute the action
                    if(config_state->binds[config_state->vim_state.mode].binds[i].key_count == config_state->key_count){
                         Command_t* command = &config_state->binds[config_state->vim_state.mode].binds[i].command;
                         ce_command* command_func = NULL;
                         CommandEntry_t* entry = NULL;
                         for(int64_t i = 0; i < config_state->command_entry_count; ++i){
                              entry = config_state->command_entries + i;
                              if(strcmp(entry->name, command->name) == 0){
                                   command_func = entry->func;
                                   break;
                              }
                         }

                         if(command_func){
                              CommandData_t command_data = {config_state, head};
                              CommandStatus_t cs = command_func(command, &command_data);

                              switch(cs){
                              default:
                                   handled_key = true;
                                   break;
                              case CS_NO_ACTION:
                                   break;
                              case CS_FAILURE:
                                   ce_message("'%s' failed", entry->name);
                                   break;
                              case CS_PRINT_HELP:
                                   ce_message("command help:");
                                   command_entry_log(entry);
                                   ce_message("");
                                   break;
                              }
                         }else{
                              ce_message("unknown command: '%s'", command->name);
                         }

                         free(config_state->keys);
                         config_state->keys = NULL;
                         break;
                    }else{
                         handled_key = true;
                    }
               }
          }

          if(no_matches){
               // hand off the keys to vim to see if that creates a valid action if we have at least matched one key
               if(config_state->key_count > 1){
                    ce_keys_free(&config_state->vim_state.command_head);
                    // skip pushing the last key since we are going to pass that one in this loop
                    for(int64_t i = 0; i < config_state->key_count - 1; ++i){
                         ce_keys_push(&config_state->vim_state.command_head, config_state->keys[i]);
                    }
               }

               free(config_state->keys);
               config_state->keys = NULL;
               config_state->key_count = 0;
          }
     }

     // if we haven't yet handled the key and we are in insert mode in the terminal, then send that key
     if(!handled_key && config_state->vim_state.mode == VM_INSERT && key != KEY_ESCAPE){
          TerminalNode_t* terminal_node = is_terminal_buffer(config_state->terminal_head, buffer);
          if(terminal_node){
               buffer_view->cursor = terminal_node->terminal.cursor;

               if(key == KEY_ENTER) misc_move_jump_location_to_end_of_output(terminal_node);

               if(terminal_send_key(&terminal_node->terminal, key)){
                    handled_key = true;
                    if(cursor->x < (terminal_node->terminal.width - 1)) cursor->x++;
                    view_follow_cursor(buffer_view, LNT_NONE);
               }
          }
     }

     // regardless of vim state, if enter is pressed, see if we can perform an action
     if(!handled_key && key == KEY_ENTER){
          if(confirm_action(config_state, head)){
               ce_keys_free(&config_state->vim_state.command_head);
               handled_key = true;
          }
     }

     // send the key to the vim key handler
     if(!handled_key){
          // save the vim mode before running the key handle
          config_state->last_vim_mode = config_state->vim_state.mode;

          Point_t save_cursor = *cursor;
          Buffer_t* save_buffer = buffer_view->buffer;
          VimKeyHandlerResult_t vkh_result = vim_key_handler(key, &config_state->vim_state, config_state->tab_current->view_current->buffer,
                                                             &config_state->tab_current->view_current->cursor, &buffer_state->commit_tail,
                                                             &buffer_state->vim_buffer_state, false);

          // save the key handler result type
          config_state->last_vim_result_type = vkh_result.type;

          switch(vkh_result.type){
          default:
               break;
          case VKH_HANDLED_KEY:
               if(config_state->vim_state.mode == VM_INSERT){
                    switch(key){
                    default:
                         break;
                    case '{':
                    case '}':
                    case '(':
                    case ')':
                    case '[':
                    case ']':
                    case '<':
                    case '>':
                    {
                         buffer->check_left_for_pair = true;
                    } break;
                    }

                    if(config_state->input.type > INPUT_NONE){
                         switch(key){
                         default:
                              break;
                         case KEY_UP:
                              if(input_history_iterate(&config_state->input, true)){
                                   if(buffer->line_count && buffer->lines[cursor->y][0]) cursor->x++;
                              }
                              break;
                         case KEY_DOWN:
                              if(input_history_iterate(&config_state->input, false)){
                                   if(buffer->line_count && buffer->lines[cursor->y][0]) cursor->x++;
                              }
                              break;
                         }

                         switch(config_state->input.type){
                         default:
                              break;
                         case INPUT_LOAD_FILE:
                              completion_calc_start_and_path(&config_state->auto_complete,
                                                             buffer->lines[cursor->y],
                                                             *cursor,
                                                             config_state->completion_buffer,
                                                             config_state->input.load_file_search_path);
                              break;
                         case INPUT_COMMAND:
                              // intentional fallthrough
                         case INPUT_SWITCH_BUFFER:
                         {
                              Point_t end = {cursor->x - 1, cursor->y};
                              if(end.x < 0) end.x = 0;
                              char* match = "";

                              pthread_mutex_lock(&completion_lock);
                              if(auto_completing(&config_state->auto_complete)){
                                   if(!ce_points_equal(config_state->auto_complete.start, *cursor)) match = ce_dupe_string(buffer, config_state->auto_complete.start, end);
                                   if(strstr(config_state->auto_complete.current->option, match) == NULL){
                                        auto_complete_next(&config_state->auto_complete, match);
                                   }
                              }else{
                                   auto_complete_start(&config_state->auto_complete, ACT_OCCURANCE, (Point_t){0, cursor->y});
                                   if(!ce_points_equal(config_state->auto_complete.start, *cursor)) match = ce_dupe_string(buffer, config_state->auto_complete.start, end);
                                   auto_complete_next(&config_state->auto_complete, match);
                              }

                              completion_update_buffer(config_state->completion_buffer, &config_state->auto_complete, match);
                              pthread_mutex_unlock(&completion_lock);
                              if(!ce_points_equal(config_state->auto_complete.start, end))free(match);
                         } break;
                         }
                    }else if(buffer->type == BFT_C || buffer->type == BFT_CPP){
                         if(auto_completing(&config_state->auto_complete)){
                              Point_t end = {cursor->x - 1, cursor->y};
                              if(end.x < 0) end.x = 0;
                              char* match = "";
                              match = ce_dupe_string(buffer, config_state->auto_complete.start, end);
                              size_t match_len = 0;
                              if(match) match_len = strlen(match);
                              if(config_state->auto_complete.current && strncmp(config_state->auto_complete.current->option, match, match_len) == 0){
                                   // pass
                              }else{
                                   pthread_mutex_lock(&completion_lock);
                                   auto_complete_next(&config_state->auto_complete, match);
                                   completion_update_buffer(config_state->completion_buffer, &config_state->auto_complete, match);
                                   pthread_mutex_unlock(&completion_lock);
                              }

                              free(match);
                         }

                         switch(key){
                         default:
                              // check if key is a valid c identifier
                              if((isalnum(key) || key == '_' ) && !auto_completing(&config_state->auto_complete)){
                                   char prev_char = 0;
                                   if(cursor->x > 1 && ce_get_char(buffer, (Point_t){cursor->x - 2, cursor->y}, &prev_char)){
                                        if(!isalnum(prev_char) && prev_char != '_'){
                                             clang_completion(config_state, (Point_t){cursor->x - 1, cursor->y});
                                        }
                                   }
                              }
                              break;
                         case '>':
                         {
                              // only continue to complete if we type the full '->'
                              char prev_char = 0;
                              if(cursor->x > 1 && ce_get_char(buffer, (Point_t){cursor->x - 2, cursor->y}, &prev_char)){
                                   if(prev_char != '-'){
                                        break;
                                   }
                              }else{
                                   break;
                              }

                              // intentional fallthrough
                         }
                         case '.':
                              clang_completion(config_state, *cursor);
                              break;
                         }
                    }
               }
               break;
          case VKH_COMPLETED_ACTION_FAILURE:
               if(vkh_result.completed_action.change.type == VCT_DELETE &&
                  config_state->tab_current->view_current->buffer == &config_state->buffer_list_buffer){
                    VimActionRange_t action_range;
                    if(vim_action_get_range(&vkh_result.completed_action, buffer, cursor, &config_state->vim_state,
                                            &buffer_state->vim_buffer_state, &action_range)){
                         int64_t delete_index = action_range.sorted_start->y - 1;
                         int64_t buffers_to_delete = (action_range.sorted_end->y - action_range.sorted_start->y) + 1;

                         for(int64_t b = 0; b < buffers_to_delete; ++b){
                              if(!buffer_delete_at_index(head, config_state->tab_head, delete_index,
                                                         &config_state->terminal_head, &config_state->terminal_current)){
                                   return false; // quit !
                              }
                         }

                         info_update_buffer_list_buffer(&config_state->buffer_list_buffer, *head);

                         if(cursor->y >= config_state->buffer_list_buffer.line_count){
                              cursor->y = config_state->buffer_list_buffer.line_count - 1;
                         }

                         vim_enter_normal_mode(&config_state->vim_state);
                         ce_keys_free(&config_state->vim_state.command_head);
                    }
               }else if(vkh_result.completed_action.change.type == VCT_PASTE_BEFORE ||
                        vkh_result.completed_action.change.type == VCT_PASTE_AFTER){
                    TerminalNode_t* terminal_node = is_terminal_buffer(config_state->terminal_head, config_state->tab_current->view_current->buffer);
                    if(terminal_node){
                         Terminal_t* terminal = &terminal_node->terminal;
                         char reg = vkh_result.completed_action.change.reg ? vkh_result.completed_action.change.reg : '"';
                         VimYankNode_t* yank = vim_yank_find(config_state->vim_state.yank_head, reg);
                         if(yank){
                              const char* itr = yank->text;
                              while(*itr){
                                   terminal_send_key(terminal, *itr);
                                   itr++;
                              }
                         }
                    }
               }
               break;
          case VKH_COMPLETED_ACTION_SUCCESS:
          {
               if(config_state->vim_state.mode == VM_INSERT){
                    TerminalNode_t* terminal_node = is_terminal_buffer(config_state->terminal_head, buffer_view->buffer);
                    if(terminal_node){
                         buffer_view->cursor = terminal_node->terminal.cursor;
                         view_follow_cursor(buffer_view, config_state->line_number_type);
                    }
               }else if(vkh_result.completed_action.motion.type == VMT_SEARCH ||
                        vkh_result.completed_action.motion.type == VMT_SEARCH_WORD_UNDER_CURSOR){
                    config_state->do_not_highlight_search = false;
                    view_center_when_cursor_outside_portion(buffer_view, 0.15f, 0.85f);
                    JumpArray_t* jump_array = &((BufferViewState_t*)(buffer_view->user_data))->jump_array;
                    jump_insert(jump_array, buffer_view->buffer->filename, save_cursor);
               }else if(vkh_result.completed_action.motion.type == VMT_GOTO_MARK){
                    config_state->do_not_highlight_search = false;
                    view_center_when_cursor_outside_portion(buffer_view, 0.15f, 0.85f);
                    JumpArray_t* jump_array = &((BufferViewState_t*)(buffer_view->user_data))->jump_array;
                    jump_insert(jump_array, buffer_view->buffer->filename, save_cursor);
               }else if(vkh_result.completed_action.motion.type == VMT_BEGINNING_OF_FILE ||
                        vkh_result.completed_action.motion.type == VMT_END_OF_FILE){
                    JumpArray_t* jump_array = &((BufferViewState_t*)(buffer_view->user_data))->jump_array;
                    jump_insert(jump_array, save_buffer->filename, save_cursor);
               }else if(vkh_result.completed_action.change.type == VCT_YANK){
                    VimActionRange_t action_range;
                    if(vim_action_get_range(&vkh_result.completed_action, buffer, cursor, &config_state->vim_state,
                                            &buffer_state->vim_buffer_state, &action_range)){
                         buffer->blink = true;
                         buffer->highlight_start = *action_range.sorted_start;
                         buffer->highlight_end = *action_range.sorted_end;
                    }
               }else if(vkh_result.completed_action.change.type == VCT_SUBSTITUTE){
                    VimYankNode_t* yank = vim_yank_find(config_state->vim_state.yank_head,
                                                        vkh_result.completed_action.change.reg ? vkh_result.completed_action.change.reg : '"');
                    if(yank){
                         VimActionRange_t action_range;
                         if(vim_action_get_range(&vkh_result.completed_action, buffer, cursor, &config_state->vim_state,
                                                 &buffer_state->vim_buffer_state, &action_range)){
                              buffer->blink = true;
                              buffer->highlight_start = *action_range.sorted_start;
                              buffer->highlight_end = buffer->highlight_start;
                              int64_t len = strlen(yank->text) - 1;
                              ce_advance_cursor(buffer, &buffer->highlight_end, len);
                         }
                    }
               }else if(vkh_result.completed_action.change.type == VCT_PASTE_BEFORE){
                    VimYankNode_t* yank = vim_yank_find(config_state->vim_state.yank_head,
                                                        vkh_result.completed_action.change.reg ? vkh_result.completed_action.change.reg : '"');
                    if(yank){
                         switch(yank->mode){
                         default:
                              break;
                         case YANK_NORMAL:
                         {
                              buffer->blink = true;
                              buffer->highlight_start = *cursor;
                              buffer->highlight_end = buffer->highlight_start;
                              int64_t len = strlen(yank->text) - 1;
                              ce_advance_cursor(buffer, &buffer->highlight_end, len);
                         } break;
                         case YANK_LINE:
                         {
                              buffer->blink = true;
                              buffer->highlight_start = (Point_t){0, cursor->y};
                              buffer->highlight_end = buffer->highlight_start;
                              int64_t len = strlen(yank->text) - 1;
                              ce_advance_cursor(buffer, &buffer->highlight_end, len);
                         } break;
                         }
                    }
               }else if(vkh_result.completed_action.change.type == VCT_PASTE_AFTER){
                    VimYankNode_t* yank = vim_yank_find(config_state->vim_state.yank_head,
                                                        vkh_result.completed_action.change.reg ? vkh_result.completed_action.change.reg : '"');

                    switch(yank->mode){
                    default:
                         break;
                    case YANK_NORMAL:
                    {
                         buffer->blink = true;
                         buffer->highlight_end = *cursor;
                         buffer->highlight_start = buffer->highlight_end;
                         int64_t len = strlen(yank->text) - 1;
                         ce_advance_cursor(buffer, &buffer->highlight_start, -len);
                    } break;
                    case YANK_LINE:
                    {
                         buffer->blink = true;
                         buffer->highlight_start = (Point_t){0, cursor->y};
                         buffer->highlight_end = buffer->highlight_start;
                         int64_t len = strlen(yank->text) - 1;
                         ce_advance_cursor(buffer, &buffer->highlight_end, len);
                    } break;
                    }
               }
          } break;
          }
     }

     // incremental search, move the cursor and center the view as we find matches
     switch(config_state->input.type){
     default:
          break;
     case INPUT_SEARCH:
     case INPUT_REVERSE_SEARCH:
          if(config_state->input.buffer.lines == NULL){
               pthread_mutex_lock(&view_input_save_lock);
               config_state->input.view_save->cursor = config_state->vim_state.search.start;
               pthread_mutex_unlock(&view_input_save_lock);
          }else{
               size_t search_len = strlen(config_state->input.buffer.lines[0]);
               if(search_len){
                    int rc = regcomp(&config_state->vim_state.search.regex, config_state->input.buffer.lines[0], REG_EXTENDED);
                    if(rc == 0){
                         config_state->do_not_highlight_search = false;
                         config_state->vim_state.search.valid_regex = true;

                         Point_t match = {};
                         int64_t match_len = 0;
                         if(config_state->input.buffer.lines[0][0] &&
                              ce_find_regex(config_state->input.view_save->buffer,
                                            config_state->vim_state.search.start, &config_state->vim_state.search.regex, &match,
                                            &match_len, config_state->vim_state.search.direction)){
                              pthread_mutex_lock(&view_input_save_lock);
                              ce_set_cursor(config_state->input.view_save->buffer,
                                            &config_state->input.view_save->cursor, match);
                              pthread_mutex_unlock(&view_input_save_lock);
                              view_center(config_state->input.view_save);
                         }else{
                              pthread_mutex_lock(&view_input_save_lock);
                              config_state->input.view_save->cursor = config_state->vim_state.search.start;
                              pthread_mutex_unlock(&view_input_save_lock);
                              view_center(config_state->input.view_save);
                         }
                    }else{
                         config_state->vim_state.search.valid_regex = false;
                    }
               }else{
                    config_state->vim_state.search.valid_regex = false;
               }
          }
          break;
     }

     if(config_state->vim_state.mode != VM_INSERT){
          auto_complete_end(&config_state->auto_complete);
     }

     if(config_state->quit) return false;

     if(ce_buffer_in_view(config_state->tab_current->view_head, &config_state->buffer_list_buffer)){
          info_update_buffer_list_buffer(&config_state->buffer_list_buffer, *head);
     }

     if(config_state->tab_current->view_current->buffer != &config_state->mark_list_buffer &&
        ce_buffer_in_view(config_state->tab_current->view_head, &config_state->mark_list_buffer)){
          info_update_mark_list_buffer(&config_state->mark_list_buffer, buffer);
     }

     if(ce_buffer_in_view(config_state->tab_current->view_head, &config_state->yank_list_buffer)){
          info_update_yank_list_buffer(&config_state->yank_list_buffer, config_state->vim_state.yank_head);
     }

     if(ce_buffer_in_view(config_state->tab_current->view_head, &config_state->macro_list_buffer)){
          info_update_macro_list_buffer(&config_state->macro_list_buffer, &config_state->vim_state);
     }

     // grab the draw lock so we can draw
     if(pthread_mutex_trylock(&draw_lock) == 0){
          view_drawer(user_data);
          pthread_mutex_unlock(&draw_lock);
     }

     g_last_key = key;
     return true;
}

void view_drawer(void* user_data)
{
     // clear all lines in the terminal
     erase();

     ConfigState_t* config_state = user_data;
     Buffer_t* buffer = config_state->tab_current->view_current->buffer;
     BufferState_t* buffer_state = buffer->user_data;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Point_t* cursor = &config_state->tab_current->view_current->cursor;

     Point_t top_left;
     Point_t bottom_right;
     misc_get_user_terminal_view_rect(config_state->tab_head, &top_left, &bottom_right);
     ce_calc_views(config_state->tab_current->view_head, top_left, bottom_right);

     // TODO: don't draw over borders!

     LineNumberType_t line_number_type = config_state->line_number_type;
     if(buffer->absolutely_no_line_numbers_under_any_circumstances){
          line_number_type = LNT_NONE;
     }

     Point_t terminal_cursor = {};

     // draw input dialogue or auto complete if visible
     Point_t input_top_left = {};
     Point_t input_bottom_right = {};
     Point_t auto_complete_top_left = {};
     Point_t auto_complete_bottom_right = {};
     if(config_state->input.type > INPUT_NONE){
          int64_t input_view_height = config_state->input.buffer.line_count;
          if(input_view_height) input_view_height--;
          pthread_mutex_lock(&view_input_save_lock);
          input_top_left = (Point_t){config_state->input.view_save->top_left.x,
                                     (config_state->input.view_save->bottom_right.y - input_view_height) - 1};
          input_bottom_right = config_state->input.view_save->bottom_right;
          pthread_mutex_unlock(&view_input_save_lock);
          if(input_top_left.y < 1) input_top_left.y = 1; // clamp to growing to 1, account for input message
          if(input_bottom_right.y == g_terminal_dimensions->y - 2){
               input_top_left.y++;
               input_bottom_right.y++; // account for bottom status bar
          }
          ce_calc_views(config_state->input.view, input_top_left, input_bottom_right);
          pthread_mutex_lock(&view_input_save_lock);
          config_state->input.view_save->bottom_right.y = input_top_left.y - 1;
          pthread_mutex_unlock(&view_input_save_lock);

          // update cursor based on view size changing
          view_follow_cursor(buffer_view, line_number_type);
          terminal_cursor = misc_get_cursor_on_user_terminal(cursor, buffer_view, line_number_type);

          if(auto_completing(&config_state->auto_complete)){
               int64_t auto_complete_view_height = config_state->view_auto_complete->buffer->line_count;
               if(auto_complete_view_height > config_state->max_auto_complete_height) auto_complete_view_height = config_state->max_auto_complete_height;
               auto_complete_top_left = (Point_t){input_top_left.x, (input_top_left.y - auto_complete_view_height) - 1};
               if(auto_complete_top_left.y < 0) auto_complete_top_left.y = 0; // account for separator line
               auto_complete_bottom_right = (Point_t){input_bottom_right.x, input_top_left.y - 1};
               ce_calc_views(config_state->view_auto_complete, auto_complete_top_left, auto_complete_bottom_right);
               view_follow_highlight(config_state->view_auto_complete);
          }
     }else if(auto_completing(&config_state->auto_complete)){
          view_follow_cursor(buffer_view, line_number_type);
          terminal_cursor = misc_get_cursor_on_user_terminal(cursor, buffer_view, line_number_type);

          int64_t auto_complete_view_height = config_state->view_auto_complete->buffer->line_count;
          if(auto_complete_view_height > config_state->max_auto_complete_height) auto_complete_view_height = config_state->max_auto_complete_height;
          auto_complete_top_left = (Point_t){config_state->tab_current->view_current->top_left.x,
                                             (config_state->tab_current->view_current->bottom_right.y - auto_complete_view_height) - 1};
          if(auto_complete_top_left.y <= terminal_cursor.y) auto_complete_top_left.y = terminal_cursor.y + 2;
          auto_complete_bottom_right = (Point_t){config_state->tab_current->view_current->bottom_right.x,
                                                 config_state->tab_current->view_current->bottom_right.y};
          ce_calc_views(config_state->view_auto_complete, auto_complete_top_left, auto_complete_bottom_right);
          view_follow_highlight(config_state->view_auto_complete);
     }else{
          view_follow_cursor(buffer_view, line_number_type);
          terminal_cursor = misc_get_cursor_on_user_terminal(cursor, buffer_view, line_number_type);
     }

     // setup highlight for visual modes
     if(config_state->vim_state.mode == VM_VISUAL_RANGE){
          const Point_t* start = &config_state->vim_state.visual_start;
          const Point_t* end = &config_state->tab_current->view_current->cursor;

          ce_sort_points(&start, &end);

          buffer->highlight_start = *start;
          buffer->highlight_end = *end;
     }else if(config_state->vim_state.mode == VM_VISUAL_LINE){
          int64_t start_line = config_state->vim_state.visual_start.y;
          int64_t end_line = config_state->tab_current->view_current->cursor.y;

          if(start_line > end_line){
               int64_t tmp = start_line;
               start_line = end_line;
               end_line = tmp;
          }

          buffer->highlight_start = (Point_t){0, start_line};
          buffer->highlight_end = (Point_t){strlen(config_state->tab_current->view_current->buffer->lines[end_line]), end_line};
     }else if(!buffer->blink){
          buffer->highlight_start = (Point_t){0, 0};
          buffer->highlight_end = (Point_t){-1, 0};
     }

     // setup highlight for hotmark
     Point_t* vim_mark = vim_mark_find(buffer_state->vim_buffer_state.mark_head, '0');
     if(vim_mark) buffer->mark = *vim_mark;

     // turn off highlighting the current line when in visual mode
     HighlightLineType_t highlight_line_type = config_state->highlight_line_type;
     if(config_state->vim_state.mode == VM_VISUAL_RANGE || config_state->vim_state.mode == VM_VISUAL_LINE){
          highlight_line_type = HLT_NONE;
     }

     // set search regex if one is available to highlight all matches
     regex_t* highlight_regex = NULL;
     if(!config_state->do_not_highlight_search && config_state->vim_state.search.valid_regex){
          highlight_regex = &config_state->vim_state.search.regex;
     }

     // draw starting from the head
     ce_draw_views(config_state->tab_current->view_head, highlight_regex, config_state->line_number_type, highlight_line_type);

     // draw input status
     if(auto_completing(&config_state->auto_complete)){
          move(auto_complete_top_left.y - 1, auto_complete_top_left.x);

          attron(COLOR_PAIR(S_BORDERS));
          for(int i = auto_complete_top_left.x; i < auto_complete_bottom_right.x; ++i) addch(ACS_HLINE);
          // if we are at the edge of the terminal, draw the inclusing horizontal line. We
          if(auto_complete_bottom_right.x == g_terminal_dimensions->x - 1) addch(ACS_HLINE);

          // clear auto complete buffer section
          for(int y = auto_complete_top_left.y; y <= auto_complete_bottom_right.y; ++y){
               move(y, auto_complete_top_left.x);
               for(int x = auto_complete_top_left.x; x <= auto_complete_bottom_right.x; ++x){
                    addch(' ');
               }
          }

          ce_draw_views(config_state->view_auto_complete, NULL, LNT_NONE, HLT_NONE);
     }

     // draw all view statuses recursively
     draw_view_statuses(config_state->tab_current->view_head, config_state->tab_current->view_current,
                        config_state->vim_state.mode, config_state->vim_state.recording_macro,
                        config_state->terminal_current);

     // if in input mode, draw input mode's status line
     if(config_state->input.type > INPUT_NONE){
          if(config_state->input.view == config_state->tab_current->view_current){
               move(input_top_left.y - 1, input_top_left.x);

               attron(COLOR_PAIR(S_BORDERS));
               for(int i = input_top_left.x; i < input_bottom_right.x; ++i) addch(ACS_HLINE);
               // if we are at the edge of the terminal, draw the inclusing horizontal line. We
               if(input_bottom_right.x == g_terminal_dimensions->x - 1) addch(ACS_HLINE);

               attron(COLOR_PAIR(S_INPUT_STATUS));
               mvprintw(input_top_left.y - 1, input_top_left.x + 1, " %s ", config_state->input.message);
          }

          standend();
          // clear input buffer section
          for(int y = input_top_left.y; y <= input_bottom_right.y; ++y){
               move(y, input_top_left.x);
               for(int x = input_top_left.x; x <= input_bottom_right.x; ++x){
                    addch(' ');
               }
          }

          ce_draw_views(config_state->input.view, NULL, LNT_NONE, HLT_NONE);
          draw_view_statuses(config_state->input.view, config_state->tab_current->view_current,
                             config_state->vim_state.mode, config_state->vim_state.recording_macro,
                             config_state->terminal_current);
     }

     // draw auto complete
     if(auto_completing(&config_state->auto_complete) && config_state->auto_complete.current && config_state->auto_complete.type == ACT_EXACT){
          move(terminal_cursor.y, terminal_cursor.x);
          int64_t offset = cursor->x - config_state->auto_complete.start.x;
          if(offset >= 0){
               const char* option = config_state->auto_complete.current->option + offset;
               attron(COLOR_PAIR(S_AUTO_COMPLETE));
               while(*option){
                    addch(*option);
                    option++;
               }
          }
     }

     // draw tab line
     if(config_state->tab_head->next){
          // clear tab line with inverses
          move(0, 0);
          attron(COLOR_PAIR(S_BORDERS));
          for(int i = 0; i < g_terminal_dimensions->x; ++i) addch(ACS_HLINE);
          for(int i = 0; i < g_terminal_dimensions->x; ++i){
               Point_t p = {i, 0};
               ce_connect_border_lines(p);
          }

          TabView_t* tab_itr = config_state->tab_head;
          move(0, 1);

          // draw before current tab
          attron(COLOR_PAIR(S_TAB_NAME));
          addch(' ');
          while(tab_itr && tab_itr != config_state->tab_current){
               printw(tab_itr->view_current->buffer->name);
               addch(' ');
               tab_itr = tab_itr->next;
          }

          // draw current tab
          assert(tab_itr == config_state->tab_current);
          attron(COLOR_PAIR(S_CURRENT_TAB_NAME));
          printw(tab_itr->view_current->buffer->name);
          addch(' ');

          // draw rest of tabs
          attron(COLOR_PAIR(S_TAB_NAME));
          tab_itr = tab_itr->next;
          while(tab_itr){
               printw(tab_itr->view_current->buffer->name);
               addch(' ');
               tab_itr = tab_itr->next;
          }
     }

     // reset the cursor
     move(terminal_cursor.y, terminal_cursor.x);

     // update the screen with what we drew
     refresh();

     // clear mark if one existed
     if(vim_mark) buffer->mark = (Point_t){-1, -1};
     if(buffer->blink) buffer->blink = false;

     gettimeofday(&config_state->last_draw_time, NULL);
}
