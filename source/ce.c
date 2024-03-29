#include "ce.h"
#include "syntax.h"

#include <ctype.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>

Point_t* g_terminal_dimensions = NULL;

Direction_t ce_reverse_direction(Direction_t to_reverse){
     return (to_reverse == CE_UP) ? CE_DOWN : CE_UP;
}

int64_t ce_count_string_lines(const char* string)
{
     int64_t string_length = strlen(string);
     int64_t line_count = 0;
     for(int64_t i = 0; i <= string_length; ++i){
          if(string[i] == NEWLINE || string[i] == 0) line_count++;
     }

     // one line files usually contain newlines at the end
     if(line_count == 2 && string[string_length-1] == NEWLINE){
          line_count--;
     }

     return line_count;
}

void mark_buffer_as_modified(Buffer_t* buffer)
{
     if(buffer->status != BS_READONLY){
          buffer->status = BS_MODIFIED;
     }
}

bool ce_alloc_lines(Buffer_t* buffer, int64_t line_count)
{
     if(buffer->status == BS_READONLY) return false;

     if(buffer->lines) ce_clear_lines(buffer);

     if(line_count <= 0){
          ce_message("%s() tried to allocate %"PRId64" lines for a buffer, but we can only allocated > 0 lines", __FUNCTION__, line_count);
          return false;
     }

     // NOTE: if we have lines, we should free them here!
     buffer->lines = calloc(1, line_count * sizeof(char*));
     if(!buffer->lines){
          ce_message("%s() failed to allocate %"PRId64" lines for buffer", __FUNCTION__, line_count);
          return false;
     }

     buffer->line_count = line_count;

     // clear the lines
     for(int64_t i = 0; i < line_count; ++i){
          buffer->lines[i] = calloc(1, sizeof(buffer->lines[i]));
          if(!buffer->lines[i]){
               ce_message("failed to calloc() new line %"PRId64, i);
               return false;
          }
     }

     mark_buffer_as_modified(buffer);
     return true;
}

LoadFileResult_t ce_load_file(Buffer_t* buffer, const char* filename)
{
     ce_message("load file '%s'", filename);

     if(buffer->lines) ce_free_buffer(buffer);

     // check if directory
     {
          struct stat info = {};
          stat(filename, &info);
          if(S_ISDIR(info.st_mode)){
               ce_message("%s() '%s' is a directory.", __FUNCTION__, filename);
               return LF_IS_DIRECTORY;
          }
     }

     // read the entire file
     size_t content_size;
     char* contents = NULL;
     {
          FILE* file = fopen(filename, "rb");
          if(!file){
               ce_message("%s() fopen('%s', 'rb') failed: %s", __FUNCTION__, filename, strerror(errno));
               return LF_DOES_NOT_EXIST;
          }

          fseek(file, 0, SEEK_END);
          content_size = ftell(file);
          fseek(file, 0, SEEK_SET);

          contents = malloc(content_size + 1);
          fread(contents, content_size, 1, file);
          contents[content_size] = 0;

          // strip the ending '\n'
          if(contents[content_size - 1] == NEWLINE) contents[content_size - 1] = 0;

          ce_load_string(buffer, contents);

          fclose(file);

          buffer->filename = strdup(filename);
     }

     if(access(filename, W_OK) != 0){
          buffer->status = BS_READONLY;
     }else{
          buffer->status = BS_NONE;
     }

     free(contents);
     return LF_SUCCESS;
}

bool ce_load_string(Buffer_t* buffer, const char* str)
{
     return ce_insert_string(buffer, (Point_t){0, 0}, str);
}

void ce_free_buffer(Buffer_t* buffer)
{
     if(!buffer){
          return;
     }

     free(buffer->filename);
     buffer->filename = NULL;

     buffer->status = BS_NONE;

     ce_clear_lines(buffer);
}

void clear_lines_impl(Buffer_t* buffer)
{
     if(buffer->lines){
          for(int64_t i = 0; i < buffer->line_count; ++i){
               free(buffer->lines[i]);
          }

          free(buffer->lines);
          buffer->lines = NULL;
          buffer->line_count = 0;
     }

     mark_buffer_as_modified(buffer);
}

void ce_clear_lines(Buffer_t* buffer)
{
     if(buffer->status == BS_READONLY) return;

     clear_lines_impl(buffer);
}

void ce_clear_lines_readonly(Buffer_t* buffer)
{
     if(buffer->status != BS_READONLY) return;

     clear_lines_impl(buffer);
}

bool ce_point_on_buffer(const Buffer_t* buffer, Point_t location)
{
     if(location.y < 0 || location.x < 0){
          return false;
     }

     if(location.y >= buffer->line_count){
          return false;
     }

     char* line = buffer->lines[location.y];
     int64_t line_len = 0;

     if(line) line_len = strlen(line);

     if(location.x > line_len){
          return false;
     }

     return true;
}

static bool insert_line_impl(Buffer_t* buffer, int64_t line, const char* string);

bool ce_insert_char(Buffer_t* buffer, Point_t location, char c)
{
     const char str[2] = {c, 0};
     return ce_insert_string(buffer, location, str);
}

bool ce_insert_char_readonly(Buffer_t* buffer, Point_t location, char c)
{
     const char str[2] = {c, 0};
     return ce_insert_string_readonly(buffer, location, str);
}

bool ce_append_char(Buffer_t* buffer, char c)
{
     Point_t end = {0, 0};
     if(buffer->line_count){
          end.y = buffer->line_count - 1;
          end.x = strlen(buffer->lines[end.y]);
     }

     return ce_insert_char(buffer, end, c);
}

bool ce_append_char_readonly(Buffer_t* buffer, char c)
{
     Point_t end = {0, 0};
     if(buffer->line_count){
          end.y = buffer->line_count - 1;
          end.x = strlen(buffer->lines[end.y]);
     }

     return ce_insert_char_readonly(buffer, end, c);
}

static bool insert_string_impl(Buffer_t* buffer, Point_t location, const char* new_string)
{
     if(location.x == 0 && location.y == 0){
          // pass
     }else{
          if(!ce_point_on_buffer(buffer, location)){
               if(location.x == 0 && location.y == buffer->line_count){
                    // append to end of file
                    return insert_line_impl(buffer, location.y, new_string);
               }else{
                    return false;
               }
          }
     }

     int64_t new_string_length = strlen(new_string);

     if(new_string_length == 0){
          ce_message("%s() failed to insert empty string", __FUNCTION__);
          return false;
     }

     // if the whole buffer is empty
     if(!buffer->lines){
          int64_t line_count = ce_count_string_lines(new_string);
          ce_alloc_lines(buffer, line_count);

          int64_t line = 0;
          int64_t last_newline = -1;
          for(int64_t i = 0; i <= new_string_length; ++i){
               if(new_string[i] != NEWLINE && new_string[i] != 0) continue;
               if(line >= line_count) break;

               int64_t length = (i - 1) - last_newline;
               char* new_line = realloc(buffer->lines[line], length + 1);
               if(!new_line){
                    ce_message("%s() failed to alloc line %"PRId64, __FUNCTION__, line);
                    return false;
               }

               memcpy(new_line, new_string + last_newline + 1, length);
               new_line[length] = 0;
               buffer->lines[line] = new_line;

               last_newline = i;
               line++;
          }

          mark_buffer_as_modified(buffer);
          return true;
     }

     char* current_line = buffer->lines[location.y];
     const char* first_part = current_line;
     const char* second_part = current_line + location.x;

     int64_t first_length = location.x;
     int64_t second_length = strlen(second_part);

     // find the first line range
     const char* end_of_line = new_string;
     while(*end_of_line != NEWLINE && *end_of_line != 0) end_of_line++;

     // if the string they want to insert does NOT contain any newlines
     if(*end_of_line == 0){
          // we are only adding a single line, so include all the pieces
          int64_t new_line_length = new_string_length + first_length + second_length;
          char* new_line = realloc(current_line, new_line_length + 1);
          if(!new_line){
               ce_message("%s() failed to allocate new string", __FUNCTION__);
               return false;
          }

          memmove(new_line + first_length + new_string_length, new_line + location.x, second_length);
          memcpy(new_line + first_length, new_string, new_string_length);
          new_line[new_line_length] = 0;
          buffer->lines[location.y] = new_line;
     }else{
          // include the first part and the string up to the newline
          const char* itr = new_string;
          int64_t first_new_line_length = end_of_line - itr;
          int64_t new_line_length = first_new_line_length + first_length;

          // NOTE: mallocing because we want to break up the current line
          char* new_line = malloc(new_line_length + 1);
          if(!new_line){
               ce_message("%s() failed to allocate new string", __FUNCTION__);
               return false;
          }

          strncpy(new_line, first_part, first_length);
          strncpy(new_line + first_length, new_string, first_new_line_length);
          new_line[new_line_length] = 0;

          buffer->lines[location.y] = new_line;

          // now start inserting lines
          int64_t lines_added = 1;
          while(*end_of_line){
               end_of_line++;
               itr = end_of_line;
               while(*end_of_line != NEWLINE && *end_of_line != 0) end_of_line++;

               if(*end_of_line == 0){
                    int64_t next_line_length = (end_of_line - itr);
                    new_line_length = next_line_length + second_length;
                    new_line = malloc(new_line_length + 1);
                    strncpy(new_line, itr, next_line_length);
                    memcpy(new_line + next_line_length, second_part, second_length);
                    new_line[new_line_length] = 0;
                    insert_line_impl(buffer, location.y + lines_added, new_line);
                    free(new_line);
               }else if(itr == end_of_line){
                    insert_line_impl(buffer, location.y + lines_added, NULL);
               }else{
                    new_line_length = end_of_line - itr;
                    new_line = malloc(new_line_length + 1);
                    strncpy(new_line, itr, new_line_length);
                    new_line[new_line_length] = 0;
                    insert_line_impl(buffer, location.y + lines_added, new_line);
                    free(new_line);
               }

               lines_added++;
          }
          free(current_line);
     }

     mark_buffer_as_modified(buffer);
     return true;
}

bool ce_insert_string(Buffer_t* buffer, Point_t location, const char* new_string)
{
     if(buffer->status == BS_READONLY) return false;

     return insert_string_impl(buffer, location, new_string);
}

bool ce_insert_string_readonly(Buffer_t* buffer, Point_t location, const char* new_string)
{
     if(buffer->status != BS_READONLY) return false;

     return insert_string_impl(buffer, location, new_string);
}

bool ce_prepend_string(Buffer_t* buffer, int64_t line, const char* new_string)
{
     Point_t beginning_of_line = {0, line};
     return ce_insert_string(buffer, beginning_of_line, new_string);
}

bool ce_append_string(Buffer_t* buffer, int64_t line, const char* new_string)
{
     Point_t end_of_line = {0, line};
     if(buffer->line_count > line) end_of_line.x = strlen(buffer->lines[line]);
     return ce_insert_string(buffer, end_of_line, new_string);
}

