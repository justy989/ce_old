#define _GNU_SOURCE
#include "ce.h"
#include <ctype.h>
#include <string.h>
#include <inttypes.h>

Buffer* g_message_buffer = NULL;
Point* g_terminal_dimensions = NULL;

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
          buffer->lines[i] = NULL;
     }

     return true;
}

void ce_load_string(Buffer* buffer, const char* str){
     // TODO: merge with ce_insert_string
     // count lines
     size_t str_size = strlen(str);
     int64_t line_count = 1;
     for(size_t i = 0; i < str_size; ++i){
          if(str[i] != NEWLINE) continue;

          line_count++;
     }

     if(!line_count && str_size) line_count = 1;

     if(line_count){
          ce_alloc_lines(buffer, line_count);
          int64_t last_newline = -1;
          for(int64_t i = 0, l = 0; i < (int64_t)(str_size); ++i){
               if(str[i] != NEWLINE) continue;

               int64_t length = (i - 1) - last_newline;
               if(length){
                    char* new_line = malloc(length + 1);
                    strncpy(new_line, str + last_newline + 1, length);
                    new_line[length] = 0;
                    buffer->lines[l] = new_line;
               }
               last_newline = i;
               l++;
          }

          int64_t length = str_size - last_newline;
          if(length > 1){
               char* new_line = malloc(length + 1);
               strncpy(new_line, str + last_newline + 1, length);
               new_line[length] = 0;
               if(new_line[length - 1] == NEWLINE){
                    new_line[length - 1] = 0;
               }
               buffer->lines[line_count - 1] = new_line;
          }
     }
}

bool ce_load_file(Buffer* buffer, const char* filename)
{
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

          ce_load_string(buffer, contents);

          fclose(file);

          buffer->filename = strdup(filename);
     }


     free(contents);

     ce_message("loaded file '%s'", filename);

     return true;
}

void ce_free_buffer(Buffer* buffer)
{
     if(!buffer){
          return;
     }

     free(buffer->filename);

     if(buffer->lines){
          for(int64_t i = 0; i < buffer->line_count; ++i){
               if(buffer->lines[i]){
                    free(buffer->lines[i]);
               }
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

     if(c == NEWLINE) return ce_insert_line(buffer, location->y, NULL);

     char* line = buffer->lines[location->y];
     char* new_line = NULL;
     if(line){
          // allocate new line with length + 1 + NULL terminator
          int64_t line_len = strlen(line);
          int64_t new_len = line_len + 2;
          new_line = malloc(new_len);
          if(!new_line){
               ce_message("%s() failed to allocate line with %"PRId64" characters", __FUNCTION__, new_len);
               return false;
          }

          // copy before the insert, add the new char, copy after the insert
          for(int64_t i = 0; i < location->x; ++i){new_line[i] = line[i];}
          new_line[location->x] = c;
          for(int64_t i = location->x; i < line_len; ++i){new_line[i+1] = line[i];}

          // NULL terminate the newline, and free the old line
          new_line[new_len - 1] = 0;
          free(line);
     }else{
          new_line = malloc(2);
          if(!new_line){
               ce_message("%s() failed to allocate line with 2 characters", __FUNCTION__);
               return false;
          }
          new_line[0] = c;
          new_line[1] = 0;
     }

     buffer->lines[location->y] = new_line;
     return true;
}

bool ce_insert_string(Buffer* buffer, const Point* location, const char* string)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);
     CE_CHECK_PTR_ARG(string);

     if(!ce_point_on_buffer(buffer, location)){
          return false;
     }

#if 0
     char* current_line = buffer->lines[location->y];
     if(!current_line){

     }

     int64_t new_string_length = strlen(string);
     const char* first_part = current_line;
     const char* second_part = current_line + location->x;

     if(location->x == 0) first_part = NULL;
     if(location->x >= string_length) second_part = NULL;

     int64_t string_length = strlen(string);
     int64_t first_length = first_part ? strlen(first_part) : 0;
     int64_t second_length = second_part ? strlen(second_part) : 0;

     // find the first line
     const char* itr = string;
     const char* end_of_line = string;
     while(*end_of_line != NEWLINE && *end_of_line != 0) end_of_line++;

     if(*end_of_line == 0){
          // we are only adding a single line
          int64_t new_line_length = new_string_length + first_length + second_length;
          char* new_line = malloc();

          if(first_part) strncpy();
     }else{

     }

     return true;
#endif
     ce_message("unimplemented");
     return false;
}

