#include "ce.h"
#include <ctype.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

Buffer* g_message_buffer = NULL;
Point* g_terminal_dimensions = NULL;

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

bool ce_alloc_lines(Buffer* buffer, int64_t line_count)
{
     CE_CHECK_PTR_ARG(buffer);

     if(line_count <= 0){
          ce_message("%s() tried to allocate %"PRId64" lines for a buffer, but we can only allocated > 0 lines", line_count);
          return false;
     }

     buffer->lines = malloc(line_count * sizeof(char*));
     if(!buffer->lines){
          ce_message("%s() failed to allocate %"PRId64" lines for buffer", line_count);
          return false;
     }

     buffer->line_count = line_count;

     // clear the lines
     for(int64_t i = 0; i < line_count; ++i){
          buffer->lines[i] = calloc(1, sizeof(buffer->lines[i]));
          if(!buffer->lines[i]){
               ce_message("failed to calloc() new line %lld", i);
               return false;
          }
     }

     return true;
}

bool ce_load_file(Buffer* buffer, const char* filename)
{
     ce_message("load file '%s'", filename);

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

     free(contents);

     return true;
}

bool ce_load_string(Buffer* buffer, const char* str)
{
     Point start = {0, 0};
     return ce_insert_string(buffer, &start, str);
}

void ce_free_buffer(Buffer* buffer)
{
     if(!buffer){
          return;
     }

     free(buffer->filename);

     ce_clear_lines(buffer);
}

void ce_clear_lines(Buffer* buffer)
{
     if(buffer->lines){
          for(int64_t i = 0; i < buffer->line_count; ++i){
               free(buffer->lines[i]);
          }

          free(buffer->lines);
          buffer->line_count = 0;
     }
}

bool ce_point_on_buffer(const Buffer* buffer, const Point* location)
{
     if(location->y < 0 || location->x < 0){
          ce_message("%s() %"PRId64", %"PRId64" not in buffer", __FUNCTION__, location->x, location->y);
          return false;
     }

     if(location->y >= buffer->line_count){
          ce_message("%s() %"PRId64", %"PRId64" not in buffer with %"PRId64" lines",
                     __FUNCTION__, location->x, location->y, buffer->line_count);
          return false;
     }

     char* line = buffer->lines[location->y];
     int64_t line_len = 0;

     if(line) line_len = strlen(line);

     if(location->x > line_len){
          ce_message("%s() %"PRId64", %"PRId64" not in buffer with line %"PRId64" only %"PRId64" characters long",
                     __FUNCTION__, location->x, location->y, buffer->line_count, line_len);
          return false;
     }

     return true;
}

bool ce_insert_char(Buffer* buffer, const Point* location, char c)
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
          if(!ce_insert_line(buffer, location->y + 1, line + location->x)){
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
     return true;
}

bool ce_insert_string(Buffer* buffer, const Point* location, const char* new_string)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);
     CE_CHECK_PTR_ARG(new_string);

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

               int64_t length = (i - 1) - last_newline;
               char* new_line = realloc(buffer->lines[line], length + 1);
               if(!new_line){
                    ce_message("%s() failed to alloc line %d", __FUNCTION__, line);
                    return false;
               }

               memcpy(new_line, new_string + last_newline + 1, length);
               new_line[length] = 0;
               buffer->lines[line] = new_line;

               last_newline = i;
               line++;
          }

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

     return true;
}

bool ce_append_string(Buffer* buffer, int64_t line, const char* new_string)
{
     Point end_of_line = {0, line};
     if(buffer->lines[line]) end_of_line.x = strlen(buffer->lines[line]);
     return ce_insert_string(buffer, &end_of_line, new_string);
}

bool ce_remove_char(Buffer* buffer, const Point* location)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);

     if(!ce_point_on_buffer(buffer, location)) return false;

     char* line = buffer->lines[location->y];
     int64_t line_len = strlen(line);

     // remove the line from the list if it is empty
     if(line_len == 0){
          char** new_lines = calloc((buffer->line_count - 1), sizeof(char*));
          if(!new_lines){
               ce_message("%s() failed alloc lines", __FUNCTION__);
               return false;
          }

          free(buffer->lines[location->y]);
          for(int64_t i = 0; i < location->y; ++i) new_lines[i] = buffer->lines[i];
          for(int64_t i = location->y + 1; i < buffer->line_count; ++i) new_lines[i - 1] = buffer->lines[i];

          free(buffer->lines);
          buffer->lines = new_lines;
          return true;
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

     return true;
}

