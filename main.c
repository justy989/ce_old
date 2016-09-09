/*
NOTES:
-tabs suck, do we have to deal with them?
-get full file path
-be able to yank from man pages
*/

#include <dlfcn.h>

#include "ce.h"

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

     void* config_so_handle = dlopen("ce_config.so", RTLD_NOW);
     if(!config_so_handle){
          printf("missing ce_config.so\n"); // TODO: most helpful error message ever
          return -1;
     }

     ce_key_handler* key_handler = dlsym(config_so_handle, "key_handler");

     Buffer message_buffer = {0};
     Buffer buffer = {0};

     g_message_buffer = &message_buffer;

     if(argc == 2){
          ce_load_file(&buffer, argv[1]);
     }else{
          ce_alloc_lines(&buffer, 1);
          buffer.filename = ce_alloc_string("test_file.txt");
     }

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
               if(!key_handler(key, &buffer, &cursor)){
                    done = true;
               }
          }

          free(line_buffer);
     }

     // ncurses_free()
     endwin();
     ce_save_buffer(&message_buffer, "messages.txt");
     ce_free_buffer(&buffer);
     ce_free_buffer(&message_buffer);
     dlclose(config_so_handle);
     return 0;
}
