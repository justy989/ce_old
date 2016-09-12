/*
NOTES:
-tabs suck, do we have to deal with them?
-get full file path

WANTS:
-be able to yank from man pages
-regexes that don't suck
-tailing files
*/

#include <dlfcn.h>

#include "ce.h"

int64_t g_start_line = 0;
int g_last_key = 0;

bool default_key_handler(int key, Buffer* buffer, Point* cursor);
void default_view_drawer(const Buffer* buffer, const Point* cursor);

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

     Buffer message_buffer = {0};
     Buffer buffer = {0};
     Point cursor = {0, 0};

     g_message_buffer = &message_buffer;

     // try to load the config shared object
     ce_initializer* initializer = NULL;
     ce_key_handler* key_handler = NULL;
     ce_view_drawer* view_drawer = NULL;
     void* config_so_handle = dlopen(CE_CONFIG, RTLD_NOW);
     if(!config_so_handle){
          ce_message("missing config '%s': '%s', using defaults\n", CE_CONFIG, strerror(errno));
          key_handler = default_key_handler;
          view_drawer = default_view_drawer;
     }else{
          initializer = dlsym(config_so_handle, "initializer");
          if(!initializer){
               ce_message("no initializer() found in '%s'", CE_CONFIG);
          }

          key_handler = dlsym(config_so_handle, "key_handler");
          if(!key_handler){
               ce_message("no key_handler() found in '%s', using default", CE_CONFIG);
               key_handler = default_key_handler;
          }

          view_drawer = dlsym(config_so_handle, "view_drawer");
          if(!view_drawer){
               ce_message("no draw_view() found in '%s', using default", CE_CONFIG);
               view_drawer = default_view_drawer;
          }
     }

     initializer(&message_buffer, &cursor);

     // load the file given as the first argument
     if(argc == 2){
          ce_load_file(&buffer, argv[1]);
     }else{
          ce_alloc_lines(&buffer, 1);
          buffer.filename = ce_alloc_string("test_file.txt");
     }

     start_color();
     use_default_colors();

     // NOTE: just messing with colors
     int color_id = 1;
     init_pair(color_id, COLOR_RED, COLOR_BACKGROUND);

     Point terminal_dimensions = {0, 0};
     g_terminal_dimensions = &terminal_dimensions;

     bool done = false;

     // main loop
     while(!done){
          // ncurses macro that gets height and width
          getmaxyx(stdscr, terminal_dimensions.y, terminal_dimensions.x);

          // clear all lines
          for(int64_t i = 0; i < terminal_dimensions.y; ++i){
               move(i, 0);
               clrtoeol();
          }

          // user-defined or default draw_view()
          view_drawer(&buffer, &cursor);

          // update the terminal with what we drew
          refresh();

          g_last_key = getch();

          // user-defined or default key_handler()
          if(!key_handler(g_last_key, &buffer, &cursor)){
               done = true;
          }
     }

     // cleanup ncurses
     endwin();

     // free our open buffers
     ce_save_buffer(&message_buffer, "messages.txt");
     ce_free_buffer(&buffer);
     ce_free_buffer(&message_buffer);

     dlclose(config_so_handle);

     return 0;
}

bool default_key_handler(int key, Buffer* buffer, Point* cursor)
{
     switch(key){
     default:
          if(ce_insert_char(buffer, cursor, key)) cursor->x++;
          break;
     case '':
          return false;
     case '':
          ce_save_buffer(buffer, buffer->filename);
          break;
     }

     return true;
}

void default_view_drawer(const Buffer* buffer, const Point* cursor)
{
     // calculate the last line we can draw
     int64_t last_line = g_start_line + (g_terminal_dimensions->y - 2);

     // adjust the starting line based on where the cursor is
     if(cursor->y > last_line) g_start_line++;
     if(cursor->y < g_start_line) g_start_line--;

     // recalc the starting line
     last_line = g_start_line + (g_terminal_dimensions->y - 2);

     if(last_line > (buffer->line_count - 1)){
          last_line = buffer->line_count - 1;
          g_start_line = last_line - (g_terminal_dimensions->y - 2);
     }

     if(g_start_line < 0) g_start_line = 0;

     // print the range of lines we want to show
     Point buffer_top_left = {0, g_start_line};
     if(buffer->line_count){
          standend();
          Point term_top_left = {0, 0};
          Point term_bottom_right = {g_terminal_dimensions->x, g_terminal_dimensions->y - 1};
          ce_draw_buffer(buffer, &term_top_left, &term_bottom_right, &buffer_top_left);
     }

     // print the file and terminal info
     char line_info[g_terminal_dimensions->x];
     attron(A_REVERSE);
     mvprintw(g_terminal_dimensions->y - 1, 0, "%s %d lines", buffer->filename, buffer->line_count);
     snprintf(line_info, g_terminal_dimensions->x, "key: %d, term: %ld, %ld cursor: %ld, %ld",
              g_last_key, g_terminal_dimensions->x, g_terminal_dimensions->y, cursor->x, cursor->y);
     mvaddstr(g_terminal_dimensions->y - 1, g_terminal_dimensions->x - strlen(line_info), line_info);
     attroff(A_REVERSE);

     // reset the cursor
     move(cursor->y - buffer_top_left.y, cursor->x);
}
