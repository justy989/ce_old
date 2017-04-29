#include "destination.h"

#include <assert.h>
#include <unistd.h>
#include <ctype.h>

static bool str_all_digits(const char* string)
{
     for(const char* c = string; *c; c++){
          if(!isdigit(*c)) return false;
     }

     return true;
}

// NOTE: modifies last_jump only if we succeed
bool dest_goto_file_location_in_buffer(BufferNode_t* head, Buffer_t* buffer, int64_t line, BufferView_t* head_view,
                                       BufferView_t* view, int64_t* last_jump, char* terminal_current_directory)
{
     if(!buffer->line_count) return false;

     assert(line >= 0);
     assert(line < buffer->line_count);

     char filename[BUFSIZ];
     char line_number_str[BUFSIZ];
     char column_number_str[BUFSIZ];

     column_number_str[0] = 0;

     // prepend the terminal current directory with a slash
     strncpy(filename, terminal_current_directory, BUFSIZ);
     int64_t filename_start = strlen(filename);
     filename[filename_start] = '/';
     filename_start++;

     // check for different file destination formats we understand
     if(buffer->lines[line][0] == '@' && buffer->lines[line][1] == '@'){
          // handle git diff format
          // '@@ -1633,9 +1636,26 @@ static int set_color(Syntax_t syntax, HighlightType_t highlight_type)'

          // search backward for a filename
          int64_t file_line = line - 1;
          for(;file_line >= 0; --file_line){
               if(buffer->lines[file_line][0] == '-' && buffer->lines[file_line][1] == '-' && buffer->lines[file_line][2] == '-'){
                    break;
               }else if(buffer->lines[file_line][0] == '+' && buffer->lines[file_line][1] == '+' && buffer->lines[file_line][2] == '+'){
                    break;
               }
          }

          if(file_line < 0) return false;

          // --- a/ce.c
          // +++ b/ce.c
          char* first_slash = strchr(buffer->lines[file_line], '/');
          if(!first_slash) return false;
          strncpy(filename + filename_start, first_slash + 1, BUFSIZ - filename_start);
          if(access(filename, F_OK) == -1) return false; // file does not exist

          char* plus = strchr(buffer->lines[line], '+');
          if(!plus) return false;
          char* comma = strchr(plus, ',');
          if(!comma) return false;

          int64_t line_number_len = comma - (plus + 1);
          assert(line_number_len < BUFSIZ);
          strncpy(line_number_str, plus + 1, line_number_len);
          line_number_str[line_number_len] = 0;
     }else if(buffer->lines[line][0] == '=' && buffer->lines[line][1] == '='){
          // handle valgrind format
          // '==7330==    by 0x638B16A: initializer (ce_config.c:1983)'
          char* open_paren = strchr(buffer->lines[line], '(');
          char* close_paren = strchr(buffer->lines[line], ')');
          if(!open_paren || !close_paren) return false;

          char* file_end = strchr(open_paren, ':');
          if(!file_end) return false;

          int64_t filename_len = file_end - (open_paren + 1);
          if(filename_len > (BUFSIZ - filename_start)) return false;
          strncpy(filename + filename_start, (open_paren + 1), filename_len);

          int64_t line_number_len = (close_paren - file_end) - 1;
          strncpy(line_number_str, file_end + 1, line_number_len);

          if(!str_all_digits(line_number_str)) return false;
     }else{
          // handle grep, and gcc formats
          // 'ce_config.c:1983:15'
          char* file_end = strpbrk(buffer->lines[line], ": ");
          if(!file_end) return false;
          if(buffer->lines[line][0] == '/') filename_start = 0; // if the buffer line starts with a '/', then overwrite the initial path
          int64_t filename_len = file_end - buffer->lines[line];
          if(filename_len > (BUFSIZ - filename_start)) return false;
          strncpy(filename + filename_start, buffer->lines[line], filename_len);
          filename[filename_start + filename_len] = 0;
          if(access(filename, F_OK) == -1) return false; // file does not exist

          char* line_number_begin_delim = NULL;
          char* line_number_end_delim = NULL;
          if(*file_end == ' '){
               // format: 'filepath search_symbol line '
               char* second_space = strchr(file_end + 1, ' ');
               if(!second_space) return false;
               line_number_begin_delim = second_space;
               line_number_end_delim = strchr(second_space + 1, ' ');
               if(!line_number_end_delim) return false;
          }else{
               // format: 'filepath:line:column:'
               line_number_begin_delim = file_end;
               char* second_colon = strchr(line_number_begin_delim + 1, ':');
               if(!second_colon) return false;
               line_number_end_delim = second_colon;
          }

          int64_t line_number_len = line_number_end_delim - (line_number_begin_delim + 1);
          strncpy(line_number_str, line_number_begin_delim + 1, line_number_len);
          line_number_str[line_number_len] = 0;

          if(!str_all_digits(line_number_str)) return false;

          char* third_colon = strchr(line_number_end_delim + 1, ':');
          if(third_colon){
               line_number_len = third_colon - (line_number_end_delim + 1);
               strncpy(column_number_str, line_number_end_delim + 1, line_number_len);
               column_number_str[line_number_len] = 0;

               if(!str_all_digits(column_number_str)) column_number_str[0] = 0;
          }
     }

     if(line_number_str[0]){
          int line_number = atoi(line_number_str);
          int column = (*column_number_str) ? atoi(column_number_str) : 0;
          bool opened_file = dest_open_file(head, view, filename, line_number, column);
          if(opened_file){
               BufferView_t* command_view = ce_buffer_in_view(head_view, buffer);
               if(command_view) command_view->top_row = line;
               *last_jump = line;
               return true;
          }
     }

     return false;
}

