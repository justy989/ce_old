#include "ce.h"
#include <ctype.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <unistd.h>

Point* g_terminal_dimensions = NULL;

Direction ce_reverse_direction(Direction to_reverse){
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

bool ce_alloc_lines(Buffer_t* buffer, int64_t line_count)
{
     CE_CHECK_PTR_ARG(buffer);

     if(buffer->readonly) return false;

     if(buffer->lines) ce_clear_lines(buffer);

     if(line_count <= 0){
          ce_message("%s() tried to allocate %"PRId64" lines for a buffer, but we can only allocated > 0 lines", __FUNCTION__, line_count);
          return false;
     }

     // NOTE: if we have lines, we should free them here!
     buffer->lines = malloc(line_count * sizeof(char*));
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

     buffer->modified = true;
     return true;
}

bool ce_load_file(Buffer_t* buffer, const char* filename)
{
     ce_message("load file '%s'", filename);

     if(buffer->lines) ce_free_buffer(buffer);

     // read the entire file
     size_t content_size;
     char* contents = NULL;
     {
          FILE* file = fopen(filename, "rb");
          if(!file){
               ce_message("%s() fopen('%s', 'rb') failed: %s", __FUNCTION__, filename, strerror(errno));
               return false;
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
          buffer->readonly = true;
     }

     free(contents);
     buffer->modified = false;
     return true;
}

bool ce_load_string(Buffer_t* buffer, const char* str)
{
     Point start = {0, 0};
     return ce_insert_string(buffer, &start, str);
}

void ce_free_buffer(Buffer_t* buffer)
{
     if(!buffer){
          return;
     }

     free(buffer->filename);

     buffer->modified = false;
     buffer->readonly = false;

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
     buffer->modified = true;
}

void ce_clear_lines(Buffer_t* buffer)
{
     if(buffer->readonly) return;

     clear_lines_impl(buffer);
}

void ce_clear_lines_readonly(Buffer_t* buffer)
{
     if(!buffer->readonly) return;

     clear_lines_impl(buffer);
}

bool ce_point_on_buffer(const Buffer_t* buffer, const Point* location)
{
     if(location->y < 0 || location->x < 0){
          return false;
     }

     if(location->y >= buffer->line_count){
          return false;
     }

     char* line = buffer->lines[location->y];
     int64_t line_len = 0;

     if(line) line_len = strlen(line);

     if(location->x > line_len){
          return false;
     }

     return true;
}

bool insert_line_impl(Buffer_t* buffer, int64_t line, const char* string);

bool insert_char_impl(Buffer_t* buffer, const Point* location, char c)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);

     if(buffer->line_count == 0 && location->x == 0 && location->y == 0) ce_alloc_lines(buffer, 1);

     if(!ce_point_on_buffer(buffer, location)){
          return false;
     }

     char* line = buffer->lines[location->y];

     if(c == NEWLINE){
          // copy the rest of the line to the next line
          if(!insert_line_impl(buffer, location->y + 1, line + location->x)){
               return false;
          }

          // kill the rest of the current line
          char* new_line = realloc(line, location->x + 1);
          if(!new_line){
               ce_message("%s() failed to alloc new line after split", __FUNCTION__);
               return false;
          }
          new_line[location->x] = 0;
          buffer->lines[location->y] = new_line;
          return true;
     }

     char* new_line = NULL;

     // allocate new line with length + 1 + NULL terminator
     int64_t line_len = strlen(line);
     int64_t new_len = line_len + 2;
     new_line = realloc(line, new_len);
     if(!new_line){
          ce_message("%s() failed to allocate line with %"PRId64" characters", __FUNCTION__, new_len);
          return false;
     }

     // copy before the insert, add the new char, copy after the insert
     memmove(new_line + location->x + 1, new_line + location->x, line_len - location->x);
     new_line[location->x] = c;

     // NULL terminate the newline, and free the old line
     new_line[new_len - 1] = 0;
     buffer->lines[location->y] = new_line;
     buffer->modified = true;
     return true;
}

bool ce_insert_char(Buffer_t* buffer, const Point* location, char c)
{
     if(buffer->readonly) return false;

     return insert_char_impl(buffer, location, c);
}

bool ce_insert_char_readonly(Buffer_t* buffer, const Point* location, char c)
{
     if(!buffer->readonly) return false;

     return insert_char_impl(buffer, location, c);
}

bool ce_append_char(Buffer_t* buffer, char c)
{
     Point end = {0, 0};
     if(buffer->line_count){
          end.y = buffer->line_count - 1;
          end.x = strlen(buffer->lines[end.y]);
     }

     return ce_insert_char(buffer, &end, c);
}

bool ce_append_char_readonly(Buffer_t* buffer, char c)
{
     Point end = {0, 0};
     if(buffer->line_count){
          end.y = buffer->line_count - 1;
          end.x = strlen(buffer->lines[end.y]);
     }

     return ce_insert_char_readonly(buffer, &end, c);
}

bool insert_string_impl(Buffer_t* buffer, const Point* location, const char* new_string)
{
     if(location->x != 0 && location->y != 0){
          if(!ce_point_on_buffer(buffer, location)){
               return false;
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

          buffer->modified = true;
          return true;
     }

     char* current_line = buffer->lines[location->y];
     const char* first_part = current_line;
     const char* second_part = current_line + location->x;

     int64_t first_length = location->x;
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

          memmove(new_line + first_length + new_string_length, new_line + location->x, second_length);
          memcpy(new_line + first_length, new_string, new_string_length);
          new_line[new_line_length] = 0;
          buffer->lines[location->y] = new_line;
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

          buffer->lines[location->y] = new_line;

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
                    ce_insert_line(buffer, location->y + lines_added, new_line);
                    free(new_line);
               }else if(itr == end_of_line){
                    ce_insert_line(buffer, location->y + lines_added, NULL);
               }else{
                    new_line_length = end_of_line - itr;
                    new_line = malloc(new_line_length + 1);
                    strncpy(new_line, itr, new_line_length);
                    new_line[new_line_length] = 0;
                    ce_insert_line(buffer, location->y + lines_added, new_line);
                    free(new_line);
               }

               lines_added++;
          }
          free(current_line);
     }

     buffer->modified = true;
     return true;
}

bool ce_insert_string(Buffer_t* buffer, const Point* location, const char* new_string)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);
     CE_CHECK_PTR_ARG(new_string);

     if(buffer->readonly) return false;

     return insert_string_impl(buffer, location, new_string);
}

bool ce_insert_string_readonly(Buffer_t* buffer, const Point* location, const char* new_string)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);
     CE_CHECK_PTR_ARG(new_string);

     if(!buffer->readonly) return false;

     return insert_string_impl(buffer, location, new_string);
}

bool ce_prepend_string(Buffer_t* buffer, int64_t line, const char* new_string)
{
     Point beginning_of_line = {0, line};
     return ce_insert_string(buffer, &beginning_of_line, new_string);
}

bool ce_append_string(Buffer_t* buffer, int64_t line, const char* new_string)
{
     Point end_of_line = {0, line};
     if(buffer->line_count > line) end_of_line.x = strlen(buffer->lines[line]);
     return ce_insert_string(buffer, &end_of_line, new_string);
}

bool ce_append_string_readonly(Buffer_t* buffer, int64_t line, const char* new_string)
{
     Point end_of_line = {0, line};
     if(buffer->line_count > line) end_of_line.x = strlen(buffer->lines[line]);
     return ce_insert_string_readonly(buffer, &end_of_line, new_string);
}

bool ce_remove_char(Buffer_t* buffer, const Point* location)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);

     if(buffer->readonly) return false;

     if(!ce_point_on_buffer(buffer, location)) return false;

     char* line = buffer->lines[location->y];
     int64_t line_len = strlen(line);

     // remove the line from the list if it is empty
     if(line_len == 0){
          return ce_remove_line(buffer, location->y);
     }

     if(location->x == line_len){
          // removing the newline at the end of the line
          return ce_join_line(buffer, location->y);
     }

     // NOTE: mallocing a new line because I'm thinking about overall memory usage here
     //       if you trim the a ton of lines, then you would be using a lot more memory then the file requires
     char* new_line = malloc(line_len);

     // copy before the removed char copy after the removed char
     for(int64_t i = 0; i <= location->x; ++i){new_line[i] = line[i];}
     for(int64_t i = location->x + 1; i < line_len; ++i){new_line[i-1] = line[i];}
     new_line[line_len-1] = 0;

     free(line);
     buffer->lines[location->y] = new_line;

     buffer->modified = true;
     return true;
}