bool ce_append_string_readonly(Buffer_t* buffer, int64_t line, const char* new_string)
{
     Point_t end_of_line = {0, line};
     if(buffer->line_count > line) end_of_line.x = strlen(buffer->lines[line]);
     return ce_insert_string_readonly(buffer, end_of_line, new_string);
}

bool ce_remove_char(Buffer_t* buffer, Point_t location)
{
     return ce_remove_string(buffer, location, 1);
}

bool ce_remove_char_readonly(Buffer_t* buffer, Point_t location)
{
     return ce_remove_string_readonly(buffer, location, 1);
}

char* ce_dupe_string(const Buffer_t* buffer, Point_t start, Point_t end)
{
     if(!ce_point_on_buffer(buffer, start) || !ce_point_on_buffer(buffer, end)) return NULL;

     int64_t total_len = ce_compute_length(buffer, start, end);
     assert(total_len);

     if(start.y == end.y){
          // single line allocation
          char* new_str = malloc(total_len + 1);
          if(!new_str){
               ce_message("%s() failed to alloc string", __FUNCTION__);
               return NULL;
          }
          memcpy(new_str, buffer->lines[start.y] + start.x, total_len);
          if(!new_str[total_len-1]) new_str[total_len-1] = '\n';
          new_str[total_len] = 0;

          return new_str;
     }

     // multi line allocation

     // build string
     char* new_str = malloc(total_len + 1);
     if(!new_str){
          ce_message("%s() failed to alloc string", __FUNCTION__);
          return NULL;
     }

     char* itr = new_str;
     int64_t len = strlen(buffer->lines[start.y] + start.x);
     if(len) memcpy(itr, buffer->lines[start.y] + start.x, len);
     itr[len] = '\n'; // add newline
     itr += len + 1;

     for(int64_t i = start.y + 1; i < end.y; ++i){
          len = strlen(buffer->lines[i]);
          memcpy(itr, buffer->lines[i], len);
          itr[len] = '\n';
          itr += len + 1;
     }

     memcpy(itr, buffer->lines[end.y], end.x+1);
     // if the end cursor is on the NUL terminator, append newline
     if(!new_str[total_len-1]) new_str[total_len-1] = '\n';
     new_str[total_len] = 0;

     return new_str;
}

char* ce_dupe_line(const Buffer_t* buffer, int64_t line)
{
     if(buffer->line_count <= line){
          ce_message("%s() specified line (%"PRId64") above buffer line count (%"PRId64")",
                     __FUNCTION__, line, buffer->line_count);
          return NULL;
     }

     size_t len = strlen(buffer->lines[line]) + 2;
     char* duped_line = malloc(len);
     duped_line[len - 2] = '\n';
     duped_line[len - 1] = 0;
     return memcpy(duped_line, buffer->lines[line], len - 2);
}

char* ce_dupe_lines(const Buffer_t* buffer, int64_t start_line, int64_t end_line)
{
     if(start_line < 0){
          ce_message("%s() specified start line (%"PRId64") less than 0",
                     __FUNCTION__, start_line);
          return NULL;
     }

     if(end_line < 0){
          ce_message("%s() specified end line (%"PRId64") less than 0",
                     __FUNCTION__, end_line);
          return NULL;
     }

     if(buffer->line_count <= start_line){
          ce_message("%s() specified start line (%"PRId64") above buffer line count (%"PRId64")",
                     __FUNCTION__, start_line, buffer->line_count);
          return NULL;
     }

     if(buffer->line_count <= end_line){
          ce_message("%s() specified end line (%"PRId64") above buffer line count (%"PRId64")",
                     __FUNCTION__, end_line, buffer->line_count);
          return NULL;
     }

     // NOTE: I feel like swap() should be in the c standard library? Maybe I should make a macro.
     if(start_line > end_line){
          int64_t tmp = end_line;
          end_line = start_line;
          start_line = tmp;
     }

     int64_t len = 0;
     for(int64_t i = start_line; i <= end_line; ++i){
          len += strlen(buffer->lines[i]) + 1; // account for newline
     }
     len++; // account for null terminator after final newline

     char* duped_line = malloc(len);
     if(!duped_line){
          ce_message("%s() failed to allocate duped line", __FUNCTION__);
          return NULL;
     }

     // copy each line, adding a newline to the end
     int64_t line_len = 0;
     char* itr = duped_line;
     for(int64_t i = start_line; i <= end_line; ++i){
          line_len = strlen(buffer->lines[i]);
          memcpy(itr, buffer->lines[i], line_len);
          itr[line_len] = '\n';
          itr += (line_len + 1);
     }

     duped_line[len-1] = '\0';
     return duped_line;
}

char* ce_dupe_buffer(const Buffer_t* buffer)
{
     Point_t start = {0, 0};
     Point_t end = {0, 0};
     ce_move_cursor_to_end_of_file(buffer, &end);
     return ce_dupe_string(buffer, start, end);
}

bool find_matching_string_forward(const Buffer_t* buffer, Point_t* location, char matchee)
{
     if(!ce_point_on_buffer(buffer, *location)) return false;

     Point_t itr = (Point_t){location->x + 1, location->y};
     char curr = 0;
     char prev = 0;
     char prev_prev = 0;
     int64_t last_index = ce_last_index(buffer->lines[itr.y]);

     while(ce_point_on_buffer(buffer, itr)){
          ce_get_char(buffer, itr, &curr);

          if(curr == matchee){
               if(prev != '\\' || (prev == '\\' && prev_prev == '\\')){
                    *location = itr;
                    return true;
               }
          }

          itr.x++;
          if(itr.x > last_index){
               itr.x = 0;
               itr.y++;
               if(itr.y >= buffer->line_count) break;
               last_index = ce_last_index(buffer->lines[itr.y]);
          }

          prev_prev = prev;
          prev = curr;
     }

     return false;
}

bool find_matching_pair_forward(const Buffer_t* buffer, Point_t* location, char matchee, char match)
{
     if(!ce_point_on_buffer(buffer, *location)) return false;

     Point_t itr = *location;
     char curr = 0;
     char prev = 0;
     int64_t count = 0;
     bool inside_multiline_comment = false;
     int64_t last_index = ce_last_index(buffer->lines[itr.y]);

     while(ce_point_on_buffer(buffer, itr)){
          prev = curr;
          ce_get_char(buffer, itr, &curr);

          if(inside_multiline_comment){
               // TODO: these must be on the same line
               if(curr == '/' && prev == '*'){
                    inside_multiline_comment = false;
               }
          }else{
               if(curr == match){
                    if(count == 0){
                         *location = itr;
                         return true;
                    }else{
                         count--;
                    }
               }else if(curr == matchee && !ce_points_equal(*location, itr)){
                    count++;
               }else if(curr == '"'){
                    if(!find_matching_string_forward(buffer, &itr, '"')){
                         return false;
                    }
               }else if(curr == '\''){
                    if(!find_matching_string_forward(buffer, &itr, '\'')){
                         return false;
                    }
               }else if(curr == '/' && prev == '/'){
                    // this is a comment, ignore the rest of the line
                    itr.x = 0;
                    itr.y++;
                    if(itr.y >= buffer->line_count) break;
                    last_index = ce_last_index(buffer->lines[itr.y]);
                    continue;
               }else if(curr == '*' && prev == '/'){
                    inside_multiline_comment = true;
               }
          }

          itr.x++;
          if(itr.x > last_index){
               itr.y++;
               itr.x = 0;
               if(itr.y >= buffer->line_count) break;
               last_index = ce_last_index(buffer->lines[itr.y]);
          }
     }

     return false;
}

bool index_inside_string_literal(const char* string, int64_t index)
{
     bool inside_double_quote_string = false;
     char prev = 0;

     for(int64_t i = 0; i <= index; ++i){
          char c = string[i];
          switch(c){
          default:
               break;
          case '"':
               if(prev != '\\'){
                    inside_double_quote_string = !inside_double_quote_string;
               }
               break;
          }

          prev = c;
     }

     return inside_double_quote_string;
}

int64_t last_index_before_comment(const Buffer_t* buffer, int64_t line)
{
     int64_t index = 0;

     char* line_string = buffer->lines[line];

     char prev_char = 0;

     while(*line_string){
          if(*line_string == '/' && prev_char == '/'){
               if(!index_inside_string_literal(buffer->lines[line], line_string - buffer->lines[line])){
                    break;
               }
          }

          prev_char = *line_string;
          line_string++;
          index++;
     }

     return index;
}

bool find_matching_string_backward(const Buffer_t* buffer, Point_t* location, char matchee)
{
     if(!ce_point_on_buffer(buffer, *location)) return false;

     Point_t itr = (Point_t){location->x, location->y};
     itr.x--;
     if(itr.x < 0){
          itr.y--;
          if(itr.y < 0) return false;
          itr.x = last_index_before_comment(buffer, itr.y);
     }

     char curr = 0;
     char prev = 0;
     Point_t prev_itr;
     while(ce_point_on_buffer(buffer, itr)){
          ce_get_char(buffer, itr, &curr);

          if(prev == matchee){
               if(curr != '\\'){
                    *location = prev_itr;
                    return true;
               }

               // if the backslashes are adjacent, then they aren't applied to the quote
               Point_t next_point = {itr.x - 1, itr.y};
               if(next_point.x >= 0){
                    char next;
                    ce_get_char(buffer, next_point, &next);
                    if(curr == '\\' && next == '\\'){
                         *location = prev_itr;
                         return true;
                    }
               }
          }

          prev = curr;
          prev_itr = itr;

          itr.x--;
          if(itr.x < 0){
               itr.y--;
               if(itr.y < 0) break;
               itr.x = last_index_before_comment(buffer, itr.y);
          }
     }

     return false;
}

bool find_matching_pair_backward(const Buffer_t* buffer, Point_t* location, char matchee, char match)
{
     if(!ce_point_on_buffer(buffer, *location)) return false;

     Point_t itr = *location;
     char curr = 0;
     char prev = 0;
     int64_t count = 0;
     bool inside_multiline_comment = false;

     itr.x--;
     if(itr.x < 0){
          itr.y--;
          if(itr.y < 0) return false;
          itr.x = last_index_before_comment(buffer, itr.y);
     }

     while(ce_point_on_buffer(buffer, itr)){
          prev = curr;
          ce_get_char(buffer, itr, &curr);

          if(inside_multiline_comment){
               // TODO: these must be on the same line
               if(curr == '*' && prev == '/'){
                    inside_multiline_comment = false;
               }
          }else{
               if(curr == match){
                    if(count == 0){
                         *location = itr;
                         return true;
                    }else{
                         count--;
                    }
               }else if(curr == matchee && !ce_points_equal(*location, itr)){
                    count++;
               }else if(curr == '"'){
                    if(!find_matching_string_backward(buffer, &itr, '"')){
                         return false;
                    }
               }else if(curr == '\''){
                    if(!find_matching_string_backward(buffer, &itr, '\'')){
                         return false;
                    }
               }else if(curr == '/' && prev == '*'){
                    inside_multiline_comment = true;
               }
          }

          itr.x--;
          if(itr.x < 0){
               itr.y--;
               if(itr.y < 0) break;
               itr.x = last_index_before_comment(buffer, itr.y);
          }
     }

	return false;
}

