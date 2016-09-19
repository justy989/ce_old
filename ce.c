#include "ce.h"

Buffer* g_message_buffer = NULL;
Point* g_terminal_dimensions = NULL;

bool ce_alloc_lines(Buffer* buffer, int64_t line_count)
{
     CE_CHECK_PTR_ARG(buffer);

     if(line_count <= 0){
          ce_message("%s() tried to allocate %ld lines for a buffer, but we can only allocated > 0 lines", line_count);
          return false;
     }

     buffer->lines = malloc(line_count * sizeof(char*));
     if(!buffer->lines){
          ce_message("%s() failed to allocate %ld lines for buffer", line_count);
          return false;
     }

     buffer->line_count = line_count;

     // clear the lines
     for(int64_t i = 0; i < line_count; ++i){
          buffer->lines[i] = NULL;
     }

     return true;
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

          fclose(file);

          buffer->filename = strdup(filename);
     }

     // count lines
     int64_t line_count = 0;
     for(size_t i = 0; i < content_size; ++i){
          if(contents[i] != NEWLINE) continue;

          line_count++;
     }

     // TODO: use ce_load_string
     if(line_count){
          ce_alloc_lines(buffer, line_count);
          int64_t last_newline = -1;
          for(int64_t i = 0, l = 0; i < (int64_t)(content_size); ++i){
               if(contents[i] != NEWLINE) continue;

               int64_t length = (i - 1) - last_newline;
               if(length){
                    char* new_line = malloc(length + 1);
                    strncpy(new_line, contents + last_newline + 1, length);
                    new_line[length] = 0;
                    buffer->lines[l] = new_line;
               }
               last_newline = i;
               l++;
          }

          int64_t length = content_size - last_newline;
          if(length > 1){
               char* new_line = malloc(length + 1);
               strncpy(new_line, contents + last_newline + 1, length);
               new_line[length] = 0;
               if(new_line[length - 1] == NEWLINE){
                    new_line[length - 1] = 0;
               }
               buffer->lines[line_count - 1] = new_line;
          }
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
          ce_message("%s() %ld, %ld not in buffer", __FUNCTION__, location->x, location->y);
          return false;
     }

     if(location->y >= buffer->line_count){
          ce_message("%s() %ld, %ld not in buffer with %ld lines",
                     __FUNCTION__, location->x, location->y, buffer->line_count);
          return false;
     }

     char* line = buffer->lines[location->y];
     int64_t line_len = 0;

     if(line) line_len = strlen(line);

     if(location->x > line_len){
          ce_message("%s() %ld, %ld not in buffer with line %ld only %ld characters long",
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
               ce_message("%s() failed to allocate line with %ld characters", __FUNCTION__, new_len);
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

bool ce_insert_string(Buffer* buffer, const Point* location, const char* new_string)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);
     CE_CHECK_PTR_ARG(new_string);

     if(!ce_point_on_buffer(buffer, location)){
          return false;
     }

     int64_t new_string_length = strlen(new_string);

     if(new_string_length == 0){
          ce_message("%s() failed to insert empty string", __FUNCTION__);
          return false;
     }

     if(!buffer->lines[location->y]){
          char* new_line = malloc(new_string_length + 1);
          if(!new_line){
               ce_message("%s() failed to allocate new string", __FUNCTION__);
               return false;
          }

          strncpy(new_line, new_string, new_string_length);
          new_line[new_string_length] = 0;

          buffer->lines[location->y] = new_line;
          // TODO: handle multiple lines
          return true;
     }

     char* current_line = buffer->lines[location->y];
     int64_t current_line_length = strlen(current_line);
     const char* first_part = current_line;
     const char* second_part = current_line + location->x;

     if(location->x == 0) first_part = NULL;
     if(location->x >= current_line_length) second_part = NULL;

     int64_t first_length = location->x;
     int64_t second_length = second_part ? strlen(second_part) : 0;

     // find the first line range
     const char* end_of_line = new_string;
     while(*end_of_line != NEWLINE && *end_of_line != 0) end_of_line++;

     // if the string they want to insert does NOT contain any newlines
     if(*end_of_line == 0){
          // we are only adding a single line, so include all the pieces
          int64_t new_line_length = new_string_length + first_length + second_length;
          char* new_line = malloc(new_line_length + 1);
          if(!new_line){
               ce_message("%s() failed to allocate new string", __FUNCTION__);
               return false;
          }

          if(first_part) strncpy(new_line, first_part, first_length);
          strncpy(new_line + first_length, new_string, new_string_length);
          if(second_part) strncpy(new_line + first_length + new_string_length, second_part, second_length);

          new_line[new_line_length] = 0;

          buffer->lines[location->y] = new_line;
     }else{
          // include the first part and the string up to the newline
          const char* itr = new_string;
          int64_t first_new_line_length = end_of_line - itr;
          int64_t new_line_length = first_new_line_length + first_length;

          char* new_line = malloc(new_line_length + 1);
          if(!new_line){
               ce_message("%s() failed to allocate new string", __FUNCTION__);
               return false;
          }

          if(first_part) strncpy(new_line, first_part, first_length);
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
                    if(second_part) strncpy(new_line + next_line_length, second_part, second_length);
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
     }

     free(current_line);
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

char* ce_dupe_string(Buffer* buffer, const Point* start, const Point* end)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(start);
     CE_CHECK_PTR_ARG(end);

     if(!ce_point_on_buffer(buffer, start)) return NULL;
     if(!ce_point_on_buffer(buffer, end)) return NULL;

     if(start->y > end->y){
          ce_message("%s() start(%ld, %ld) needs to be below end(%ld, %ld)",
                     __FUNCTION__, start->x, start->y, end->x, end->y);
          return NULL;
     }

     if(start->y == end->y){
          if(start->x >= end->x){
               ce_message("%s() start(%ld, %ld) needs to be below end(%ld, %ld)",
                          __FUNCTION__, start->x, start->y, end->x, end->y);
               return NULL;
          }

          // single line allocation
          int64_t len = end->x - start->x;
          char* new_str = malloc(len + 1);
          if(!new_str){
               ce_message("%s() failed to alloc string", __FUNCTION__);
               return NULL;
          }
          strncpy(new_str, buffer->lines[start->y] + start->x, len);
          new_str[len] = 0;

          return new_str;
     }

     // multi line allocation

     // count total length
     int64_t len = 1; // account for newline
     if(buffer->lines[start->y]) len += strlen(buffer->lines[start->y] + start->x);
     for(int64_t i = start->y + 1; i < end->y; ++i){
          if(buffer->lines[i]) len += strlen(buffer->lines[i]);
          len++; // account for newline
     }
     len += end->x; // do not account for newline

     // build string
     char* new_str = malloc(len + 1);
     if(!new_str){
          ce_message("%s() failed to alloc string", __FUNCTION__);
          return NULL;
     }

     char* itr = new_str;
     if(buffer->lines[start->y]){
          int64_t len = strlen(buffer->lines[start->y] + start->x);
          strncpy(itr, buffer->lines[start->y] + start->x, len);
          itr[len] = '\n'; // add newline
          itr += len + 1;
     }

     for(int64_t i = start->y + 1; i < end->y; ++i){
          if(buffer->lines[i]){
               int64_t len = strlen(buffer->lines[i]);
               strncpy(itr, buffer->lines[i], len);
               itr[len] = '\n';
               itr += len + 1;
          }
     }

     strncpy(itr, buffer->lines[end->y], end->x);
     new_str[len] = 0;

     return new_str;
}

