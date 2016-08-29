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
     char* start; // NULL terminated, does not include '\n' character at the end
     int64_t length;
} Line;

typedef struct {
     int64_t x;
     int64_t y;
} Point;

typedef struct {
     Line* lines;
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

#if 0
     Buffer buffer = {0};
     buffer.lines = malloc(line_count * sizeof(Line));
     if(!buffer.lines){
          printf("failed to allocate buffer for %ld line file %s\n", line_count, filename);
          return -1;
     }
     buffer.line_count = line_count;
#endif

     // build lines from content
     Line* content_lines = malloc(line_count * sizeof(Line));
     {
          content_lines[0].start = contents;
          for(size_t i = 0, l = 0; i < content_size; ++i){
               if(contents[i] == NEWLINE){
                    l++;
                    Line* cur_line = content_lines + l;
                    Line* prev_line = cur_line - 1;
                    cur_line->start = contents + i + 1; // exclude the newline
                    prev_line->length = (contents + i + 1) - prev_line->start; // include the newline
               }
          }

          // fix up the last line since it won't get touched in the loop
          Line* last_line = content_lines + line_count - 1;
          last_line->length = (contents + content_size) - last_line->start;
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

          Point cursor = {0};

          while(!done){
               move(0, 0);

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

               if(last_line > (line_count - 1)){
                    last_line = line_count - 1;
                    start_line = last_line - (height - 2);
               }

               if(start_line < 0) start_line = 0;

               // print the range of lines we want to show
               standend();
               for(int64_t i = start_line; i <= last_line; ++i){
                    memset(line_buffer, 0, width + 1);
                    int64_t max = width < content_lines[i].length ? width : content_lines[i].length;
                    strncpy(line_buffer, content_lines[i].start, max);
                    addstr(line_buffer); // NOTE: rather than printw() because it could contain format specifiers
               }

               // print the file and terminal info
               attron(A_REVERSE);
               mvprintw(height - 1, 0, "%s %d lines", filename, line_count);
               memset(line_buffer, 0, width + 1);
               snprintf(line_buffer, width, "term: %d, %d cursor: %ld, %ld, lines: %ld, %ld",
                        width, height, cursor.x, cursor.y, start_line, last_line);
               attroff(A_REVERSE);
               attron(COLOR_PAIR(color_id));
               mvaddstr(height - 1, width - strlen(line_buffer), line_buffer);
               attroff(COLOR_PAIR(color_id));

               // reset the cursor
               move(cursor.y - start_line, cursor.x);

               // swap the back buffer
               refresh();

               // perform actions
               int ch = getch();
               switch(ch){
               default:
                    done = true;
                    break;
               case 'j':
                    if(cursor.y < (line_count - 1)){
                         cursor.y++;
                    }

                    if(cursor.x >= content_lines[cursor.y].length){
                         cursor.x = content_lines[cursor.y].length - 1;
                         if(cursor.x < 0){
                              cursor.x = 0;
                         }
                    }
                    break;
               case 'k':
                    if(cursor.y > 0){
                         cursor.y--;
                    }

                    if(cursor.x >= content_lines[cursor.y].length){
                         cursor.x = content_lines[cursor.y].length - 1;
                         if(cursor.x < 0){
                              cursor.x = 0;
                         }
                    }
                    break;
               case 'h':
                    if(cursor.x > 0){
                         cursor.x--;
                    }
                    break;
               case 'l':
               {
                    if(cursor.x < (content_lines[cursor.y].length - 1)){
                         cursor.x++;
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