// returns the delta to the matching character; return success
bool ce_move_cursor_to_matching_pair(const Buffer_t* buffer, Point_t* location, char matchee)
{
     switch(matchee){
     case '{':
          return find_matching_pair_forward(buffer, location, '{', '}');
     case '}':
          return find_matching_pair_backward(buffer, location, '}', '{');
     case '(':
          return find_matching_pair_forward(buffer, location, '(', ')');
     case ')':
          return find_matching_pair_backward(buffer, location, ')', '(');
     case '[':
          return find_matching_pair_forward(buffer, location, '[', ']');
     case ']':
          return find_matching_pair_backward(buffer, location, ']', '[');
     case '<':
          return find_matching_pair_forward(buffer, location, '<', '>');
     case '>':
          return find_matching_pair_backward(buffer, location, '>', '<');
     default:
          break;
     }

     ce_message("%s() unhandled match character: '%c'", __FUNCTION__, matchee);
     return false;
}

// returns Point_t at the next matching string; return success
bool ce_find_string(const Buffer_t* buffer, Point_t location, const char* search_str, Point_t* match, Direction_t direction)
{
     size_t search_str_len = strlen(search_str);
     if(!search_str_len) return false;

     int64_t delta = (direction == CE_DOWN) ? 1 : -1;

     location.x += delta;

     if(location.x < 0){
          location.y--;
          location.x = strlen(buffer->lines[location.y]) - 1;
     }else if(location.x >= (int64_t)(strlen(buffer->lines[location.y]))){
          location.x = 0;
          location.y++;
     }

     Point_t end = {0, 0};
     if(direction == CE_DOWN) ce_move_cursor_to_end_of_file(buffer, &end);

     while(!ce_points_equal(location, end)){
          char* str = buffer->lines[location.y] + location.x;

          if(strncmp(str, search_str, search_str_len) == 0){
               *match = location;
               return true;
          }

          location.x += delta;

          if(location.x < 0){
               location.y--;
               if(location.y < 0) break;
               location.x = strlen(buffer->lines[location.y]) - 1;
          }else if(location.x >= (int64_t)(strlen(buffer->lines[location.y]))){
               location.x = 0;
               location.y++;
          }
     }

     return false;
}

bool ce_find_regex(const Buffer_t* buffer, Point_t location, const regex_t* regex, Point_t* match, int64_t* match_len, Direction_t direction)
{
     if(!ce_point_on_buffer(buffer, location)) return false;

     const size_t match_count = 1;
     regmatch_t matches[match_count];

     if(direction == CE_DOWN){
          while(location.y < buffer->line_count){
               int rc = regexec(regex, buffer->lines[location.y] + location.x, match_count, matches, 0);

               // did we find a match?
               if(rc == 0){
                    *match = location;
                    match->x += matches[0].rm_so;
                    *match_len = matches[0].rm_eo - matches[0].rm_so;
                    return true;
               }

               // keep going on 'no match' error, but error out if we hit some other error
               if(rc != REG_NOMATCH){
                    char error_buffer[BUFSIZ];
                    regerror(rc, regex, error_buffer, BUFSIZ);
                    ce_message("regexec() failed: '%s'", error_buffer);
                    return false;
               }

               location.y++;
               location.x = 0;
          }
     }else{
          Point_t start = location;

          // loop over each line, backwards
          while(true){
               Point_t last_valid_match = {-1, location.y};
               int64_t last_valid_match_len = 0;

               location.x = 0;

               if(buffer->lines[location.y][0]){
                    // dupe the line up to the current index
                    char* search_str = strdup(buffer->lines[location.y]);
                    int64_t search_str_len = strlen(search_str);

                    // start at the beginning of the line, find all matches up to the cursor and take that one
                    while(location.x < search_str_len){
                         int rc = regexec(regex, search_str + location.x, match_count, matches, 0);

                         if(rc == 0){
                              int64_t match_x = location.x + matches[0].rm_so;

                              // if the match is after the start, then stop looking in this line
                              if(match_x >= start.x && location.y == start.y) break;

                              // save the match if we find one
                              last_valid_match.x = match_x;
                              last_valid_match_len = matches[0].rm_eo - matches[0].rm_so;
                         }else{
                              // error out if regexec() fails for some reason other than no match
                              if(rc != REG_NOMATCH){
                                   char error_buffer[BUFSIZ];
                                   regerror(rc, regex, error_buffer, BUFSIZ);
                                   ce_message("regexec() failed: '%s'", error_buffer);
                                   return false;
                              }

                              // if there was no match, stop looking in this line
                              break;
                         }

                         // update the next location to start after the match
                         location.x = last_valid_match.x + last_valid_match_len;
                    }

                    free(search_str);
               }

               if(last_valid_match.x >= 0){
                    *match = last_valid_match;
                    *match_len = last_valid_match_len;
                    return true;
               }

               location.y--;

               if(location.y < 0) break;
          }
     }

     return false;
}

void ce_move_cursor_to_beginning_of_line(const Buffer_t* buffer __attribute__((unused)), Point_t* cursor)
{
     assert(ce_point_on_buffer(buffer, *cursor));
     cursor->x = 0;
}

bool ce_move_cursor_to_soft_beginning_of_line(const Buffer_t* buffer, Point_t* cursor)
{
     if(!ce_point_on_buffer(buffer, *cursor)) return false;
     const char* line = buffer->lines[cursor->y];
     int64_t line_len = strlen(line);
     int i;
     for(i = 0; i < line_len && isblank(line[i]); i++);

     cursor->x = i;
     return true;
}

bool ce_move_cursor_to_soft_end_of_line(const Buffer_t* buffer, Point_t* cursor)
{
     if(!ce_point_on_buffer(buffer, *cursor)) return false;
     const char* line = buffer->lines[cursor->y];
     int64_t i = CE_MAX((int64_t) strlen(line) - 1, 0);
     while(i>0 && isblank(line[i])) i--;

     cursor->x = i;
     return true;
}

// underscores are not treated as punctuation for vim movement
int ce_ispunct(int c)
{
     return c != '_' && ispunct(c);
}

bool ce_move_cursor_to_beginning_of_word(const Buffer_t* buffer, Point_t* cursor, bool punctuation_word_boundaries)
{
     assert(ce_point_on_buffer(buffer, *cursor));

     const char* line = buffer->lines[cursor->y];
     int64_t start_x = cursor->x;
     while(cursor->x > 0){
          if(isblank(line[cursor->x-1])){
               // we are starting at a boundary move to the beginning of the previous word
               while(isblank(line[cursor->x-1]) && cursor->x) cursor->x--;
          }
          else if(punctuation_word_boundaries && ce_ispunct(line[cursor->x-1])){
               while(ce_ispunct(line[cursor->x-1]) && cursor->x) cursor->x--;
               break;
          }
          else{
               while(!isblank(line[cursor->x-1]) && (!punctuation_word_boundaries || !ce_ispunct(line[cursor->x-1])) && cursor->x) cursor->x--;
               break;
          }
     }

     if(cursor->x == 0 && cursor->y > 0 && (isblank(line[cursor->x]) || start_x == 0)){
          cursor->y--;
          cursor->x = strlen(buffer->lines[cursor->y]);
          if(cursor->x) return ce_move_cursor_to_beginning_of_word(buffer, cursor, punctuation_word_boundaries);
          // if the previous line is empty, stop there
     }

     return true;
}

bool ce_move_cursor_to_end_of_word(const Buffer_t* buffer, Point_t* location, bool punctuation_word_boundaries)
{
     if(!ce_point_on_buffer(buffer, *location)) return false;
     const char* line = buffer->lines[location->y];
     int line_len = strlen(line);
     bool start_outside_word = false;

     int64_t first_check = location->x + 1;
     int64_t i = first_check;

     for(; i < line_len; ++i){
          if(isblank(line[i])){
               if(!start_outside_word){
                    if(i == first_check){
                         start_outside_word = true;
                    }else{
                         break;
                    }
               }
          }else{
               if(ce_ispunct(line[i])){
                    if(punctuation_word_boundaries){
                         // pass if we start a the end of a word
                         if(i == first_check || start_outside_word){
                              i++;
                         }
                         break;
                    }
               }
               start_outside_word = false;
          }
     }

     if(i == first_check && i >= line_len && location->y < (buffer->line_count - 1)){
          location->y++;
          location->x = 0;
          char first_char = buffer->lines[location->y][0];
          if(isblank(first_char) || !ce_ispunct(first_char) || !punctuation_word_boundaries){
               return ce_move_cursor_to_end_of_word(buffer, location, punctuation_word_boundaries);
          }
     }else if(i != first_check){
          location->x = i - 1;
     }

     return true;
}

bool ce_move_cursor_to_next_word(const Buffer_t* buffer, Point_t* location, bool punctuation_word_boundaries)
{
     if(!ce_point_on_buffer(buffer, *location)) return false;
     const char* line = buffer->lines[location->y];
     int line_len = strlen(line);
     int64_t first_check = location->x + 1;
     int64_t i = first_check;
     bool word_end = isblank(line[location->x]) || (punctuation_word_boundaries && ce_ispunct(line[location->x]));

     for(; i < line_len; ++i){
          if(isblank(line[i])){
               word_end = true;
          }else if(ce_ispunct(line[i])){
               if(punctuation_word_boundaries) break;
               if(word_end) break;
          }else if(word_end){
               break;
          }
     }

     location->x = i;

     if(i >= line_len && location->y < (buffer->line_count - 1)){
          location->y++;
          location->x = 0;
          char first_char = buffer->lines[location->y][0];
          if(line[0] != 0 && first_char != 0){
               if(isblank(first_char)){
                    return ce_move_cursor_to_next_word(buffer, location, punctuation_word_boundaries);
               }
          }
     }

     return true;
}

bool ce_move_cursor_forward_to_char(const Buffer_t* buffer, Point_t* location, char c)
{
     Point_t next_location = {location->x + 1, location->y};

     if(!ce_point_on_buffer(buffer, next_location)) return false;

     char* line = buffer->lines[next_location.y];
     const char* found_char = strchr(line + next_location.x, c);
     if(!found_char) return false;

     location->x = (found_char - line);
     return true;
}

bool ce_move_cursor_backward_to_char(const Buffer_t* buffer, Point_t* location, char c)
{
     if(!ce_point_on_buffer(buffer, *location)) return false;

     char* line = buffer->lines[location->y];
     const char* found_char = ce_memrchr(line, c, location->x);
     if(!found_char) return false;

     location->x = (found_char - line);
     return true;
}

bool ce_get_char(const Buffer_t* buffer, Point_t location, char* c)
{
     if(!ce_point_on_buffer(buffer, location)) return false;

     *c = buffer->lines[location.y][location.x];

     if(*c == 0) *c = NEWLINE;

     return true;
}