char* ce_dupe_string(const Buffer_t* buffer, const Point* start, const Point* end)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(start);
     CE_CHECK_PTR_ARG(end);

     int64_t total_len = ce_compute_length(buffer, start, end);
     assert(total_len);

     if(start->y == end->y){
          // single line allocation
          char* new_str = malloc(total_len + 1);
          if(!new_str){
               ce_message("%s() failed to alloc string", __FUNCTION__);
               return NULL;
          }
          memcpy(new_str, buffer->lines[start->y] + start->x, total_len);
          if(start->x != start->y && !new_str[total_len-1]) new_str[total_len-1] = '\n';
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
     int64_t len = strlen(buffer->lines[start->y] + start->x);
     if(len) memcpy(itr, buffer->lines[start->y] + start->x, len);
     itr[len] = '\n'; // add newline
     itr += len + 1;

     for(int64_t i = start->y + 1; i < end->y; ++i){
          len = strlen(buffer->lines[i]);
          memcpy(itr, buffer->lines[i], len);
          itr[len] = '\n';
          itr += len + 1;
     }

     memcpy(itr, buffer->lines[end->y], end->x+1);
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
     Point start = {0, 0};
     Point end = {0, 0};
     ce_move_cursor_to_end_of_file(buffer, &end);
     return ce_dupe_string(buffer, &start, &end);
}

// return x delta between location and the located character 'c' if found. return -1 if not found
int64_t ce_find_delta_to_char_forward_in_line(const Buffer_t* buffer, const Point* location, char c)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);

     const char* cur_char = &buffer->lines[location->y][location->x];
     if(cur_char == '\0') return -1; // we are at the end of the line
     const char* search_str = cur_char + 1; // start looking forward from the next character
     const char* found_char = strchr(search_str, c);
     if(!found_char) return -1;
     return found_char - cur_char;
}

// return -x delta between location and the located character 'c' if found. return -1 if not found
int64_t ce_find_delta_to_char_backward_in_line(const Buffer_t* buffer, const Point* location, char c)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);

     // TODO do I need to validate that the provided location is a real character in the buffer?
     const char* cur_char = &buffer->lines[location->y][location->x];
     const char* line = buffer->lines[location->y];
     const char* found_char = ce_memrchr(line, c, cur_char - line);
     if(!found_char) return -1;
     return cur_char - found_char;
}

// returns the delta to the matching character; return success
bool ce_find_delta_to_match(const Buffer_t* buffer, const Point* location, Point* delta)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);
     CE_CHECK_PTR_ARG(delta);

     char matchee, match;
     if(!ce_get_char(buffer, location, &matchee)) return false;

     Direction d;
     switch(matchee){
     case '{':
          d = CE_DOWN;
          match = '}';
          break;
     case '}':
          d = CE_UP;
          match = '{';
          break;
     case '(':
          d = CE_DOWN;
          match = ')';
          break;
     case ')':
          d = CE_UP;
          match = '(';
          break;
     case '[':
          d = CE_DOWN;
          match = ']';
          break;
     case ']':
          d = CE_UP;
          match = '[';
          break;
     case '<':
          d = CE_DOWN;
          match = '>';
          break;
     case '>':
          d = CE_UP;
          match = '<';
          break;
     default:
          return false;
     }

     uint64_t n_unmatched = 0; // when n_unmatched decrements back to 0, we have found our match

     char curr;
     for(Point iter = *location;
         d == CE_UP? iter.y >= 0 : iter.y < buffer->line_count;
         iter.y += d ){

          // first iteration we want iter.x to be on our matchee, other iterations we want it at eol/bol
          if(iter.y != location->y) iter.x = (d == CE_UP) ? ce_last_index(buffer->lines[iter.y]) : 0;

          // loop over buffer
          for(; ce_point_on_buffer(buffer, &iter); iter.x +=d){
               ce_get_char(buffer, &iter, &curr);

               // loop over line
               if(curr == match){
                    if(--n_unmatched == 0){
                         delta->x = iter.x - location->x;
                         delta->y = iter.y - location->y;
                         return true;
                    }
               }else if(curr == matchee){
                    n_unmatched++;
               }
          }
     }
     return false;
}

bool ce_find_match(const Buffer_t* buffer, const Point* location, Point* match)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);
     CE_CHECK_PTR_ARG(match);

     Point delta = {0, 0};
     if(!ce_find_delta_to_match(buffer, location, &delta)) return false;

     *match = *location;
     ce_move_cursor(buffer, match, delta);

     return true;
}

// returns Point at the next matching string; return success
bool ce_find_string(const Buffer_t* buffer, const Point* location, const char* search_str, Point* match, Direction direction)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);
     CE_CHECK_PTR_ARG(search_str);
     CE_CHECK_PTR_ARG(match);

     Point search_loc = *location;
     ce_advance_cursor(buffer, &search_loc, 1);
     char* line_str = &buffer->lines[search_loc.y][search_loc.x];

     int64_t n_lines = (direction == CE_UP) ? search_loc.y : buffer->line_count - search_loc.y;
     for(int64_t i = 0; i < n_lines;){
          const char* match_str = strstr(line_str, search_str);
          if(match_str){
               int64_t line = search_loc.y + i*direction;
               match->x = match_str - buffer->lines[line];
               match->y = line;
               return true;
          }
          i++;
          line_str = buffer->lines[search_loc.y + i*direction];
     }
     return false;
}

void ce_move_cursor_to_beginning_of_line(const Buffer_t* buffer __attribute__((unused)), Point* cursor)
{
     assert(ce_point_on_buffer(buffer, cursor));
     cursor->x = 0;
}

bool ce_move_cursor_to_soft_beginning_of_line(const Buffer_t* buffer, Point* cursor)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(cursor);

     if(!ce_point_on_buffer(buffer, cursor)) return false;
     int64_t delta_x_sbol = ce_find_delta_to_soft_beginning_of_line(buffer, cursor);

     cursor->x += delta_x_sbol;
     return true;
}

bool ce_move_cursor_to_soft_end_of_line(const Buffer_t* buffer, Point* cursor)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(cursor);

     if(!ce_point_on_buffer(buffer, cursor)) return false;
     int64_t delta_x_seol = ce_find_delta_to_soft_end_of_line(buffer, cursor);

     cursor->x += delta_x_seol;
     return true;
}

// underscores are not treated as punctuation for vim movement
int ce_ispunct(int c)
{
     return c != '_' && ispunct(c);
}

// delta to move to the soft beginning of line (sbol)
// if there is not a sbol, returns eol
int64_t ce_find_delta_to_soft_beginning_of_line(const Buffer_t* buffer, const Point* cursor)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(cursor);

     const char* line = buffer->lines[cursor->y];
     int64_t line_len = strlen(line);
     int i;
     for(i = 0; i < line_len && isblank(line[i]); i++);
     return i - cursor->x;
}

// delta to move to the soft end of line (seol)
// if there is not a seol, returns bol
int64_t ce_find_delta_to_soft_end_of_line(const Buffer_t* buffer, const Point* cursor)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(cursor);

     const char* line = buffer->lines[cursor->y];
     int64_t i = CE_MAX((int64_t) strlen(line) - 1, 0);
     while(i>0 && isblank(line[i])) i--;
     return i - cursor->x;
}

bool ce_move_cursor_to_beginning_of_word(const Buffer_t* buffer, Point* cursor, bool punctuation_word_boundaries)
{
     /* TODO: do we really care about CE_CHECK_PTR_ARG? */
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(cursor);

     assert(ce_point_on_buffer(buffer, cursor));

     const char* line = buffer->lines[cursor->y];
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
     return true;
}

// return -1 on failure, delta to move right to the end of the word on success
int64_t ce_find_delta_to_end_of_word(const Buffer_t* buffer, const Point* location, bool punctuation_word_boundaries)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);

     if(!ce_point_on_buffer(buffer, location)) return -1;
     const char* line = buffer->lines[location->y];
     int line_len = strlen(line);
     int i = location->x;
     if(i == line_len) return 0;
     while(i < line_len){
          if(isblank(line[i+1])){
               // we are starting at a boundary move to the beginning of the previous word
               while(isblank(line[i+1]) && (i+1 < line_len)) i++;
          }
          else if(punctuation_word_boundaries && ce_ispunct(line[i+1])){
               while(ce_ispunct(line[i+1]) && (i+1 < line_len)) i++;
               break;
          }
          else{
               while(!isblank(line[i+1]) && (!punctuation_word_boundaries || !ce_ispunct(line[i+1])) && (i+1 < line_len)) i++;
               break;
          }
     }
     return i - location->x;
}