char* ce_dupe_string(Buffer* buffer, Point start, Point end)
{
     CE_CHECK_PTR_ARG(buffer);

     int64_t total_len = ce_compute_length(buffer, start, end);

     if(start.y == end.y){
          // single line allocation
          char* new_str = malloc(total_len + 1);
          if(!new_str){
               ce_message("%s() failed to alloc string", __FUNCTION__);
               return NULL;
          }
          memcpy(new_str, buffer->lines[start.y] + start.x, total_len);
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

     memcpy(itr, buffer->lines[end.y], end.x);
     new_str[total_len] = 0;

     return new_str;
}

char* ce_dupe_line(Buffer* buffer, int64_t line)
{
     if(buffer->line_count <= line){
          ce_message("%s() specified line (%d) above buffer line count (%d)",
                     __FUNCTION__, line, buffer->line_count);
          return NULL;
     }

     size_t len = strlen(buffer->lines[line]) + 2;
     char* duped_line = malloc(len);
     duped_line[len - 2] = '\n';
     duped_line[len - 1] = 0;
     return memcpy(duped_line, buffer->lines[line], len - 2);
}

// return x delta between location and the located character 'c' if found. return -1 if not found
int64_t ce_find_delta_to_char_forward_in_line(Buffer* buffer, const Point* location, char c)
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
int64_t ce_find_delta_to_char_backward_in_line(Buffer* buffer, const Point* location, char c)
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

typedef enum{
     CE_UP = -1,
     CE_DOWN = 1
} Direction;
// returns the delta to the matching character; return success
bool ce_find_match(Buffer* buffer, const Point* location, Point* delta)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);
     CE_CHECK_PTR_ARG(delta);

     const char* cur_char = &buffer->lines[location->y][location->x];
     Direction d;
     char match;
     switch(*cur_char){
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

     const char* iter_char = cur_char + d;
     uint64_t counter = 1; // when counter goes to 0, we have found our match

     int64_t line = location->y;
     const char* line_str = buffer->lines[line];

     int64_t n_lines = (d == CE_UP) ? location->y : buffer->line_count - location->y;
     for(int64_t i = 0; i < n_lines;){
          while(*iter_char != '\0'){
               // loop over line
               if(*iter_char == match){
                    if(--counter == 0){
                         delta->x = (iter_char - buffer->lines[line]) - location->x;
                         delta->y = line - location->y;
                         return true;
                    }
               }
               else if(*iter_char == *cur_char) counter++;
               iter_char += d;
          }

          do i++;
          while(!(line_str = buffer->lines[line += d]) && i < n_lines);
          iter_char = (d == CE_UP) ? &line_str[strlen(line_str) - 1] : line_str;
     }
     return false;
}

// returns Point at the next matching string; return success
bool ce_find_string(Buffer* buffer, const Point* location, const char* search_str, Point* match)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);
     CE_CHECK_PTR_ARG(search_str);
     CE_CHECK_PTR_ARG(match);

     Direction d = CE_DOWN; // TODO support reverse search

     Point search_loc = *location;
     ce_advance_cursor(buffer, &search_loc, 1);
     char* line_str = &buffer->lines[search_loc.y][search_loc.x];

     int64_t n_lines = (d == CE_UP) ? search_loc.y : buffer->line_count - search_loc.y;
     for(int64_t i = 0; i < n_lines;){
          const char* match_str = strstr(line_str, search_str);
          if(match_str){
               int64_t line = search_loc.y + i*d;
               match->x = match_str - buffer->lines[line];
               match->y = line;
               return true;
          }
          i++;
          line_str = buffer->lines[search_loc.y + i*d];
     }
     return false;
}

bool ce_move_cursor_to_soft_beginning_of_line(Buffer* buffer, Point* cursor)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(cursor);

     if(!ce_point_on_buffer(buffer, cursor)) false;
     const char* line = buffer->lines[cursor->y];
     int i;
     for(i = 0; isblank(line[i]); i++);
     cursor->x = i;
     return true;
}

// underscores are not treated as punctuation for vim movement
int ce_ispunct(int c)
{
     return c != '_' && ispunct(c);
}

// return -1 on failure, delta to move left to the beginning of the word on success
int64_t ce_find_delta_to_beginning_of_word(Buffer* buffer, const Point* location, bool punctuation_word_boundaries)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);

     if(!ce_point_on_buffer(buffer, location)) return -1;
     const char* line = buffer->lines[location->y];
     int i = location->x;
     if(i == 0) return 0;
     while(i > 0){
          if(isblank(line[i-1])){
               // we are starting at a boundary move to the beginning of the previous word
               while(isblank(line[i-1]) && i) i--;
          }
          else if(punctuation_word_boundaries && ce_ispunct(line[i-1])){
               while(ce_ispunct(line[i-1]) && i) i--;
               break;
          }
          else{
               while(!isblank(line[i-1]) && (!punctuation_word_boundaries || !ce_ispunct(line[i-1])) && i) i--;
               break;
          }
     }
     return location->x - i;
}

// return -1 on failure, delta to move right to the end of the word on success
int64_t ce_find_delta_to_end_of_word(Buffer* buffer, const Point* location, bool punctuation_word_boundaries)
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
int64_t ce_find_next_word(Buffer* buffer, const Point* location, bool punctuation_word_boundaries)
{
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

bool ce_get_char(Buffer* buffer, const Point* location, char* c)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);

     if(!ce_point_on_buffer(buffer, location)) return false;

     *c = buffer->lines[location->y][location->x];

     return true;
}