char ce_get_char_raw(const Buffer_t* buffer, Point_t location)
{
     return buffer->lines[location.y][location.x];
}

static bool set_char_impl(Buffer_t* buffer, Point_t location, char c)
{
     if(!ce_point_on_buffer(buffer, location)) return false;

     if(c == NEWLINE){
          if(buffer->status == BS_READONLY){
               return ce_insert_string_readonly(buffer, location, "\n");
          }else{
               return ce_insert_string(buffer, location, "\n");
          }
     }

     buffer->lines[location.y][location.x] = c;
     mark_buffer_as_modified(buffer);
     return true;
}

bool ce_set_char(Buffer_t* buffer, Point_t location, char c)
{
     if(buffer->status == BS_READONLY) return false;

     return set_char_impl(buffer, location, c);
}

bool ce_set_char_readonly(Buffer_t* buffer, Point_t location, char c)
{
     if(buffer->status != BS_READONLY) return false;

     return set_char_impl(buffer, location, c);
}

static bool insert_line_impl(Buffer_t* buffer, int64_t line, const char* string)
{
     // make sure we are only inserting in the middle or at the very end, or the buffer is empty
     assert(buffer->line_count == 0 || line >= 0 && line <= buffer->line_count);
     int64_t string_line_count = 1;
     if(string) string_line_count = ce_count_string_lines(string);

     int64_t new_line_count = buffer->line_count + string_line_count;
     char** new_lines = realloc(buffer->lines, new_line_count * sizeof(char*));
     if(!new_lines){
          printf("%s() failed to malloc new lines: %"PRId64"\n", __FUNCTION__, new_line_count);
          return false;
     }

     if(buffer->line_count){
          memmove(new_lines + line + string_line_count, new_lines + line, (buffer->line_count - line) * sizeof(*new_lines));
     }

     if(string){
          const char* line_start = string;
          for(int i = 0; i < string_line_count; ++i){
               const char* line_end = strchr(line_start, NEWLINE);
               if(line_end){
                    int64_t current_len = line_end - line_start;
                    char current_line[current_len + 1];
                    strncpy(current_line, line_start, current_len);
                    current_line[current_len] = 0;
                    new_lines[line + i] = strdup(current_line);
                    line_start = line_end + 1;
               }else{
                    new_lines[line + i] = strdup(line_start);
               }
          }
     }else{
          new_lines[line] = strdup("");
     }

     buffer->lines = new_lines;
     buffer->line_count = new_line_count;
     mark_buffer_as_modified(buffer);

     return true;
}


// NOTE: passing NULL to string causes an empty line to be inserted
bool ce_insert_line(Buffer_t* buffer, int64_t line, const char* string)
{
     if(buffer->status == BS_READONLY) return false;

     return insert_line_impl(buffer, line, string);
}

bool ce_insert_line_readonly(Buffer_t* buffer, int64_t line, const char* string)
{
     if(buffer->status != BS_READONLY) return false;

     return insert_line_impl(buffer, line, string);
}

bool ce_append_line(Buffer_t* buffer, const char* string)
{
     return ce_insert_line(buffer, buffer->line_count, string);
}

bool ce_append_line_readonly(Buffer_t* buffer, const char* string)
{
     return ce_insert_line_readonly(buffer, buffer->line_count, string);
}

bool ce_insert_newline(Buffer_t* buffer, int64_t line)
{
     return ce_insert_line(buffer, line, NULL);
}

// appends line + 1 to line
bool ce_join_line(Buffer_t* buffer, int64_t line){
     if(line >= buffer->line_count || line < 0){
          ce_message("%s() specified line %"PRId64" ouside of buffer, which has %"PRId64" lines", __FUNCTION__, line, buffer->line_count);
          return false;
     }

     if(buffer->status == BS_READONLY) return false;

     if(line == buffer->line_count - 1) return true; // nothing to do
     char* l1 = buffer->lines[line];
     size_t l1_len = strlen(l1);
     char* l2 = buffer->lines[line+1];
     size_t l2_len = strlen(l2);
     buffer->lines[line] = realloc(l1, l1_len + l2_len + 1);
     if(!buffer->lines[line]) return false; // TODO: ENOMEM
     l1 = buffer->lines[line];
     memcpy(&l1[l1_len], l2, l2_len+1);
     mark_buffer_as_modified(buffer);
     return ce_remove_line(buffer, line+1);
}

bool ce_remove_line(Buffer_t* buffer, int64_t line)
{
     if(line >= buffer->line_count || line < 0){
          ce_message("%s() specified line %"PRId64" ouside of buffer, which has %"PRId64" lines", __FUNCTION__, line, buffer->line_count);
          return false;
     }

     if(buffer->status == BS_READONLY) return false;

     // free the old line
     free(buffer->lines[line]);

     int64_t new_line_count = buffer->line_count - 1;

     if(new_line_count){
          // move trailing lines up 1
          memmove(buffer->lines + line, buffer->lines + line + 1, (new_line_count - line) * sizeof(*buffer->lines));

          buffer->lines = realloc(buffer->lines, new_line_count * sizeof(*buffer->lines));
          if(!buffer->lines){
               ce_message("%s() failed to realloc new lines: %"PRId64"", __FUNCTION__, new_line_count);
               return false;
          }
     }

     buffer->line_count = new_line_count;
     mark_buffer_as_modified(buffer);
     return true;
}

static bool remove_string_impl(Buffer_t* buffer, Point_t location, int64_t length)
{
     if(buffer->status == BS_READONLY) return false;

     if(length == 0) return true;

     // TODO: should this return false and not do anything if we try to remove
     //       a string longer than the size of the rest of the buffer?

     if(!ce_point_on_buffer(buffer, location)) return false;

     int64_t current_line_len = strlen(buffer->lines[location.y]);
     int64_t rest_of_the_line_len = (current_line_len - location.x);

     // easy case: string is on a single line
     if(length <= rest_of_the_line_len){
          int64_t new_line_len = current_line_len - length;
          memmove(buffer->lines[location.y] + location.x,
                  buffer->lines[location.y] + location.x + length,
                  current_line_len - (location.x + length));

          // shrink the allocation now that we have fixed up the line
          buffer->lines[location.y] = realloc(buffer->lines[location.y], new_line_len + 1);
          if(!buffer->lines[location.y]){
               ce_message("%s() failed to realloc new line", __FUNCTION__);
               return false;
          }
          buffer->lines[location.y][new_line_len] = 0;

          mark_buffer_as_modified(buffer);
          return true;
     }

     // don't delete the rest of the first line yet, we'll do this when we mash the first and last lines
     length -= rest_of_the_line_len + 1; // account for newline
     buffer->lines[location.y][location.x] = '\0';
     if(location.x == 0 && length == 0){
          ce_remove_line(buffer, location.y);
          mark_buffer_as_modified(buffer);
          return true;
     }

     // hard case: string spans multiple lines
     int64_t delete_index = location.y + 1;

     while(length >= 0){
          assert(delete_index <= buffer->line_count);
          if(delete_index >= buffer->line_count) break;

          int64_t next_line_len = strlen(buffer->lines[delete_index]);
          if(length >= next_line_len + 1){
               // remove any lines that we have the length to remove completely
               ce_remove_line(buffer, delete_index);
               length -= next_line_len + 1;
          }else{ // we have to mash together our first and last line
               // NOTE: this is only run once
               // slurp up end of first line and beginning of last line
               int64_t next_line_part_len = next_line_len - length;
               int64_t new_line_len = location.x + next_line_part_len;
               buffer->lines[location.y] = realloc(buffer->lines[location.y], new_line_len + 1);
               if(!buffer->lines[location.y]){
                    ce_message("%s() failed to realloc new line", __FUNCTION__);
                    return false;
               }

               assert(buffer->lines[location.y+1][length+next_line_part_len] == '\0');
               memcpy(buffer->lines[location.y] + location.x,
                      buffer->lines[location.y+1] + length, next_line_part_len + 1);
               ce_remove_line(buffer, location.y+1);
               break;
          }
     }

     mark_buffer_as_modified(buffer);
     return true;
}

bool ce_remove_string(Buffer_t* buffer, Point_t location, int64_t length)
{
     if(buffer->status == BS_READONLY) return false;

     return remove_string_impl(buffer, location, length);
}

bool ce_remove_string_readonly(Buffer_t* buffer, Point_t location, int64_t length)
{
     if(buffer->status != BS_READONLY) return false;

     return remove_string_impl(buffer, location, length);
}

bool ce_save_buffer(Buffer_t* buffer, const char* filename)
{
     // save file loaded
     FILE* file = fopen(filename, "w");
     if(!file){
          // TODO: console output ? perror!
          ce_message("%s() failed to open '%s': %s", __FUNCTION__, filename, strerror(errno));
          return false;
     }

     for(int64_t i = 0; i < buffer->line_count; ++i){
          if(buffer->lines[i]){
               size_t len = strlen(buffer->lines[i]);
               fwrite(buffer->lines[i], len, 1, file);
          }
          fwrite("\n", 1, 1, file);
     }

     fclose(file);
     buffer->status = BS_NONE;
     return true;
}

static int64_t count_digits(int64_t n)
{
     if(n == 0) return 1;

     int count = 0;
     while(n > 0){
          n /= 10;
          count++;
     }

     return count;
}

static const char non_printable_repr = '~';