void dest_jump_to_next_in_terminal(BufferNode_t* head, TerminalNode_t* terminal_head, TerminalNode_t** terminal_current,
                                   BufferView_t* view_head, BufferView_t* view_current, bool forwards)
{
     if(!terminal_current) return;

     Buffer_t* terminal_buffer = (*terminal_current)->buffer;
     BufferView_t* terminal_view = ce_buffer_in_view(view_head, terminal_buffer);

     if(!terminal_view){
          TerminalNode_t* term_itr = terminal_head;
          while(term_itr){
               terminal_view = ce_buffer_in_view(view_head, term_itr->buffer);
               if(terminal_view){
                    terminal_buffer = term_itr->buffer;
                    *terminal_current = term_itr;
                    break;
               }
               term_itr = term_itr->next;
          }
     }

     int64_t lines_checked = 0;
     int64_t delta = forwards ? 1 : -1;
     for(int64_t i = (*terminal_current)->last_jump_location + delta; lines_checked < terminal_buffer->line_count;
         i += delta, lines_checked++){
          if(i == terminal_buffer->line_count && forwards){
               i = 0;
          }else if(i <= 0 && !forwards){
               i = terminal_buffer->line_count - 1;
          }

          char* terminal_current_directory = terminal_get_current_directory(&(*terminal_current)->terminal);
          if(dest_goto_file_location_in_buffer(head, terminal_buffer, i, view_head,
                                               view_current, &(*terminal_current)->last_jump_location,
                                               terminal_current_directory)){
               // NOTE: change the cursor, so when you go back to that buffer, your cursor is on the line we last jumped to
               terminal_buffer->cursor.x = 0;
               terminal_buffer->cursor.y = i;
               if(terminal_view) terminal_view->cursor = terminal_buffer->cursor;
               free(terminal_current_directory);
               break;
          }

          free(terminal_current_directory);
     }
}

void dest_cscope_goto_definition(BufferView_t* view_current, BufferNode_t* head, const char* search_word)
{
     char command[BUFSIZ];
     snprintf(command, BUFSIZ, "cscope -L1%s", search_word);
     FILE* cscope_output_file = popen(command, "r");
     if(cscope_output_file == NULL){
          ce_message("popen(%s) failed: %s", command, strerror(errno));
          return;
     }

     char cscope_output[BUFSIZ];
     if(fgets(cscope_output, sizeof(cscope_output), cscope_output_file) == NULL){
          ce_message("fgets() failed to obtain cscope output for %s", command);
     }else{
          // parse cscope output and jump to destination
          char* file_end = strpbrk(cscope_output, ": ");
          if(!file_end) {
               ce_message("unsupported cscope output %s", cscope_output);
               goto pclose_cscope;
          }
          int64_t filename_len = file_end - cscope_output;
          char* filename = strndupa(cscope_output, filename_len);
          if(access(filename, F_OK) == -1){
               ce_message("cscope file %s not found", filename);
               goto pclose_cscope;
          }

          char* line_number_begin_delim = NULL;
          char* line_number_end_delim = NULL;
          if(*file_end == ' '){
               // format: 'filepath search_symbol line '
               char* second_space = strchr(file_end + 1, ' ');
               if(!second_space){
                    ce_message("cscope line number not found in %s", cscope_output);
                    goto pclose_cscope;
               }
               line_number_begin_delim = second_space;
               line_number_end_delim = strchr(second_space + 1, ' ');
               if(!line_number_end_delim){
                    ce_message("cscope output in unexpected format %s", cscope_output);
                    goto pclose_cscope;
               }
               *line_number_end_delim = '\0';
               int line_number = atoi(line_number_begin_delim + 1);
               dest_open_file(head, view_current, filename, line_number, 0);
          }else{
               ce_message("unexpected cscope output %s", cscope_output);
          }
     }

pclose_cscope:
     if(pclose(cscope_output_file) == -1){
          ce_message("pclose(%s) failed: %s", command, strerror(errno));
     }
}