// return -1 on failure, delta to move right to the beginning of the next word on success
int64_t ce_find_delta_to_next_word(const Buffer_t* buffer, const Point* location, bool punctuation_word_boundaries)
{
     // TODO: make this ce_move_cursor_to_next_word. Also, this function appears to be broken.
     // test case: word = blah // put the cursor on the d in word and hit w or put the cursor on = and hit w
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);

     int64_t delta = ce_find_delta_to_end_of_word(buffer, location, punctuation_word_boundaries);
     if(delta == -1) return -1;
     const char* line = buffer->lines[location->y];
     int line_len = strlen(line);
     int cur_x = location->x + delta;
     if(cur_x + 1 <= line_len){ // if at eol, the null character is considered the next word
          do{
               // churn through all whitespace following end of word
               cur_x++;
          } while(isblank(line[cur_x]) && (cur_x+1 < line_len));
     }
     return cur_x - location->x;
}

bool ce_get_char(const Buffer_t* buffer, const Point* location, char* c)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);
     // NOTE: this assert seems to be pretty useful for debugging

     if(!ce_point_on_buffer(buffer, location)) return false;

     *c = buffer->lines[location->y][location->x];

     return true;
}

char ce_get_char_raw(const Buffer_t* buffer, const Point* location)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);

     return buffer->lines[location->y][location->x];
}

bool ce_set_char(Buffer_t* buffer, const Point* location, char c)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);

     if(buffer->readonly) return false;

     if(!ce_point_on_buffer(buffer, location)) return false;

     if(c == NEWLINE) return ce_insert_string(buffer, location, "\n");

     buffer->lines[location->y][location->x] = c;
     buffer->modified = true;
     return true;
}

bool insert_line_impl(Buffer_t* buffer, int64_t line, const char* string)
{
     CE_CHECK_PTR_ARG(buffer);

     // make sure we are only inserting in the middle or at the very end
     assert(line >= 0 && line <= buffer->line_count);
     int64_t string_line_count = 1;
     if(string) string_line_count = ce_count_string_lines(string);

     int64_t new_line_count = buffer->line_count + string_line_count;
     char** new_lines = realloc(buffer->lines, new_line_count * sizeof(char*));
     if(!new_lines){
          printf("%s() failed to malloc new lines: %"PRId64"\n", __FUNCTION__, new_line_count);
          return false;
     }

     memmove(new_lines + line + string_line_count, new_lines + line, (buffer->line_count - line) * sizeof(*new_lines));

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

     return true;
}


// NOTE: passing NULL to string causes an empty line to be inserted
bool ce_insert_line(Buffer_t* buffer, int64_t line, const char* string)
{
     CE_CHECK_PTR_ARG(buffer);

     if(buffer->readonly) return false;

     return insert_line_impl(buffer, line, string);
}

bool ce_insert_line_readonly(Buffer_t* buffer, int64_t line, const char* string)
{
     CE_CHECK_PTR_ARG(buffer);

     if(!buffer->readonly) return false;

     return insert_line_impl(buffer, line, string);
}

bool ce_append_line(Buffer_t* buffer, const char* string)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(string);

     return ce_insert_line(buffer, buffer->line_count, string);
}

bool ce_append_line_readonly(Buffer_t* buffer, const char* string)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(string);

     return ce_insert_line_readonly(buffer, buffer->line_count, string);
}

bool ce_insert_newline(Buffer_t* buffer, int64_t line)
{
     return ce_insert_line(buffer, line, NULL);
}

// appends line + 1 to line
bool ce_join_line(Buffer_t* buffer, int64_t line){
     CE_CHECK_PTR_ARG(buffer);

     if(line >= buffer->line_count || line < 0){
          ce_message("%s() specified line %"PRId64" ouside of buffer, which has %"PRId64" lines", __FUNCTION__, line, buffer->line_count);
          return false;
     }

     if(buffer->readonly) return false;

     if(line == buffer->line_count - 1) return true; // nothing to do
     char* l1 = buffer->lines[line];
     size_t l1_len = strlen(l1);
     char* l2 = buffer->lines[line+1];
     size_t l2_len = strlen(l2);
     buffer->lines[line] = realloc(l1, l1_len + l2_len + 2); //space and null
     if(!buffer->lines[line]) return false; // TODO: ENOMEM
     l1 = buffer->lines[line];
     l1[l1_len] = ' ';
     memcpy(&l1[l1_len+1], l2, l2_len+1);
     buffer->modified = true;
     return ce_remove_line(buffer, line+1);
}

bool ce_remove_line(Buffer_t* buffer, int64_t line)
{
     CE_CHECK_PTR_ARG(buffer);

     if(line >= buffer->line_count || line < 0){
          ce_message("%s() specified line %"PRId64" ouside of buffer, which has %"PRId64" lines", __FUNCTION__, line, buffer->line_count);
          return false;
     }

     if(buffer->readonly) return false;

     // free the old line
     free(buffer->lines[line]);

     int64_t new_line_count = buffer->line_count - 1;
     // move trailing lines up 1
     memmove(buffer->lines + line, buffer->lines + line + 1, (new_line_count - line) * sizeof(*buffer->lines));

     buffer->lines = realloc(buffer->lines, new_line_count * sizeof(*buffer->lines));
     if(!buffer->lines){
          ce_message("%s() failed to realloc new lines: %"PRId64"", __FUNCTION__, new_line_count);
          return false;
     }

     buffer->line_count = new_line_count;
     buffer->modified = true;
     return true;
}

bool ce_remove_string(Buffer_t* buffer, const Point* location, int64_t length)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);

     if(buffer->readonly) return false;

     if(length == 0) return true;

     // TODO: should this return false and not do anything if we try to remove
     //       a string longer than the size of the rest of the buffer?

     if(!ce_point_on_buffer(buffer, location)) return false;

     int64_t current_line_len = strlen(buffer->lines[location->y]);
     int64_t rest_of_the_line_len = (current_line_len - location->x);

     // easy case: string is on a single line
     if(length <= rest_of_the_line_len){
          int64_t new_line_len = current_line_len - length;
          memmove(buffer->lines[location->y] + location->x,
                  buffer->lines[location->y] + location->x + length,
                  current_line_len - (location->x + length));
          buffer->lines[location->y][new_line_len] = 0;

          // shrink the allocation now that we have fixed up the line
          buffer->lines[location->y] = realloc(buffer->lines[location->y], new_line_len + 1);
          if(!buffer->lines[location->y]){
               ce_message("%s() failed to realloc new line", __FUNCTION__);
               return false;
          }
          buffer->modified = true;
          return true;
     }

     // don't delete the rest of the first line yet, we'll do this when we mash the first and last lines
     length -= rest_of_the_line_len + 1; // account for newline
     buffer->lines[location->y][location->x] = '\0';

     // hard case: string spans multiple lines
     int64_t delete_index = location->y + 1;

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
               int64_t new_line_len = location->x + next_line_part_len;
               buffer->lines[location->y] = realloc(buffer->lines[location->y], new_line_len + 1);
               if(!buffer->lines[location->y]){
                    ce_message("%s() failed to realloc new line", __FUNCTION__);
                    return false;
               }

               assert(buffer->lines[location->y+1][length+next_line_part_len] == '\0');
               memcpy(buffer->lines[location->y] + location->x,
                      buffer->lines[location->y+1] + length, next_line_part_len + 1);
               ce_remove_line(buffer, location->y+1);
               break;
          }
     }

     buffer->modified = true;
     return true;
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
     buffer->modified = false;
     return true;
}

int64_t match_keyword(const char* line, int64_t start_offset, const char** keywords, int64_t keyword_count)
{
     int64_t highlighting_left = 0;
     for(int64_t k = 0; k < keyword_count; ++k){
          int64_t keyword_len = strlen(keywords[k]);
          if(strncmp(line + start_offset, keywords[k], keyword_len) == 0){
               char pre_char = 0;
               char post_char = line[start_offset + keyword_len];
               if(start_offset > 0) pre_char = line[start_offset - 1];

               if(!isalnum(pre_char) && pre_char != '_' &&
                  !isalnum(post_char) && post_char != '_'){
                    highlighting_left = keyword_len;
                    break;
               }
          }
     }

     return highlighting_left;
}