bool ce_draw_buffer(const Buffer_t* buffer, const Point_t* cursor, const Point_t* term_top_left,
                    const Point_t* term_bottom_right, const Point_t* buffer_top_left, const regex_t* highlight_regex,
                    LineNumberType_t line_number_type, HighlightLineType_t highlight_line_type)
{
     if(!g_terminal_dimensions){
          ce_message("%s() unknown terminal dimensions", __FUNCTION__);
          return false;
     }

     if(term_top_left->x > term_bottom_right->x){
          ce_message("%s() top_left's x (%"PRId64") must be lower than bottom_right's x(%"PRId64")", __FUNCTION__,
                     term_top_left->x, term_bottom_right->x);
          return false;
     }

     if(term_top_left->y > term_bottom_right->y){
          ce_message("%s() top_left's y (%"PRId64") must be lower than bottom_right's y(%"PRId64")", __FUNCTION__,
                     term_top_left->y, term_bottom_right->y);
          return false;
     }

     if(term_top_left->x < 0){
          ce_message("%s() top_left's x(%"PRId64") must be greater than 0", __FUNCTION__, term_top_left->x);
          return false;
     }

     if(term_top_left->y < 0){
          ce_message("%s() top_left's y(%"PRId64") must be greater than 0", __FUNCTION__, term_top_left->y);
          return false;
     }

     if(term_bottom_right->x >= g_terminal_dimensions->x){
          ce_message("%s() bottom_right's x(%"PRId64") must be less than the terminal dimensions x(%"PRId64")", __FUNCTION__,
                     term_bottom_right->x, g_terminal_dimensions->x);
          return false;
     }

     if(term_bottom_right->y >= g_terminal_dimensions->y){
          ce_message("%s() bottom_right's y(%"PRId64") must be less than the terminal dimensions y(%"PRId64")", __FUNCTION__,
                     term_bottom_right->y, g_terminal_dimensions->y);
          return false;
     }

     int64_t last_line = buffer_top_left->y + (term_bottom_right->y - term_top_left->y);
     int64_t last_line_in_view = last_line;
          if(last_line >= buffer->line_count) last_line = buffer->line_count - 1;

     if(buffer->line_count){
          int64_t max_width = (term_bottom_right->x - term_top_left->x) + 1;
          int64_t max_height = (term_bottom_right->y - term_top_left->y) + 1;

          standend();

          // figure out how wide the line number margin needs to be
          int line_number_size = 0;
          if(!buffer->absolutely_no_line_numbers_under_any_circumstances){
               line_number_size = ce_get_line_number_column_width(line_number_type, buffer->line_count, buffer_top_left->y, last_line);
               if(line_number_size){
                    max_width -= line_number_size;
                    line_number_size--;
               }
          }

          Point_t buffer_bottom_right = *buffer_top_left;
          buffer_bottom_right.x += max_width;
          buffer_bottom_right.y += max_height;

          SyntaxHighlighterData_t syntax_data;

          syntax_data.buffer = buffer;
          syntax_data.top_left = *buffer_top_left;
          syntax_data.bottom_right = buffer_bottom_right;
          syntax_data.cursor = *cursor;
          syntax_data.highlight_regex = highlight_regex;
          syntax_data.line_number_type = line_number_type;
          syntax_data.highlight_line_type = highlight_line_type;
          syntax_data.state = SS_INITIALIZING;
          syntax_data.loc = (Point_t){0, buffer_top_left->y};

          if(buffer->syntax_fn) buffer->syntax_fn(&syntax_data, buffer->syntax_user_data);

          for(int64_t i = buffer_top_left->y; i <= last_line; ++i) {
               move(term_top_left->y + (i - buffer_top_left->y), term_top_left->x);

               if(!buffer->absolutely_no_line_numbers_under_any_circumstances && line_number_type){
                    long value = i + 1;
                    if(line_number_type == LNT_RELATIVE || (line_number_type == LNT_RELATIVE_AND_ABSOLUTE && cursor->y != i)){
                         value = abs((int)(cursor->y - i));
                    }
                    printw("%*d ", line_number_size, value);
               }

               const char* buffer_line = buffer->lines[i];
               if(!buffer_line) continue;
               int64_t line_length = strlen(buffer_line);

               const char* line_to_print = buffer_line + buffer_top_left->x;
               int64_t print_line_length = strlen(line_to_print);
               int64_t min = max_width < print_line_length ? max_width : print_line_length;

               if(buffer->syntax_fn){
                    // call syntax function at the beginning of the line
                    syntax_data.loc = (Point_t){buffer_top_left->x, i};
                    syntax_data.state = SS_BEGINNING_OF_LINE;
                    buffer->syntax_fn(&syntax_data, buffer->syntax_user_data);

                    if(line_length >= buffer_top_left->x){
                         for(int64_t c = 0; c < min; ++c){
                              // call syntax function for each character
                              syntax_data.loc = (Point_t){buffer_top_left->x + c, i};
                              syntax_data.state = SS_CHARACTER;
                              buffer->syntax_fn(&syntax_data, buffer->syntax_user_data);

                              // print each character
                              if(isprint(line_to_print[c])){
                                   addch(line_to_print[c]);
                              }else{
                                   addch(non_printable_repr);
                              }
                         }
                    }

                    // call syntax function at the end of the line
                    syntax_data.loc = (Point_t){buffer_top_left->x + min, i};
                    syntax_data.state = SS_END_OF_LINE;
                    buffer->syntax_fn(&syntax_data, buffer->syntax_user_data);
               }else{
                    if(line_length >= buffer_top_left->x){
                         for(int64_t c = 0; c < min; ++c){
                              // print each character
                              if(isprint(line_to_print[c])){
                                   addch(line_to_print[c]);
                              }else{
                                   addch(non_printable_repr);
                              }
                         }
                    }
               }
          }
     }else{
          attron(COLOR_PAIR(S_LINE_NUMBERS));
     }

     if(!buffer->absolutely_no_line_numbers_under_any_circumstances){
          for(int64_t i = last_line + 1; i <= last_line_in_view; ++i) {
               move(term_top_left->y + (i - buffer_top_left->y), term_top_left->x);
               addch('~');
          }
     }

     return true;
}

BufferNode_t* ce_append_buffer_to_list(BufferNode_t** head, Buffer_t* buffer)
{
     BufferNode_t* itr = *head;

     if(itr){
          // find last element
          while(itr->next){
               itr = itr->next;
          }
     }

     BufferNode_t* new = malloc(sizeof(BufferNode_t));
     if(!new){
          ce_message("%s() failed to alloc new BufferNode_t for '%s'", __FUNCTION__, buffer->filename);
          return NULL;
     }

     if(itr){
          itr->next = new;
     }else{
          *head = new;
     }

     new->buffer = buffer;
     new->next = NULL;

     return new;
}

bool ce_remove_buffer_from_list(BufferNode_t** head, Buffer_t* buffer)
{
     BufferNode_t* itr = *head;
     BufferNode_t* prev = NULL;
     while(itr){
          if(itr->buffer == buffer){
               // patch up the previous node's next point
               if(prev) prev->next = itr->next;

               // advance head if are deleting it
               if(itr == *head) *head = itr->next;

               free(itr);
               return true;
          }

          prev = itr;
          itr = itr->next;
     }

     // didn't find the node to remove
     return false;
}

bool ce_move_cursor_to_end_of_line(const Buffer_t* buffer, Point_t* cursor)
{
     if(!ce_point_on_buffer(buffer, *cursor)) return false;

     cursor->x = strlen(buffer->lines[cursor->y])-1;
     if(cursor->x < 0) cursor->x = 0;
     return true;
}

bool ce_set_cursor(const Buffer_t* buffer, Point_t* cursor, Point_t location)
{
     assert(cursor->x >= 0);
     assert(cursor->y >= 0);

     if(!buffer->line_count){
          *cursor = (Point_t){0, 0};
          return false;
     }

     Point_t dst = location;

     if(dst.x < 0) dst.x = 0;
     if(dst.y < 0) dst.y = 0;

     if(dst.y >= buffer->line_count) dst.y = buffer->line_count - 1;

     if(buffer->lines && buffer->lines[dst.y]){
          int64_t line_len = strlen(buffer->lines[dst.y]);
          if(!line_len){
               dst.x = 0;
          }else if(dst.x >= line_len){
               dst.x = line_len - 1;
          }
     }else{
          dst.x = 0;
     }

     *cursor = dst;

     return true;
}

// modifies cursor and also returns a pointer to cursor for convience
Point_t* ce_clamp_cursor(const Buffer_t* buffer, Point_t* cursor){
     ce_move_cursor(buffer, cursor, (Point_t){0,0});
     return cursor;
}

bool ce_move_cursor(const Buffer_t* buffer, Point_t* cursor, Point_t delta)
{
     if(!buffer->line_count){
          *cursor = (Point_t){0, 0};
          return true;
     }

     Point_t dst = *cursor;
     dst.x += delta.x;
     dst.y += delta.y;

     if(dst.x < 0) dst.x = 0;
     if(dst.y < 0) dst.y = 0;

     if(dst.y >= buffer->line_count) dst.y = buffer->line_count - 1;

     if(buffer->lines && buffer->lines[dst.y]){
          int64_t line_len = strlen(buffer->lines[dst.y]);
          if(!line_len){
               dst.x = 0;
          }else if(dst.x >= line_len){
               dst.x = line_len - 1;
          }
     }else{
          dst.x = 0;
     }

     *cursor = dst;

     return true;
}

bool ce_advance_cursor(const Buffer_t* buffer, Point_t* cursor, int64_t delta)
{
     if(!ce_point_on_buffer(buffer, *cursor)){
          // NOTE: hack to allow cursor to be passed the end of the line
          if(cursor->y < 0 || cursor->y >= buffer->line_count){
               return false;
          }else if(cursor->x > (int64_t)(strlen(buffer->lines[cursor->y]))){
               return false;
          }
     }

     if(delta == 0) return true;

     Direction_t d = (delta > 0 ) ? CE_DOWN : CE_UP;
     delta *= d;

     int64_t line_len = (d == CE_DOWN) ? (strlen(buffer->lines[cursor->y]) + 1) : 0; // account for newline
     int64_t line_len_left = (d == CE_DOWN) ? line_len - cursor->x : cursor->x;

     // if the movement fits on this line, go for it
     if(delta <= line_len_left){
          cursor->x += delta * d;
          return true;
     }

     delta -= line_len_left;
     cursor->y += d;
     cursor->x = 0;
     if(cursor->y < buffer->line_count && d == CE_DOWN) cursor->x = strlen(buffer->lines[cursor->y]);

     while(true){
          if(d == CE_DOWN && cursor->y >= buffer->line_count) return ce_move_cursor_to_end_of_file(buffer, cursor);
          else if(cursor->y < 0) return ce_move_cursor_to_beginning_of_file(buffer, cursor);

          line_len = strlen(buffer->lines[cursor->y]) + 1; // account for newline

          if(delta < line_len){
               cursor->x = (d == CE_DOWN) ? delta : line_len - delta;
               break;
          }

          cursor->y += d;
          delta -= line_len;
     }

     return true;
}

bool ce_move_cursor_to_end_of_file(const Buffer_t* buffer, Point_t* cursor)
{
     if(!buffer->line_count) return false;

     int64_t last_line = buffer->line_count - 1;

     cursor->x = ce_last_index(buffer->lines[last_line]);
     cursor->y = last_line;

     return true;
}

bool ce_move_cursor_to_beginning_of_file(const Buffer_t* buffer, Point_t* cursor)
{
     if(!buffer->line_count) return false;

     *cursor = (Point_t) {0, 0};

     return true;
}

// TODO: Threshold for top, left, bottom and right before scrolling happens
bool ce_follow_cursor(Point_t cursor, int64_t* left_column, int64_t* top_row, int64_t view_width, int64_t view_height,
                      bool at_terminal_width_edge, bool at_terminal_height_edge, LineNumberType_t line_number_type,
                      int64_t line_count)
{
     assert(cursor.x >= 0);
     assert(cursor.y >= 0);

     if(!at_terminal_width_edge) view_width--;
     if(!at_terminal_height_edge) view_height--;

     int64_t bottom_row = *top_row + view_height;
     int64_t right_column = *left_column + view_width;

     if(cursor.y < *top_row){
          *top_row = cursor.y;
     }else if(cursor.y > bottom_row){
          bottom_row = cursor.y;
          *top_row = bottom_row - view_height;
     }

     // adjust based on line numbers
     int64_t line_number_adjustment = ce_get_line_number_column_width(line_number_type, line_count, *top_row, bottom_row);

     if(cursor.x < *left_column){
          *left_column = cursor.x;
     }else if(cursor.x > (right_column - line_number_adjustment)){
          right_column = cursor.x + line_number_adjustment;
          *left_column = right_column - view_width;
     }

     if(*top_row < 0) *top_row = 0;
     if(*left_column < 0) *left_column = 0;

     return true;
}