char* ce_dupe_line(Buffer* buffer, int64_t line)
{
     Point start = {0, line};
     int64_t line_len = 0;
     if(buffer->lines[line]) line_len = strlen(buffer->lines[line]);
     Point end = {line_len, line};
     return ce_dupe_string(buffer, &start, &end);
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
               printf("%s() failed to malloc new lines: %ld\n", __FUNCTION__, new_line_count);
          }else{
               ce_message("%s() failed to malloc new lines: %ld", __FUNCTION__, new_line_count);
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

     if(line >= buffer->line_count){
          ce_message("%s() specified line %d ouside of buffer, which has %d lines", line, buffer->line_count);
          return false;
     }

     int64_t new_line_count = buffer->line_count - 1;
     char** new_lines = malloc(new_line_count * sizeof(char*));
     if(!new_lines){
          ce_message("%s() failed to malloc new lines: %ld", __FUNCTION__, new_line_count);
          return false;
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

bool ce_remove_string(Buffer* buffer, const Point* location, int64_t length)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);

     // TODO: should this return false and not do anything if we try to remove
     //       a string longer than the size of the rest of the buffer?

     if(!ce_point_on_buffer(buffer, location)) return false;

     char* current_line = buffer->lines[location->y];
     int64_t current_line_len = 0;

     if(current_line) current_line_len = strlen(current_line);
     int64_t rest_of_the_line_len = (current_line_len - location->x);

     if(length <= rest_of_the_line_len){
          int64_t new_line_len = current_line_len - length;
          char* new_line = malloc(new_line_len + 1);
          if(!new_line){
               ce_message("%s() failed to malloc new line", __FUNCTION__);
               return false;
          }

          strncpy(new_line, current_line, location->x);
          strncpy(new_line + location->x, current_line + location->x + length,
                  current_line_len - (location->x + length));
          new_line[new_line_len] = 0;

          buffer->lines[location->y] = new_line;
          free(current_line);
     }else{
          int64_t line_index = location->y;

          if(current_line){
               length -= rest_of_the_line_len;
               line_index++;
          }else{
               // removing has to be counted as 1 in length due to 'newline' character's being used
               // to insert strings. They need to be symmetrical
               ce_remove_line(buffer, location->y);
          }

          length--; // account for line newline at the end of lines that doesn't physically exist

          while(length >= 0){
               if(line_index >= buffer->line_count) break;

               char* next_line = buffer->lines[line_index];
               int64_t next_line_len = 0;
               if(next_line) next_line_len = strlen(next_line);
               if(length >= next_line_len){
                    // remove any lines that we have the length to remove completely
                    ce_remove_line(buffer, line_index);
                    if(next_line_len != 0){
                         length -= next_line_len;
                    }
                    length--; // account for line newline at the end of lines that doesn't physically exist
               }else{
                    int64_t next_line_part_len = next_line_len - length;
                    int64_t new_line_len = location->x + next_line_part_len;
                    char* new_line = malloc(new_line_len + 1);
                    if(!new_line){
                         ce_message("%s() failed to malloc new line", __FUNCTION__);
                         return false;
                    }

                    strncpy(new_line, current_line, location->x);
                    strncpy(new_line + location->x, next_line + length, next_line_part_len);
                    new_line[new_line_len] = 0;
                    buffer->lines[location->y] = new_line;
                    ce_remove_line(buffer, line_index);
                    if(next_line) free(next_line);
                    length = -1;
               }
          }
     }

     return true;
}

