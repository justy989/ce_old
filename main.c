/*
TODOS:

BIG:
-unicode support
-network editing
-autocomplete for: code, shell commands, etc.
-parse c to do real syntax highlighting/autocomplete?
-tail file
-support python and other mode syntax highlighting so I don't go insane at work
-async file saving/loading
-async autocomplete building
-incremental replace
-regex search/replace
-support tabs in addition to spaces
-separate out vim functionality into module for inclusion and unittest it

LITTLE:
-r<enter>
-when re-opening a file, go to the cursor position you exited on
-do searching inside macro
-step through macro one change at a time
-saw an undo bug around removing and pasting lines, looking for ways to reproduce
 -visually selected then used 'o'?
-separate dot for input buffer
-valgrind run clean
-'*' and '#' should be words not search strings
-clean up trailing whitespace so i don't have to
-matching pairs is still wrong when going CE_UP over multiple lines in some instances
-word movement commands should work across lines
-when there are 3 lines in a file and you do 'dj', you still have 2 lines...
-puting // inside quotes causing incorrect syntax highlighting

*/

#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "ce.h"

typedef struct Config_t{
     char* path;
     void* so_handle;
     ce_initializer* initializer;
     ce_destroyer* destroyer;
     ce_key_handler* key_handler;
     ce_view_drawer* view_drawer;
} Config_t;