bool ce_commit_insert_char(BufferCommitNode_t** tail, Point_t start, Point_t undo_cursor, Point_t redo_cursor, char c, BufferCommitChain_t chain)
{
     BufferCommit_t change;
     change.type = BCT_INSERT_CHAR;
     change.start = start;
     change.undo_cursor = undo_cursor;
     change.redo_cursor = redo_cursor;
     change.c = c;
     change.chain = chain;

     return ce_commit_change(tail, &change);
}

bool ce_commit_insert_string(BufferCommitNode_t** tail, Point_t start, Point_t undo_cursor, Point_t redo_cursor,  char* string, BufferCommitChain_t chain)
{
     BufferCommit_t change;
     change.type = BCT_INSERT_STRING;
     change.start = start;
     change.undo_cursor = undo_cursor;
     change.redo_cursor = redo_cursor;
     change.str = string;
     change.chain = chain;

     return ce_commit_change(tail, &change);
}

bool ce_commit_remove_char(BufferCommitNode_t** tail, Point_t start, Point_t undo_cursor, Point_t redo_cursor, char c, BufferCommitChain_t chain)
{
     BufferCommit_t change;
     change.type = BCT_REMOVE_CHAR;
     change.start = start;
     change.undo_cursor = undo_cursor;
     change.redo_cursor = redo_cursor;
     change.c = c;
     change.chain = chain;

     return ce_commit_change(tail, &change);
}

bool ce_commit_remove_string(BufferCommitNode_t** tail, Point_t start, Point_t undo_cursor, Point_t redo_cursor,  char* string, BufferCommitChain_t chain)
{
     BufferCommit_t change;
     change.type = BCT_REMOVE_STRING;
     change.start = start;
     change.undo_cursor = undo_cursor;
     change.redo_cursor = redo_cursor;
     change.str = string;
     change.chain = chain;

     return ce_commit_change(tail, &change);
}

bool ce_commit_change_char(BufferCommitNode_t** tail, Point_t start, Point_t undo_cursor, Point_t redo_cursor, char c, char prev_c, BufferCommitChain_t chain)
{
     BufferCommit_t change;
     change.type = BCT_CHANGE_CHAR;
     change.start = start;
     change.undo_cursor = undo_cursor;
     change.redo_cursor = redo_cursor;
     change.c = c;
     change.prev_c = prev_c;
     change.chain = chain;

     return ce_commit_change(tail, &change);
}

bool ce_commit_change_string(BufferCommitNode_t** tail, Point_t start, Point_t undo_cursor, Point_t redo_cursor, char* new_string,  char* prev_string, BufferCommitChain_t chain)
{
     BufferCommit_t change;
     change.type = BCT_CHANGE_STRING;
     change.start = start;
     change.undo_cursor = undo_cursor;
     change.redo_cursor = redo_cursor;
     change.str = new_string;
     change.prev_str = prev_string;
     change.chain = chain;

     return ce_commit_change(tail, &change);
}

void free_commit(BufferCommitNode_t* node)
{
     if(node->commit.type == BCT_INSERT_STRING ||
        node->commit.type == BCT_REMOVE_STRING){
          free(node->commit.str);
     }else if(node->commit.type == BCT_CHANGE_STRING){
          free(node->commit.str);
          free(node->commit.prev_str);
     }

     free(node);
}

bool ce_commit_change(BufferCommitNode_t** tail, const BufferCommit_t* commit)
{
     BufferCommitNode_t* new_node = calloc(1, sizeof(*new_node));
     if(!new_node){
          ce_message("%s() failed to allocate new change", __FUNCTION__);
          return false;
     }

     new_node->commit = *commit;
     new_node->prev = *tail;

     // TODO: rather than branching, just free rest of the redo list on this change
     if(*tail){
          if((*tail)->next) ce_commits_free((*tail)->next);
          (*tail)->next = new_node;
     }

     *tail = new_node;
     return true;
}

bool ce_commits_free(BufferCommitNode_t* tail)
{
     while(tail){
          BufferCommitNode_t* tmp = tail;
          tail = tail->next;
          free_commit(tmp);
     }

     return true;
}

bool ce_commits_dump(BufferCommitNode_t* tail)
{
     const char* type_str [] = {
          "NONE",
          "INSERT CHAR",
          "INSERT STRING",
          "REMOVE CHAR",
          "REMOVE STRING",
          "CHANGE CHAR",
          "CHANGE STRING",
     };

     const char* chain_str [] = {
          "STOP",
          "KEEP GOING",
     };

     while(tail){
          ce_message("type: %s", type_str[tail->commit.type]);
          switch(tail->commit.type){
          default:
               break;
          case BCT_INSERT_CHAR:
               ce_message("    inserted char: '%c'", tail->commit.c);
               break;
          case BCT_REMOVE_CHAR:
               ce_message("     removed char: '%c'", tail->commit.c);
               break;
          case BCT_INSERT_STRING:
               ce_message("  inserted string: '%s'", tail->commit.str);
               break;
          case BCT_REMOVE_STRING:
               ce_message("   removed string: '%s'", tail->commit.str);
               break;
          case BCT_CHANGE_CHAR:
               ce_message("    inserted char: '%c'", tail->commit.c);
               ce_message("     removed char: '%c'", tail->commit.prev_c);
               break;
          case BCT_CHANGE_STRING:
               ce_message("    inserted char: '%s'", tail->commit.str);
               ce_message("     removed char: '%s'", tail->commit.prev_str);
               break;
          }

          ce_message("            start: %"PRId64", %"PRId64"", tail->commit.start.x, tail->commit.start.y);
          ce_message("      undo cursor: %"PRId64", %"PRId64"", tail->commit.undo_cursor.x, tail->commit.undo_cursor.y);
          ce_message("      redo cursor: %"PRId64", %"PRId64"", tail->commit.redo_cursor.x, tail->commit.redo_cursor.y);
          ce_message("            chain: %s", chain_str[tail->commit.chain]);

          tail = tail->prev;
     }

     return true;
}

bool ce_commit_undo(Buffer_t* buffer, BufferCommitNode_t** tail, Point_t* cursor)
{
     if(!*tail){
          ce_message("%s() empty undo history", __FUNCTION__);
          return false;
     }

     BufferCommit_t* commit;

     do{
          commit = &((*tail)->commit);

          switch(commit->type){
          default:
               ce_message("unsupported BufferCommitType_t: %d", commit->type);
               return false;
          case BCT_NONE:
               ce_message("%s() empty undo history", __FUNCTION__);
               return false;
          case BCT_INSERT_CHAR:
               ce_remove_char(buffer, commit->start);
               break;
          case BCT_INSERT_STRING:
               ce_remove_string(buffer, commit->start, strlen(commit->str));
               break;
          case BCT_REMOVE_CHAR:
               ce_insert_char(buffer, commit->start, commit->c);
               break;
          case BCT_REMOVE_STRING:
               ce_insert_string(buffer, commit->start, commit->str);
               break;
          case BCT_CHANGE_CHAR:
               ce_set_char(buffer, commit->start, commit->prev_c);
               break;
          case BCT_CHANGE_STRING:
               ce_remove_string(buffer, commit->start, strlen(commit->str));
               ce_insert_string(buffer, commit->start, commit->prev_str);
               break;
          }

          *cursor = *ce_clamp_cursor(buffer, &(*tail)->commit.undo_cursor);
          *tail = (*tail)->prev;
     }while(*tail && (*tail)->commit.chain == BCC_KEEP_GOING);

     return true;
}

bool ce_commit_redo(Buffer_t* buffer, BufferCommitNode_t** tail, Point_t* cursor)
{
     if(!*tail){
          ce_message("%s() empty redo history", __FUNCTION__);
          return false;
     }

     if(!(*tail)->next){
          ce_message("%s() empty redo history", __FUNCTION__);
          return false;
     }

     BufferCommit_t* commit = NULL;

     do{
          *tail = (*tail)->next;

          BufferCommitNode_t* undo_commit = *tail;
          commit = &undo_commit->commit;

          switch(commit->type){
          default:
               ce_message("unsupported BufferCommitType_t: %d", commit->type);
               return false;
          case BCT_INSERT_CHAR:
               ce_insert_char(buffer, commit->start, commit->c);
               break;
          case BCT_INSERT_STRING:
               ce_insert_string(buffer, commit->start, commit->str);
               break;
          case BCT_REMOVE_CHAR:
               ce_remove_char(buffer, commit->start);
               break;
          case BCT_REMOVE_STRING:
               ce_remove_string(buffer, commit->start, strlen(commit->str));
               break;
          case BCT_CHANGE_CHAR:
               ce_set_char(buffer, commit->start, commit->c);
               break;
          case BCT_CHANGE_STRING:
               ce_remove_string(buffer, commit->start, strlen(commit->prev_str));
               ce_insert_string(buffer, commit->start, commit->str);
               break;
          }

          *cursor = (*tail)->commit.redo_cursor;
     }while((*tail)->next && commit->chain == BCC_KEEP_GOING);

     return true;
}

BufferView_t* ce_split_view(BufferView_t* view, Buffer_t* buffer, bool horizontal)
{
     BufferView_t* itr = view;

     // loop to the end of the list
     if(horizontal){
          while(itr->next_horizontal){
               itr = itr->next_horizontal;
          }
     }else{
          while(itr->next_vertical){
               itr = itr->next_vertical;
          }
     }

     BufferView_t* new_view = calloc(1, sizeof(*new_view));
     if(!new_view){
          ce_message("%s() failed to allocate buffer view", __FUNCTION__);
          return NULL;
     }

     new_view->buffer = buffer;
     new_view->bottom_right = *g_terminal_dimensions;

     if(buffer) new_view->cursor = buffer->cursor;

     if(horizontal){
          itr->next_horizontal = new_view;
     }else{
          itr->next_vertical = new_view;
     }

     return new_view;
}

BufferView_t* find_connecting_view(BufferView_t* start, BufferView_t* match)
{
     if(start->next_horizontal){
          if(start->next_horizontal == match) return start;
          BufferView_t* result = find_connecting_view(start->next_horizontal, match);
          if(result) return result;
     }

     if(start->next_vertical){
          if(start->next_vertical == match) return start;
          BufferView_t* result = find_connecting_view(start->next_vertical, match);
          if(result) return result;
     }

     return NULL;
}

