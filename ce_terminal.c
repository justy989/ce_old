#include "ce_terminal.h"
#include "ce_syntax.h"

#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <signal.h>
#include <sys/wait.h>
#include <ctype.h>
#include <assert.h>

#ifdef __APPLE__
     #include <util.h>
     #include <sys/ioctl.h>
#else
     #include <pty.h>
     #include <fcntl.h> // for O_* constants (sem_open)
#endif

TerminalColorPairNode_t* terminal_color_pairs_head = NULL;

void terminal_switch_color(int fg, int bg)
{
     assert(terminal_color_pairs_head);

     TerminalColorPairNode_t* pair_itr = terminal_color_pairs_head;
     TerminalColorPairNode_t* prev = NULL;

     int color_id = TERM_START_COLOR;
     while(pair_itr){
          if(pair_itr->fg == fg && pair_itr->bg == bg) break;
          prev = pair_itr;
          pair_itr = pair_itr->next;
          color_id++;
     }

     if(!pair_itr){
          TerminalColorPairNode_t* node = calloc(1, sizeof(*node));
          if(!node) return;

          node->fg = fg;
          node->bg = bg;
          node->next = NULL;

          init_pair(color_id, node->fg, node->bg);

          prev->next = node;
     }

     attron(COLOR_PAIR(color_id));
}

void handle_sigchld(int signal, siginfo_t* info, void *ptr)
{
     (void)(ptr);

     int stat;
     pid_t p;

     ce_message("terminal shell saw signal: %d", signal);

     if(signal != SIGINT){
          if((p = waitpid(info->si_pid, &stat, WNOHANG)) < 0){
               ce_message("terminal waiting for shell pid %d failed: %s\n", info->si_pid, strerror(errno));
          }

          if(info->si_pid != p) return;

          if(!WIFEXITED(stat) || WEXITSTATUS(stat)){
               ce_message("terminal shell child exitted with code %d\n", stat);
          }
     }
}

void terminal_clear_region(Terminal_t* term, int64_t a_x, int64_t a_y, int64_t b_x, int64_t b_y)
{
     assert(a_x <= b_x && a_y <= b_y);

     for(int64_t y = a_y; y <= b_y; ++y){
          for(int64_t x = a_x; x <= b_x; ++x){
               ce_set_char(term->buffer, (Point_t){x, y}, ' ');
          }
     }
}

void terminal_scroll_up(Terminal_t* term, int64_t start, int64_t lines)
{
     int64_t max_lines = (term->scroll_bottom - start) + 1;

     if(lines < 0){
          lines = 0;
     }else if(lines > max_lines){
          lines = max_lines;
     }

     int64_t last_line = term->scroll_bottom - lines;

     terminal_clear_region(term, 0, start, term->width - 1, (start + lines) - 1);

     for(int64_t i = start; i <= last_line; ++i){
          char* tmp = term->buffer->lines[i];
          term->buffer->lines[i] = term->buffer->lines[i + lines];
          term->buffer->lines[i + lines] = tmp;
     }
}

void terminal_scroll_down(Terminal_t* term, int64_t start, int64_t lines)
{
     int64_t max_lines = (term->scroll_bottom - start) + 1;

     if(lines < 0){
          lines = 0;
     }else if(lines > max_lines){
          lines = max_lines;
     }

     int64_t last_line = term->scroll_bottom - lines;

     terminal_clear_region(term, 0, last_line + 1, term->width - 1, term->scroll_bottom);

     for(int64_t i = start; i <= last_line; ++i){
          char* tmp = term->buffer->lines[i];
          term->buffer->lines[i] = term->buffer->lines[i - lines];
          term->buffer->lines[i - lines] = tmp;
     }
}

void terminal_scroll_set(Terminal_t* term, int64_t scroll_top, int64_t scroll_bottom)
{
     int64_t last_row = term->height - 1;

     if(scroll_top < 0){
          scroll_top = 0;
     }else if(scroll_top > last_row){
          scroll_top = last_row;
     }

     if(scroll_bottom < 0){
          scroll_bottom = 0;
     }else if(scroll_bottom > last_row){
          scroll_bottom = last_row;
     }

     if(scroll_top > scroll_bottom){
          term->scroll_top = scroll_bottom;
          term->scroll_bottom = scroll_top;
     }else{
          term->scroll_top = scroll_top;
          term->scroll_bottom = scroll_bottom;
     }
}