bool ce_set_char(Buffer* buffer, const Point* location, char c)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);

     if(!ce_point_on_buffer(buffer, location)) return false;

     if(c == NEWLINE) return ce_insert_string(buffer, location, "\n");

     buffer->lines[location->y][location->x] = c;

     return true;
}

// NOTE: passing NULL to string causes an empty line to be inserted
bool ce_insert_line(Buffer* buffer, int64_t line, const char* string)
{
     CE_CHECK_PTR_ARG(buffer);

     int64_t new_line_count = buffer->line_count + 1;
     char** new_lines = realloc(buffer->lines, new_line_count * sizeof(char*));
     if(!new_lines){
          if(buffer == g_message_buffer){
               printf("%s() failed to malloc new lines: %"PRId64"\n", __FUNCTION__, new_line_count);
          }else{
               ce_message("%s() failed to malloc new lines: %"PRId64"", __FUNCTION__, new_line_count);
          }
          return false;
     }

     memmove(new_lines + line + 1, new_lines + line, (buffer->line_count - line) * sizeof(*new_lines));

     if(string){
          new_lines[line] = strdup(string);
     }else{
          new_lines[line] = strdup("");
     }

     buffer->lines = new_lines;
     buffer->line_count = new_line_count;

     return true;
}

bool ce_append_line(Buffer* buffer, const char* string)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(string);

     return ce_insert_line(buffer, buffer->line_count, string);
}

bool ce_insert_newline(Buffer* buffer, int64_t line)
{
     return ce_insert_line(buffer, line, NULL);
}

// appends line + 1 to line
bool ce_join_line(Buffer* buffer, int64_t line){
     CE_CHECK_PTR_ARG(buffer);

     if(line >= buffer->line_count || line < 0){
          ce_message("%s() specified line %lld ouside of buffer, which has %lld lines", __FUNCTION__, line, buffer->line_count);
          return false;
     }

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
     return ce_remove_line(buffer, line+1);
}

bool ce_remove_line(Buffer* buffer, int64_t line)
{
     CE_CHECK_PTR_ARG(buffer);

     if(line >= buffer->line_count || line < 0){
          ce_message("%s() specified line %lld ouside of buffer, which has %lld lines", __FUNCTION__, line, buffer->line_count);
          return false;
     }

     int64_t new_line_count = buffer->line_count - 1;
     char** new_lines = malloc(new_line_count * sizeof(*new_lines));
     if(!new_lines){
          ce_message("%s() failed to malloc new lines: %"PRId64"", __FUNCTION__, new_line_count);
          return false;
     }

     // copy up to deleted line, copy the rest in the buffer
     memcpy(new_lines, buffer->lines, line * sizeof(*new_lines));
     memcpy(new_lines + line, buffer->lines + line + 1, (new_line_count - line) * sizeof(*new_lines));

     // free and update the buffer ptr
     free(buffer->lines[line]);
     free(buffer->lines);
     buffer->lines = new_lines;
     buffer->line_count = new_line_count;

     return true;
}

bool ce_remove_string(Buffer* buffer, const Point* location, int64_t length)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);

     if(length == 0) return true;

     // TODO: should this return false and not do anything if we try to remove
     //       a string longer than the size of the rest of the buffer?

     if(!ce_point_on_buffer(buffer, location)) return false;

     char* current_line = buffer->lines[location->y];
     int64_t current_line_len = strlen(current_line);
     int64_t rest_of_the_line_len = (current_line_len - location->x);

     // easy case: string is on a single line
     if(length <= rest_of_the_line_len){
          int64_t new_line_len = current_line_len - length;
          char* new_line = realloc(current_line, new_line_len + 1);
          if(!new_line){
               ce_message("%s() failed to realloc new line", __FUNCTION__);
               return false;
          }

          memmove(new_line + location->x, current_line + location->x + length,
                  current_line_len - (location->x + length));
          new_line[new_line_len] = 0;

          buffer->lines[location->y] = new_line;
          return true;
     }

     // hard case: string spans multiple lines
     int64_t line_index = location->y;

     if(current_line_len){
          // don't delete the rest of the first line yet, we'll do this when we mash the first and last lines
          length -= rest_of_the_line_len + 1; // account for newline
          line_index++;
     }else{
          ce_remove_line(buffer, location->y);
          length--;
     }

     while(length >= 0){
          assert(line_index <= buffer->line_count);
          if(line_index >= buffer->line_count) break;

          char* next_line = buffer->lines[line_index];
          int64_t next_line_len = strlen(next_line);
          if(length >= next_line_len + 1){
               // remove any lines that we have the length to remove completely
               ce_remove_line(buffer, line_index);
               length -= next_line_len + 1;
          }else{
               // slurp up end of first line and beginning of last line
               int64_t next_line_part_len = next_line_len - length;
               int64_t new_line_len = location->x + next_line_part_len;
               char* new_line = current_line = realloc(current_line, new_line_len + 1);
               if(!new_line){
                    ce_message("%s() failed to malloc new line", __FUNCTION__);
                    return false;
               }

               memcpy(new_line + location->x, next_line + length, next_line_part_len);
               new_line[new_line_len] = 0;
               buffer->lines[location->y] = new_line;
               ce_remove_line(buffer, line_index);
               break;
          }
     }

     return true;
}

