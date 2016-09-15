/*
NOTES:
-tabs suck, do we have to deal with them?
-get full file path

TODO:
-crash resurrections
-user input
-undo/redo
-use tabs instead of spaces

WANTS:
-realloc() rather than malloc() ?
-be able to yank from man pages
-regexes that don't suck, regcomp()
-tailing files
-visual section changes using . always uses the top of the region
-input box should act like a buffer in terms of key mappings
-pair programming? Each connected user can edit text with their own cursor? show other users' cursors!
*/

#include <dlfcn.h>

#include "ce.h"

typedef struct{
     int64_t start_line;
     int last_key;
     bool split;
     Point cursor;
} DefaultConfigState;

bool default_initializer(BufferNode* head, Point* terminal_dimensions, void** user_data);
void default_destroyer(BufferNode* head, void* user_data);
bool default_key_handler(int key, BufferNode* head, void* user_data);
void default_view_drawer(const BufferNode* head, void* user_data);

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

     // init message buffer
     g_message_buffer = malloc(sizeof(*g_message_buffer));
     if(!g_message_buffer){
          printf("failed to allocate message buffer: %s\n", strerror(errno));
          return -1;
     }

     g_message_buffer->filename = strdup(MESSAGE_FILE);
     g_message_buffer->line_count = 0;
     g_message_buffer->lines = NULL;
     g_message_buffer->user_data = NULL;

     // init buffer list
     BufferNode* buffer_list_head = malloc(sizeof(*buffer_list_head));
     if(!buffer_list_head){
          printf("failed to allocate buffer list: %s\n", strerror(errno));
          return -1;
     }
     buffer_list_head->buffer = g_message_buffer;
     buffer_list_head->next = NULL;

     // try to load the config shared object
     ce_initializer* initializer = default_initializer;
     ce_destroyer* destroyer = default_destroyer;
     ce_key_handler* key_handler = default_key_handler;
     ce_view_drawer* view_drawer = default_view_drawer;
     void* config_so_handle = dlopen(CE_CONFIG, RTLD_NOW);
     if(!config_so_handle){
          ce_message("missing config '%s': '%s', using defaults", CE_CONFIG, strerror(errno));
     }else{
          // TODO: macro?
          // NOTE: if we fail to load the initializer buf succeed in loading the destroyer, that seems bad
          initializer = dlsym(config_so_handle, "initializer");
          if(!initializer) ce_message("no initializer() found in '%s', using default", CE_CONFIG);

          destroyer = dlsym(config_so_handle, "destroyer");
          if(!destroyer) ce_message("no destroyer() found in '%s', using default", CE_CONFIG);

          key_handler = dlsym(config_so_handle, "key_handler");
          if(!key_handler) ce_message("no key_handler() found in '%s', using default", CE_CONFIG);

          view_drawer = dlsym(config_so_handle, "view_drawer");
          if(!view_drawer) ce_message("no draw_view() found in '%s', using default", CE_CONFIG);
     }

     Point terminal_dimensions = {0, 0};
     g_terminal_dimensions = &terminal_dimensions;

     // load the file given as the first argument
     if(argc == 2){
          ce_message("loading '%s' at startup", argv[1]);
          Buffer* buffer = malloc(sizeof(*buffer));
          if(ce_load_file(buffer, argv[1])){
               BufferNode* node = ce_append_buffer_to_list(buffer_list_head, buffer);
               if(!node){
                    free(buffer);
               }
          }else{
               free(buffer);
          }
     }else{
          ce_message("no file opened on startup");
     }

     void* user_data = NULL;

     initializer(buffer_list_head, g_terminal_dimensions, &user_data);

     start_color();
     use_default_colors();

     // NOTE: just messing with colors
     int color_id = 1;
     init_pair(color_id, COLOR_RED, COLOR_BACKGROUND);

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
          view_drawer(buffer_list_head, user_data);

          // update the terminal with what we drew
          refresh();

          int key = getch();

          // user-defined or default key_handler()
          if(!key_handler(key, buffer_list_head, user_data)){
               done = true;
          }
     }

     // cleanup ncurses
     endwin();

     destroyer(buffer_list_head, user_data);

     ce_save_buffer(g_message_buffer, g_message_buffer->filename);

     // free our buffers
     BufferNode* itr = buffer_list_head;
     BufferNode* tmp;
     while(itr){
          tmp = itr;
          itr = itr->next;
          ce_free_buffer(tmp->buffer);
          free(tmp->buffer);
          free(tmp);
     }

     if(config_so_handle) dlclose(config_so_handle);

     return 0;
}

bool default_initializer(BufferNode* head, Point* terminal_dimensions, void** user_data)
{
     (void)(head);
     (void)(terminal_dimensions);

     DefaultConfigState* config_state = malloc(sizeof(*config_state));
     if(!config_state) return false;

     *user_data = config_state;
     return true;
}

void default_destroyer(BufferNode* head, void* user_data)
{
     (void)(head);
     free(user_data);
}

bool default_key_handler(int key, BufferNode* head, void* user_data)
{
     DefaultConfigState* config_state = user_data;
     Buffer* buffer = head->buffer;
     if(head->next) buffer = head->next->buffer;

     config_state->last_key = key;

     switch(key){
     default:
          if(ce_insert_char(buffer, &config_state->cursor, key)) config_state->cursor.x++;
          break;
     case '':
          return false;
     case '':
          ce_save_buffer(buffer, buffer->filename);
          break;
     }

     return true;
}

void default_view_drawer(const BufferNode* head, void* user_data)
{
     DefaultConfigState* config_state = user_data;
     Buffer* buffer = head->buffer;
     if(head->next) buffer = head->next->buffer;

     // calculate the last line we can draw
     int64_t last_line = config_state->start_line + (g_terminal_dimensions->y - 2);

     // adjust the starting line based on where the cursor is
     if(config_state->cursor.y > last_line) config_state->start_line++;
     if(config_state->cursor.y < config_state->start_line) config_state->start_line--;

     // recalc the starting line
     last_line = config_state->start_line + (g_terminal_dimensions->y - 2);

     if(last_line > (buffer->line_count - 1)){
          last_line = buffer->line_count - 1;
          config_state->start_line = last_line - (g_terminal_dimensions->y - 2);
     }

     if(config_state->start_line < 0) config_state->start_line = 0;

     // print the range of lines we want to show
     Point buffer_top_left = {0, config_state->start_line};
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
              config_state->last_key, g_terminal_dimensions->x, g_terminal_dimensions->y, config_state->cursor.x, config_state->cursor.y);
     mvaddstr(g_terminal_dimensions->y - 1, g_terminal_dimensions->x - strlen(line_info), line_info);
     attroff(A_REVERSE);

     // reset the cursor
     move(config_state->cursor.y - buffer_top_left.y, config_state->cursor.x);
}
