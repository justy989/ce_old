/*
NOTES:
-tabs suck, do we have to deal with them?
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

typedef struct {
     int64_t x;
     int64_t y;
} Point;

typedef struct {
     char** lines; // NULL terminated, not newline terminated
     int64_t line_count;
     Point cursor;
} Buffer;

int main(int argc, char** argv)
{
     // enforce usage for now
     if(argc != 2){
          printf("usage: %s [file]\n", argv[0]);
          return -1;
     }

     char* filename = argv[1];

     // ncurses_init()
     initscr();
     cbreak();
     noecho();
     if(has_colors() == FALSE){
          printf("terminal doesn't support colors\n");
          return -1;
     }

     // read the entire file
     size_t content_size;
     char* contents = NULL;
     {
          FILE* file = fopen(filename, "rb");
          if(!file){
               printf("failed to open: '%s'\n", filename);
               perror(NULL);
               return -1;
          }

          fseek(file, 0, SEEK_END);
          content_size = ftell(file);
          fseek(file, 0, SEEK_SET);

          contents = malloc(content_size);
          fread(contents, content_size, 1, file);

          fclose(file);
     }

     // count lines
     int64_t line_count = 0;
     for(size_t i = 0; i < content_size; ++i){
          if(contents[i] == NEWLINE){
               line_count++;
          }
     }

     // TODO: as the error message says !
     if(!line_count){
          printf("empty? Handle this Justin!\n");
          return -1;
     }

     // alloc buffer
     Buffer buffer = {0};
     {
          buffer.lines = malloc(line_count * sizeof(char*));
          if(!buffer.lines){
               printf("failed to allocate buffer for %ld line file %s\n", line_count, filename);
               return -1;
          }
          buffer.line_count = line_count;
     }

     // fill buffer from file contents
     {
          char* last_newline = contents - 1;
          for(size_t i = 0, l = 0; i < content_size; ++i){
               if(contents[i] != NEWLINE) continue;
               l++;

               char* current_char = contents + i;
               size_t len = current_char - last_newline;
               char* prev_line = malloc(len + 1);

               strncpy(prev_line, last_newline + 1, len);

               prev_line[len] = 0;
               last_newline = current_char;
               buffer.lines[l - 1] = prev_line;
          }

          // finish up the last line
          strncpy(buffer.lines[line_count - 1], last_newline + 1, (contents + content_size) - (last_newline + 1));
     }

     start_color();
     use_default_colors();

     int color_id = 1;
     init_pair(color_id, COLOR_RED, COLOR_BACKGROUND);

     // main loop
     {
          bool done = false;
          int64_t start_line = 0;
          char* line_buffer = NULL;
          int width = 0;
          int prev_width = 0;
          int height = 0;

          int key = 0;

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
               if(buffer.cursor.y > last_line) start_line++;
               if(buffer.cursor.y < start_line) start_line--;

               // recalc the starting line
               last_line = start_line + (height - 2);

               if(last_line > (line_count - 1)){
                    last_line = line_count - 1;
                    start_line = last_line - (height - 2);
               }

               if(start_line < 0) start_line = 0;

               // print the range of lines we want to show
               standend();
               for(int64_t i = start_line; i <= last_line; ++i){
                    move(i - start_line, 0);
                    memset(line_buffer, 0, width + 1);
                    int64_t line_length = strlen(buffer.lines[i]);
                    int64_t max = width < line_length ? width : line_length;
                    strncpy(line_buffer, buffer.lines[i], max);
                    addstr(line_buffer); // NOTE: rather than printw() because it could contain format specifiers
               }

               // print the file and terminal info
               attron(A_REVERSE);
               mvprintw(height - 1, 0, "%s %d lines", filename, line_count);
               memset(line_buffer, 0, width + 1);
               snprintf(line_buffer, width, "key: %d, term: %d, %d cursor: %ld, %ld, lines: %ld, %ld",
                        key, width, height, buffer.cursor.x, buffer.cursor.y, start_line, last_line);
               attroff(A_REVERSE);
               attron(COLOR_PAIR(color_id));
               mvaddstr(height - 1, width - strlen(line_buffer), line_buffer);
               attroff(COLOR_PAIR(color_id));

               // reset the cursor
               move(buffer.cursor.y - start_line, buffer.cursor.x);

               // swap the back buffer
               refresh();

               // perform actions
               key = getch();

               switch(key){
               default:
                    break;
               case 'q':
                    done = true;
                    break;
               case 'j':
               case 66: // down
               {
                    if(buffer.cursor.y < (line_count - 1)){
                         buffer.cursor.y++;
                    }

                    int64_t line_length = strlen(buffer.lines[buffer.cursor.y]);

                    if(buffer.cursor.x >= line_length){
                         buffer.cursor.x = line_length - 1;
                         if(buffer.cursor.x < 0){
                              buffer.cursor.x = 0;
                         }
                    }
               } break;
               case 'k':
               case 65: // up
                    if(buffer.cursor.y > 0){
                         buffer.cursor.y--;
                    }

                    int64_t line_length = strlen(buffer.lines[buffer.cursor.y]);

                    if(buffer.cursor.x >= line_length){
                         buffer.cursor.x = line_length - 1;
                         if(buffer.cursor.x < 0){
                              buffer.cursor.x = 0;
                         }
                    }
                    break;
               case 'h':
               case 68: // left
                    if(buffer.cursor.x > 0){
                         buffer.cursor.x--;
                    }
                    break;
               case 'l':
               case 67: // right
               {
                    int64_t line_length = strlen(buffer.lines[buffer.cursor.y]);
                    if(buffer.cursor.x < (line_length - 2)){
                         buffer.cursor.x++;
                    }
               } break;
               }
          }

          free(line_buffer);
     }

     // ncurses_free()
     endwin();
     return 0;
}