int64_t ce_is_c_keyword(const char* line, int64_t start_offset)
{
     static const char* keywords [] = {
          "__thread",
          "auto",
          "case",
          "default",
          "do",
          "else",
          "enum",
          "extern",
          "false",
          "for",
          "if",
          "inline",
          "register",
          "sizeof",
          "static",
          "struct",
          "switch",
          "true",
          "typedef",
          "typeof",
          "union",
          "volatile",
          "while",
     };

     static const int keyword_count = sizeof(keywords) / sizeof(keywords[0]);

     return match_keyword(line, start_offset, keywords, keyword_count);
}

int64_t ce_is_c_control(const char* line, int64_t start_offset)
{
     static const char* keywords [] = {
          "break",
          "const",
          "continue",
          "goto",
          "return",

          // c++
          "try",
          "catch",
     };

     static const int keyword_count = sizeof(keywords) / sizeof(keywords[0]);

     return match_keyword(line, start_offset, keywords, keyword_count);
}

int iscidentifier(int c)
{
     return isalnum(c) || c == '_';
}

int64_t ce_is_c_typename(const char* line, int64_t start_offset)
{
     // NOTE: simple rules for now:
     //       -if it is one of the c standard type names
     //       -ends in _t

     static const char* keywords [] = {
          "bool",
          "char",
          "double",
          "float",
          "int",
          "long",
          "short",
          "signed",
          "unsigned",
          "void",
     };

     static const int keyword_count = sizeof(keywords) / sizeof(keywords[0]);

     int64_t match = match_keyword(line, start_offset, keywords, keyword_count);
     if(match) return match;

     if(start_offset > 0 && iscidentifier(line[start_offset - 1])) return 0; // weed out middle of words

     // try valid identifier ending in '_t'
     const char* itr = line + start_offset;
     int64_t count = 0;
     for(char ch = *itr; iscidentifier(ch); ++itr){
          ch = *itr;
          count++;
     }

     if(!count) return 0;

     // we overcounted on the last iteration!
     count--;

     // reset itr before checking the final 2 characters
     itr = line + start_offset;
     if(count >= 2 && itr[count-2] == '_' && itr[count-1] == 't') return count;

     return 0;
}

int64_t ce_is_preprocessor(const char* line, int64_t start_offset)
{
     static const char* keywords [] = {
          "define",
          "include",
          "undef",
          "ifdef",
          "ifndef",
          "if",
          "else",
          "elif",
          "endif",
          "error",
          "pragma",
          "push",
          "pop",
     };

     static const int keyword_count = sizeof(keywords) / sizeof(keywords[0]);

     // exit early if this isn't a preproc cmd
     if(line[start_offset] != '#') return 0;

     int64_t highlighting_left = 0;
     for(int64_t k = 0; k < keyword_count; ++k){
          // NOTE: I wish we could strlen at compile time ! Can we?
          int64_t keyword_len = strlen(keywords[k]);
          if(strncmp(line + start_offset + 1, keywords[k], keyword_len) == 0){
               highlighting_left = keyword_len + 1; // account for missing prepended #
               break;
          }
     }

     return highlighting_left;
}

CommentType_t ce_is_comment(const char* line, int64_t start_offset)
{
     char ch = line[start_offset];

     if(ch == '/'){
          char next_ch = line[start_offset + 1];
          if(next_ch == '*'){
               return CT_BEGIN_MULTILINE;
          }else if(next_ch == '/'){
               return CT_SINGLE_LINE;
          }

          int64_t prev_index = start_offset - 1;
          if(prev_index >= 0 && line[prev_index] == '*'){
               return CT_END_MULTILINE;
          }
     }

     return CT_NONE;
}

void ce_is_string_literal(const char* line, int64_t start_offset, int64_t line_len, bool* inside_string, char* last_quote_char)
{
     char ch = line[start_offset];
     if(ch == '"'){
          // ignore single quotes inside double quotes
          if(*inside_string && *last_quote_char == '\''){
               return;
          }
          *inside_string = !*inside_string;
          if(*inside_string){
               *last_quote_char = ch;
          }
     }else if(ch == '\''){
          if(*inside_string){
               if(*last_quote_char == '"'){
                    return;
               }
               *inside_string = false;
          }else{
               char next_char = line[start_offset + 1];
               int64_t next_next_index = start_offset + 2;
               char next_next_char = (next_next_index < line_len) ? line[next_next_index] : 0;

               if(next_char == '\\' || next_next_char == '\''){
                    *inside_string = true;
                    *last_quote_char = ch;
               }
          }
     }
}

int iscapsvarchar(int c)
{
     return isupper(c) || c == '_' || isdigit(c);
}

int64_t ce_is_caps_var(const char* line, int64_t start_offset)
{
     const char* itr = line + start_offset;
     int64_t count = 0;
     for(char ch = *itr; iscapsvarchar(ch); ++itr){
          ch = *itr;
          count++;
     }

     if(!count) return 0;

     int64_t prev_index = start_offset - 1;

     // if the surrounding chars are letters, we haven't found a constant
     if(islower(*(itr - 1))) return 0;
     if(prev_index >= 0 && (iscapsvarchar(line[prev_index]) || isalpha(line[prev_index]))) return 0;

     return count - 1; // we over-counted on the last iteration
}

int set_color(Syntax syntax, bool highlighted)
{
     standend();
     if(highlighted) attron(COLOR_PAIR(syntax + S_NORMAL_HIGHLIGHTED - 1));
     else attron(COLOR_PAIR(syntax));
     return syntax;
}

static const char non_printable_repr = '~';