void terminal_set_mode(Terminal_t* term, bool priv, bool on, int* args, int arg_count)
{
     for(int i = 0; i < arg_count; ++i){
          if(priv){
               switch(args[i]){
               default:
                    break;
               case 6:
                    term->cursor_origin = on;
                    break;
               // TODO: 1049 save/load cursor for xterm compatibility
               }
          }else{
               switch(args[i]){
               default:
                    break;
               }
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
               sem_post(term->updated);
               pthread_exit(NULL);
          }

          bytes[rc] = 0;

          int csi_arguments[16]; // NPAR, does it exist?
          int csi_argument_index = 0;
          bool csi_priv = false;

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
#if 0
                                   ce_message("unhandled csi: %c", *byte);
                                   for(int i = 0; i <= csi_argument_index; ++i){
                                        ce_message("  arg: %d", csi_arguments[i]);
                                   }
#endif
                                   break;
                              case '@': // insert blank chars
                              {
                                   int spaces = 1;
                                   if(csi_arguments[0]) spaces = csi_arguments[0];

                                   for(int c = 0; c < spaces; c++){
                                        if(ce_insert_char(term->buffer, term->cursor, ' ')){
                                             term->cursor.x++;

                                             if(term->cursor.x >= term->width){
                                                  term->cursor.x = 0;
                                                  term->cursor.y++;
                                             }
                                        }
                                   }
                              } break;
                              case 'A': // move cursor up
                              {
                                   int distance = 1;
                                   if(csi_arguments[0]) distance = csi_arguments[0];

                                   term->cursor.y -= distance;
                              } break;
                              case 'B': // move cursor down
                              case 'e':
                              {
                                   int distance = 1;
                                   if(csi_arguments[0]) distance = csi_arguments[0];

                                   term->cursor.y += distance;
                              } break;
                              case 'C': // move cursor forward
                              case 'a':
                              {
                                   int distance = 1;
                                   if(csi_arguments[0]) distance = csi_arguments[0];

                                   term->cursor.x += distance;
                              } break;
                              case 'D': // move cursor backward
                              {
                                   int distance = 1;
                                   if(csi_arguments[0]) distance = csi_arguments[0];

                                   term->cursor.x += distance;
                              } break;
                              case 'E': // move cursor down and to first column
                              {
                                   int distance = 1;
                                   if(csi_arguments[0]) distance = csi_arguments[0];

                                   term->cursor.y += distance;
                                   term->cursor.x = 0;
                              } break;
                              case 'F': // move cursor up and to first column
                              {
                                   int distance = 1;
                                   if(csi_arguments[0]) distance = csi_arguments[0];

                                   term->cursor.y -= distance;
                                   term->cursor.x = 0;
                              } break;
                              case 'G': // move to collumn
                              case '`':
                              {
                                   int column = 1;
                                   if(csi_arguments[0]) column = csi_arguments[0];

                                   term->cursor.x = column - 1;
                              } break;
                              case 'H': // move to row column
                              case 'f':
                              {
                                   int column = 1;
                                   if(csi_arguments[1]) column = csi_arguments[1];

                                   int row = 1;
                                   if(csi_arguments[0]) row = csi_arguments[0];

                                   term->cursor.x = column - 1;
                                   term->cursor.y = row - 1;
                              } break;
                              case 'J': // clear screen
                                   switch(csi_arguments[0]){
                                   default:
                                        break;
                                   case 0: // below cursor
                                        terminal_clear_region(term, term->cursor.x, term->cursor.y, term->width - 1, term->cursor.y);
                                        if(term->cursor.y < term->height){
                                             terminal_clear_region(term, 0, term->cursor.y + 1, term->width - 1, term->height - 1);
                                        }
                                        break;
                                   case 1: // above cursor
                                        terminal_clear_region(term, 0, term->cursor.y, term->cursor.x, term->cursor.y);
                                        if(term->cursor.y > 0){
                                             terminal_clear_region(term, 0, 0, term->width - 1, term->cursor.y - 1);
                                        }
                                        break;
                                   case 2: // everthang
                                        terminal_clear_region(term, 0, 0, term->width - 1, term->cursor.y);
                                        break;
                                   }
                                   break;
                              case 'K': // clear line
                                   switch(csi_arguments[0]){
                                   default:
                                        break;
                                   case 0: // right
                                        terminal_clear_region(term, term->cursor.x, term->cursor.y, term->width - 1, term->cursor.y);
                                        break;
                                   case 1: // left
                                        terminal_clear_region(term, 0, term->cursor.y, term->cursor.x, term->cursor.y);
                                        break;
                                   case 2: // all
                                        terminal_clear_region(term, 0, term->cursor.y, term->width - 1, term->cursor.y);
                                        break;
                                   }
                                   break;
                              case 'S':
                              {
                                   int lines = 1;
                                   if(csi_arguments[1]) lines = csi_arguments[1];
                                   terminal_scroll_up(term, term->scroll_top, lines);
                              } break;
                              case 'T':
                              {
                                   int lines = 1;
                                   if(csi_arguments[1]) lines = csi_arguments[1];
                                   terminal_scroll_down(term, term->scroll_top, lines);
                              } break;
                              case 'L': // insert newlines
                              {
                                   int lines = 1;
                                   if(csi_arguments[1]) lines = csi_arguments[1];

                                   for(int l = 0; l < lines; ++l){
                                        ce_insert_char(term->buffer, term->cursor, '\n');
                                        term->cursor.y++;
                                        term->cursor.x = 0;
                                   }
                              } break;
                              case 'M': // delete lines
                              {
                                   int characters = 1;
                                   if(csi_arguments[1]) characters = csi_arguments[1];

                                   for(int c = 0; c < characters; ++c){
                                        if(ce_remove_line(term->buffer, term->cursor.y)){
                                             term->cursor.y--;
                                        }
                                   }
                              } break;
                              case 'X': // erase n characters
                              {
                                   int characters = 1;
                                   if(csi_arguments[1]) characters = csi_arguments[1];

                                   terminal_clear_region(term, term->cursor.x, term->cursor.y, term->cursor.x + characters - 1, term->cursor.y);
                              } break;
                              case 'P':
                              {
                                   int characters = 1;
                                   if(csi_arguments[1]) characters = csi_arguments[1];

                                   for(int c = 0; c < characters; ++c){
                                        ce_remove_char(term->buffer, term->cursor);
                                   }
                              } break;
                              case 'd': // move to row
                              {
                                   int row = 1;
                                   if(csi_arguments[0]) row = csi_arguments[0];

                                   term->cursor.y = row - 1;
                              } break;
                              case '?':
                                   csi_priv = true;
                                   break;
                              case 'r': // set scrolling region
                                   if(!csi_priv){
                                        int top = 0;
                                        int bottom = term->scroll_bottom - 1;

                                        terminal_scroll_set(term, top, bottom);
                                        term->cursor = (Point_t){0, 0};
                                   }
                                   break;
                              case 'h':
                                   terminal_set_mode(term, csi_priv, true, csi_arguments, csi_argument_index + 1);
                                   break;
                              case 'l':
                                   terminal_set_mode(term, csi_priv, false, csi_arguments, csi_argument_index + 1);
                                   break;
                              case 'm':
                              {
                                   // get last node in row
                                   TerminalColorNode_t* last_node = term->color_lines + (term->buffer->line_count - 1);
                                   while(last_node->next) last_node = last_node->next;

                                   TerminalColorNode_t* new_last_node = calloc(1, sizeof(*new_last_node));
                                   if(!new_last_node) break;

                                   new_last_node->fg = last_node->fg;
                                   new_last_node->bg = last_node->bg;
                                   new_last_node->index = strlen(term->buffer->lines[term->buffer->line_count - 1]);
                                   new_last_node->next = NULL;
                                   last_node->next = new_last_node;

                                   for(int a = 0; a <= csi_argument_index; ++a){
                                        switch(csi_arguments[a]){
                                        default:
                                             break;
                                        case 1: // bold
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
                                   }

                              } break;
                              }

                              csi_argument_index = 0;
                              csi_priv = false;

                              // clear arguments
                              for(int c = 0; c < 16; ++c) csi_arguments[c] = 0;

                              csi = false;
                         }
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
                    if(ce_point_on_buffer(term->buffer, term->cursor)){
                         int line_last_index = ce_last_index(term->buffer->lines[term->cursor.y]);

                         if(line_last_index && term->cursor.x <= line_last_index){
                              ce_set_char_readonly(term->buffer, term->cursor, *byte);
                         }else{
                              ce_insert_char_readonly(term->buffer, term->cursor, *byte);
                         }
                    }else{
                         ce_insert_char_readonly(term->buffer, term->cursor, *byte);
                    }

                    term->cursor.x++;

                    if(term->cursor.x >= term->width){
                         ce_append_char_readonly(term->buffer, NEWLINE); // ignore where the cursor is
                         term->cursor.x = 0;
                         term->cursor.y++;

                         // TODO: consolidate with code below
                         term->color_lines = realloc(term->color_lines, term->buffer->line_count * sizeof(*term->color_lines));

                         TerminalColorNode_t* itr = term->color_lines + (term->buffer->line_count - 2);
                         while(itr->next) itr = itr->next;

                         // NOTE: copy the color profile from the end of the previous line
                         term->color_lines[term->buffer->line_count - 1] = *itr;
                    }
               }else{
                    switch(*byte){
                    case '\033': // escape
                         escape = true;
                         break;
                    case '\030': // clear csi
                         csi = false;
                         csi_argument_index = 0;
                         csi_priv = false;
                         for(int c = 0; c < 16; ++c) csi_arguments[c] = 0;
                         break;
                    case '\b': // backspace
                         term->cursor.x--;
                         if(term->cursor.x < 0) term->cursor.x = 0;
                         //ce_remove_char_readonly(&term->buffer, term->cursor);
                         break;
                    case NEWLINE:
                    {
                         ce_append_char_readonly(term->buffer, NEWLINE); // ignore where the cursor is
                         term->cursor.x = 0;
                         term->cursor.y++;

                         term->color_lines = realloc(term->color_lines, term->buffer->line_count * sizeof(*term->color_lines));

                         TerminalColorNode_t* itr = term->color_lines + (term->buffer->line_count - 2);
                         while(itr->next) itr = itr->next;

                         // NOTE: copy the color profile from the end of the previous line
                         term->color_lines[term->buffer->line_count - 1] = *itr;
                    } break;
                    case '\r': // Carriage return
                         term->cursor.x = 0;
                         break;
                    }
               }

               byte++;
          }

          // if we've read anything, say that we've updated!
          if(rc) sem_post(term->updated);
     }

     pthread_exit(NULL);
     return NULL;
}