// NOTE: unused/untested
bool ce_set_line(Buffer* buffer, int64_t line, const char* string)
{
     CE_CHECK_PTR_ARG(buffer);

     if(line < 0 || line >= buffer->line_count){
          ce_message("%s() line %"PRId64" outside buffer with %"PRId64" lines", __FUNCTION__, line, buffer->line_count);
          return false;
     }

     if(buffer->lines[line]){
          free(buffer->lines[line]);
     }

     buffer->lines[line] = strdup(string);
     return true;
}

bool ce_save_buffer(const Buffer* buffer, const char* filename)
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
     return true;
}

bool ce_message(const char* format, ...)
{
     if(!g_message_buffer){
          printf("%s() NULL message buffer\n", __FUNCTION__);
          return false;
     }

     const int64_t max_line_size = 1024;
     char line[max_line_size];

     va_list args;
     va_start(args, format);
     vsnprintf(line, max_line_size, format, args);
     va_end(args);

     return ce_append_line(g_message_buffer, line);
}

bool ce_draw_buffer(const Buffer* buffer, const Point* term_top_left, const Point* term_bottom_right,
                    const Point* buffer_top_left)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(term_top_left);
     CE_CHECK_PTR_ARG(term_bottom_right);
     CE_CHECK_PTR_ARG(buffer_top_left);

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

     char line_to_print[g_terminal_dimensions->x];

     int64_t max_width = (term_bottom_right->x - term_top_left->x) + 1;
     int64_t last_line = buffer_top_left->y + (term_bottom_right->y - term_top_left->y);
     if(last_line >= buffer->line_count) last_line = buffer->line_count - 1;

     static const char* keywords [] = {
          "if",
          "else",
          "for",
          "return",
          "continue",
          "switch",
          "break",
          "enum",
          "while",
          "char",
          "int",
          "bool",
          "const",
          "void",
          "NULL",
          "case",
          "typedef",
          "default",
          "struct",
          "static",
          "inline",
          "volatile",
          "extern",
          "union",
          "sizeof",
          "typeof",
          "do",
          "__thread",
     };

     static int keyword_count = sizeof(keywords) / sizeof(keywords[0]);
     bool inside_multiline_comment = false;

     for(int64_t i = buffer_top_left->y; i <= last_line; ++i) {
          move(term_top_left->y + (i - buffer_top_left->y), term_top_left->x);

          if(!buffer->lines[i][0]) continue;
          const char* buffer_line = buffer->lines[i];
          int64_t line_length = strlen(buffer_line);

          // skip line if we are offset by too much and can't show the line
          if(line_length <= buffer_top_left->x) continue;
          buffer_line += buffer_top_left->x;
          line_length = strlen(buffer_line);

          int64_t min = max_width < line_length ? max_width : line_length;
          memset(line_to_print, 0, min + 1);
          strncpy(line_to_print, buffer_line, min);

          if(inside_multiline_comment) attron(COLOR_PAIR(2));

          bool inside_string = false;
          int64_t highlighting_left = 0;

          if(has_colors() == TRUE){
               for(int64_t c = 0; c < min; ++c){
                    // syntax highlighting
                    {
                         if(highlighting_left == 0){
                              if(!inside_string && !inside_multiline_comment){
                                   for(int64_t k = 0; k < keyword_count; ++k){
                                        int64_t keyword_len = strlen(keywords[k]);
                                        if(strncmp(line_to_print + c, keywords[k], keyword_len) == 0){
                                             char pre_char = 0;
                                             char post_char = line_to_print[c + keyword_len];
                                             if(c > 0) pre_char = line_to_print[c - 1];

                                             if(!isalnum(pre_char) && pre_char != '_' &&
                                                !isalnum(post_char) && post_char != '_'){
                                                  highlighting_left = keyword_len;
                                                  attron(COLOR_PAIR(1));
                                                  break;
                                             }
                                        }
                                   }
                              }

                              if(line_to_print[c] == '/'){
                                   if(line_to_print[c + 1] == '/'){
                                        attron(COLOR_PAIR(2));
                                        highlighting_left = min;
                                   }else if(line_to_print[c + 1] == '*'){
                                        inside_multiline_comment = true;
                                        attron(COLOR_PAIR(2));
                                        highlighting_left = min;
                                   }
                              }else if(inside_multiline_comment && line_to_print[c] == '*' && line_to_print[c + 1] == '/'){
                                   inside_multiline_comment = false;
                                   highlighting_left = 2;
                              }else if(!inside_multiline_comment && (line_to_print[c] == '"' || line_to_print[c] == '\'')){
                                   // NOTE: obviously this doesn't work if a " or ' is inside a string
                                   inside_string = !inside_string;
                                   if(inside_string){
                                        attron(COLOR_PAIR(3));
                                   }else{
                                        highlighting_left = 1;
                                   }
                              }
                         }else{
                              highlighting_left--;
                              if(highlighting_left == 0){
                                   attroff(COLOR_PAIR(1));
                                   attroff(COLOR_PAIR(2));
                                   attroff(COLOR_PAIR(3));
                              }
                         }
                    }

                    // print the character
                    addch(line_to_print[c]);
               }

               standend();
          }else{
               for(int64_t c = 0; c < min; ++c){
                    // print the character
                    addch(line_to_print[c]);
               }
          }
     }

     standend();
     return true;
}