bool ce_draw_buffer(const Buffer_t* buffer, const Point* cursor,const Point* term_top_left,
                    const Point* term_bottom_right, const Point* buffer_top_left, const char* highlight_word)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(term_top_left);
     CE_CHECK_PTR_ARG(term_bottom_right);
     CE_CHECK_PTR_ARG(buffer_top_left);

     if(!buffer->line_count) return true;

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

     int64_t max_width = (term_bottom_right->x - term_top_left->x) + 1;
     int64_t last_line = buffer_top_left->y + (term_bottom_right->y - term_top_left->y);
     if(last_line >= buffer->line_count) last_line = buffer->line_count - 1;

     bool inside_multiline_comment = false;

     // do a pass looking for only an ending multiline comment
     for(int64_t i = buffer_top_left->y; i <= last_line; ++i) {
          if(!buffer->lines[i][0]) continue;
          const char* buffer_line = buffer->lines[i];
          int64_t len = strlen(buffer_line);
          bool found_open_multiline_comment = false;

          for(int64_t c = 0; c < len; ++c){
               if(buffer_line[c] == '/' && buffer_line[c + 1] == '*'){
                    found_open_multiline_comment = true;
                    break;
               }

               if(buffer_line[c] == '*' && buffer_line[c + 1] == '/'){
                    inside_multiline_comment = true;
               }
          }

          if(found_open_multiline_comment) break;
     }

     // TODO: if we found a closing multiline comment, make sure there is a matching opening multiline comment
     size_t highlight_word_len = 0;
     if(highlight_word) highlight_word_len = strlen(highlight_word);

     standend();

     for(int64_t i = buffer_top_left->y; i <= last_line; ++i) {
          move(term_top_left->y + (i - buffer_top_left->y), term_top_left->x);

          if(!buffer->lines[i][0]) continue;
          const char* buffer_line = buffer->lines[i];
          int64_t line_length = strlen(buffer_line);

          int64_t print_line_length = strlen(buffer_line + buffer_top_left->x);

          int64_t min = max_width < print_line_length ? max_width : print_line_length;
          const char* line_to_print = buffer_line + buffer_top_left->x;

          if(has_colors() == TRUE){
               bool inside_string = false;
               char last_quote_char = 0;
               bool inside_comment = false;
               int64_t color_left = 0;
               int highlight_color = 0;
               bool diff_add = buffer->lines[i][0] == '+';
               bool diff_remove = buffer->lines[i][0] == '-';
               bool inside_highlight = false;
               int64_t highlighting_left = 0;
               int fg_color = 0;

               int64_t begin_trailing_whitespace = line_length;

               // NOTE: pre-pass to find trailing whitespace if it exists
               if(cursor->y != i){
                    for(int64_t c = line_length - 1; c >= 0; --c){
                         if(isblank(buffer_line[c])){
                              begin_trailing_whitespace--;
                         }else{
                              break;
                         }
                    }
               }

               // NOTE: pre-pass check for comments and strings out of view
               for(int64_t c = 0; c < buffer_top_left->x; ++c){
                    CommentType_t comment_type = ce_is_comment(buffer_line, c);
                    switch(comment_type){
                    default:
                         break;
                    case CT_SINGLE_LINE:
                         inside_comment = true;
                         break;
                    case CT_BEGIN_MULTILINE:
                         inside_multiline_comment = true;
                         break;
                    case CT_END_MULTILINE:
                         inside_multiline_comment = false;
                         break;
                    }

                    if(highlight_word && strncmp(buffer_line + c, highlight_word, highlight_word_len) == 0){
                         highlighting_left = highlight_word_len;
                         inside_highlight = true;
                    }else if(inside_highlight){
                         highlighting_left--;
                         if(!highlighting_left){
                              inside_highlight = false;
                         }
                    }

                    ce_is_string_literal(buffer_line, c, line_length, &inside_string, &last_quote_char);

                    // subtract from what is left of the keyword if we found a keyword earlier
                    if(color_left){
                         color_left--;
                    }else{
                         if(!inside_string){
                              if(!inside_comment && !inside_multiline_comment){

                                   int64_t keyword_left = ce_is_c_control(buffer_line, c);
                                   if(keyword_left){
                                        color_left = keyword_left;
                                        highlight_color = S_CONTROL;
                                   }

                                   if(!keyword_left){
                                        keyword_left = ce_is_c_typename(buffer_line, c);
                                        if(keyword_left){
                                             color_left = keyword_left;
                                             highlight_color = S_TYPE;
                                        }
                                   }

                                   if(!keyword_left){
                                        keyword_left = ce_is_c_keyword(buffer_line, c);
                                        if(keyword_left){
                                             color_left = keyword_left;
                                             highlight_color = S_KEYWORD;
                                        }
                                   }

                                   if(!keyword_left){
                                        keyword_left = ce_is_caps_var(buffer_line, c);
                                        if(keyword_left){
                                             color_left = keyword_left;
                                             highlight_color = S_CONSTANT;
                                        }
                                   }

                                   if(!keyword_left){
                                        keyword_left = ce_is_preprocessor(buffer_line, c);
                                        if(keyword_left){
                                             color_left = keyword_left;
                                             highlight_color = S_PREPROCESSOR;
                                        }
                                   }
                              }
                         }
                    }
               }

               // skip line if we are offset by too much and can't show the line
               if(line_length <= buffer_top_left->x) continue;

               fg_color = set_color(S_NORMAL, inside_highlight);

               if(inside_comment || inside_multiline_comment){
                    fg_color = set_color(S_COMMENT, inside_highlight);
               }else if(inside_string){
                    fg_color = set_color(S_STRING, inside_highlight);
               }else if(color_left){
                    fg_color = set_color(highlight_color, inside_highlight);
               }else if(diff_add){
                    fg_color = set_color(S_DIFF_ADD, inside_highlight);
               }else if(diff_remove){
                    fg_color = set_color(S_DIFF_REMOVE, inside_highlight);
               }

               for(int64_t c = 0; c < min; ++c){
                    // check for the highlight
                    Point point = {c + buffer_top_left->x, i};
                    if(ce_point_in_range(&point, &buffer->highlight_start, &buffer->highlight_end)){
                         inside_highlight = true;
                         set_color(fg_color, inside_highlight);
                    }else{
                         if(highlight_word && strncmp(buffer_line + c + buffer_top_left->x, highlight_word,
                                                      highlight_word_len) == 0){
                              highlighting_left = highlight_word_len;
                              inside_highlight = true;
                              set_color(fg_color, inside_highlight);
                         }else if(inside_highlight){
                              if(highlighting_left){
                                   highlighting_left--;
                                   if(!highlighting_left){
                                        inside_highlight = false;
                                        set_color(fg_color, inside_highlight);
                                   }
                              }else{
                                   inside_highlight = false;
                                   set_color(fg_color, inside_highlight);
                              }
                         }
                    }

                    // syntax highlighting
                    if(color_left == 0){
                         if(!inside_string){
                              if(!inside_comment && !inside_multiline_comment){
                                   color_left = ce_is_c_control(line_to_print, c);
                                   if(color_left){
                                        fg_color = set_color(S_CONTROL, inside_highlight);
                                   }

                                   if(!color_left){
                                        color_left = ce_is_c_typename(line_to_print, c);
                                        if(color_left){
                                             fg_color = set_color(S_TYPE, inside_highlight);
                                        }
                                   }

                                   if(!color_left){
                                        color_left = ce_is_c_keyword(line_to_print, c);
                                        if(color_left){
                                             fg_color = set_color(S_KEYWORD, inside_highlight);
                                        }
                                   }

                                   if(!color_left){
                                        color_left = ce_is_caps_var(line_to_print, c);
                                        if(color_left){
                                             fg_color = set_color(S_CONSTANT, inside_highlight);
                                        }
                                   }

                                   if(!color_left){
                                        color_left = ce_is_preprocessor(line_to_print, c);
                                        if(color_left){
                                             fg_color = set_color(S_PREPROCESSOR, inside_highlight);
                                        }
                                   }
                              }
                         }

                         CommentType_t comment_type = ce_is_comment(line_to_print, c);
                         switch(comment_type){
                         default:
                              break;
                         case CT_SINGLE_LINE:
                              inside_comment = true;
                              fg_color = set_color(S_COMMENT, inside_highlight);
                              break;
                         case CT_BEGIN_MULTILINE:
                              inside_multiline_comment = true;
                              fg_color = set_color(S_COMMENT, inside_highlight);
                              break;
                         case CT_END_MULTILINE:
                              inside_multiline_comment = false;
                              break;
                         }

                         bool pre_quote_check = inside_string;
                         ce_is_string_literal(line_to_print, c, print_line_length, &inside_string, &last_quote_char);

                         // if inside_string has changed, update the color
                         if(pre_quote_check != inside_string){
                              if(inside_string) fg_color = set_color(S_STRING, inside_highlight);
                              else color_left = 1;
                         }
                    }else{
                         color_left--;
                         if(color_left == 0){
                              fg_color = set_color(S_NORMAL, inside_highlight);

                              if(inside_comment || inside_multiline_comment){
                                   fg_color = set_color(S_COMMENT, inside_highlight);
                              }else if(inside_string){
                                   fg_color = set_color(S_STRING, inside_highlight);
                              }else if(diff_add){
                                   fg_color = set_color(S_DIFF_ADD, inside_highlight);
                              }else if(diff_remove){
                                   fg_color = set_color(S_DIFF_REMOVE, inside_highlight);
                              }
                         }
                    }

                    if(c >= begin_trailing_whitespace){
                         fg_color = set_color(S_TRAILING_WHITESPACE, inside_highlight);
                    }

                    // print each character
                    if(isprint(line_to_print[c])){
                         addch(line_to_print[c]);
                    }else{
                         addch(non_printable_repr);
                    }
               }

               // NOTE: post pass after the line to see if multiline comments begin or end
               for(int64_t c = min; c < line_length; ++c){
                    CommentType_t comment_type = ce_is_comment(buffer_line, c);
                    switch(comment_type){
                    default:
                         break;
                    case CT_BEGIN_MULTILINE:
                         inside_multiline_comment = true;
                         break;
                    case CT_END_MULTILINE:
                         inside_multiline_comment = false;
                         break;
                    }
               }
          }else{
               for(int64_t c = 0; c < min; ++c){
                    // print each character
                    if(isprint(line_to_print[c])){
                         addch(line_to_print[c]);
                    }else{
                         addch(non_printable_repr);
                    }
               }
          }

          standend();
     }

     return true;
}

BufferNode_t* ce_append_buffer_to_list(BufferNode_t* head, Buffer_t* buffer)
{
     CE_CHECK_PTR_ARG(head);
     CE_CHECK_PTR_ARG(buffer);

     // find last element
     while(head->next){
          head = head->next;
     }

     BufferNode_t* new = malloc(sizeof(BufferNode_t));
     if(!new){
          ce_message("%s() failed to alloc new BufferNode_t for '%s'", __FUNCTION__, buffer->filename);
          return NULL;
     }

     head->next = new;
     new->buffer = buffer;
     new->next = NULL;

     return new;
}

bool ce_remove_buffer_from_list(BufferNode_t* head, BufferNode_t** node)
{
     CE_CHECK_PTR_ARG(head);
     CE_CHECK_PTR_ARG(node);

     BufferNode_t* tmp = head;
     while(head){
          if(head == *node){
               tmp->next = head->next;
               free(head);
               *node = NULL;
               return true;
          }
          tmp = head;
          head = head->next;
     }

     // didn't find the node to remove
     return false;
}