bool terminal_init(Terminal_t* term, int64_t width, int64_t height, Buffer_t* buffer)
{
     if(term->is_alive) return false;

     int master_fd;
     int slave_fd;

     struct winsize window_size = {};

     window_size.ws_row = height;
     window_size.ws_col = width;

     // init tty
     if(openpty(&master_fd, &slave_fd, NULL, NULL, &window_size)){
          ce_message("%s() openpty() failed: %s", __FUNCTION__, strerror(errno));
          return false;
     }

     term->pid = fork();

     switch(term->pid){
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

          char* sh = "/bin/bash";

          // reset env
          unsetenv("COLUMNS");
          unsetenv("LINES");
          unsetenv("TERMCAP");
          setenv("LOGNAME", pw->pw_name, 1);
          setenv("USER", pw->pw_name, 1);
          setenv("SHELL", sh, 1);
          setenv("HOME", pw->pw_dir, 1);
          setenv("TERM", "rxvt", 1);

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
     {
          close(slave_fd);
          term->fd = master_fd;

          struct sigaction sa;
          memset(&sa, 0, sizeof(sa));
          sa.sa_sigaction = handle_sigchld;
          sigemptyset(&sa.sa_mask);
          if(sigaction(SIGCHLD, &sa, NULL) == -1){
               // TODO: handle error
          }
     } break;
     }

     term->is_alive = true;

     term->width = width;
     term->height = height;

     term->buffer = buffer;

     if(term->buffer->line_count == 0){
          if(!ce_alloc_lines(term->buffer, 1)){
               term->is_alive = false;
               return false;
          }
     }

     term->buffer->status = BS_READONLY;

     sem_unlink("terminal_updated");
     term->updated = sem_open("terminal_updated", O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 0);
     if(term->updated == SEM_FAILED){
          ce_message("%s() sem_open() failed %s", __FUNCTION__, strerror(errno));
          return false;
     }

     int rc = pthread_create(&term->reader_thread, NULL, terminal_reader, term);
     if(rc != 0){
          term->is_alive = false;
          ce_message("pthread_create() failed");
          return false;
     }

     int64_t last_line = term->buffer->line_count - 1;

     term->cursor = (Point_t){0, last_line};

     if(term->buffer->lines[last_line][0]){
          term->cursor.x = strlen(term->buffer->lines[last_line]);
     }

     if(!term->color_lines){
          term->color_lines = calloc(1, sizeof(*term->color_lines));
          term->color_lines->fg = COLOR_FOREGROUND;
          term->color_lines->bg = COLOR_BACKGROUND;
     }

     if(!terminal_color_pairs_head){
          terminal_color_pairs_head = calloc(1, sizeof(*terminal_color_pairs_head));
          if(!terminal_color_pairs_head) return false; // leak !

          terminal_color_pairs_head->fg = -1;
          terminal_color_pairs_head->bg = -1;

          init_pair(TERM_START_COLOR, terminal_color_pairs_head->fg, terminal_color_pairs_head->bg);
     }

     return true;
}