BufferNode* ce_append_buffer_to_list(BufferNode* head, Buffer* buffer)
{
     CE_CHECK_PTR_ARG(head);
     CE_CHECK_PTR_ARG(buffer);

     // find last element
     while(head->next){
          head = head->next;
     }

     BufferNode* new = malloc(sizeof(BufferNode));
     if(!new){
          ce_message("%s() failed to alloc new BufferNode for '%s'", __FUNCTION__, buffer->filename);
          return NULL;
     }

     head->next = new;
     new->buffer = buffer;
     new->next = NULL;

     return new;
}

bool ce_remove_buffer_from_list(BufferNode* head, BufferNode** node)
{
     CE_CHECK_PTR_ARG(head);
     CE_CHECK_PTR_ARG(node);

     BufferNode* tmp = head;
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

// return x delta to the last character in the line, -1 on error
int64_t ce_find_delta_to_end_of_line(const Buffer* buffer, Point* cursor)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(cursor);

     if(!ce_point_on_buffer(buffer, cursor)) return -1;

     const char* line = buffer->lines[cursor->y];
     size_t len = strlen(line);
     return len ? (len - 1) - cursor->x : 0;
}

bool ce_set_cursor(const Buffer* buffer, Point* cursor, const Point* location)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(cursor);
     CE_CHECK_PTR_ARG(location);

     Point dst = *location;

     if(dst.x < 0) dst.x = 0;
     if(dst.y < 0) dst.y = 0;

     if(dst.y >= buffer->line_count) dst.y = buffer->line_count - 1;

     if(buffer->lines[dst.y]){
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
Point* ce_clamp_cursor(const Buffer* buffer, Point* cursor){
     Point clamp = {0, 0};
     ce_move_cursor(buffer, cursor, &clamp);
     return cursor;
}

bool ce_move_cursor(const Buffer* buffer, Point* cursor, const Point* delta)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(cursor);
     CE_CHECK_PTR_ARG(delta);

     Point dst = *cursor;
     dst.x += delta->x;
     dst.y += delta->y;

     if(dst.x < 0) dst.x = 0;
     if(dst.y < 0) dst.y = 0;

     if(dst.y >= buffer->line_count) dst.y = buffer->line_count - 1;

     if(buffer->lines[dst.y]){
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

bool ce_advance_cursor(const Buffer* buffer, Point* cursor, int64_t delta)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(cursor);

     if(!ce_point_on_buffer(buffer, cursor)) return false;

     int64_t line_len = strlen(buffer->lines[cursor->y]);
     int64_t line_len_left = line_len - cursor->x;

     // if the movement fits on this line, go for it
     if(delta < line_len_left){
          cursor->x += delta;
          return true;
     }

     delta -= line_len_left;
     cursor->y++;
     cursor->x = 0;

     while(true){
          if(cursor->y >= buffer->line_count) return ce_move_cursor_to_end_of_file(buffer, cursor);

          line_len = strlen(buffer->lines[cursor->y]);

          if(delta < line_len){
               cursor->x = delta;
               break;
          }

          cursor->y++;
          delta -= line_len;
     }

     return true;
}

bool ce_move_cursor_to_end_of_file(const Buffer* buffer, Point* cursor)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(cursor);

     if(!buffer->line_count) return false;

     int64_t last_line = buffer->line_count - 1;
     int64_t len = strlen(buffer->lines[last_line]);

     cursor->x = len - 1;
     cursor->y = last_line;

     return true;
}

bool ce_move_cursor_to_beginning_of_file(const Buffer* buffer, Point* cursor)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(cursor);

     *cursor = (Point) {0, 0};
     while(!ce_point_on_buffer(buffer, cursor)){
          if(cursor->y >= buffer->line_count) return false;
          cursor->y++;
     }

     return true;
}

// TODO: Threshold for top, left, bottom and right before scrolling happens
bool ce_follow_cursor(const Point* cursor, int64_t* left_column, int64_t* top_row, int64_t view_width, int64_t view_height,
                      bool at_terminal_width_edge, bool at_terminal_height_edge)
{
     CE_CHECK_PTR_ARG(cursor);
     CE_CHECK_PTR_ARG(top_row);

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

     return true;
}