bool ce_move_cursor_to_end_of_line(const Buffer_t* buffer, Point* cursor)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(cursor);

     if(!ce_point_on_buffer(buffer, cursor)) return false;

     cursor->x = strlen(buffer->lines[cursor->y])-1;
     if(cursor->x < 0) cursor->x = 0;
     return true;
}

bool ce_set_cursor(const Buffer_t* buffer, Point* cursor, const Point* location)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(cursor);
     CE_CHECK_PTR_ARG(location);

     assert(cursor->x >= 0);
     assert(cursor->y >= 0);

     if(!buffer->line_count){
          *cursor = (Point){0, 0};
          return false;
     }

     Point dst = *location;

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
Point* ce_clamp_cursor(const Buffer_t* buffer, Point* cursor){
     ce_move_cursor(buffer, cursor, (Point){0,0});
     return cursor;
}

bool ce_move_cursor(const Buffer_t* buffer, Point* cursor, Point delta)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(cursor);

     if(!buffer->line_count){
          *cursor = (Point){0, 0};
          return true;
     }

     Point dst = *cursor;
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

bool ce_advance_cursor(const Buffer_t* buffer, Point* cursor, int64_t delta)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(cursor);

     if(!ce_point_on_buffer(buffer, cursor)) return false;
     if(!delta) return true;

     Direction d = (delta > 0 ) ? CE_DOWN : CE_UP;
     delta *= d;

     int64_t line_len = (d == CE_DOWN) ? strlen(buffer->lines[cursor->y]) : 0;
     int64_t line_len_left = (d == CE_DOWN) ? line_len - cursor->x : cursor->x;

     // if the movement fits on this line, go for it
     if(delta < line_len_left){
          cursor->x += delta*d;
          return true;
     }

     delta -= line_len_left;
     cursor->y += d;
     cursor->x = (d == CE_DOWN) ? strlen(buffer->lines[cursor->y]) : 0;

     while(true){
          if(d == CE_DOWN && cursor->y >= buffer->line_count) return ce_move_cursor_to_end_of_file(buffer, cursor);
          else if(cursor->y < 0) return ce_move_cursor_to_beginning_of_file(buffer, cursor);

          line_len = strlen(buffer->lines[cursor->y]);

          if(delta < line_len){
               cursor->x = (d == CE_DOWN) ? delta : line_len - delta;
               break;
          }

          cursor->y += d;
          delta -= line_len;
     }

     return true;
}

bool ce_move_cursor_to_end_of_file(const Buffer_t* buffer, Point* cursor)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(cursor);

     if(!buffer->line_count) return false;

     int64_t last_line = buffer->line_count - 1;

     cursor->x = ce_last_index(buffer->lines[last_line]);
     cursor->y = last_line;

     return true;
}

bool ce_move_cursor_to_beginning_of_file(const Buffer_t* buffer, Point* cursor)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(cursor);

     *cursor = (Point) {0, 0};

     return true;
}

// TODO: Threshold for top, left, bottom and right before scrolling happens
bool ce_follow_cursor(const Point* cursor, int64_t* left_column, int64_t* top_row, int64_t view_width, int64_t view_height,
                      bool at_terminal_width_edge, bool at_terminal_height_edge)
{
     CE_CHECK_PTR_ARG(cursor);
     CE_CHECK_PTR_ARG(top_row);

     assert(cursor->x >= 0);
     assert(cursor->y >= 0);

     if(!at_terminal_width_edge) view_width--;
     if(!at_terminal_height_edge) view_height--;

     int64_t bottom_row = *top_row + view_height;

     if(cursor->y < *top_row){
          *top_row = cursor->y;
     }else if(cursor->y > bottom_row){
          bottom_row = cursor->y;
          *top_row = bottom_row - view_height;
     }

     int64_t right_column = *left_column + view_width;

     if(cursor->x < *left_column){
          *left_column = cursor->x;
     }else if(cursor->x > right_column){
          right_column = cursor->x;
          *left_column = right_column - view_width;
     }

     if(*top_row < 0) *top_row = 0;
     if(*left_column < 0) *left_column = 0;

     return true;
}

bool ce_commit_insert_char(BufferCommitNode_t** tail, const Point* start, const Point* undo_cursor, const Point* redo_cursor, char c)
{
     CE_CHECK_PTR_ARG(tail);
     CE_CHECK_PTR_ARG(start);
     CE_CHECK_PTR_ARG(undo_cursor);
     CE_CHECK_PTR_ARG(redo_cursor);

     BufferCommit_t change;
     change.type = BCT_INSERT_CHAR;
     change.start = *start;
     change.undo_cursor = *undo_cursor;
     change.redo_cursor = *redo_cursor;
     change.c = c;

     return ce_commit_change(tail, &change);
}

bool ce_commit_insert_string(BufferCommitNode_t** tail, const Point* start, const Point* undo_cursor, const Point* redo_cursor,  char* string)
{
     CE_CHECK_PTR_ARG(tail);
     CE_CHECK_PTR_ARG(start);
     CE_CHECK_PTR_ARG(undo_cursor);
     CE_CHECK_PTR_ARG(redo_cursor);
     CE_CHECK_PTR_ARG(string);

     BufferCommit_t change;
     change.type = BCT_INSERT_STRING;
     change.start = *start;
     change.undo_cursor = *undo_cursor;
     change.redo_cursor = *redo_cursor;
     change.str = string;

     return ce_commit_change(tail, &change);
}

bool ce_commit_remove_char(BufferCommitNode_t** tail, const Point* start, const Point* undo_cursor, const Point* redo_cursor, char c)
{
     CE_CHECK_PTR_ARG(tail);
     CE_CHECK_PTR_ARG(start);
     CE_CHECK_PTR_ARG(undo_cursor);
     CE_CHECK_PTR_ARG(redo_cursor);

     BufferCommit_t change;
     change.type = BCT_REMOVE_CHAR;
     change.start = *start;
     change.undo_cursor = *undo_cursor;
     change.redo_cursor = *redo_cursor;
     change.c = c;

     return ce_commit_change(tail, &change);
}

bool ce_commit_remove_string(BufferCommitNode_t** tail, const Point* start, const Point* undo_cursor, const Point* redo_cursor,  char* string)
{
     CE_CHECK_PTR_ARG(tail);
     CE_CHECK_PTR_ARG(start);
     CE_CHECK_PTR_ARG(undo_cursor);
     CE_CHECK_PTR_ARG(redo_cursor);
     CE_CHECK_PTR_ARG(string);

     BufferCommit_t change;
     change.type = BCT_REMOVE_STRING;
     change.start = *start;
     change.undo_cursor = *undo_cursor;
     change.redo_cursor = *redo_cursor;
     change.str = string;

     return ce_commit_change(tail, &change);
}

bool ce_commit_change_char(BufferCommitNode_t** tail, const Point* start, const Point* undo_cursor, const Point* redo_cursor, char c, char prev_c)
{
     CE_CHECK_PTR_ARG(tail);
     CE_CHECK_PTR_ARG(start);
     CE_CHECK_PTR_ARG(undo_cursor);
     CE_CHECK_PTR_ARG(redo_cursor);

     BufferCommit_t change;
     change.type = BCT_CHANGE_CHAR;
     change.start = *start;
     change.undo_cursor = *undo_cursor;
     change.redo_cursor = *redo_cursor;
     change.c = c;
     change.prev_c = prev_c;

     return ce_commit_change(tail, &change);
}

bool ce_commit_change_string(BufferCommitNode_t** tail, const Point* start, const Point* undo_cursor, const Point* redo_cursor, char* new_string,  char* prev_string)
{
     CE_CHECK_PTR_ARG(tail);
     CE_CHECK_PTR_ARG(start);
     CE_CHECK_PTR_ARG(undo_cursor);
     CE_CHECK_PTR_ARG(redo_cursor);
     CE_CHECK_PTR_ARG(new_string);
     CE_CHECK_PTR_ARG(prev_string);

     BufferCommit_t change;
     change.type = BCT_CHANGE_STRING;
     change.start = *start;
     change.undo_cursor = *undo_cursor;
     change.redo_cursor = *redo_cursor;
     change.str = new_string;
     change.prev_str = prev_string;

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
     CE_CHECK_PTR_ARG(tail);
     CE_CHECK_PTR_ARG(commit);

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

bool ce_commit_undo(Buffer_t* buffer, BufferCommitNode_t** tail, Point* cursor)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(tail);
     CE_CHECK_PTR_ARG(cursor);

     if(!*tail){
          ce_message("%s() empty undo history", __FUNCTION__);
          return false;
     }

     BufferCommitNode_t* undo_commit = *tail;
     BufferCommit_t* commit = &undo_commit->commit;

     switch(commit->type){
     default:
          ce_message("unsupported BufferCommitType_t: %d", commit->type);
          return false;
     case BCT_NONE:
          ce_message("%s() empty undo history", __FUNCTION__);
          return false;
     case BCT_INSERT_CHAR:
          ce_remove_char(buffer, &commit->start);
          break;
     case BCT_INSERT_STRING:
          ce_remove_string(buffer, &commit->start, strlen(commit->str));
          break;
     case BCT_REMOVE_CHAR:
          ce_insert_char(buffer, &commit->start, commit->c);
          break;
     case BCT_REMOVE_STRING:
          ce_insert_string(buffer, &commit->start, commit->str);
          break;
     case BCT_CHANGE_CHAR:
          ce_set_char(buffer, &commit->start, commit->prev_c);
          break;
     case BCT_CHANGE_STRING:
          ce_remove_string(buffer, &commit->start, strlen(commit->str));
          ce_insert_string(buffer, &commit->start, commit->prev_str);
          break;
     }

     *cursor = *ce_clamp_cursor(buffer, &(*tail)->commit.undo_cursor);
     *tail = (*tail)->prev;

     return true;
}