// NOTE: unused/untested
bool ce_set_line(Buffer* buffer, int64_t line, const char* string)
{
     CE_CHECK_PTR_ARG(buffer);

     if(line < 0 || line >= buffer->line_count){
          ce_message("%s() line %ld outside buffer with %ld lines", __FUNCTION__, line, buffer->line_count);
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
          ce_message("%s() top_left's x (%d) must be lower than bottom_right's x(%d)", __FUNCTION__,
                     term_top_left->x, term_bottom_right->x);
          return false;
     }

     if(term_top_left->y >= term_bottom_right->y){
          ce_message("%s() top_left's y (%d) must be lower than bottom_right's y(%d)", __FUNCTION__,
                     term_top_left->y, term_bottom_right->y);
          return false;
     }

     if(term_top_left->x < 0){
          ce_message("%s() top_left's x(%d) must be greater than 0", __FUNCTION__, term_top_left->x);
          return false;
     }

     if(term_top_left->y < 0){
          ce_message("%s() top_left's y(%d) must be greater than 0", __FUNCTION__, term_top_left->y);
          return false;
     }

     if(term_bottom_right->x >= g_terminal_dimensions->x){
          ce_message("%s() bottom_right's x(%d) must be less than the terminal dimensions x(%d)", __FUNCTION__,
                     term_bottom_right->x, g_terminal_dimensions->x);
          return false;
     }

     if(term_bottom_right->y >= g_terminal_dimensions->y){
          ce_message("%s() bottom_right's y(%d) must be less than the terminal dimensions y(%d)", __FUNCTION__,
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

// TODO: Threshold for top, left, bottom and right before scrolling happens
bool ce_follow_cursor(const Point* cursor, int64_t* top_row, int64_t* left_collumn, int64_t view_height, int64_t view_width)
{
     CE_CHECK_PTR_ARG(cursor);
     CE_CHECK_PTR_ARG(top_row);

     view_height--;
     view_width--;

     int64_t bottom_row = *top_row + view_height;

     if(cursor->y < *top_row){
          *top_row = cursor->y;
     }else if(cursor->y > bottom_row){
          bottom_row = cursor->y;
          *top_row = bottom_row - view_height;
     }

     int64_t right_collumn = *left_collumn + view_width;

     if(cursor->x < *left_collumn){
          *left_collumn = cursor->x;
     }else if(cursor->x > right_collumn){
          right_collumn = cursor->x;
          *left_collumn = right_collumn - view_width;
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
     new_change->next = NULL;

     // TODO: rather than branching, just free rest of the redo list on this change
     if(*tail){
          if((*tail)->next){
               BufferChangeNode* itr = (*tail)->next;

               while(itr){
                    BufferChangeNode* tmp = itr;
                    itr = itr->next;
                    if(tmp->change.type == BCT_INSERT_STRING ||
                       tmp->change.type == BCT_REMOVE_STRING){
                         if(tmp->change.str) free(tmp->change.str);
                    }else if(tmp->change.type == BCT_CHANGE_STRING){
                         if(tmp->change.str) free(tmp->change.str);
                         if(tmp->change.changed_str) free(tmp->change.changed_str);
                    }
                    free(tmp);
               }

               (*tail)->next = NULL;
          }

          (*tail)->next = new_change;
     }

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

     switch(change->type){
     default:
          ce_message("unsupported BufferChangeType: %d", change->type);
          return false;
     case BCT_NONE:
          ce_message("%s() empty undo history", __FUNCTION__);
          return false;
     case BCT_INSERT_CHAR:
          ce_remove_char(buffer, &change->start);
          break;
     case BCT_INSERT_STRING:
          ce_remove_string(buffer, &change->start, strlen(change->str));
          break;
     case BCT_REMOVE_CHAR:
          ce_insert_char(buffer, &change->start, change->c);
          break;
     case BCT_REMOVE_STRING:
          ce_insert_string(buffer, &change->start, change->str);
          break;
     case BCT_CHANGE_CHAR:
          ce_set_char(buffer, &change->start, change->c);
          break;
     case BCT_CHANGE_STRING:
          ce_remove_string(buffer, &change->start, strlen(change->str));
          ce_insert_string(buffer, &change->start, change->changed_str);
          break;
     }

     *tail = (*tail)->prev;

     return true;
}

bool ce_buffer_redo(Buffer* buffer, BufferChangeNode** tail)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(tail);

     if(!*tail){
          ce_message("%s() empty redo history", __FUNCTION__);
          return false;
     }

     if(!(*tail)->next){
          ce_message("%s() empty redo history", __FUNCTION__);
          return false;
     }

     *tail = (*tail)->next;

     BufferChangeNode* undo_change = *tail;
     BufferChange* change = &undo_change->change;

     switch(change->type){
     default:
          ce_message("unsupported BufferChangeType: %d", change->type);
          return false;
     case BCT_INSERT_CHAR:
          ce_insert_char(buffer, &change->start, change->c);
          break;
     case BCT_INSERT_STRING:
          ce_insert_string(buffer, &change->start, change->str);
          break;
     case BCT_REMOVE_CHAR:
          ce_remove_char(buffer, &change->start);
          break;
     case BCT_REMOVE_STRING:
          ce_remove_string(buffer, &change->start, strlen(change->str));
          break;
     case BCT_CHANGE_CHAR:
          ce_set_char(buffer, &change->start, change->changed_c);
          break;
     case BCT_CHANGE_STRING:
          ce_remove_string(buffer, &change->start, strlen(change->changed_str));
          ce_insert_string(buffer, &change->start, change->str);
          break;
     }

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

     BufferView* new_view = malloc(sizeof(*new_view));
     if(!new_view){
          ce_message("%s() failed to allocate buffer view", __FUNCTION__);
          return NULL;
     }

     memset(new_view, 0, sizeof(*new_view));
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

bool ce_remove_view(BufferView* head, BufferView* view)
{
     CE_CHECK_PTR_ARG(head);
     CE_CHECK_PTR_ARG(view);

     BufferView* itr = find_connecting_view(head, view);
     if(!itr){
          ce_message("%s() failed to remove unconnected view", __FUNCTION__);
          return false;
     }

     if(itr->next_vertical == view){
          if(view->next_horizontal){
               itr->next_vertical = view->next_horizontal;
               // NOTE: This is totally not what we want going forward, however,
               //       until we implement a more complicated window system, this
               //       let's us not lose track of windows
               // bandage up the windows !
               if(view->next_vertical){
                    BufferView* itr = view->next_horizontal;
                    while(itr->next_vertical) itr = itr->next_vertical;
                    itr->next_vertical = view->next_vertical;
               }
          }else if(view->next_vertical){
               itr->next_vertical = view->next_vertical;
          }else{
               itr->next_vertical = NULL;
          }

          free(view);
     }else{
          // TODO: assert(itr->next_horizontal == view)
          if(view->next_vertical){
               itr->next_horizontal = view->next_vertical;
               if(view->next_horizontal){
                    BufferView* itr = view->next_vertical;
                    while(itr->next_horizontal) itr = itr->next_horizontal;
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

bool calc_vertical_views(BufferView* view, const Point* top_left, const Point* bottom_right, bool first_calc);

bool calc_horizontal_views(BufferView* view, const Point* top_left, const Point* bottom_right, bool first_calc)
{
     int64_t view_count = 0;
     BufferView* itr = view;
     while(itr){
          itr = itr->next_horizontal;
          view_count++;
     }

     int64_t shift = (bottom_right->x - top_left->x) / view_count;
     Point new_top_left = *top_left;
     Point new_bottom_right = *bottom_right;
     new_bottom_right.x = new_top_left.x + shift;
     if(new_bottom_right.x >= g_terminal_dimensions->x) new_bottom_right.x = g_terminal_dimensions->x - 1;

     itr = view;
     while(itr){
          itr->top_left = new_top_left;
          itr->bottom_right = new_bottom_right;
          if(!first_calc && itr == view && itr->next_vertical){
               calc_vertical_views(itr, &new_top_left, &new_bottom_right, true);
          }else if(itr != view && itr->next_vertical){
               calc_vertical_views(itr, &new_top_left, &new_bottom_right, true);
          }

          if(itr->next_horizontal){
               new_top_left.x++;
          }

          new_top_left.x += shift;
          new_bottom_right.x = new_top_left.x + shift;
          if(new_bottom_right.x >= g_terminal_dimensions->x) new_bottom_right.x = g_terminal_dimensions->x - 1;
          itr = itr->next_horizontal;
     }

     return true;
}

bool calc_vertical_views(BufferView* view, const Point* top_left, const Point* bottom_right, bool first_calc)
{
     int64_t view_count = 0;
     BufferView* itr = view;
     while(itr){
          itr = itr->next_vertical;
          view_count++;
     }

     int64_t shift = (bottom_right->y - top_left->y) / view_count;
     Point new_top_left = *top_left;
     Point new_bottom_right = *bottom_right;
     new_bottom_right.y = new_top_left.y + shift;
     if(new_bottom_right.y >= g_terminal_dimensions->y) new_bottom_right.y = g_terminal_dimensions->y - 1;

     itr = view;
     while(itr){
          itr->top_left = new_top_left;
          itr->bottom_right = new_bottom_right;
          if(!first_calc && itr == view && itr->next_horizontal){
               calc_horizontal_views(itr, &new_top_left, &new_bottom_right, true);
          }else if(itr != view && itr->next_horizontal){
               calc_horizontal_views(itr, &new_top_left, &new_bottom_right, true);
          }

          if(itr->next_vertical){
               new_top_left.y++;
          }

          new_top_left.y += shift;
          new_bottom_right.y = new_top_left.y + shift;
          if(new_bottom_right.y >= g_terminal_dimensions->y) new_bottom_right.y = g_terminal_dimensions->y - 1;
          itr = itr->next_vertical;
     }

     return true;
}

bool ce_calc_views(BufferView* view)
{
     CE_CHECK_PTR_ARG(view);

     Point top_left = {0, 0};
     Point bottom_right = {g_terminal_dimensions->x - 1, g_terminal_dimensions->y - 1};
     return calc_horizontal_views(view, &top_left, &bottom_right, false);
}

bool draw_vertical_views(const BufferView* view, bool first_drawn);

bool draw_horizontal_views(const BufferView* view, bool first_drawn)
{
     const BufferView* itr = view;
     while(itr){
          if(!first_drawn && itr == view && itr->next_vertical){
               draw_vertical_views(itr, true);
          }else if(itr != view && itr->next_vertical){
               draw_vertical_views(itr, true);
          }else{
               Point buffer_top_left = {itr->left_collumn, itr->top_row};
               ce_draw_buffer(itr->buffer_node->buffer, &itr->top_left, &itr->bottom_right, &buffer_top_left);
          }

          if(itr->next_horizontal){
               for(int64_t i = itr->top_left.y; i <= itr->bottom_right.y; ++i){
                    move(i, itr->bottom_right.x);
                    addch('|'); // TODO: make configurable
               }
          }

          itr = itr->next_horizontal;
     }

     return true;
}

bool draw_vertical_views(const BufferView* view, bool first_drawn)
{
     int64_t width = view->bottom_right.x - view->top_left.x;

     char separators[width+1];
     for(int i = 0; i < width; ++i) separators[i] = '-'; // TODO: make configurable
     separators[width] = 0;

     const BufferView* itr = view;
     while(itr){
          if(!first_drawn && itr == view && itr->next_horizontal){
               draw_horizontal_views(itr, true);
          }else if(itr != view && itr->next_horizontal){
               draw_horizontal_views(itr, true);
          }else{
               Point buffer_top_left = {itr->left_collumn, itr->top_row};
               ce_draw_buffer(itr->buffer_node->buffer, &itr->top_left, &itr->bottom_right, &buffer_top_left);
          }

          if(itr->next_vertical){
               move(itr->bottom_right.y, itr->top_left.x);
               addstr(separators);
          }

          itr = itr->next_vertical;
     }

     return true;
}

bool ce_draw_views(const BufferView* view)
{
     CE_CHECK_PTR_ARG(view);

     return draw_horizontal_views(view, false);
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

     if(!result &&view->next_vertical){
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
