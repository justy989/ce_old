#include "ce_terminal.h"
#include "ce_syntax.h"

#include <pty.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <signal.h>
#include <sys/wait.h>
#include <ctype.h>
#include <assert.h>

pid_t pid;

void handle_sigchld(int signal)
{
     int stat;
     pid_t p;

     ce_message("terminal shell saw signal: %d", signal);

     if(signal != SIGINT){
          if((p = waitpid(pid, &stat, WNOHANG)) < 0){
               ce_message("terminal waiting for shell pid %d failed: %s\n", pid, strerror(errno));
          }

          if (pid != p) return;

          if(!WIFEXITED(stat) || WEXITSTATUS(stat)){
               ce_message("terminal shell child finished with error '%d'\n", stat);
          }
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
               sem_post(&term->updated);
               pthread_exit(NULL);
          }

          bytes[rc] = 0;

          int csi_arguments[16]; // NPAR, does it exist?
          int csi_argument_index = 0;

          for(int c = 0; c < 16; ++c) csi_arguments[c] = 0;

          bool escape = false;
          bool csi = false;
          char* byte = bytes;
          while(*byte){
               if(csi){
                    if(isdigit(*byte)){
                         csi_arguments[csi_argument_index] *= 10;
                         csi_arguments[csi_argument_index] += (*byte - '0');
                    }else{
                         if(*byte == ';'){
                              csi_argument_index++;
                         }else{
                              switch(*byte){
                              default:
                                   break;
                              case 'm':
                              {
                                   // get last node in row
                                   TerminalColorNode_t* last_node = term->color_lines + (term->buffer.line_count - 1);
                                   while(last_node->next) last_node = last_node->next;

                                   TerminalColorNode_t* new_last_node = malloc(sizeof(*new_last_node));
                                   if(!new_last_node) break;

                                   last_node->next = new_last_node;
                                   new_last_node->fg = last_node->fg;
                                   new_last_node->bg = last_node->bg;
                                   new_last_node->index = strlen(term->buffer.lines[term->buffer.line_count - 1]);
                                   new_last_node->next = NULL;

                                   switch(csi_arguments[0]){
                                   default:
                                        break;
                                   case 0:
                                        new_last_node->fg = COLOR_FOREGROUND;
                                        new_last_node->bg = COLOR_BACKGROUND;
                                        break;
                                   case 30:
                                        new_last_node->fg = COLOR_BLACK;
                                        break;
                                   case 31:
                                        new_last_node->fg = COLOR_RED;
                                        break;
                                   case 32:
                                        new_last_node->fg = COLOR_GREEN;
                                        break;
                                   case 33:
                                        new_last_node->fg = COLOR_YELLOW;
                                        break;
                                   case 34:
                                        new_last_node->fg = COLOR_BLUE;
                                        break;
                                   case 35:
                                        new_last_node->fg = COLOR_MAGENTA;
                                        break;
                                   case 36:
                                        new_last_node->fg = COLOR_CYAN;
                                        break;
                                   case 37:
                                        new_last_node->fg = COLOR_WHITE;
                                        break;
                                   case 38: // underscore on
                                        new_last_node->fg = COLOR_FOREGROUND;
                                        break;
                                   case 39: // underscore off
                                        new_last_node->fg = COLOR_FOREGROUND;
                                        break;
                                   case 40:
                                        new_last_node->bg = COLOR_BLACK;
                                        break;
                                   case 41:
                                        new_last_node->bg = COLOR_RED;
                                        break;
                                   case 42:
                                        new_last_node->bg = COLOR_GREEN;
                                        break;
                                   case 43:
                                        new_last_node->bg = COLOR_YELLOW;
                                        break;
                                   case 44:
                                        new_last_node->bg = COLOR_BLUE;
                                        break;
                                   case 45:
                                        new_last_node->bg = COLOR_MAGENTA;
                                        break;
                                   case 46:
                                        new_last_node->bg = COLOR_CYAN;
                                        break;
                                   case 47:
                                        new_last_node->bg = COLOR_WHITE;
                                        break;
                                   case 48:
                                        break;
                                   case 49:
                                        new_last_node->bg = COLOR_BACKGROUND;
                                        break;
                                   }

                              } break;
                              }

                              csi_argument_index = 0;

                              // clear arguments
                              for(int c = 0; c < 16; ++c) csi_arguments[c] = 0;
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
                    if(ce_point_on_buffer(&term->buffer, term->cursor)){
                         int line_last_index = ce_last_index(term->buffer.lines[term->cursor.y]);

                         if(line_last_index && term->cursor.x <= line_last_index){
                              ce_set_char_readonly(&term->buffer, term->cursor, *byte);
                         }else{
                              ce_insert_char_readonly(&term->buffer, term->cursor, *byte);
                         }
                    }else{
                         ce_insert_char_readonly(&term->buffer, term->cursor, *byte);
                    }

                    term->cursor.x++;

                    if(term->cursor.x >= term->width){
                         ce_append_char_readonly(&term->buffer, NEWLINE); // ignore where the cursor is
                         term->cursor.x = 0;
                         term->cursor.y++;

                         // TODO: consolidate with code below
                         term->color_lines = realloc(term->color_lines, term->buffer.line_count * sizeof(*term->color_lines));

                         TerminalColorNode_t* itr = term->color_lines + (term->buffer.line_count - 2);
                         while(itr->next) itr = itr->next;

                         // NOTE: copy the color profile from the end of the previous line
                         term->color_lines[term->buffer.line_count - 1] = *itr;
                    }
               }else{
                    switch(*byte){
                    case 27: // escape
                         escape = true;
                         break;
                    case '\b': // backspace
                         term->cursor.x--;
                         if(term->cursor.x < 0) term->cursor.x = 0;

                         // TODO: did we delete far enough passed a color node?
                         break;
                    case NEWLINE:
                    {
                         ce_append_char_readonly(&term->buffer, NEWLINE); // ignore where the cursor is
                         term->cursor.x = 0;
                         term->cursor.y++;

                         term->color_lines = realloc(term->color_lines, term->buffer.line_count * sizeof(*term->color_lines));

                         TerminalColorNode_t* itr = term->color_lines + (term->buffer.line_count - 2);
                         while(itr->next) itr = itr->next;

                         // NOTE: copy the color profile from the end of the previous line
                         term->color_lines[term->buffer.line_count - 1] = *itr;
                    } break;
                    case '\r': // Carriage return
                         term->cursor.x = 0;
                         break;
                    }
               }

               byte++;
          }

          if(rc){
               sem_post(&term->updated);
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

          // query env
          const struct passwd *pw = getpwuid(getuid());
          if(pw == NULL){
               fprintf(stderr, "getpwuid() failed %s\n", strerror(errno));
               return false;
          }

          char* sh = (pw->pw_shell[0]) ? pw->pw_shell : "/bin/bash";

          // reset env
          unsetenv("COLUMNS");
          unsetenv("LINES");
          unsetenv("TERMCAP");
          setenv("LOGNAME", pw->pw_name, 1);
          setenv("USER", pw->pw_name, 1);
          setenv("SHELL", sh, 1);
          setenv("HOME", pw->pw_dir, 1);
          setenv("TERM", "dumb", 1); // we can change this to "urxvt" when we implement all VT102 commands

          // reset signal handlers
          signal(SIGCHLD, SIG_DFL);
          signal(SIGHUP, SIG_DFL);
          signal(SIGINT, SIG_DFL);
          signal(SIGQUIT, SIG_DFL);
          signal(SIGTERM, SIG_DFL);
          signal(SIGALRM, SIG_DFL);

          // exec shell
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

     term->is_alive = true;

     term->width = width;
     term->height = height;

     if(!ce_alloc_lines(&term->buffer, 1)){
          term->is_alive = false;
          return false;
     }

     int rc = pthread_create(&term->reader_thread, NULL, terminal_reader, term);
     if(rc != 0){
          term->is_alive = false;
          ce_message("pthread_create() failed");
          return false;
     }

     sem_init(&term->updated, 0, 1);

     term->buffer.status = BS_READONLY;
     term->buffer.name = strdup("[terminal]");

     term->cursor = (Point_t){0, 0};

     term->color_lines = calloc(1, sizeof(*term->color_lines));
     term->color_lines->fg = COLOR_FOREGROUND;
     term->color_lines->bg = COLOR_BACKGROUND;

     return true;
}

void terminal_free(Terminal_t* term)
{
     if(term->is_alive){
          kill(pid, SIGHUP);
     }

     if(term->fd){
          sem_destroy(&term->updated);
          pthread_cancel(term->reader_thread);
          ce_free_buffer(&term->buffer);
     }

     term->is_alive = false;
     term->width = 0;
     term->height = 0;
     term->fd = 0;

     free(term->color_lines);
}

bool terminal_resize(Terminal_t* term, int64_t width, int64_t height)
{
     struct winsize window_size = {width, height, 0, 0};

     if(ioctl(term->fd, TIOCSWINSZ, &window_size) < 0){
          ce_message("%s() ioctl() failed %s", __FUNCTION__, strerror(errno));
          return false;
     }

     return true;
}

bool terminal_send_key(Terminal_t* term, int key)
{
     char character;
     int size;
     const char* string;

     if(key >= 0 && key < 256){
          character = (char)(key);
          size = 1;
          string = &character;
     }else{
          switch(key){
          case -1:
               character = 3;
               size = 1;
               string = &character;
               break;
          case 339:
               character = 4;
               size = 1;
               string = &character;
               break;
          default:
               string = keybound(key, 0);
               size = strlen(string);
               break;
          }
     }

     // TODO: get mutex

     int rc = write(term->fd, string, size);
     if(rc < 0){
          ce_message("%s() write() to terminal failed: %s", __FUNCTION__, strerror(errno));
          return false;
     }

     return true;
}

void terminal_highlight(const Buffer_t* buffer, Point_t top_left, Point_t bottom_right, Point_t cursor, Point_t loc,
                        const regex_t* highlight_regex, LineNumberType_t line_number_type, HighlightLineType_t highlight_line_type,
                        void* user_data, bool first_call)
{
     (void)(buffer);
     (void)(top_left);
     (void)(bottom_right);
     (void)(cursor);
     (void)(loc);
     (void)(highlight_regex);
     (void)(line_number_type);
     (void)(highlight_line_type);

     if(!user_data) return;

     TerminalHighlight_t* terminal_highlight = user_data;
     TerminalColorNode_t* color_node = terminal_highlight->terminal->color_lines + loc.y;

     if(first_call){
          terminal_highlight->unique_color_id = S_AUTO_COMPLETE + 1;
          terminal_highlight->last_fg = -1;
          terminal_highlight->last_bg = -1;
          return;
     }

     while(color_node){
          if(!color_node->next) break;
          if(color_node->next->index > loc.x) break;

          color_node = color_node->next;
     }

     if(!color_node) return;

     if(terminal_highlight->last_fg == color_node->fg && terminal_highlight->last_bg == color_node->bg){
          return;
     }

     standend();

     if(color_node->fg >= 0 || color_node->bg >= 0){
          init_pair(terminal_highlight->unique_color_id, color_node->fg, color_node->bg);

          attron(COLOR_PAIR(terminal_highlight->unique_color_id));

          terminal_highlight->last_fg = color_node->fg;
          terminal_highlight->last_bg = color_node->bg;

          terminal_highlight->unique_color_id++;
     }
}
