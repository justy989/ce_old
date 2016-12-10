#include "ce_terminal.h"

#include <pty.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <signal.h>
#include <sys/wait.h>
#include <ctype.h>

pid_t pid;

void handle_sigchld(int signal)
{
     int stat;
     pid_t p;

     ce_message("terminal shell saw signal: %d", signal);

     if((p = waitpid(pid, &stat, WNOHANG)) < 0){
          ce_message("terminal waiting for shell pid %d failed: %s\n", pid, strerror(errno));
     }

     if (pid != p) return;

     if(!WIFEXITED(stat) || WEXITSTATUS(stat)){
          ce_message("terminal shell child finished with error '%d'\n", stat);
     }
}

void* terminal_reader(void* data)
{
     Terminal_t* term = data;
     char bytes[BUFSIZ];

     memset(bytes, 0, BUFSIZ);

     while(term->is_alive){
          int rc = read(term->fd, bytes, BUFSIZ);
          if(rc < 0){
               ce_message("%s() read() from shell failed: %s\n", __FUNCTION__, strerror(errno));
               term->is_alive = false;
               pthread_exit(NULL);
          }

          bytes[rc] = 0;

          bool escape = false;
          bool csi = false;
          char* byte = bytes;
          while(*byte){
               if(csi){
                    if(isdigit(*byte)){

                    }else{
                         switch(*byte){
                         default:
                              break;
                         }
                         csi = false;
                    }
               }else if(escape){
                    switch(*byte){
                    default:
                         break;
                    case '[': // handle "Control Sequence Inducers"
                         csi = true;
                         break;
                    }

                    escape = false;
               }else if(isprint(*byte)){
                    ce_append_char(&term->buffer, *byte);
                    term->cursor.x++;
                    if(term->cursor.x >= term->width){
                         ce_append_char(&term->buffer, NEWLINE);
                         term->cursor.x = 0;
                         term->cursor.y++;
                    }
               }else{
                    switch(*byte){
                    case 27: // escape
                         escape = true;
                         break;
                    case 8: // backspace
                         term->cursor.x--;
                         if(term->cursor.x < 0) term->cursor.x = 0;
                         ce_remove_char(&term->buffer, term->cursor);
                         break;
                    case NEWLINE:
                         ce_append_char(&term->buffer, NEWLINE);
                         term->cursor.x = 0;
                         term->cursor.y++;
                         break;
                    }
               }

               byte++;
          }

          if(rc){
               term->is_updated = true;
          }
     }

     pthread_exit(NULL);
     return NULL;
}

bool terminal_init(Terminal_t* term, int64_t width, int64_t height)
{
     int master_fd;
     int slave_fd;

     struct winsize window_size = {width, height, 0, 0};

     // init tty
     if(openpty(&master_fd, &slave_fd, NULL, NULL, &window_size)){
          ce_message("%s() openpty() failed: %s", __FUNCTION__, strerror(errno));
          return false;
     }

     pid = fork();

     switch(pid){
     case -1:
          ce_message("%s() fork() failed", __FUNCTION__);
          return false;
     case 0:
     {
          setsid();

          dup2(slave_fd, STDERR_FILENO);
          dup2(slave_fd, STDOUT_FILENO);
          dup2(slave_fd, STDIN_FILENO);

          if(ioctl(slave_fd, TIOCSCTTY, NULL) < 0){
               ce_message("%s() ioctl() failed: %s\n", __FUNCTION__, strerror(errno));
               return false;
          }

          close(slave_fd);
          close(master_fd);

          // exec shell
          const struct passwd *pw = getpwuid(getuid());
          if(pw == NULL){
               fprintf(stderr, "getpwuid() failed %s\n", strerror(errno));
               return false;
          }

          char* sh = (pw->pw_shell[0]) ? pw->pw_shell : "/bin/bash";

          // resetting env
          unsetenv("COLUMNS");
          unsetenv("LINES");
          unsetenv("TERMCAP");
          setenv("LOGNAME", pw->pw_name, 1);
          setenv("USER", pw->pw_name, 1);
          setenv("SHELL", sh, 1);
          setenv("HOME", pw->pw_dir, 1);
          setenv("TERM", "dumb", 1); // we can change this to "urxvt" when we implement all VT102 commands

          // resetting signals
          signal(SIGCHLD, SIG_DFL);
          signal(SIGHUP, SIG_DFL);
          signal(SIGINT, SIG_DFL);
          signal(SIGQUIT, SIG_DFL);
          signal(SIGTERM, SIG_DFL);
          signal(SIGALRM, SIG_DFL);

          char** arg = NULL;
          execvp(sh, arg);
          _exit(1);
     } break;
     default:
          close(slave_fd);
          term->fd = master_fd;

          signal(SIGCHLD, handle_sigchld);
          break;
     }

     int rc = pthread_create(&term->reader_thread, NULL, terminal_reader, term);
     if(rc != 0){
          ce_message("pthread_create() failed");
          return false;
     }

     term->width = width;
     term->height = height;

     ce_free_buffer(&term->buffer);

     if(!ce_alloc_lines(&term->buffer, 1)){
          return false;
     }

     term->buffer.name = strdup("[terminal]");

     term->cursor = (Point_t){0, 0};
     term->is_alive = true;
     term->is_updated = false;
     return true;
}

void terminal_free(Terminal_t* term)
{
     term->is_alive = false;

     term->width = 0;
     term->height = 0;
     term->fd = 0;

     pthread_cancel(term->reader_thread);
     ce_free_buffer(&term->buffer);
}

#if 0
bool terminal_resize(Terminal_t* term, int64_t width, int64_t height)
{
     return true;
}
#endif

bool terminal_send_key(Terminal_t* term, int key)
{
     // conversion
     switch(key){
     case 343: // enter
          key = '\r';
          break;
     case 263: // backspace
          key = '\b';
          break;
     }

     char character = (char)(key);
     // TODO: get mutex

     int rc = write(term->fd, &character, 1);
     if(rc < 0){
          ce_message("%s() write() to terminal failed: %s", __FUNCTION__, strerror(errno));
          return false;
     }

     return true;
}
