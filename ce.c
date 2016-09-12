#include "ce.h"

Buffer* g_message_buffer = NULL;
Point* g_terminal_dimensions = NULL;

// TODO: strdup() isn't in my string.h c standard libary ?
char* ce_alloc_string(const char* string)
{
     int64_t length = strlen(string);
     char* new = malloc(length + 1);
     strncpy(new, string, length);
     new[length] = 0;

     return new;
}

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
               ce_message("%s", strerror(errno));
               return false;
          }

          fseek(file, 0, SEEK_END);
          content_size = ftell(file);
          fseek(file, 0, SEEK_SET);

          contents = malloc(content_size + 1);
          fread(contents, content_size, 1, file);
          contents[content_size] = 0;

          fclose(file);

          buffer->filename = ce_alloc_string(filename);
     }

     // count lines
     int64_t line_count = 0;
     for(size_t i = 0; i < content_size; ++i){
          if(contents[i] != NEWLINE) continue;

          line_count++;
     }

     if(line_count){
          ce_alloc_lines(buffer, line_count);
          int64_t last_newline = -1;
          for(int64_t i = 0, l = 0; i < (int64_t)(content_size); ++i){
               if(contents[i] != NEWLINE) continue;

               int64_t length = (i - 1) - last_newline;
               char* new_line = malloc(length + 1);
               strncpy(new_line, contents + last_newline + 1, length);
               new_line[length] = 0;
               buffer->lines[l] = new_line;
               last_newline = i;
               l++;
          }

          int64_t length = (content_size - 1) - last_newline;
          if(length){
               char* new_line = malloc(length + 1);
               strncpy(new_line, contents + last_newline + 1, length);
               new_line[length] = 0;
               if(new_line[length - 1] == NEWLINE){
                    new_line[length - 1] = 0;
               }
               buffer->lines[line_count - 1] = new_line;
          }
     }

     return true;
}

void ce_free_buffer(Buffer* buffer)
{
     if(!buffer){
          return;
     }

     if(buffer->filename){
          free(buffer->filename);
     }

     if(buffer->lines){
          for(int64_t i; i < buffer->line_count; ++i){
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

     if(!ce_point_on_buffer(buffer, location)){
          return false;
     }

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

bool ce_insert_string(Buffer* buffer, const Point* location, const char* string)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);
     CE_CHECK_PTR_ARG(string);

     if(!ce_point_on_buffer(buffer, location)){
          return false;
     }

     char* line = buffer->lines[location->y];
     char* new_line = NULL;
     int64_t string_len = strlen(string);
     if(line){
          // allocate new line with length + 1 + NULL terminator
          int64_t line_len = strlen(line);
          int64_t new_len = line_len + string_len + 1;
          new_line = malloc(new_len);
          if(!new_line){
               ce_message("%s() failed to allocate line with %ld characters", __FUNCTION__, new_len);
               return false;
          }

          // copy before the insert, add the new char, copy after the insert
          for(int64_t i = 0; i < location->x; ++i){new_line[i] = line[i];}
          strncpy(new_line + location->x, string, string_len);
          for(int64_t i = location->x; i < line_len; ++i){new_line[i + string_len] = line[i];}

          // NULL terminate the newline, and free the old line
          new_line[new_len - 1] = 0;
          free(line);
     }else{
          new_line = malloc(string_len + 1);
          if(!new_line){
               ce_message("%s() failed to allocate line with %ld characters", __FUNCTION__, string_len + 1);
               return false;
          }
          strncpy(new_line, string, string_len);
          new_line[string_len] = 0;
     }

     buffer->lines[location->y] = new_line;
     return true;
}

bool ce_remove_char(Buffer* buffer, const Point* location)
{
     CE_CHECK_PTR_ARG(buffer);
     CE_CHECK_PTR_ARG(location);

     if(!ce_point_on_buffer(buffer, location)) return false;

     char* line = buffer->lines[location->y];
     if(!line){
          ce_message("%s() cannot remove character from empty line\n", __FUNCTION__);
          return false;
     }

     int64_t line_len = strlen(line);

     // if we want to delete the only character on the line, just clear the whole line
     if(line_len == 1){
          free(line);
          buffer->lines[location->y] = NULL;
          return true;
     }

     char* new_line = malloc(line_len);

     // copy before the removed char copy after the removed char
     for(int64_t i = 0; i < location->x; ++i){new_line[i] = line[i];}
     for(int64_t i = location->x; i < line_len; ++i){new_line[i-1] = line[i];}
     new_line[line_len-1] = 0;

     free(line);
     buffer->lines[location->y] = new_line;

     return true;
}

// NOTE: passing NULL to string causes an empty line to be inserted
bool ce_insert_line(Buffer* buffer, int64_t line, const char* string)
{
     CE_CHECK_PTR_ARG(buffer);

     int64_t new_line_count = buffer->line_count + 1;
     char** new_lines = malloc(new_line_count * sizeof(char*));
     if(!new_lines){
          ce_message("%s() failed to malloc new lines: %ld\n", __FUNCTION__, new_line_count);
          return -1;
     }

     // copy up to new empty line, add empty line, copy the rest in the buffer
     for(int64_t i = 0; i < line; ++i) new_lines[i] = buffer->lines[i];
     if(string){
          new_lines[line] = ce_alloc_string(string);
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

     bool rc = ce_insert_line(buffer, buffer->line_count, string);
     return rc;
}

bool ce_insert_newline(Buffer* buffer, int64_t line)
{
     bool rc = ce_insert_line(buffer, line, NULL);
     return rc;
}

bool ce_remove_line(Buffer* buffer, int64_t line)
{
     CE_CHECK_PTR_ARG(buffer);

     int64_t new_line_count = buffer->line_count - 1;
     char** new_lines = malloc(new_line_count * sizeof(char*));
     if(!new_lines){
          ce_message("%s() failed to malloc new lines: %ld\n", __FUNCTION__, new_line_count);
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
          ce_message("%s() line %ld outside buffer with %ld lines\n", __FUNCTION__, line, buffer->line_count);
          return false;
     }

     if(buffer->lines[line]){
          free(buffer->lines[line]);
     }

     buffer->lines[line] = ce_alloc_string(string);
     return true;
}

bool ce_save_buffer(const Buffer* buffer, const char* filename)
{
     // save file loaded
     FILE* file = fopen(filename, "w");
     if(!file){
          // TODO: console output ? perror!
          ce_message("%s() failed to open '%s': %s", __FUNCTION__, filename, strerror(errno));
          return false;;
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
          ce_message("%s() top_left must be lower than bottom_right horizontally", __FUNCTION__);
          return false;
     }

     if(term_top_left->y >= term_bottom_right->y){
          ce_message("%s() top_left must be lower than bottom_right vertically", __FUNCTION__);
          return false;
     }

     char line_to_print[g_terminal_dimensions->x];

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

          int64_t min = g_terminal_dimensions->x < line_length ? g_terminal_dimensions->x : line_length;
          memset(line_to_print, 0, min + 1);
          strncpy(line_to_print, buffer_line, min);
          addstr(line_to_print); // NOTE: use addstr() rather than printw() because it could contain format specifiers
     }

     return true;
}
