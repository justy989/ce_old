/*
NOTES:
-tabs suck, do we have to deal with them?
-get full file path
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ncurses.h>

#define NEWLINE 10
#define COLOR_BACKGROUND -1
#define COLOR_FOREGROUND -1
#define COLOR_BRIGHT_BLACK 8
#define COLOR_BRIGHT_RED 9
#define COLOR_BRIGHT_GREEN 10
#define COLOR_BRIGHT_YELLOW 11
#define COLOR_BRIGHT_BLUE 12
#define COLOR_BRIGHT_MAGENTA 13
#define COLOR_BRIGHT_CYAN 14
#define COLOR_BRIGHT_WHITE 15

#define CHECK_PTR_ARG(arg)                                                \
     if(!arg){                                                            \
          printf("%s() received NULL argument %s\n", __FUNCTION__, #arg); \
          return false;                                                   \
     }

typedef struct {
     int64_t x;
     int64_t y;
} Point;

typedef struct {
     char** lines; // NULL terminated, does not contain newlines
     int64_t line_count;
     char* filename;
} Buffer;

// TODO: strdup() isn't in my string.h c standard libary ?
char* alloc_string(const char* string)
{
     int64_t length = strlen(string);
     char* new = malloc(length + 1);
     strncpy(new, string, length);
     new[length] = 0;

     return new;
}

bool alloc_lines(Buffer* buffer, int64_t line_count)
{
     CHECK_PTR_ARG(buffer);

     if(line_count <= 0){
          printf("tried to allocate %ld lines for a buffer, but we can only allocated > 0 lines\n", line_count);
          return false;
     }

     buffer->lines = malloc(line_count * sizeof(char*));
     if(!buffer->lines){
          printf("failed to allocate %ld lines for buffer\n", line_count);
          return false;
     }

     buffer->line_count = line_count;

     // clear the lines
     for(int64_t i = 0; i < line_count; ++i){
          buffer->lines[i] = NULL;
     }
     return true;
}

bool load_file(Buffer* buffer, const char* filename)
{
     // read the entire file
     size_t content_size;
     char* contents = NULL;
     {
          FILE* file = fopen(filename, "rb");
          if(!file){
               printf("%s() failed to open: '%s'\n", __FUNCTION__, filename);
               perror(NULL);
               return false;
          }

          fseek(file, 0, SEEK_END);
          content_size = ftell(file);
          fseek(file, 0, SEEK_SET);

          contents = malloc(content_size);
          fread(contents, content_size, 1, file);

          fclose(file);

          buffer->filename = alloc_string(filename);
     }

     // count lines
     int64_t line_count = 0;
     for(size_t i = 0; i < content_size; ++i){
          if(contents[i] != NEWLINE) continue;

          line_count++;
     }

     if(line_count){
          alloc_lines(buffer, line_count);
          int64_t last_newline = -1;
          for(int64_t i = 0; i < (int64_t)(content_size); ++i){
               if(contents[i] != NEWLINE) continue;

               int64_t length = i - last_newline;
               char* new_line = malloc(length + 1);
               strncpy(new_line, contents + last_newline + 1, length);
               new_line[length] = 0;
               buffer->lines[i] = new_line;
          }

          int64_t length = (content_size - 1) - last_newline;
          char* new_line = malloc(length + 1);
          strncpy(new_line, contents + last_newline + 1, length);
          new_line[length] = 0;
          if(new_line[length - 1] == NEWLINE){
               new_line[length - 1] = 0;
          }
          buffer->lines[line_count - 1] = new_line;
     }

     return true;
}

void free_buffer(Buffer* buffer)
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

bool point_on_buffer(const Buffer* buffer, const Point* location)
{
     if(location->y < 0 || location->x < 0){
          printf("%ld, %ld not in buffer\n",
                 location->x, location->y);
          return false;
     }

     if(location->y >= buffer->line_count){
          printf("%ld, %ld not in buffer with %ld lines\n",
                 location->x, location->y, buffer->line_count);
          return false;
     }

     char* line = buffer->lines[location->y];
     int64_t line_len = 0;

     if(line) line_len = strlen(line);

     if(location->x > line_len){
          printf("%ld, %ld not in buffer with line %ld only %ld characters long\n",
                 location->x, location->y, buffer->line_count, line_len);
          return false;
     }

     return true;
}

bool insert_char(Buffer* buffer, const Point* location, char c)
{
     CHECK_PTR_ARG(buffer);
     CHECK_PTR_ARG(location);

     if(!point_on_buffer(buffer, location)){
          printf("%s() invalid location\n", __FUNCTION__);
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
               printf("%s() failed to allocate line with %ld characters\n", __FUNCTION__, new_len);
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
               printf("%s() failed to allocate line with 2 characters\n", __FUNCTION__);
               return false;
          }
          new_line[0] = c;
          new_line[1] = 0;
     }

     buffer->lines[location->y] = new_line;
     return true;
}

bool insert_string(Buffer* buffer, const Point* location, const char* string)
{
     CHECK_PTR_ARG(buffer);
     CHECK_PTR_ARG(location);
     CHECK_PTR_ARG(string);

     if(!point_on_buffer(buffer, location)){
          printf("%s() invalid location\n", __FUNCTION__);
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
               printf("%s() failed to allocate line with %ld characters\n", __FUNCTION__, new_len);
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
               printf("%s() failed to allocate line with 2 characters\n", __FUNCTION__);
               return false;
          }
          strncpy(new_line, string, string_len);
          new_line[string_len] = 0;
     }

     buffer->lines[location->y] = new_line;
     return true;
}

bool remove_char(Buffer* buffer, const Point* location)
{
     CHECK_PTR_ARG(buffer);
     CHECK_PTR_ARG(location);

     if(!point_on_buffer(buffer, location)) return false;

     char* line = buffer->lines[location->y];
     if(!line){
          printf("cannot remove character from empty line\n");
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
bool insert_line(Buffer* buffer, int64_t line, const char* string)
{
     CHECK_PTR_ARG(buffer);

     int64_t new_line_count = buffer->line_count + 1;
     char** new_lines = malloc(new_line_count * sizeof(char*));
     if(!new_lines){
          printf("%s() failed to malloc new lines: %ld\n", __FUNCTION__, new_line_count);
          return -1;
     }

     // copy up to new empty line, add empty line, copy the rest in the buffer
     for(int64_t i = 0; i < line; ++i) new_lines[i] = buffer->lines[i];
     if(string){
          new_lines[line] = alloc_string(string);
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

bool append_line(Buffer* buffer, const char* string)
{
     CHECK_PTR_ARG(buffer);
     CHECK_PTR_ARG(string);

     bool rc = insert_line(buffer, buffer->line_count, string);
     if(!rc) printf("%s() failed\n", __FUNCTION__);
     return rc;
}

bool insert_newline(Buffer* buffer, int64_t line)
{
     bool rc = insert_line(buffer, line, NULL);
     if(!rc) printf("%s() failed\n", __FUNCTION__);
     return rc;
}

bool remove_line(Buffer* buffer, int64_t line)
{
     CHECK_PTR_ARG(buffer);

     int64_t new_line_count = buffer->line_count - 1;
     char** new_lines = malloc(new_line_count * sizeof(char*));
     if(!new_lines){
          printf("%s() failed to malloc new lines: %ld\n", __FUNCTION__, new_line_count);
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
bool set_line(Buffer* buffer, int64_t line, const char* string)
{
     CHECK_PTR_ARG(buffer);

     if(line < 0 || line >= buffer->line_count){
          printf("%s() line %ld outside buffer with %ld lines\n", __FUNCTION__, line, buffer->line_count);
          return false;
     }

     if(buffer->lines[line]){
          free(buffer->lines[line]);
     }

     buffer->lines[line] = alloc_string(string);
     return true;
}

int main(int argc, char** argv)
{
     // ncurses_init()
     initscr();
     cbreak();
     noecho();
     if(has_colors() == FALSE){
          printf("terminal doesn't support colors\n");
          return -1;
     }

     Buffer error_buffer = {0};
     Buffer buffer = {0};

     if(argc == 2){
          load_file(&buffer, argv[1]);
     }else{
          alloc_lines(&buffer, 1);
          buffer.filename = alloc_string("test_file.txt");
     }

     alloc_lines(&error_buffer, 1);

     start_color();
     use_default_colors();

     int color_id = 1;
     init_pair(color_id, COLOR_RED, COLOR_BACKGROUND);

     // main loop
     {
          bool done = false;
          bool insert = false;
          char* line_buffer = NULL;
          int64_t start_line = 0;
          int width = 0;
          int prev_width = 0;
          int height = 0;
          int key = 0;
          Point cursor = {0, 0};

          while(!done){
               // macro that modifies height and width
               getmaxyx(stdscr, height, width);

               // realloc line_buffer if the width has changed
               if(width != prev_width){
                    if(line_buffer) free(line_buffer);
                    line_buffer = malloc(width + 1);
                    prev_width = width;
               }

               // calculate the last line we can draw
               int64_t last_line = start_line + (height - 2);

               // adjust the starting line based on where the cursor is
               if(cursor.y > last_line) start_line++;
               if(cursor.y < start_line) start_line--;

               // recalc the starting line
               last_line = start_line + (height - 2);

               if(last_line > (buffer.line_count - 1)){
                    last_line = buffer.line_count - 1;
                    start_line = last_line - (height - 2);
               }

               if(start_line < 0) start_line = 0;

               // clear all lines
               for(int64_t i = 0; i < height; ++i){
                    move(i, 0);
                    clrtoeol();
               }

               // print the range of lines we want to show
               if(buffer.line_count){
                    standend();
                    for(int64_t i = start_line; i <= last_line; ++i){
                         move(i - start_line, 0);
                         if(!buffer.lines[i]) continue;

                         memset(line_buffer, 0, width + 1);
                         int64_t line_length = strlen(buffer.lines[i]);
                         int64_t min = width < line_length ? width : line_length;
                         strncpy(line_buffer, buffer.lines[i], min);
                         addstr(line_buffer); // NOTE: rather than printw() because it could contain format specifiers
                    }
               }

               // print the file and terminal info
               attron(A_REVERSE);
               mvprintw(height - 1, 0, "%s %d lines %s", buffer.filename, buffer.line_count, insert ? "INSERT" : "NORMAL");
               memset(line_buffer, 0, width + 1);
               snprintf(line_buffer, width, "key: %d, term: %d, %d cursor: %ld, %ld, lines: %ld, %ld",
                        key, width, height, cursor.x, cursor.y, start_line, last_line);
               attroff(A_REVERSE);
               attron(COLOR_PAIR(color_id));
               mvaddstr(height - 1, width - strlen(line_buffer), line_buffer);
               attroff(COLOR_PAIR(color_id));

               // reset the cursor
               move(cursor.y - start_line, cursor.x);

               // swap the back buffer
               refresh();

               // perform actions
               key = getch();

               if(insert){
                    // TODO: should be a switch
                    if(key == 27){ // escape
                         insert = false;
                         if(buffer.lines[cursor.y]){
                              int64_t line_len = strlen(buffer.lines[cursor.y]);
                              if(cursor.x == line_len){
                                   cursor.x--;
                              }
                         }
                    }else if(key == 127){ // backspace
                         if(buffer.line_count){
                              if(cursor.x == 0){
                                   if(cursor.y != 0){
                                        // remove the line and join the next line with the previous
                                   }
                              }else{
                                   if(remove_char(&buffer, &cursor)){
                                        cursor.x--;
                                   }
                              }
                         }
                    }else if(key == 10){ // add empty line
                         if(!buffer.line_count){
                              alloc_lines(&buffer, 1);
                         }
                         if(insert_newline(&buffer, cursor.y + 1)){
                              cursor.y++;
                              cursor.x = 0;
                         }
                    }else{ // insert
                         if(buffer.line_count == 0) alloc_lines(&buffer, 1);

                         if(insert_char(&buffer, &cursor, key)) cursor.x++;
                    }
               }else{
                    switch(key){
                    default:
                         break;
                    case 'q':
                         done = true;
                         break;
                    case 'j':
                    {
                         if(cursor.y < (buffer.line_count - 1)){
                              cursor.y++;
                         }

                         if(buffer.lines[cursor.y]){
                              int64_t line_length = strlen(buffer.lines[cursor.y]);

                              if(cursor.x >= line_length){
                                   cursor.x = line_length - 1;
                                   if(cursor.x < 0){
                                        cursor.x = 0;
                                   }
                              }
                         }else{
                              cursor.x = 0;
                         }
                    } break;
                    case 'k':
                    {
                         if(cursor.y > 0){
                              cursor.y--;
                         }

                         if(buffer.lines[cursor.y]){
                              int64_t line_length = strlen(buffer.lines[cursor.y]);

                              if(cursor.x >= line_length){
                                   cursor.x = line_length - 1;
                                   if(cursor.x < 0){
                                        cursor.x = 0;
                                   }
                              }
                         }else{
                              cursor.x = 0;
                         }
                    } break;
                    case 'h':
                         if(cursor.x > 0){
                              cursor.x--;
                         }
                         break;
                    case 'l':
                    {
                         if(buffer.lines[cursor.y]){
                              int64_t line_length = strlen(buffer.lines[cursor.y]);
                              if(cursor.x < (line_length - 1)){
                                   cursor.x++;
                              }
                         }
                    } break;
                    case 'i':
                         insert = true;
                         break;
                    case 'a':
                    {
                         if(buffer.lines[cursor.y]){
                              cursor.x++;
                         }
                         insert = true;
                    } break;
                    case 'd':
                    // delete line
                    if(buffer.line_count){
                         if(remove_line(&buffer, cursor.y)){
                              if(cursor.y >= buffer.line_count){
                                   cursor.y = buffer.line_count - 1;
                              }
                         }
                    }
                    break;
                    case 'p':
                    append_line(&buffer, "TACOS");
                    break;
                    case 's':
                    {
                         // save file loaded
                         FILE* file = fopen(buffer.filename, "w");
                         if(!file){
                              // TODO: console output ?
                              printf("failed to save: '%s'\n", buffer.filename);
                              perror(NULL);
                              break;
                         }

                         for(int64_t i = 0; i < buffer.line_count; ++i){
                              if(buffer.lines[i]){
                                   size_t len = strlen(buffer.lines[i]);
                                   fwrite(buffer.lines[i], len, 1, file);
                              }
                              fwrite("\n", 1, 1, file);
                         }

                         fclose(file);
                    } break;
                    }
               }
          }

          free(line_buffer);
     }

     // ncurses_free()
     endwin();
     free_buffer(&buffer);
     free_buffer(&error_buffer);
     return 0;
}
