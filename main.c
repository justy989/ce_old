/*
NOTES:
-tabs suck, do we have to deal with them?
-get full file path

TODO:
-crash resurrections
-user input
-undo/redo
-use tabs instead of spaces
-window management library
-client/server

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
#include <getopt.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>

#include "ce.h"

typedef struct{
     int64_t start_line;
     int last_key;
     bool split;
     Point cursor;
} DefaultConfigState;

bool default_initializer(BufferNode* head, Point* terminal_dimensions, int argc, char** argv, void** user_data);
void default_destroyer(BufferNode* head, void* user_data);
bool default_key_handler(int key, BufferNode* head, void* user_data);
void default_view_drawer(const BufferNode* head, void* user_data);

typedef struct Config{
     char* path;
     void* so_handle;
     ce_initializer* initializer;
     ce_destroyer* destroyer;
     ce_key_handler* key_handler;
     ce_view_drawer* view_drawer;
}Config;

const Config config_defaults = {NULL, NULL, default_initializer, default_destroyer, default_key_handler, default_view_drawer};

bool config_open_and_init(Config* config, const char* path, BufferNode* head, void** user_data){
     // try to load the config shared object
     *config = config_defaults;
     config->so_handle = dlopen(path, RTLD_NOW);
     if(!config->so_handle){
          ce_message("missing config '%s': '%s', using defaults", path, strerror(errno));
          return false;
     }
     // TODO: macro?
     config->path = strdup(path);
     config->initializer = dlsym(config->so_handle, "initializer");
     if(!config->initializer) ce_message("no initializer() found in '%s'", path);

     config->destroyer = dlsym(config->so_handle, "destroyer");
     if(!config->destroyer) ce_message("no destroyer() found in '%s'", path);

     config->key_handler = dlsym(config->so_handle, "key_handler");
     if(!config->key_handler) ce_message("no key_handler() found in '%s', using default", path);

     config->view_drawer = dlsym(config->so_handle, "view_drawer");
     if(!config->view_drawer) ce_message("no draw_view() found in '%s', using default", path);

     if(config->initializer) config->initializer(head, g_terminal_dimensions, user_data);
     return true;
}

void config_close(Config* config, BufferNode* head, void* user_data){
     if(!config->so_handle) return;
     free(config->path);
     if(config->destroyer) config->destroyer(head, user_data);
     if(dlclose(config->so_handle)) ce_message("dlclose() failed with error %s", dlerror());
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
     const char* config = CE_CONFIG;
     int opt = 0;
     int parsed_args = 1;
     bool done_parsing = false;

     // TODO: create config parser
     // TODO: pass unhandled main arguments to the config's arg parser?
	while((opt = getopt(argc, argv, "c:h")) != -1 && !done_parsing){
          parsed_args++;
          switch(opt){
          case 'c':
               config = optarg;
               parsed_args++;
               break;
          case 'h':
               printf("usage: %s [options]\n", argv[0]);
               printf(" -c [config] shared object config\n");
               printf(" -h see this message for help");
               return 0;
          default:
               parsed_args--;
               done_parsing = true;
               break;
          }
     }

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

     void* user_data = NULL;

     start_color();
     use_default_colors();

     // NOTE: just messing with colors
     int color_id = 1;
     init_pair(color_id, COLOR_RED, COLOR_BACKGROUND);

     bool done = false;

     Config stable_config;
     config_open_and_init(&stable_config, CE_CONFIG, buffer_list_head, &user_data);
     Config current_config = stable_config;

     struct sigaction sa;
     sa.sa_handler = segv_handler;
     sigemptyset(&sa.sa_mask);
     if(sigaction(SIGSEGV, &sa, NULL) == -1){
          // TODO: handle error
     }

     // handle the segfault by reverting the config
     if(sigsetjmp(segv_ctxt, 1) != 0){
          if(current_config.so_handle == stable_config.so_handle){
               done = true;
          }
          else{
               ce_message("config '%s' crashed with SIGSEGV. restoring stable config '%s'",
                          current_config.path, stable_config.path);
               free(current_config.path);
               if(!dlclose(current_config.so_handle)){
                    ce_message("dlclose(crash_recovery) failed with error %s", dlerror());
               }
               current_config = stable_config;
          }
     }

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
          current_config.view_drawer(buffer_list_head, user_data);

          // update the terminal with what we drew
          refresh();

          int key = getch();
          if(key == ''){
               ce_message("reloading config '%s'", current_config.path);
               // TODO: specify the path for the test config to load here
               if(!config_open_and_init(&current_config, current_config.path, buffer_list_head, &user_data)){
                    current_config = stable_config;
               }
          }
          // user-defined or default key_handler()
          else if(!current_config.key_handler(key, buffer_list_head, user_data)){
               done = true;
          }
     }

     // cleanup ncurses
     endwin();

     ce_save_buffer(g_message_buffer, g_message_buffer->filename);

     if(current_config.so_handle != stable_config.so_handle)
          config_close(&current_config, buffer_list_head, user_data);
     config_close(&stable_config, buffer_list_head, user_data);

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

bool default_initializer(BufferNode* head, Point* terminal_dimensions, int argc, char** argv, void** user_data)
{
     (void)(argc);
     (void)(argv);
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
     snprintf(line_info, g_terminal_dimensions->x, "DEFAULT_CONFIG key: %d, term: %ld, %ld cursor: %ld, %ld",
              config_state->last_key, g_terminal_dimensions->x, g_terminal_dimensions->y, config_state->cursor.x, config_state->cursor.y);
     mvaddstr(g_terminal_dimensions->y - 1, g_terminal_dimensions->x - strlen(line_info), line_info);
     attroff(A_REVERSE);

     // reset the cursor
     move(config_state->cursor.y - buffer_top_left.y, config_state->cursor.x);
}