void terminal_free(Terminal_t* term)
{
     if(term->is_alive){
          kill(term->pid, SIGHUP);
     }

     if(term->fd){
          for(int64_t i = 0; i < term->buffer->line_count; ++i){
               TerminalColorNode_t* itr = term->color_lines + i;
               if(!itr->next) continue; // ignore the first node, skip if it's the only one

               itr = itr->next;

               while(itr){
                    TerminalColorNode_t* tmp = itr;
                    itr = itr->next;
                    free(tmp);
               }
          }

          free(term->color_lines);

          sem_close(term->updated);
          pthread_cancel(term->reader_thread);
          pthread_join(term->reader_thread, NULL);
     }

     term->is_alive = false;
     term->width = 0;
     term->height = 0;
     term->fd = 0;
}

bool terminal_resize(Terminal_t* term, int64_t width, int64_t height)
{
     struct winsize window_size = {};

     window_size.ws_row = height;
     window_size.ws_col = width;

     if(ioctl(term->fd, TIOCSWINSZ, &window_size) < 0){
          ce_message("%s() ioctl() failed %s", __FUNCTION__, strerror(errno));
          return false;
     }

     term->width = width;
     term->height = height;

     return true;
}

bool terminal_send_key(Terminal_t* term, int key)
{
     char character;
     int size;
     const char* string;
     bool free_string = false;

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
               free_string = true;
               break;
          }
     }

     int rc = write(term->fd, string, size);
     if(rc < 0){
          ce_message("%s() write() to terminal failed: %s", __FUNCTION__, strerror(errno));
          return false;
     }

     if(free_string) free((char*)string);

     return true;
}