bool ce_commit_insert_char(BufferCommitNode** tail, const Point* start, const Point* undo_cursor, const Point* redo_cursor, char c)
{
     CE_CHECK_PTR_ARG(tail);
     CE_CHECK_PTR_ARG(start);
     CE_CHECK_PTR_ARG(undo_cursor);
     CE_CHECK_PTR_ARG(redo_cursor);

     BufferCommit change;
     change.type = BCT_INSERT_CHAR;
     change.start = *start;
     change.undo_cursor = *undo_cursor;
     change.redo_cursor = *redo_cursor;
     change.c = c;

     return ce_commit_change(tail, &change);
}

bool ce_commit_insert_string(BufferCommitNode** tail, const Point* start, const Point* undo_cursor, const Point* redo_cursor,  char* string)
{
     CE_CHECK_PTR_ARG(tail);
     CE_CHECK_PTR_ARG(start);
     CE_CHECK_PTR_ARG(undo_cursor);
     CE_CHECK_PTR_ARG(redo_cursor);
     CE_CHECK_PTR_ARG(string);

     BufferCommit change;
     change.type = BCT_INSERT_STRING;
     change.start = *start;
     change.undo_cursor = *undo_cursor;
     change.redo_cursor = *redo_cursor;
     change.str = string;

     return ce_commit_change(tail, &change);
}

bool ce_commit_remove_char(BufferCommitNode** tail, const Point* start, const Point* undo_cursor, const Point* redo_cursor, char c)
{
     CE_CHECK_PTR_ARG(tail);
     CE_CHECK_PTR_ARG(start);
     CE_CHECK_PTR_ARG(undo_cursor);
     CE_CHECK_PTR_ARG(redo_cursor);

     BufferCommit change;
     change.type = BCT_REMOVE_CHAR;
     change.start = *start;
     change.undo_cursor = *undo_cursor;
     change.redo_cursor = *redo_cursor;
     change.c = c;

     return ce_commit_change(tail, &change);
}

bool ce_commit_remove_string(BufferCommitNode** tail, const Point* start, const Point* undo_cursor, const Point* redo_cursor,  char* string)
{
     CE_CHECK_PTR_ARG(tail);
     CE_CHECK_PTR_ARG(start);
     CE_CHECK_PTR_ARG(undo_cursor);
     CE_CHECK_PTR_ARG(redo_cursor);
     CE_CHECK_PTR_ARG(string);

     BufferCommit change;
     change.type = BCT_REMOVE_STRING;
     change.start = *start;
     change.undo_cursor = *undo_cursor;
     change.redo_cursor = *redo_cursor;
     change.str = string;

     return ce_commit_change(tail, &change);
}

bool ce_commit_change_char(BufferCommitNode** tail, const Point* start, const Point* undo_cursor, const Point* redo_cursor, char c, char prev_c)
{
     CE_CHECK_PTR_ARG(tail);
     CE_CHECK_PTR_ARG(start);
     CE_CHECK_PTR_ARG(undo_cursor);
     CE_CHECK_PTR_ARG(redo_cursor);

     BufferCommit change;
     change.type = BCT_CHANGE_CHAR;
     change.start = *start;
     change.undo_cursor = *undo_cursor;
     change.redo_cursor = *redo_cursor;
     change.c = c;
     change.prev_c = prev_c;

     return ce_commit_change(tail, &change);
}

bool ce_commit_change_string(BufferCommitNode** tail, const Point* start, const Point* undo_cursor, const Point* redo_cursor, char* new_string,  char* prev_string)
{
     CE_CHECK_PTR_ARG(tail);
     CE_CHECK_PTR_ARG(start);
     CE_CHECK_PTR_ARG(undo_cursor);
     CE_CHECK_PTR_ARG(redo_cursor);
     CE_CHECK_PTR_ARG(new_string);
     CE_CHECK_PTR_ARG(prev_string);

     BufferCommit change;
     change.type = BCT_CHANGE_STRING;
     change.start = *start;
     change.undo_cursor = *undo_cursor;
     change.redo_cursor = *redo_cursor;
     change.str = new_string;
     change.prev_str = prev_string;

     return ce_commit_change(tail, &change);
}

void free_commit(BufferCommitNode* node)
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