bool ce_remove_view(BufferView_t** head, BufferView_t* view)
{
     BufferView_t* itr = *head;

     if(view == *head){
          // patch up view pointers
          if(itr->next_horizontal && !itr->next_vertical){
               *head = itr->next_horizontal;
               free(itr);
          }else if(!itr->next_horizontal && itr->next_vertical){
               *head = itr->next_vertical;
               free(itr);
          }else if(itr->next_horizontal &&
                   itr->next_vertical){
               // we have to choose which becomes head, so we choose vertical
               *head = itr->next_vertical;
               BufferView_t* tmp = *head;
               // loop to the end of vertical's horizontal
               while(tmp->next_horizontal) tmp = tmp->next_horizontal;
               // tack on old head's next_horizontal to the end of new head's last horizontal
               tmp->next_horizontal = itr->next_horizontal;
               free(itr);
          }else{
               free(itr);
               *head = NULL;
          }
          return true;
     }

     itr = find_connecting_view(*head, view);
     if(!itr){
          ce_message("%s() failed to remove unconnected view", __FUNCTION__);
          return false;
     }

     if(itr->next_vertical == view){
          if(view->next_horizontal){
               // patch up view pointers
               itr->next_vertical = view->next_horizontal;
               // NOTE: This is totally not what we want going forward, however,
               //       until we implement a more complicated window system, this
               //       let's us not lose track of windows
               // bandage up the windows !
               if(view->next_vertical){
                    BufferView_t* tmp = view->next_horizontal;
                    // find the last vertical in new node
                    while(tmp->next_vertical) tmp = tmp->next_vertical;
                    // tack on the current view's next vertical to the end of the new node's verticals
                    tmp->next_vertical = view->next_vertical;
               }
          }else if(view->next_vertical){
               itr->next_vertical = view->next_vertical;
          }else{
               itr->next_vertical = NULL;
          }

          free(view);
     }else{
          assert(itr->next_horizontal == view);
          if(view->next_vertical){
               // patch up view pointers
               itr->next_horizontal = view->next_vertical;
               if(view->next_horizontal){
                    BufferView_t* itr = view->next_vertical;
                    // find the last vertical in new node
                    while(itr->next_horizontal) itr = itr->next_horizontal;
                    // tack on the current view's next vertical to the end of the new node's verticals
                    itr->next_horizontal = view->next_horizontal;
               }
          }else if(view->next_horizontal){
               itr->next_horizontal = view->next_horizontal;
          }else{
               itr->next_horizontal = NULL;
          }

          free(view);
     }

     return true;
}

bool ce_change_buffer_in_views(BufferView_t* head, Buffer_t* match, Buffer_t* new)
{
     if(head->next_horizontal) ce_change_buffer_in_views(head->next_horizontal, match, new);
     if(head->next_vertical) ce_change_buffer_in_views(head->next_vertical, match, new);

     if(head->buffer == match){
          head->buffer = new;
          head->cursor = (Point_t){0, 0};
          head->top_row = 0;
          head->left_column = 0;
     }

     free(head->user_data);
     head->user_data = NULL;
     return true;
}

// NOTE: recursive function for free-ing splits
bool free_buffer_views(BufferView_t* head)
{
     if(head->next_horizontal) free_buffer_views(head->next_horizontal);
     if(head->next_vertical) free_buffer_views(head->next_vertical);

     free(head->user_data);
     head->user_data = NULL;

     free(head);

     return true;
}

bool ce_free_views(BufferView_t** head)
{
     if(!free_buffer_views(*head)){
          return false;
     }

     *head = NULL;
     return true;
}

bool calc_vertical_views(BufferView_t* view, Point_t top_left, Point_t bottom_right, bool already_calculated);

bool calc_horizontal_views(BufferView_t* view, Point_t top_left, Point_t bottom_right, bool already_calculated)
{
     int64_t view_count = 0;
     BufferView_t* itr = view;
     while(itr){
          itr = itr->next_horizontal;
          view_count++;
     }

     int64_t shift = ((bottom_right.x - top_left.x) + 1) / view_count;
     Point_t new_top_left = top_left;
     Point_t new_bottom_right = bottom_right;
     new_bottom_right.x = new_top_left.x + (shift - 1);

     itr = view;
     while(itr){
          // if this is the first view and we haven't already calculated the dimensions for it
          // or if this is any view other than the first view
          // and we have a vertical view below us, then calculate the vertical views
          if(((!already_calculated && itr == view) || (itr != view)) && itr->next_vertical){
               if(!itr->next_horizontal) new_bottom_right.x = bottom_right.x;
               calc_vertical_views(itr, new_top_left, new_bottom_right, true);
          }else{
               itr->top_left = new_top_left;
               itr->bottom_right = new_bottom_right;
          }

          new_top_left.x += shift;

          if(itr->next_horizontal){
               new_bottom_right.x = new_top_left.x + (shift - 1);
          }else{
               itr->bottom_right.x = bottom_right.x;
          }

          itr = itr->next_horizontal;
     }

     return true;
}

bool calc_vertical_views(BufferView_t* view, Point_t top_left, Point_t bottom_right, bool already_calculated)
{
     int64_t view_count = 0;
     BufferView_t* itr = view;
     while(itr){
          itr = itr->next_vertical;
          view_count++;
     }

     int64_t shift = ((bottom_right.y - top_left.y) + 1) / view_count;
     Point_t new_top_left = top_left;
     Point_t new_bottom_right = bottom_right;
     new_bottom_right.y = new_top_left.y + (shift - 1);

     itr = view;
     while(itr){
          // if this is the first view and we haven't already calculated the dimensions for it
          // or if this is any view other than the first view
          // and we have a horizontal view below us, then calculate the horizontal views
          if(((!already_calculated && itr == view) || (itr != view)) && itr->next_horizontal){
               if(!itr->next_vertical) new_bottom_right.y = bottom_right.y;
               calc_horizontal_views(itr, new_top_left, new_bottom_right, true);
          }else{
               itr->top_left = new_top_left;
               itr->bottom_right = new_bottom_right;
          }

          new_top_left.y += shift;

          if(itr->next_vertical){
               new_bottom_right.y = new_top_left.y + (shift - 1);
          }else{
               itr->bottom_right.y = bottom_right.y;
          }

          itr = itr->next_vertical;
     }

     return true;
}

bool ce_calc_views(BufferView_t* view, Point_t top_left, Point_t bottom_right)
{
     return calc_horizontal_views(view, top_left, bottom_right, false);
}

void draw_view_bottom_right_borders(const BufferView_t* view)
{
     attron(COLOR_PAIR(S_BORDERS));

     // draw right border
     if(view->bottom_right.x < (g_terminal_dimensions->x - 1)){
          for(int64_t i = view->top_left.y; i < view->bottom_right.y; ++i){
               move(i, view->bottom_right.x);
               addch(ACS_VLINE);
          }
     }

     // draw bottom border
     move(view->bottom_right.y, view->top_left.x);
     for(int64_t i = view->top_left.x; i < view->bottom_right.x; ++i){
          move(view->bottom_right.y, i);
          addch(ACS_HLINE);
     }
}

bool draw_vertical_views(const BufferView_t* view, bool already_drawn, const regex_t* highlight_regex,
                         LineNumberType_t line_number_type, HighlightLineType_t highlight_line_type);

bool draw_horizontal_views(const BufferView_t* view, bool already_drawn, const regex_t* highlight_regex,
                           LineNumberType_t line_number_type, HighlightLineType_t highlight_line_type)
{
     const BufferView_t* itr = view;
     while(itr){
          // if this is the first view and we haven't already drawn it
          // or if this is any view other than the first view
          // and we have a horizontal view below us, then draw the horizontal views
          if(((!already_drawn && itr == view) || (itr != view)) && itr->next_vertical){
               draw_vertical_views(itr, true, highlight_regex, line_number_type, highlight_line_type);
          }else{
               assert(itr->left_column >= 0);
               assert(itr->top_row >= 0);
               Point_t buffer_top_left = {itr->left_column, itr->top_row};
               ce_draw_buffer(itr->buffer, &itr->cursor, &itr->top_left, &itr->bottom_right, &buffer_top_left,
                              highlight_regex, line_number_type, highlight_line_type);
               draw_view_bottom_right_borders(itr);
          }

          itr = itr->next_horizontal;
     }

     return true;
}

bool draw_vertical_views(const BufferView_t* view, bool already_drawn, const regex_t* highlight_regex,
                         LineNumberType_t line_number_type, HighlightLineType_t highlight_line_type)
{
     const BufferView_t* itr = view;
     while(itr){
          // if this is the first view and we haven't already drawn it
          // or if this is any view other than the first view
          // and we have a vertical view below us, then draw the vertical views
          if(((!already_drawn && itr == view) || (itr != view)) && itr->next_horizontal){
               draw_horizontal_views(itr, true, highlight_regex, line_number_type, highlight_line_type);
          }else{
               Point_t buffer_top_left = {itr->left_column, itr->top_row};
               ce_draw_buffer(itr->buffer, &itr->cursor, &itr->top_left, &itr->bottom_right, &buffer_top_left,
                              highlight_regex, line_number_type, highlight_line_type);
               draw_view_bottom_right_borders(itr);
          }

          itr = itr->next_vertical;
     }

     return true;
}

bool ce_connect_border_lines(Point_t location)
{
     // connect the bottom and right borders based on ruleset
     chtype left = mvinch(location.y, location.x - 1);
     chtype right = mvinch(location.y, location.x + 1);
     chtype top = mvinch(location.y - 1, location.x);
     chtype bottom = mvinch(location.y + 1, location.x);

     // strip out the color info from each adjacent chtype
     left &= ~A_COLOR;
     right &= ~A_COLOR;
     top &= ~A_COLOR;
     bottom &= ~A_COLOR;

     if(left == ACS_HLINE && right == ACS_HLINE && top == ACS_VLINE){
          move(location.y, location.x);
          addch(ACS_BTEE);
     }

     if(left == ACS_HLINE && right == ACS_HLINE && bottom == ACS_VLINE){
          move(location.y, location.x);
          addch(ACS_TTEE);
     }

     if(top == ACS_VLINE && bottom == ACS_VLINE && left == ACS_HLINE){
          move(location.y, location.x);
          addch(ACS_RTEE);
     }

     if(top == ACS_VLINE && bottom == ACS_VLINE && right == ACS_HLINE){
          move(location.y, location.x);
          addch(ACS_LTEE);
     }

     if(top == ACS_VLINE && bottom == ACS_VLINE && right == ACS_HLINE && left == ACS_HLINE){
          move(location.y, location.x);
          addch(ACS_PLUS);
     }

     return true;
}

bool connect_borders(const BufferView_t* view)
{
     if(view->next_horizontal) connect_borders(view->next_horizontal);
     if(view->next_vertical) connect_borders(view->next_vertical);

     Point_t top_left = {view->top_left.x - 1, view->top_left.y - 1};
     Point_t top_right = {view->bottom_right.x, view->top_left.y - 1};
     Point_t bottom_right = {view->bottom_right.x, view->bottom_right.y};
     Point_t bottom_left = {view->top_left.x, view->bottom_right.y};

     return ce_connect_border_lines(top_left) && ce_connect_border_lines(top_right) &&
            ce_connect_border_lines(bottom_right) && ce_connect_border_lines(bottom_left);
}