bool ce_commit_redo(Buffer_t* buffer, BufferCommitNode_t** tail, Point* cursor)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(tail);
     CE_CHECK_PTR_ARG(cursor);

     if(!*tail){
          ce_message("%s() empty redo history", __FUNCTION__);
          return false;
     }

     if(!(*tail)->next){
          ce_message("%s() empty redo history", __FUNCTION__);
          return false;
     }

     *tail = (*tail)->next;

     BufferCommitNode_t* undo_commit = *tail;
     BufferCommit_t* commit = &undo_commit->commit;

     switch(commit->type){
     default:
          ce_message("unsupported BufferCommitType_t: %d", commit->type);
          return false;
     case BCT_INSERT_CHAR:
          ce_insert_char(buffer, &commit->start, commit->c);
          break;
     case BCT_INSERT_STRING:
          ce_insert_string(buffer, &commit->start, commit->str);
          break;
     case BCT_REMOVE_CHAR:
          ce_remove_char(buffer, &commit->start);
          break;
     case BCT_REMOVE_STRING:
          ce_remove_string(buffer, &commit->start, strlen(commit->str));
          break;
     case BCT_CHANGE_CHAR:
          ce_set_char(buffer, &commit->start, commit->c);
          break;
     case BCT_CHANGE_STRING:
          ce_remove_string(buffer, &commit->start, strlen(commit->prev_str));
          ce_insert_string(buffer, &commit->start, commit->str);
          break;
     }

     *cursor = (*tail)->commit.redo_cursor;
     return true;
}