bool ce_remove_char(Buffer* buffer, const Point* location)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);

     if(!ce_point_on_buffer(buffer, location)) return false;

     char* line = buffer->lines[location->y];
     if(!line){
          ce_message("%s() cannot remove character from empty line", __FUNCTION__);
          return false;
     }

     int64_t line_len = strlen(line);

     // if we want to delete the only character on the line, just clear the whole line
     if(line_len == 1){
          free(line);
          buffer->lines[location->y] = NULL;
          return true;
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

// return x delta between location and the located character 'c' if found. return -1 if not found
int64_t ce_find_char_forward_in_line(Buffer* buffer, const Point* location, char c)
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
int64_t ce_find_char_backward_in_line(Buffer* buffer, const Point* location, char c)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);

     // TODO do I need to validate that the provided location is a real character in the buffer?
     const char* cur_char = &buffer->lines[location->y][location->x];
     const char* line = buffer->lines[location->y];
     if(!line) return -1; // TODO is this possible?
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

// returns the delta to the matching string; return success
bool ce_find_str(Buffer* buffer, const Point* location, const char* search_str, Point* delta)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);
     CE_CHECK_PTR_ARG(search_str);
     CE_CHECK_PTR_ARG(delta);

     Direction d = CE_DOWN; // TODO support reverse search

     int64_t line = location->y;
     const char* line_str = buffer->lines[location->y];
     if(line_str) line_str = &line_str[location->x + 1];

     int64_t n_lines = (d == CE_UP) ? location->y : buffer->line_count - location->y;
     for(int64_t i = 0; i < n_lines;){
          const char* match = strstr(line_str, search_str);
          if(match){
               delta->x = (match - line_str) - location->x;
               delta->y = line - location->y;
               return true;
          }
          do i++;
          while(!(line_str = buffer->lines[line += d]) && i < n_lines);
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
bool ce_ispunct(int c){
     return c != '_' && ispunct(c);
}

// return -1 on failure, delta to move left to the beginning of the word on success
int64_t ce_find_beginning_of_word(Buffer* buffer, const Point* location, bool punctuation_word_boundaries)
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
int64_t ce_find_end_of_word(Buffer* buffer, const Point* location, bool punctuation_word_boundaries)
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

     int64_t delta = ce_find_end_of_word(buffer, location, punctuation_word_boundaries);
     if(delta == -1) return -1;
     const char* line = buffer->lines[location->y];
     int line_len = strlen(line);
     int cur_x = location->x + delta;
     if(cur_x + 1 < line_len){
          do{
               // churn through all whitespace following end of word
               cur_x++;
          } while(isblank(line[cur_x]) && (cur_x+1 < line_len));
     }
     else if(cur_x + 1 == line_len){
          cur_x++;
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

     buffer->lines[location->y][location->x] = c;

     return true;
}

// NOTE: passing NULL to string causes an empty line to be inserted
bool ce_insert_line(Buffer* buffer, int64_t line, const char* string)
{
     CE_CHECK_PTR_ARG(buffer);

     int64_t new_line_count = buffer->line_count + 1;
     char** new_lines = malloc(new_line_count * sizeof(char*));
     if(!new_lines){
          if(buffer == g_message_buffer){
               printf("%s() failed to malloc new lines: %"PRId64"\n", __FUNCTION__, new_line_count);
          }else{
               ce_message("%s() failed to malloc new lines: %"PRId64"", __FUNCTION__, new_line_count);
          }
          return false;
     }

     // copy up to new empty line, add empty line, copy the rest in the buffer
     for(int64_t i = 0; i < line; ++i) new_lines[i] = buffer->lines[i];
     if(string){
          new_lines[line] = strdup(string);
     }else{
          new_lines[line] = NULL;
     }
     for(int64_t i = line; i < buffer->line_count; ++i) new_lines[i + 1] = buffer->lines[i];

     // free and update the buffer ptr
     free(buffer->lines);
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

bool ce_remove_line(Buffer* buffer, int64_t line)
{
     CE_CHECK_PTR_ARG(buffer);

     int64_t new_line_count = buffer->line_count - 1;
     char** new_lines = malloc(new_line_count * sizeof(char*));
     if(!new_lines){
          ce_message("%s() failed to malloc new lines: %"PRId64"", __FUNCTION__, new_line_count);
          return -1;
     }

     // copy up to deleted line, copy the rest in the buffer
     for(int64_t i = 0; i < line; ++i) new_lines[i] = buffer->lines[i];
     for(int64_t i = line; i < new_line_count; ++i) new_lines[i] = buffer->lines[i + 1];

     // free and update the buffer ptr
     free(buffer->lines);
     buffer->lines = new_lines;
     buffer->line_count = new_line_count;

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

     if(term_top_left->x >= term_bottom_right->x){
          ce_message("%s() top_left's x (%"PRId64") must be lower than bottom_right's x(%"PRId64")", __FUNCTION__,
                     term_top_left->x, term_bottom_right->x);
          return false;
     }

     if(term_top_left->y >= term_bottom_right->y){
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

     int64_t max_width = term_bottom_right->x - term_top_left->x;
     int64_t last_line = buffer_top_left->y + (term_bottom_right->y - term_top_left->y) - 1;
     if(last_line >= buffer->line_count) last_line = buffer->line_count - 1;

     for(int64_t i = buffer_top_left->y; i <= last_line; ++i) {
          move(term_top_left->y + (i - buffer_top_left->y), term_top_left->x);

          if(!buffer->lines[i]) continue;
          const char* buffer_line = buffer->lines[i];
          int64_t line_length = strlen(buffer_line);

          // skip line if we are offset by too much and can't show the line
          if(line_length <= buffer_top_left->x) continue;
          buffer_line += buffer_top_left->x;
          line_length = strlen(buffer_line);

          int64_t min = max_width < line_length ? max_width : line_length;
          memset(line_to_print, 0, min + 1);
          strncpy(line_to_print, buffer_line, min);
          addstr(line_to_print); // NOTE: use addstr() rather than printw() because it could contain format specifiers
     }

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
int64_t ce_find_end_of_line(const Buffer* buffer, Point* cursor)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(cursor);

     if(!ce_point_on_buffer(buffer, cursor)) return -1;

     const char* line = buffer->lines[cursor->y];
     return (strlen(line) - 1) - cursor->x;
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

bool ce_follow_cursor(const Point* cursor, int64_t* top_line, int64_t* left_column, int64_t view_height, int64_t view_width)
{
     CE_CHECK_PTR_ARG(cursor);
     CE_CHECK_PTR_ARG(top_line);

     view_height--;
     view_width--;

     int64_t bottom_line = *top_line + view_height;

     if(cursor->y < *top_line){
          *top_line = cursor->y;
     }else if(cursor->y > bottom_line){
          bottom_line = cursor->y;
          *top_line = bottom_line - view_height;
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

bool ce_buffer_change(BufferChangeNode** tail, const BufferChange* change)
{
     CE_CHECK_PTR_ARG(tail);
     CE_CHECK_PTR_ARG(change);

     BufferChangeNode* new_change = malloc(sizeof(*new_change));
     if(!new_change){
          ce_message("%s() failed to allocate new change", __FUNCTION__);
          return false;
     }

     new_change->change = *change;
     new_change->prev = *tail;
     *tail = new_change;

     return true;
}

bool ce_buffer_undo(Buffer* buffer, BufferChangeNode** tail)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(tail);

     if(!*tail){
          ce_message("%s() empty undo history", __FUNCTION__);
          return false;
     }

     BufferChangeNode* undo_change = *tail;
     BufferChange* change = &undo_change->change;

     *tail = (*tail)->prev;

     if(change->insertion){
          // TODO: create api to remove a range of characters
          for(int64_t i = 0; i < change->length; ++i){
               ce_remove_char(buffer, &change->start);
          }
     }else{
          ce_insert_char(buffer, &change->start, change->c);
     }

     free(undo_change);
     return true;
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