bool ce_draw_views(const BufferView_t* view, const regex_t* highlight_regex, LineNumberType_t line_number_type,
                   HighlightLineType_t highlight_line_type)
{
     if(!draw_horizontal_views(view, false, highlight_regex, line_number_type, highlight_line_type)){
          return false;
     }

     attron(COLOR_PAIR(S_BORDERS));
     return connect_borders(view);
}

BufferView_t* find_view_at_point(BufferView_t* view, Point_t point)
{
     if(point.x >= view->top_left.x && point.x <= view->bottom_right.x &&
        point.y >= view->top_left.y && point.y <= view->bottom_right.y){
          return view;
     }

     BufferView_t* result = NULL;

     if(view->next_horizontal){
          result = find_view_at_point(view->next_horizontal, point);
     }

     if(!result && view->next_vertical){
          result = find_view_at_point(view->next_vertical, point);
     }

     return result;
}

BufferView_t* ce_find_view_at_point(BufferView_t* head, Point_t point)
{
     return find_view_at_point(head, point);
}

BufferView_t* buffer_in_view(BufferView_t* view, const Buffer_t* buffer)
{
     if(view->buffer == buffer){
          return view;
     }

     BufferView_t* result = NULL;

     if(view->next_horizontal){
          result = buffer_in_view(view->next_horizontal, buffer);
     }

     if(!result && view->next_vertical){
          result = buffer_in_view(view->next_vertical, buffer);
     }

     return result;
}

BufferView_t* ce_buffer_in_view(BufferView_t* head, const Buffer_t* buffer)
{
     return buffer_in_view(head, buffer);
}

int64_t ce_get_line_number_column_width(LineNumberType_t line_number_type, int64_t buffer_line_count, int64_t buffer_view_top, int64_t buffer_view_bottom)
{
     if(buffer_line_count == 0) return 0;

     int64_t column_width = 0;

     if(line_number_type == LNT_ABSOLUTE || line_number_type == LNT_RELATIVE_AND_ABSOLUTE){
          column_width += count_digits(buffer_line_count) + 1;
     }else if(line_number_type == LNT_RELATIVE){
          int64_t view_height = (buffer_view_bottom - buffer_view_top) + 1;
          if(view_height > buffer_line_count){
               column_width += count_digits(buffer_line_count - 1) + 1;
          }else{
               column_width += count_digits(view_height - 1) + 1;
          }
     }

     return column_width;
}

void* ce_memrchr(const void* s, int c, size_t n)
{
     char* rev_search = (void*)s + (n-1);
     while((uintptr_t)rev_search >= (uintptr_t)s){
          if(*rev_search == c) return rev_search;
	  rev_search--;
     }
     return NULL;
}

int64_t ce_compute_length(const Buffer_t* buffer, Point_t start, Point_t end)
{
     assert(ce_point_on_buffer(buffer, start));
     assert(ce_point_on_buffer(buffer, end));

     const Point_t* sorted_start = &start;
     const Point_t* sorted_end = &end;

     ce_sort_points(&sorted_start, &sorted_end);

     size_t length = 0;

     if(sorted_start->y < sorted_end->y){
          length = strlen(buffer->lines[sorted_start->y] + sorted_start->x) + 1; // account for newline
          for(int64_t i = sorted_start->y + 1; i < sorted_end->y; ++i){
               length += strlen(buffer->lines[i]) + 1; // account for newline
          }
          length += sorted_end->x+1; // do not account for newline. end is inclusive
     }else{
          assert(sorted_start->y == sorted_end->y);
          length += sorted_end->x+1 - sorted_start->x;
     }

     return length;
}

int ce_iswordchar(int c)
{
     return !isblank(c) && !ce_ispunct(c);
}

// given a buffer, two points, and a function ptr, return a range of characters that match defined criteria
// NOTE: start is inclusive, end is inclusive
bool ce_get_homogenous_adjacents(const Buffer_t* buffer, Point_t* start, Point_t* end, int (*is_homogenous)(int))
{
     assert(memcmp(start, end, sizeof *start) == 0);

     char curr_char;
     if(!ce_get_char(buffer, *start, &curr_char)) return false;

     do{
          start->x--;
          if(!ce_get_char(buffer, *start, &curr_char)) break;
     }while((*is_homogenous)(curr_char));

     start->x++; // the last character wasn't homogenous

     do{
          end->x++;
          if(!ce_get_char(buffer, *end, &curr_char)) break;
     }while((*is_homogenous)(curr_char));

     end->x--; // the last character wasn't homogenous

     return true;
}

// word_start is inclusive, word_end is exclusive
bool ce_get_word_at_location(const Buffer_t* buffer, Point_t location, Point_t* word_start, Point_t* word_end)
{
     *word_start = location;
     *word_end = location;
     char curr_char;
     bool success = ce_get_char(buffer, *word_start, &curr_char);
     if(!success) return false;

     if(ce_ispunct(curr_char)){
          success = ce_get_homogenous_adjacents(buffer, word_start, word_end, ce_ispunct);
          if(!success) return false;
     }else if(isblank(curr_char)){
          success = ce_get_homogenous_adjacents(buffer, word_start, word_end, isblank);
          if(!success) return false;
     }else{
          assert(ce_iswordchar(curr_char));
          success = ce_get_homogenous_adjacents(buffer, word_start, word_end, ce_iswordchar);
          if(!success) return false;
     }

     // if the end is passed the end of the line, don't include it
     if(word_end->x > ce_last_index(buffer->lines[word_end->y])) word_end->x--;

     return true;
}

int64_t ce_get_indentation_for_line(const Buffer_t* buffer, Point_t location, int64_t tab_len)
{
     if(location.y >= buffer->line_count) return 0;

     switch(buffer->type){
     default:
          break;
     case BFT_PYTHON:
          for(int64_t y = location.y; y >= 0; --y){
               const char* itr = buffer->lines[y];

               // find previous line that isn't blank
               bool blank = true;

               while(*itr){
                    if(!isblank(*itr)){
                         blank = false;
                         break;
                    }

                    itr++;
               }

               if(blank) continue;

               // use it as indentation unless it ends in a ':'
               int indentation = itr - buffer->lines[y];

               while(*itr) itr++;
               itr--;

               if(*itr == ':') indentation += 5;
               return indentation;
          }

          break;
     case BFT_C:
     case BFT_CPP:
     case BFT_JAVA:
     case BFT_CONFIG:
     {
          // first, match this line's indentation
          char curr;

          // then, check the line for a '{' that is unmatched on location's line + indent if you find one
          for(int64_t y = location.y; y >= 0; --y){
               int64_t start_x = last_index_before_comment(buffer, y);
               if(y == location.y && start_x > location.x) start_x = location.x - 1;

               for(int64_t x = start_x; x >= 0; x--){
                    Point_t iter = {x, y};
                    ce_get_char(buffer, iter, &curr);

                    switch(curr){
                    default:
                         break;
                    case '"':
                         if(!find_matching_string_backward(buffer, &iter, '"')){
                              return false;
                         }
                         x = iter.x;
                         y = iter.y;
                         break;
                    case '\'':
                         if(!find_matching_string_backward(buffer, &iter, '\'')){
                              return false;
                         }
                         x = iter.x;
                         y = iter.y;
                         break;
                    case '{':
                    {
                         Point_t match = iter;
                         bool matched = ce_move_cursor_to_matching_pair(buffer, &match, '{');

                         if(ce_point_after(match, location) || ce_points_equal(match, location) || !matched){
                              // '{' is globally unmatched, or unmatched on our line
                              Point_t bol = {0, y};
                              ce_move_cursor_to_soft_beginning_of_line(buffer, &bol);
                              Point_t start = bol;
                              Point_t end = bol;
                              ce_move_cursor_to_matching_pair(buffer, &start, ')');
                              ce_move_cursor_to_matching_pair(buffer, &end, '(');

                              if((start.y == bol.y || end.y == bol.y) && start.y != end.y && start.x > 0){
                                   start.x = 0;
                                   return ce_get_indentation_for_line(buffer, start, tab_len) + tab_len;
                              }else{
                                   return bol.x + tab_len; // if a line has "{{", we don't want to double tab the next line!
                              }
                         }
                    } break;
                    case '(':
                    {
                         Point_t match = iter;
                         bool matched = ce_move_cursor_to_matching_pair(buffer, &match, '(');

                         if(ce_point_after(match, location) || ce_points_equal(match, location) || !matched){
                              return iter.x + 1; // if a line has "{{", we don't want to double tab the next line!
                         }
                    } break;
                    }
               }
          }

          break;
     }
     }

     return 0;
}

// return a > b
bool ce_point_after(Point_t a, Point_t b)
{
     return b.y < a.y || (b.y == a.y && b.x < a.x);
}

// if a > b, swap a and b
void ce_sort_points(const Point_t** a, const Point_t** b)
{
     if(ce_point_after(**a, **b)){
          const Point_t* temp = *a;
          *a = *b;
          *b = temp;
     }
}

bool ce_point_in_range(Point_t p, Point_t start, Point_t end)
{
    if( ((p.y == start.y && p.x >= start.x) || (p.y > start.y)) &&
        ((p.y == end.y && p.x <= end.x) || (p.y < end.y )) ){
         return true;
    }

     return false;
}

bool ce_points_equal(Point_t a, Point_t b)
{
     return b.x == a.x && b.y == a.y;
}

int64_t ce_last_index(const char* string)
{
     int64_t len = strlen(string);
     if(len) len--;

     return len;
}

KeyNode_t* ce_keys_push(KeyNode_t** head, int key)
{
     KeyNode_t* new_node = malloc(sizeof(*new_node));
     if(!new_node){
          ce_message("%s() failed to malloc node", __FUNCTION__);
          return NULL;
     }

     KeyNode_t* tail = *head;

     if(tail){
          while(tail->next){
               tail = tail->next;
          }
     }

     new_node->key = key;
     new_node->next = NULL;

     if(tail){
          tail->next = new_node;
     }else{
          *head = new_node;
     }

     return new_node;
}

// string is allocated and returned, it is the user's responsibility to free it
int* ce_keys_get_string(KeyNode_t* head)
{
     int64_t len = 0;
     KeyNode_t* itr = head;
     while(itr){
          len++;
          itr = itr->next;
     }

     int* str = malloc((len + 1) * sizeof(*str));
     if(!str){
          ce_message("%s() failed to alloc string", __FUNCTION__);
          return NULL;
     }

     int64_t s = 0;
     itr = head;
     while(itr){
          str[s] = itr->key;
          s++;
          itr = itr->next;
     }

     str[len] = 0;
     return str;
}

void ce_keys_free(KeyNode_t** head)
{
     while(*head){
          KeyNode_t* tmp = *head;
          *head = (*head)->next;
          free(tmp);
     }
}