bool config_open(Config_t* config, const char* path)
{
     ce_message("load config: '%s'", path);

     // try to load the config shared object
     memset(config, 0, sizeof(*config));
     config->so_handle = dlopen(path, RTLD_NOW);
     if(!config->so_handle){
          ce_message("dlopen() failed: '%s'", dlerror());
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

     return true;
}

void config_close(Config_t* config)
{
     if(!config->so_handle) return;
     free(config->path);
     // NOTE: comment out dlclose() so valgrind can get a helpful stack frame
     if(dlclose(config->so_handle)) ce_message("dlclose() failed with error %s", dlerror());
}

bool config_revert(Config_t* config, const char* filepath, const char* stable_config_contents, size_t stable_config_size)
{
     ce_message("overwriting '%s' back to stable config", filepath);
     FILE* file = fopen(filepath, "wb");
     if(!file){
          ce_message("failed to open '%s': %s", filepath, strerror(errno));
          return false;
     }
     fwrite(stable_config_contents, stable_config_size, 1, file);
     fclose(file);

     // NOTE: could this really fail? lol, we literally just wrote it
     return config_open(config, filepath);
}

sigjmp_buf segv_ctxt;
void segv_handler(int signo)
{
     (void)signo;
     struct sigaction sa;
     sa.sa_handler = segv_handler;
     sigemptyset(&sa.sa_mask);
     if(sigaction(SIGSEGV, &sa, NULL) == -1){
          // TODO: handle error
     }
     pid_t pid = fork();
     if(pid == 0) abort();
     ce_message("dumped core for pid %d", pid);
     siglongjmp(segv_ctxt, 1);
}

const char* random_greeting()
{
     static const char* greetings [] = {
          "Thank you for flying ce",
          "There's nothing like a fresh cup of ce in the morning",
          "Why do kids love the taste of C Editor?\n\nIt's the taste you can ce",
          "ce is for C Editor, that's good enough for me",
          "I missed you.",
          "Hope you're having a great day! -ce",
          "You're a special person -- or robot. I don't judge.",
          "I missed you... in a creepy way.",
          "I'm a potato",
          "At least this isn't emacs? Am I right!",
          "TACOCAT is the best palindrome",
          "Found a bug? It's a feature.",
          "Yo.",
          "Slurp'n up whitespace since 2016",
          "Welcome to GNU Emacs, one component of the GNU/Linux operating system.",
          "ce, the world's only editor with a Michelin star.",
          "Oy! ce's a beaut!",
          "The default config has a great vimplementation!",
          "They see me slurpin' They hatin'",
          "'Days of pain are worth the years of upcoming prosperity' -confucius",
     };

     srand(time(NULL));

     return greetings[ rand() % (sizeof(greetings) / sizeof(greetings[0]))];
}

const char* config = CE_CONFIG;
bool save_messages_on_exit = false;
int main(int argc, char** argv)
{
     if(isatty(STDIN_FILENO) == 0){
          printf("please run %s inside a terminal.\n", argv[0]);
          return -1;
     }

     int opt = 0;
     int parsed_args = 1;
     bool done_parsing = false;

     // TODO: create config parser
     // TODO: pass unhandled main arguments to the config's arg parser?
     while((opt = getopt(argc, argv, "c:sh")) != -1 && !done_parsing){
          parsed_args++;
          switch(opt){
          case 's':
               save_messages_on_exit = true;
               break;
          case 'c':
               config = optarg;
               parsed_args++;
               break;
          case 'h':
               printf("usage: %s [options]\n", argv[0]);
               printf(" -c [config] shared object config\n");
               printf(" -s save message buffer to file\n");
               printf(" -h see this message for help");
               return 0;
          default:
               parsed_args--;
               done_parsing = true;
               break;
          }
     }

     // init message buffer
     Buffer_t* message_buffer = calloc(1, sizeof(*message_buffer));
     if(!message_buffer){
          printf("failed to allocate message buffer: %s\n", strerror(errno));
          return -1;
     }

     message_buffer->filename = strdup(MESSAGE_FILE);
     message_buffer->line_count = 0;
     message_buffer->user_data = NULL;
     ce_alloc_lines(message_buffer, 1);
     message_buffer->readonly = true;
     message_buffer->modified = false;

     // init buffer list
     BufferNode_t* buffer_list_head = calloc(1, sizeof(*buffer_list_head));
     if(!buffer_list_head){
          printf("failed to allocate buffer list: %s\n", strerror(errno));
          return -1;
     }
     buffer_list_head->buffer = message_buffer;
     buffer_list_head->next = NULL;

     Point_t terminal_dimensions = {};
     getmaxyx(stdscr, terminal_dimensions.y, terminal_dimensions.x);
     g_terminal_dimensions = &terminal_dimensions;

     void* user_data = NULL;

     bool done = false;
     bool stable_sigsegvd = false;

     Config_t current_config;
     if(!config_open(&current_config, config)){
          return -1;
     }

     // ncurses_init()
     initscr();
     keypad(stdscr, TRUE);
     mousemask(~((mmask_t)0), NULL);
     mouseinterval(0);
     raw();
     cbreak();
     noecho();

     if(has_colors() == TRUE){
          start_color();
          use_default_colors();
     }

     // redirect stderr to the message buffer
     int message_buffer_fds[2];
     pipe(message_buffer_fds);
     for(int i = 0 ; i < 2; i++){
          int fd_flags = fcntl(message_buffer_fds[i], F_GETFL, 0);
          fcntl(message_buffer_fds[i], F_SETFL, fd_flags | O_NONBLOCK);
     }

     fclose(stderr);
     close(STDERR_FILENO);
     dup(message_buffer_fds[1]); // redirect stderr to the message buffer
     stderr = fdopen(message_buffer_fds[1], "w");
     setvbuf(stderr, NULL, _IONBF, 0);
     FILE* message_stderr = fdopen(message_buffer_fds[0], "r");
     setvbuf(message_stderr, NULL, _IONBF, 0);

     ce_message("%s", random_greeting());

     // save the stable config in memory
     size_t stable_config_size;
     char* stable_config_contents = NULL;
     bool using_stable_config = true;
     {
          FILE* file = fopen(config, "rb");
          if(!file){
               ce_message("%s() fopen('%s', 'rb') failed: %s", __FUNCTION__, config, strerror(errno));
               return -1;
          }

          fseek(file, 0, SEEK_END);
          stable_config_size = ftell(file);
          fseek(file, 0, SEEK_SET);

          stable_config_contents = malloc(stable_config_size + 1);
          fread(stable_config_contents, stable_config_size, 1, file);
          stable_config_contents[stable_config_size] = 0;

          fclose(file);
     }

     current_config.initializer(&buffer_list_head, g_terminal_dimensions, argc - parsed_args, argv + parsed_args, &user_data);

     signal(SIGQUIT, SIG_IGN);
     struct sigaction sa = {};
     sa.sa_handler = segv_handler;
     sigemptyset(&sa.sa_mask);
     if(sigaction(SIGSEGV, &sa, NULL) == -1){
          // TODO: handle error
     }

     // handle the segfault by reverting the config
     if(sigsetjmp(segv_ctxt, 1) != 0){
          if(using_stable_config){
               ce_message("stable config sigsegv'd");
               done = true;
               stable_sigsegvd = true;
          }else{
               config_close(&current_config);
               if(!config_revert(&current_config, config, stable_config_contents, stable_config_size)){
                    ce_save_buffer(message_buffer, message_buffer->filename);
                    return -1;
               }
               ce_message("loaded config crashed with SIGSEGV. restoring stable config.");
               using_stable_config = true;
               current_config.initializer(&buffer_list_head, g_terminal_dimensions, 0, NULL, &user_data);
          }
     }

     char message_buffer_buf[BUFSIZ];
     // main loop
     while(!done){
          // NOTE: only allow message buffer modifying here
          message_buffer->readonly = false;

          // add new input to message buffer
          while(fgets(message_buffer_buf, BUFSIZ, message_stderr) != NULL){
               if(strlen(message_buffer_buf) == 1) continue;
               message_buffer_buf[strlen(message_buffer_buf)-1] = '\0';
               if(message_buffer->lines[0][0] == '\0'){
                    Point_t insert_loc = {0, 0};
                    ce_insert_string(message_buffer, insert_loc, message_buffer_buf);
                    continue;
               }
               bool ret __attribute__((unused)) = ce_append_line(message_buffer, message_buffer_buf);
               assert(ret);
          }

          message_buffer->readonly = true;
          message_buffer->modified = false;

          // ncurses macro that gets height and width
          getmaxyx(stdscr, terminal_dimensions.y, terminal_dimensions.x);

          // user-defined or default draw_view()
          current_config.view_drawer(buffer_list_head, user_data);

          int key = getch();
          if(key == KEY_F(5)){
               // NOTE: maybe at startup we should do this, so when we crash we revert back to before we did the bad thing?
               if(access(current_config.path, F_OK) != -1){
                    current_config.destroyer(&buffer_list_head, user_data);
                    config_close(&current_config);
                    // TODO: specify the path for the test config to load here
                    if(config_open(&current_config, config)){
                         // TODO: pass main args, config needs to be able to handle getting the args again!
                         using_stable_config = false;
                         current_config.initializer(&buffer_list_head, g_terminal_dimensions, 0, NULL, &user_data);
                    }else{
                         if(!config_revert(&current_config, config, stable_config_contents, stable_config_size)){
                              ce_save_buffer(message_buffer, message_buffer->filename);
                              return -1;
                         }
                         using_stable_config = true;
                         current_config.initializer(&buffer_list_head, g_terminal_dimensions, 0, NULL, &user_data);
                    }
               }else{
                    ce_message("%s: %s", current_config.path, strerror(errno));
               }
          }
          // user-defined or default key_handler()
          else if(!current_config.key_handler(key, &buffer_list_head, user_data)){
               done = true;
          }
     }

     // cleanup ncurses
     endwin();

     if(save_messages_on_exit) ce_save_buffer(message_buffer, message_buffer->filename);

     if(!stable_sigsegvd){
          current_config.destroyer(&buffer_list_head, user_data);
          config_close(&current_config);
     }

     fclose(message_stderr);
     close(message_buffer_fds[0]);
     close(message_buffer_fds[1]);

     // free our buffers
     // TODO: I think we want to move this into the config
     BufferNode_t* itr = buffer_list_head;
     BufferNode_t* tmp;
     while(itr){
          tmp = itr;
          itr = itr->next;
          ce_free_buffer(tmp->buffer);
          free(tmp->buffer);
          free(tmp);
     }

     free(stable_config_contents);

     return 0;
}