bool ce_commit_change(BufferCommitNode** tail, const BufferCommit* commit)
{
     CE_CHECK_PTR_ARG(tail);
     CE_CHECK_PTR_ARG(commit);

     BufferCommitNode* new_node = calloc(1, sizeof(*new_node));
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

bool ce_commits_free(BufferCommitNode* head)
{
     while(head){
          BufferCommitNode* tmp = head;
          head = head->next;
          free_commit(tmp);
     }

     return true;
}

bool ce_commit_undo(Buffer* buffer, BufferCommitNode** tail, Point* cursor)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(tail);
     CE_CHECK_PTR_ARG(cursor);

     if(!*tail){
          ce_message("%s() empty undo history", __FUNCTION__);
          return false;
     }

     BufferCommitNode* undo_commit = *tail;
     BufferCommit* commit = &undo_commit->commit;

     switch(commit->type){
     default:
          ce_message("unsupported BufferCommitType: %d", commit->type);
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
          ce_remove_char(buffer, &commit->start);
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

bool ce_commit_redo(Buffer* buffer, BufferCommitNode** tail, Point* cursor)
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

     BufferCommitNode* undo_commit = *tail;
     BufferCommit* commit = &undo_commit->commit;

     switch(commit->type){
     default:
          ce_message("unsupported BufferCommitType: %d", commit->type);
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
          ce_remove_char(buffer, &commit->start);
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

BufferView* ce_split_view(BufferView* view, BufferNode* buffer_node, bool horizontal)
{
     CE_CHECK_PTR_ARG(view);
     CE_CHECK_PTR_ARG(buffer_node);

     BufferView* itr = view;

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

     BufferView* new_view = calloc(1, sizeof(*new_view));
     if(!new_view){
          ce_message("%s() failed to allocate buffer view", __FUNCTION__);
          return NULL;
     }

     new_view->buffer_node = buffer_node;
     new_view->bottom_right = *g_terminal_dimensions;

     if(buffer_node->buffer) new_view->cursor = buffer_node->buffer->cursor;

     if(horizontal){
          itr->next_horizontal = new_view;
     }else{
          itr->next_vertical = new_view;
     }

     return new_view;
}

BufferView* find_connecting_view(BufferView* start, BufferView* match)
{
     if(start->next_horizontal){
          if(start->next_horizontal == match) return start;
          BufferView* result = find_connecting_view(start->next_horizontal, match);
          if(result) return result;
     }

     if(start->next_vertical){
          if(start->next_vertical == match) return start;
          BufferView* result = find_connecting_view(start->next_vertical, match);
          if(result) return result;
     }

     return NULL;
}

bool ce_remove_view(BufferView** head, BufferView* view)
{
     CE_CHECK_PTR_ARG(head);
     CE_CHECK_PTR_ARG(view);

     BufferView* itr = *head;

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
               BufferView* tmp = *head;
               // loop to the end of vertical's horizontal
               while(tmp->next_horizontal) tmp = tmp->next_horizontal;
               // tack on old head's next_horizontal to the end of new head's last horizontal
               tmp->next_horizontal = itr->next_horizontal;
               free(itr);
          }else{
               ce_message("%s() cannot remove the only existing view", __FUNCTION__);
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
                    BufferView* tmp = view->next_horizontal;
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
                    BufferView* itr = view->next_vertical;
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
bool free_buffer_views(BufferView* head)
{
     CE_CHECK_PTR_ARG(head);

     if(head->next_horizontal) free_buffer_views(head->next_horizontal);
     if(head->next_vertical) free_buffer_views(head->next_vertical);

     free(head);

     return true;
}

bool ce_free_views(BufferView** head)
{
     CE_CHECK_PTR_ARG(head);

     if(!free_buffer_views(*head)){
          return false;
     }

     *head = NULL;
     return true;
}

bool calc_vertical_views(BufferView* view, const Point* top_left, const Point* bottom_right, bool already_calculated);

bool calc_horizontal_views(BufferView* view, const Point* top_left, const Point* bottom_right, bool already_calculated)
{
     int64_t view_count = 0;
     BufferView* itr = view;
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

bool calc_vertical_views(BufferView* view, const Point* top_left, const Point* bottom_right, bool already_calculated)
{
     int64_t view_count = 0;
     BufferView* itr = view;
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

bool ce_calc_views(BufferView* view, const Point *top_left, const Point* bottom_right)
{
     CE_CHECK_PTR_ARG(view);

     return calc_horizontal_views(view, top_left, bottom_right, false);
}

void draw_view_bottom_right_borders(const BufferView* view)
{
     // draw right border
     if(view->bottom_right.x < (g_terminal_dimensions->x - 1)){
          for(int64_t i = view->top_left.y; i <= view->bottom_right.y; ++i){
               move(i, view->bottom_right.x);
               addch(ACS_VLINE);
          }
     }

     // draw bottom border
     // NOTE: accounting for the status bar with -2 here is not what we want going forward.
     //       we need a better way of determining when the view is at the edge of the terminal
     if(view->bottom_right.y < (g_terminal_dimensions->y - 2)){
          move(view->bottom_right.y, view->top_left.x);
          for(int64_t i = view->top_left.x; i <= view->bottom_right.x; ++i){
               move(view->bottom_right.y, i);
               addch(ACS_HLINE);
          }
     }
}

bool draw_vertical_views(const BufferView* view, bool already_drawn);

bool draw_horizontal_views(const BufferView* view, bool already_drawn)
{
     const BufferView* itr = view;
     while(itr){
          // if this is the first view and we haven't already drawn it
          // or if this is any view other than the first view
          // and we have a horizontal view below us, then draw the horizontal views
          if(((!already_drawn && itr == view) || (itr != view)) && itr->next_vertical){
               draw_vertical_views(itr, true);
          }else{
               Point buffer_top_left = {itr->left_column, itr->top_row};
               ce_draw_buffer(itr->buffer_node->buffer, &itr->top_left, &itr->bottom_right, &buffer_top_left);
               draw_view_bottom_right_borders(itr);
          }

          itr = itr->next_horizontal;
     }

     return true;
}

bool draw_vertical_views(const BufferView* view, bool already_drawn)
{
     const BufferView* itr = view;
     while(itr){
          // if this is the first view and we haven't already drawn it
          // or if this is any view other than the first view
          // and we have a vertical view below us, then draw the vertical views
          if(((!already_drawn && itr == view) || (itr != view)) && itr->next_horizontal){
               draw_horizontal_views(itr, true);
          }else{
               Point buffer_top_left = {itr->left_column, itr->top_row};
               ce_draw_buffer(itr->buffer_node->buffer, &itr->top_left, &itr->bottom_right, &buffer_top_left);
               draw_view_bottom_right_borders(itr);
          }

          itr = itr->next_vertical;
     }

     return true;
}

bool connect_at_point(const Point* location)
{
     CE_CHECK_PTR_ARG(location);

     // connect the bottom and right borders based on ruleset
     chtype left = mvinch(location->y, location->x - 1);
     chtype right = mvinch(location->y, location->x + 1);
     chtype top = mvinch(location->y - 1, location->x);
     chtype bottom = mvinch(location->y + 1, location->x);

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

bool connect_borders(const BufferView* view)
{
     CE_CHECK_PTR_ARG(view);

     if(view->next_horizontal) connect_borders(view->next_horizontal);
     if(view->next_vertical) connect_borders(view->next_vertical);

     Point top_left = {view->top_left.x - 1, view->top_left.y - 1};
     Point top_right = {view->bottom_right.x, view->top_left.y - 1};
     Point bottom_right = {view->bottom_right.x, view->bottom_right.y};
     Point bottom_left = {view->top_left.x, view->bottom_right.y};

     return connect_at_point(&top_left) && connect_at_point(&top_right) &&
            connect_at_point(&bottom_right) && connect_at_point(&bottom_left);
}

bool ce_draw_views(const BufferView* view)
{
     CE_CHECK_PTR_ARG(view);

     return draw_horizontal_views(view, false) && connect_borders(view);
}

BufferView* find_view_at_point(BufferView* view, const Point* point)
{
     if(point->x >= view->top_left.x && point->x <= view->bottom_right.x &&
        point->y >= view->top_left.y && point->y <= view->bottom_right.y){
          return view;
     }

     BufferView* result = NULL;

     if(view->next_horizontal){
          result = find_view_at_point(view->next_horizontal, point);
     }

     if(!result && view->next_vertical){
          result = find_view_at_point(view->next_vertical, point);
     }

     return result;
}

BufferView* ce_find_view_at_point(BufferView* head, const Point* point)
{
     CE_CHECK_PTR_ARG(head);
     CE_CHECK_PTR_ARG(point);

     return find_view_at_point(head, point);
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

int64_t ce_compute_length(Buffer* buffer, Point start, Point end)
{
     CE_CHECK_PTR_ARG(buffer);

     assert(ce_point_on_buffer(buffer, &start) || start.x == (int64_t)strlen(buffer->lines[start.y]) + 1); // account for newline
     assert(ce_point_on_buffer(buffer, &end) || end.x == (int64_t)strlen(buffer->lines[end.y]) + 1); // account for newline

     ce_sort_points(&start, &end);

     size_t length = 0;

     if( start.y < end.y){
          length = strlen(buffer->lines[start.y] + start.x) + 1; // account for newline
          for(int64_t i = start.y + 1; i < end.y; ++i){
               length += strlen(buffer->lines[i]) + 1; // account for newline
          }
          length += end.x; // do not account for newline
     }else{
          assert(start.y == end.y);
          length += end.x - start.x;
     }

     return length;
}

int ce_iswordchar(int c)
{
     return !isblank(c) && !ce_ispunct(c);
}

// given a buffer, two points, and a function ptr, return a range of characters that match defined criteria
// NOTE: start is inclusive, end is exclusive
bool ce_get_homogenous_adjacents(Buffer* buffer, Point* start, Point* end, int (*is_homogenous)(int))
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

     // the last character wasn't homogenous, but end is exclusive

     return true;
}

// if a > b, swap a and b
void ce_sort_points(Point* a, Point* b)
{
     if(b->y < a->y || (b->y == a->y && b->x < a->x)){
          Point temp = *a;
          *a = *b;
          *b = temp;
     }
}