Buffer_tView_t* ce_split_view(Buffer_tView_t* view, Buffer_t* buffer, bool horizontal)
{
     CE_CHECK_PTR_ARG(view);
     CE_CHECK_PTR_ARG(buffer);

     Buffer_tView_t* itr = view;

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

     Buffer_tView_t* new_view = calloc(1, sizeof(*new_view));
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

Buffer_tView_t* find_connecting_view(Buffer_tView_t* start, Buffer_tView_t* match)
{
     if(start->next_horizontal){
          if(start->next_horizontal == match) return start;
          Buffer_tView_t* result = find_connecting_view(start->next_horizontal, match);
          if(result) return result;
     }

     if(start->next_vertical){
          if(start->next_vertical == match) return start;
          Buffer_tView_t* result = find_connecting_view(start->next_vertical, match);
          if(result) return result;
     }

     return NULL;
}

bool ce_remove_view(Buffer_tView_t** head, Buffer_tView_t* view)
{
     CE_CHECK_PTR_ARG(head);
     CE_CHECK_PTR_ARG(view);

     Buffer_tView_t* itr = *head;

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
               Buffer_tView_t* tmp = *head;
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
                    Buffer_tView_t* tmp = view->next_horizontal;
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
                    Buffer_tView_t* itr = view->next_vertical;
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

// NOTE: recursive function for free-ing splits
bool free_buffer_views(Buffer_tView_t* head)
{
     CE_CHECK_PTR_ARG(head);

     if(head->next_horizontal) free_buffer_views(head->next_horizontal);
     if(head->next_vertical) free_buffer_views(head->next_vertical);

     free(head);

     return true;
}

bool ce_free_views(Buffer_tView_t** head)
{
     CE_CHECK_PTR_ARG(head);

     if(!free_buffer_views(*head)){
          return false;
     }

     *head = NULL;
     return true;
}

bool calc_vertical_views(Buffer_tView_t* view, const Point* top_left, const Point* bottom_right, bool already_calculated);

bool calc_horizontal_views(Buffer_tView_t* view, const Point* top_left, const Point* bottom_right, bool already_calculated)
{
     int64_t view_count = 0;
     Buffer_tView_t* itr = view;
     while(itr){
          itr = itr->next_horizontal;
          view_count++;
     }

     int64_t shift = ((bottom_right->x - top_left->x) + 1) / view_count;
     Point new_top_left = *top_left;
     Point new_bottom_right = *bottom_right;
     new_bottom_right.x = new_top_left.x + (shift - 1);

     itr = view;
     while(itr){
          // if this is the first view and we haven't already calculated the dimensions for it
          // or if this is any view other than the first view
          // and we have a vertical view below us, then calculate the vertical views
          if(((!already_calculated && itr == view) || (itr != view)) && itr->next_vertical){
               if(!itr->next_horizontal) new_bottom_right.x = bottom_right->x;
               calc_vertical_views(itr, &new_top_left, &new_bottom_right, true);
          }else{
               itr->top_left = new_top_left;
               itr->bottom_right = new_bottom_right;
          }

          new_top_left.x += shift;

          if(itr->next_horizontal){
               new_bottom_right.x = new_top_left.x + (shift - 1);
          }else{
               itr->bottom_right.x = bottom_right->x;
          }

          itr = itr->next_horizontal;
     }

     return true;
}

bool calc_vertical_views(Buffer_tView_t* view, const Point* top_left, const Point* bottom_right, bool already_calculated)
{
     int64_t view_count = 0;
     Buffer_tView_t* itr = view;
     while(itr){
          itr = itr->next_vertical;
          view_count++;
     }

     int64_t shift = ((bottom_right->y - top_left->y) + 1) / view_count;
     Point new_top_left = *top_left;
     Point new_bottom_right = *bottom_right;
     new_bottom_right.y = new_top_left.y + (shift - 1);

     itr = view;
     while(itr){
          // if this is the first view and we haven't already calculated the dimensions for it
          // or if this is any view other than the first view
          // and we have a horizontal view below us, then calculate the horizontal views
          if(((!already_calculated && itr == view) || (itr != view)) && itr->next_horizontal){
               if(!itr->next_vertical) new_bottom_right.y = bottom_right->y;
               calc_horizontal_views(itr, &new_top_left, &new_bottom_right, true);
          }else{
               itr->top_left = new_top_left;
               itr->bottom_right = new_bottom_right;
          }

          new_top_left.y += shift;

          if(itr->next_vertical){
               new_bottom_right.y = new_top_left.y + (shift - 1);
          }else{
               itr->bottom_right.y = bottom_right->y;
          }

          itr = itr->next_vertical;
     }

     return true;
}

bool ce_calc_views(Buffer_tView_t* view, const Point *top_left, const Point* bottom_right)
{
     CE_CHECK_PTR_ARG(view);

     return calc_horizontal_views(view, top_left, bottom_right, false);
}

void draw_view_bottom_right_borders(const Buffer_tView_t* view)
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

bool draw_vertical_views(const Buffer_tView_t* view, bool already_drawn, const char* highlight_word);

bool draw_horizontal_views(const Buffer_tView_t* view, bool already_drawn, const char* highlight_word)
{
     const Buffer_tView_t* itr = view;
     while(itr){
          // if this is the first view and we haven't already drawn it
          // or if this is any view other than the first view
          // and we have a horizontal view below us, then draw the horizontal views
          if(((!already_drawn && itr == view) || (itr != view)) && itr->next_vertical){
               draw_vertical_views(itr, true, highlight_word);
          }else{
               assert(itr->left_column >= 0);
               assert(itr->top_row >= 0);
               Point buffer_top_left = {itr->left_column, itr->top_row};
               ce_draw_buffer(itr->buffer, &itr->cursor, &itr->top_left, &itr->bottom_right, &buffer_top_left,
                              highlight_word);
               draw_view_bottom_right_borders(itr);
          }

          itr = itr->next_horizontal;
     }

     return true;
}

bool draw_vertical_views(const Buffer_tView_t* view, bool already_drawn, const char* highlight_word)
{
     const Buffer_tView_t* itr = view;
     while(itr){
          // if this is the first view and we haven't already drawn it
          // or if this is any view other than the first view
          // and we have a vertical view below us, then draw the vertical views
          if(((!already_drawn && itr == view) || (itr != view)) && itr->next_horizontal){
               draw_horizontal_views(itr, true, highlight_word);
          }else{
               Point buffer_top_left = {itr->left_column, itr->top_row};
               ce_draw_buffer(itr->buffer, &itr->cursor, &itr->top_left, &itr->bottom_right, &buffer_top_left,
                              highlight_word);
               draw_view_bottom_right_borders(itr);
          }

          itr = itr->next_vertical;
     }

     return true;
}

bool ce_connect_border_lines(const Point* location)
{
     CE_CHECK_PTR_ARG(location);

     // connect the bottom and right borders based on ruleset
     chtype left = mvinch(location->y, location->x - 1);
     chtype right = mvinch(location->y, location->x + 1);
     chtype top = mvinch(location->y - 1, location->x);
     chtype bottom = mvinch(location->y + 1, location->x);

     // strip out the color info from each adjacent chtype
     left &= ~A_COLOR;
     right &= ~A_COLOR;
     top &= ~A_COLOR;
     bottom &= ~A_COLOR;

     if(left == ACS_HLINE && right == ACS_HLINE && top == ACS_VLINE){
          move(location->y, location->x);
          addch(ACS_BTEE);
     }

     if(left == ACS_HLINE && right == ACS_HLINE && bottom == ACS_VLINE){
          move(location->y, location->x);
          addch(ACS_TTEE);
     }

     if(top == ACS_VLINE && bottom == ACS_VLINE && left == ACS_HLINE){
          move(location->y, location->x);
          addch(ACS_RTEE);
     }

     if(top == ACS_VLINE && bottom == ACS_VLINE && right == ACS_HLINE){
          move(location->y, location->x);
          addch(ACS_LTEE);
     }

     if(top == ACS_VLINE && bottom == ACS_VLINE && right == ACS_HLINE && left == ACS_HLINE){
          move(location->y, location->x);
          addch(ACS_PLUS);
     }

     return true;
}

bool connect_borders(const Buffer_tView_t* view)
{
     CE_CHECK_PTR_ARG(view);

     if(view->next_horizontal) connect_borders(view->next_horizontal);
     if(view->next_vertical) connect_borders(view->next_vertical);

     Point top_left = {view->top_left.x - 1, view->top_left.y - 1};
     Point top_right = {view->bottom_right.x, view->top_left.y - 1};
     Point bottom_right = {view->bottom_right.x, view->bottom_right.y};
     Point bottom_left = {view->top_left.x, view->bottom_right.y};

     return ce_connect_border_lines(&top_left) && ce_connect_border_lines(&top_right) &&
            ce_connect_border_lines(&bottom_right) && ce_connect_border_lines(&bottom_left);
}

bool ce_draw_views(const Buffer_tView_t* view, const char* highlight_word)
{
     CE_CHECK_PTR_ARG(view);

     if(!draw_horizontal_views(view, false, highlight_word)){
          return false;
     }

     attron(COLOR_PAIR(S_BORDERS));
     return connect_borders(view);
}

Buffer_tView_t* find_view_at_point(Buffer_tView_t* view, const Point* point)
{
     if(point->x >= view->top_left.x && point->x <= view->bottom_right.x &&
        point->y >= view->top_left.y && point->y <= view->bottom_right.y){
          return view;
     }

     Buffer_tView_t* result = NULL;

     if(view->next_horizontal){
          result = find_view_at_point(view->next_horizontal, point);
     }

     if(!result && view->next_vertical){
          result = find_view_at_point(view->next_vertical, point);
     }

     return result;
}

Buffer_tView_t* ce_find_view_at_point(Buffer_tView_t* head, const Point* point)
{
     CE_CHECK_PTR_ARG(head);
     CE_CHECK_PTR_ARG(point);

     return find_view_at_point(head, point);
}

Buffer_tView_t* buffer_in_view(Buffer_tView_t* view, const Buffer_t* buffer)
{
     if(view->buffer == buffer){
          return view;
     }

     Buffer_tView_t* result = NULL;

     if(view->next_horizontal){
          result = buffer_in_view(view->next_horizontal, buffer);
     }

     if(!result && view->next_vertical){
          result = buffer_in_view(view->next_vertical, buffer);
     }

     return result;
}

Buffer_tView_t* ce_buffer_in_view(Buffer_tView_t* head, const Buffer_t* buffer)
{
     CE_CHECK_PTR_ARG(head);
     CE_CHECK_PTR_ARG(buffer);

     return buffer_in_view(head, buffer);
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

int64_t ce_compute_length(const Buffer_t* buffer, const Point* start, const Point* end)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(start);
     CE_CHECK_PTR_ARG(end);

     assert(ce_point_on_buffer(buffer, start));
     assert(ce_point_on_buffer(buffer, end));

     ce_sort_points(&start, &end);

     size_t length = 0;

     if(start->y < end->y){
          length = strlen(buffer->lines[start->y] + start->x) + 1; // account for newline
          for(int64_t i = start->y + 1; i < end->y; ++i){
               length += strlen(buffer->lines[i]) + 1; // account for newline
          }
          length += end->x+1; // do not account for newline. end is inclusive
     }else{
          assert(start->y == end->y);
          length += end->x+1 - start->x;
     }

     return length;
}

int ce_iswordchar(int c)
{
     return !isblank(c) && !ce_ispunct(c);
}

// given a buffer, two points, and a function ptr, return a range of characters that match defined criteria
// NOTE: start is inclusive, end is inclusive
bool ce_get_homogenous_adjacents(const Buffer_t* buffer, Point* start, Point* end, int (*is_homogenous)(int))
{
     assert(memcmp(start, end, sizeof *start) == 0);

     char curr_char;
     if(!ce_get_char(buffer, start, &curr_char)) return false;

     do{
          start->x--;
          if(!ce_get_char(buffer, start, &curr_char)) break;
     }while((*is_homogenous)(curr_char));

     start->x++; // the last character wasn't homogenous

     do{
          end->x++;
          if(!ce_get_char(buffer, end, &curr_char)) break;
     }while((*is_homogenous)(curr_char));

     end->x--; // the last character wasn't homogenous

     return true;
}

// word_start is inclusive, word_end is exclusive
bool ce_get_word_at_location(const Buffer_t* buffer, const Point* location, Point* word_start, Point* word_end)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);
     CE_CHECK_PTR_ARG(word_start);
     CE_CHECK_PTR_ARG(word_end);

     *word_start = *location;
     *word_end = *location;
     char curr_char;
     bool success = ce_get_char(buffer, word_start, &curr_char);
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
     return true;
}

int64_t ce_get_indentation_for_next_line(const Buffer_t* buffer, const Point* location, int64_t tab_len)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);

     // first, match this line's indentation
     Point bol = *location;
     bol.x = 0;
     int64_t indent = ce_find_delta_to_soft_beginning_of_line(buffer, &bol);

     // then, check the line for a '{' that is unmatched on location's line + indent if you find one
     char curr;
     for(Point iter = {ce_last_index(buffer->lines[location->y]), location->y};
         ce_get_char(buffer, &iter, &curr);
         iter.x--){
          if(curr == '{'){
               Point match;
               if(!ce_find_match(buffer, &iter, &match) || match.y != location->y){
                    // '{' is globally unmatched, or unmatched on our line
                    indent += tab_len;
                    break; // if a line has "{{", we don't want to double tab the next line!
               }
           }
     }

     return indent;
}

// if a > b, swap a and b
void ce_sort_points(const Point** a, const Point** b)
{
     if((*b)->y < (*a)->y || ((*b)->y == (*a)->y && (*b)->x < (*a)->x)){
          const Point* temp = *a;
          *a = *b;
          *b = temp;
     }
}

bool ce_point_in_range(const Point* p, const Point* start, const Point* end)
{
    if( ((p->y == start->y && p->x >= start->x) || (p->y > start->y)) &&
        ((p->y == end->y && p->x <= end->x) || (p->y < end->y )) ){
         return true;
    }

     return false;
}

int64_t ce_last_index(const char* string)
{
     CE_CHECK_PTR_ARG(string);

     int64_t len = strlen(string);
     if(len) len--;

     return len;
}
