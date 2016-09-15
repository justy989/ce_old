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
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>

#include "ce.h"

int64_t g_start_line = 0;
int g_last_key = 0;
bool g_split = false;
Point g_cursor = {0, 0};

bool default_key_handler(int key, BufferNode* head);
void default_view_drawer(const BufferNode* head);

typedef struct{
     void* so_handle;
     ce_initializer* initializer;
     ce_destroyer* destroyer;
     ce_key_handler* key_handler;
     ce_view_drawer* view_drawer;
}Config;

Config stable_config;
Config current_config;

const Config config_defaults = {NULL, NULL, NULL, default_key_handler, default_view_drawer};

bool config_open(Config* config, const char* path, BufferNode* head){
     // try to load the config shared object
     *config = config_defaults;
     config->so_handle = dlopen(path, RTLD_NOW);
     if(!config->so_handle){
          ce_message("missing config '%s': '%s', using defaults", path, strerror(errno));
          return false;
     }
     // TODO: macro?
     config->initializer = dlsym(config->so_handle, "initializer");
     if(!config->initializer) ce_message("no initializer() found in '%s'", path);

     config->destroyer = dlsym(config->so_handle, "destroyer");
     if(!config->destroyer) ce_message("no destroyer() found in '%s'", path);

     config->key_handler = dlsym(config->so_handle, "key_handler");
     if(!config->key_handler) ce_message("no key_handler() found in '%s', using default", path);

     config->view_drawer = dlsym(config->so_handle, "view_drawer");
     if(!config->view_drawer) ce_message("no draw_view() found in '%s', using default", path);

     if(config->initializer) config->initializer(head, g_message_buffer, g_terminal_dimensions);
     return true;
}

void config_close(Config*config, BufferNode* head){
     if(!config->so_handle) return;
     if(config->destroyer) config->destroyer(head);
     if(dlclose(config->so_handle)) ce_message("dlclose() failed with error %s", dlerror());
}

void config_revert(){
     ce_message("reverting to previous config");
     current_config = stable_config;
}

sigjmp_buf segv_ctxt;
void segv_handler(int signo){
     (void)signo;
     struct sigaction sa;
     sa.sa_handler = segv_handler;
     sigemptyset(&sa.sa_mask);
     if(sigaction(SIGSEGV, &sa, NULL) == -1){
          // TODO: handle error
     }
     siglongjmp(segv_ctxt, 1);
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

     start_color();
     use_default_colors();

     // NOTE: just messing with colors
     int color_id = 1;
     init_pair(color_id, COLOR_RED, COLOR_BACKGROUND);

     bool done = false;

     config_open(&stable_config, CE_CONFIG, buffer_list_head);
     current_config = stable_config;

     struct sigaction sa;
     sa.sa_handler = segv_handler;
     sigemptyset(&sa.sa_mask);
     if(sigaction(SIGSEGV, &sa, NULL) == -1){
          // TODO: handle error
     }

     // handle the segfault by reverting the config
     if(sigsetjmp(segv_ctxt, 1) != 0) config_revert();

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
          current_config.view_drawer(buffer_list_head);

          // update the terminal with what we drew
          refresh();

          g_last_key = getch();
          if(g_last_key == ''){
               ce_message("reloading config");
               if(!config_open(&current_config, "ce_config2.so", buffer_list_head)){
                    config_revert();
               }
          }
          // user-defined or default key_handler()
          else if(!current_config.key_handler(g_last_key, buffer_list_head)){
               done = true;
          }
     }

     // cleanup ncurses
     endwin();

     ce_save_buffer(g_message_buffer, g_message_buffer->filename);

     if(current_config.so_handle != stable_config.so_handle)
          config_close(&current_config, buffer_list_head);
     config_close(&stable_config, buffer_list_head);

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

     return 0;
}

bool default_key_handler(int key, BufferNode* head)
{
     Buffer* buffer = head->buffer;
     if(head->next) buffer = head->next->buffer;

     switch(key){
     default:
          if(ce_insert_char(buffer, &g_cursor, key)) g_cursor.x++;
          break;
     case '':
          return false;
     case '':
          ce_save_buffer(buffer, buffer->filename);
          break;
     }

     return true;
}

void default_view_drawer(const BufferNode* head)
{
     Buffer* buffer = head->buffer;
     if(head->next) buffer = head->next->buffer;

     // calculate the last line we can draw
     int64_t last_line = g_start_line + (g_terminal_dimensions->y - 2);

     // adjust the starting line based on where the cursor is
     if(g_cursor.y > last_line) g_start_line++;
     if(g_cursor.y < g_start_line) g_start_line--;

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
     snprintf(line_info, g_terminal_dimensions->x, "DEFAULT_CONFIG key: %d, term: %ld, %ld cursor: %ld, %ld",
              g_last_key, g_terminal_dimensions->x, g_terminal_dimensions->y, g_cursor.x, g_cursor.y);
     mvaddstr(g_terminal_dimensions->y - 1, g_terminal_dimensions->x - strlen(line_info), line_info);
     attroff(A_REVERSE);

     // reset the cursor
     move(g_cursor.y - buffer_top_left.y, g_cursor.x);
}