char* terminal_get_current_directory(Terminal_t* term)
{
     char cwd_file[BUFSIZ];
     snprintf(cwd_file, BUFSIZ, "/proc/%d/cwd", term->pid);
     return realpath(cwd_file, NULL);
}

void terminal_highlight(SyntaxHighlighterData_t* data, void* user_data)
{
     return; // skip for now, becuase the underlying has to change!
     if(!user_data) return;

     TerminalHighlight_t* terminal_highlight = user_data;
     TerminalColorNode_t* color_node = terminal_highlight->terminal->color_lines + data->loc.y;

     switch(data->state){
     default:
     case SS_BEGINNING_OF_LINE:
          terminal_highlight->highlight_type = HL_OFF;
          break;
     case SS_INITIALIZING:
          terminal_highlight->last_fg = -1;
          terminal_highlight->last_bg = -1;
          terminal_highlight->highlight_type = HL_OFF;
          return;
     case SS_CHARACTER:
     {
          if(ce_point_in_range(data->loc, data->buffer->highlight_start, data->buffer->highlight_end)){
               terminal_highlight->highlight_type = HL_VISUAL;
          }else if(data->loc.y == data->cursor.y){
               terminal_highlight->highlight_type = HL_CURRENT_LINE;
          }else{
               terminal_highlight->highlight_type = HL_OFF;
          }

          while(color_node){
               if(!color_node->next) break;
               if(color_node->next->index > data->loc.x) break;

               color_node = color_node->next;
          }

          if(!color_node) return;

          int bg_color = color_node->bg;

          if(terminal_highlight->highlight_type == HL_VISUAL){
               short fg = 0;
               short bg = 0;
               pair_content(S_NORMAL_HIGHLIGHTED, &fg, &bg);
               bg_color = bg;
          }else if(terminal_highlight->highlight_type == HL_CURRENT_LINE){
               short fg = 0;
               short bg = 0;
               pair_content(S_NORMAL_CURRENT_LINE, &fg, &bg);
               bg_color = bg;
          }

          if(terminal_highlight->last_fg == color_node->fg && terminal_highlight->last_bg == bg_color){
               return;
          }

          terminal_switch_color(color_node->fg, bg_color);

          terminal_highlight->last_fg = color_node->fg;
          terminal_highlight->last_bg = bg_color;
     } break;
     case SS_END_OF_LINE:
     {
          while(color_node->next) color_node = color_node->next;

          if(!color_node) return;

          if(terminal_highlight->last_fg == color_node->fg && terminal_highlight->last_bg == color_node->bg){
               return;
          }

          terminal_switch_color(color_node->fg, color_node->bg);

          terminal_highlight->last_fg = color_node->fg;
          terminal_highlight->last_bg = color_node->bg;
     } break;
     }
}
