#include "completion.h"

#include <assert.h>
#include <dirent.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>

extern pthread_mutex_t draw_lock;
extern pthread_mutex_t completion_lock;

static void str_collapse_chars(char* string, char* collapseable_chars)
{
     char* src = string;
     char* dst = string;

     while(*dst){
          bool collapseable = false;
          for(char* collapse_itr = collapseable_chars; *collapse_itr != 0; ++collapse_itr){
               if(*dst == *collapse_itr){
                    collapseable = true;
                    break;
               }
          }

          *src = *dst;
          if(!collapseable) src++;
          dst++;
     }

     // append the null byte to the end
     *src = 0;
}

// NOTE: stderr is redirected to stdout
static pid_t bidirectional_popen(const char* cmd, int* in_fd, int* out_fd)
{
     int input_fds[2];
     int output_fds[2];

     if(pipe(input_fds) != 0) return 0;
     if(pipe(output_fds) != 0) return 0;

     pid_t pid = fork();
     if(pid < 0) return 0;

     if(pid == 0){
          close(input_fds[1]);
          close(output_fds[0]);

          dup2(input_fds[0], STDIN_FILENO);
          dup2(output_fds[1], STDOUT_FILENO);
          dup2(output_fds[1], STDERR_FILENO);

          execl("/bin/sh", "/bin/sh", "-c", cmd, NULL);
          assert(0);
     }else{
         close(input_fds[0]);
         close(output_fds[1]);

         *in_fd = input_fds[1];
         *out_fd = output_fds[0];
     }

     return pid;
}

void completion_update_buffer(Buffer_t* completion_buffer, AutoComplete_t* auto_complete, const char* match)
{
     assert(completion_buffer->status == BS_READONLY);
     ce_clear_lines_readonly(completion_buffer);

     int64_t match_len = 0;
     if(match){
          match_len = strlen(match);
     }else{
          match = "";
     }
     int64_t line_count = 0;
     CompleteNode_t* itr = auto_complete->head;
     char line[256];
     while(itr){
          bool matches = false;

          if(auto_complete->type == ACT_EXACT){
               matches = (strncmp(itr->option, match, match_len) == 0 || match_len == 0);
          }else if(auto_complete->type == ACT_OCCURANCE){
               matches = (strstr(itr->option, match) || match_len == 0);
          }

          if(matches){
               if(itr->description){
                    snprintf(line, 256, "%s %s", itr->option, itr->description);
               }else{
                    snprintf(line, 256, "%s", itr->option);
               }

               ce_append_line_readonly(completion_buffer, line);
               line_count++;

               if(itr == auto_complete->current){
                    int64_t last_index = line_count - 1;
                    completion_buffer->highlight_start = (Point_t){0, last_index};
                    completion_buffer->highlight_end = (Point_t){strlen(completion_buffer->lines[last_index]), last_index};
               }
          }

          itr = itr->next;
     }
}

bool completion_gen_files_in_current_dir(AutoComplete_t* auto_complete, const char* dir)
{
     struct dirent *node;
     DIR* os_dir = opendir(dir);
     if(!os_dir) return false;

     auto_complete_free(auto_complete);

     char tmp[PATH_MAX];
     struct stat info;
     while((node = readdir(os_dir)) != NULL){
          snprintf(tmp, PATH_MAX, "%s/%s", dir, node->d_name);
          stat(tmp, &info);
          if(S_ISDIR(info.st_mode)){
               snprintf(tmp, PATH_MAX, "%s/", node->d_name);
          }else{
               strncpy(tmp, node->d_name, PATH_MAX);
          }
          auto_complete_insert(auto_complete, tmp, NULL);
     }

     closedir(os_dir);

     if(!auto_complete->head) return false;
     return true;
}

bool completion_calc_start_and_path(AutoComplete_t* auto_complete, const char* line, Point_t cursor,
                                    Buffer_t* completion_buffer, const char* start_path)
{
     // we only auto complete in the case where the cursor is up against path with directories
     // -pat|
     // -/dir/pat|
     // -/base/dir/pat|
     const char* path_begin = line + cursor.x;
     const char* last_slash = NULL;

     // if the cursor is not on the null terminator, skip
     if(cursor.x > 0){
          if(*path_begin != '\0') return false;

          while(path_begin >= line){
               if(!last_slash && *path_begin == '/') last_slash = path_begin;
               if(isblank(*path_begin)) break;
               path_begin--;
          }

          path_begin++; // account for iterating 1 too far
     }

     if(!start_path) start_path = ".";

     pthread_mutex_lock(&completion_lock);

     // generate based on the path
     bool rc = false;
     if(last_slash){
          int64_t user_path_len = (last_slash - path_begin) + 1;

          if(*path_begin != '/'){
               int64_t start_path_len = strlen(start_path) + 1;
               int64_t path_len = user_path_len + start_path_len;

               char* path = malloc(path_len + 1);
               if(!path){
                    ce_message("failed to alloc path");
                    return false;
               }

               memcpy(path, start_path, start_path_len - 1);
               path[start_path_len - 1] = '/'; // add a slash between, since start_path doesn't come with one
               memcpy(path + start_path_len, path_begin, user_path_len);
               path[path_len] = 0;

               rc = completion_gen_files_in_current_dir(auto_complete, path);
               free(path);
          }else{
               char* path = malloc(user_path_len + 1);
               if(!path){
                    ce_message("failed to alloc path");
                    return false;
               }

               memcpy(path, path_begin, user_path_len);

               path[user_path_len] = 0;

               rc = completion_gen_files_in_current_dir(auto_complete, path);
               free(path);
          }
     }else{
          rc = completion_gen_files_in_current_dir(auto_complete, start_path);
     }

     // set the start point if we generated files
     if(rc){
          if(last_slash){
               const char* completion = last_slash + 1;
               auto_complete_start(auto_complete, ACT_OCCURANCE, (Point_t){(last_slash - line) + 1, cursor.y});
               auto_complete_next(auto_complete, completion);
               completion_update_buffer(completion_buffer, auto_complete, completion);
          }else{
               auto_complete_start(auto_complete, ACT_OCCURANCE, (Point_t){(path_begin - line), cursor.y});
               auto_complete_next(auto_complete, path_begin);
               completion_update_buffer(completion_buffer, auto_complete, path_begin);
          }
     }

     pthread_mutex_unlock(&completion_lock);
     return rc;
}

typedef struct{
     Buffer_t* buffer_to_complete;
     Point_t start;
     Point_t cursor;
     Buffer_t* clang_output_buffer;
     Buffer_t* completion_buffer;
     AutoComplete_t* auto_complete;
     ConfigState_t* config_state;
}ClangCompleteThreadData_t;

void clang_complete_thread_cleanup(void* data)
{
     free(data);
}

void* clang_complete_thread(void* data)
{
     ClangCompleteThreadData_t* thread_data = data;

     pthread_cleanup_push(clang_complete_thread_cleanup, data);

     const char* compiler = NULL;
     const char* language_flag = NULL;

     if(thread_data->buffer_to_complete->type == BFT_C){
          compiler = "clang";
          language_flag = "-x c";
     }else if(thread_data->buffer_to_complete->type == BFT_CPP){
          compiler = "clang++";
          language_flag = "-x c++";
     }else{
          ce_message("unsupported clang completion on buffer type %d", thread_data->buffer_to_complete->type);
          pthread_exit(NULL);
     }

     // NOTE: extend to fit more flags
     char bytes[BUFSIZ];
     bytes[0] = 0;

     char base_include[PATH_MAX];
     base_include[0] = 0;

     // build flags filepath
     if(thread_data->buffer_to_complete->name[0] == '/'){
          const char* last_slash = strrchr(thread_data->buffer_to_complete->name, '/');
          int path_len = last_slash - thread_data->buffer_to_complete->name;
          snprintf(bytes, BUFSIZ, "%.*s/.clang_complete", path_len, thread_data->buffer_to_complete->name);
          snprintf(base_include, PATH_MAX, "-I%.*s", path_len, thread_data->buffer_to_complete->name);
     }else{
          strncpy(bytes, ".clang_complete", BUFSIZ);
     }

     // load flags, each flag is on its own line
     {
          FILE* flags_file = fopen(bytes, "r");
          bytes[0] = 0;
          if(flags_file){
               char line[BUFSIZ];
               size_t line_len;
               size_t written = 0;
               while(fgets(line, BUFSIZ, flags_file)){
                    line_len = strlen(line);
                    line[line_len - 1] = ' ';
                    if(written + line_len > BUFSIZ) break;
                    // filter out linker flags
                    if(strncmp(line, "-Wl", 3) == 0) continue;
                    strncpy(bytes + written, line, line_len);
                    written += line_len;
               }
               bytes[written - 1] = 0;
               fclose(flags_file);
          }else{
               pthread_exit(NULL);
          }
     }

     // run command
     char command[BUFSIZ];
     snprintf(command, BUFSIZ, "%s %s %s -fsyntax-only -ferror-limit=1 %s - -Xclang -code-completion-macros -Xclang -code-completion-at=-:%ld:%ld",
              compiler, bytes, base_include, language_flag, thread_data->cursor.y + 1, thread_data->cursor.x + 1);

     int input_fd = 0;
     int output_fd = 0;
     pid_t pid = bidirectional_popen(command, &input_fd, &output_fd);
     if(pid == 0){
          ce_message("failed to do bidirectional_popen() with clang command\n");
          pthread_exit(NULL);
     }

     // write buffer data to stdin
     char* contents = ce_dupe_buffer(thread_data->buffer_to_complete);
     ssize_t len = strlen(contents);
     ssize_t written = 0;

     while(written < len){
          ssize_t bytes_written = write(input_fd, contents + written, len - written);
          if(bytes_written < 0){
               ce_message("failed to write to clang input fd: '%s'", strerror(errno));
               pthread_exit(NULL);
          }
          written += bytes_written;
     }

     close(input_fd);

     free(contents);

     ce_clear_lines(thread_data->clang_output_buffer);

     // collect output
     int status = 0;
     pid_t w;
     ssize_t byte_count = 1;

     do{
          while(byte_count != 0){
               byte_count = read(output_fd, bytes, BUFSIZ);
               if(byte_count < 0){
                    ce_message("%s() read from pid %d failed\n", __FUNCTION__, pid);
                    pthread_exit(NULL);
               }else if(byte_count > 0){
                    bytes[byte_count] = 0;
                    ce_append_line(thread_data->clang_output_buffer, bytes);
               }
          }

          w = waitpid(pid, &status, WNOHANG);
          if(w == -1){
               pthread_exit(NULL);
          }

          if(WIFEXITED(status)){
               int rc = WEXITSTATUS(status);
               if(rc != 0) ce_message("clang proccess pid %d exited, status = %d\n", pid, WEXITSTATUS(status));
          }else if(WIFSIGNALED(status)){
               ce_message("clang proccess pid %d killed by signal %d\n", pid, WTERMSIG(status));
          }else if(WIFSTOPPED(status)){
               ce_message("clang proccess pid %d stopped by signal %d\n", pid, WSTOPSIG(status));
          }else if (WIFCONTINUED(status)){
               ce_message("clang proccess pid %d continued\n", pid);
          }
     }while(!WIFEXITED(status) && !WIFSIGNALED(status));

     pthread_mutex_lock(&completion_lock);
     auto_complete_free(thread_data->auto_complete);

     // populate auto complete
     // addnstr : [#int#]addnstr(<#const char *#>, <#int#>)
     for(int64_t i = 0; i < thread_data->clang_output_buffer->line_count; ++i){
          const char* line = thread_data->clang_output_buffer->lines[i];
          if(strncmp(line, "COMPLETION: ", 12) == 0){
               const char* start = line + 12;
               const char* end = strchr(start, ' ');
               const char* type_start = NULL;
               const char* type_end = NULL;
               const char* prototype = NULL;
               int completion_len = (end - start);
               if(end){
                    type_start = strstr(end, "[#");
                    if(type_start) type_start += 2; // advance the pointer
                    type_end = strstr(end, "#]");

                    if(type_start && type_end) prototype = type_end + 2;
               }else{
                    completion_len = strlen(start);
               }

               char option[completion_len + 1];
               strncpy(option, start, completion_len);
               option[completion_len] = 0;

               if(type_start && type_end){
                    const char* prototype_has_parens = strchr(prototype, '(');
                    if(prototype_has_parens){
                         snprintf(bytes, BUFSIZ, "%.*s %s", (int)(type_end - type_start), type_start, prototype);
                         str_collapse_chars(bytes, "<>#");
                    }else{
                         snprintf(bytes, BUFSIZ, "%.*s", (int)(type_end - type_start), type_start);
                    }

                    auto_complete_insert(thread_data->auto_complete, option, bytes);
               }else{
                    auto_complete_insert(thread_data->auto_complete, option, NULL);
               }
          }
     }

     // if any elements existed, let us know
     if(thread_data->auto_complete->head){
          auto_complete_start(thread_data->auto_complete, ACT_EXACT, thread_data->start);
          Point_t end = thread_data->cursor;
          end.x--;
          if(end.x < 0) end.x = 0;
          if(!ce_points_equal(thread_data->start, end)){
               char* match = ce_dupe_string(thread_data->buffer_to_complete, thread_data->start, end);
               auto_complete_next(thread_data->auto_complete, match);
               completion_update_buffer(thread_data->completion_buffer, thread_data->auto_complete, match);
               free(match);
          }else{
               completion_update_buffer(thread_data->completion_buffer, thread_data->auto_complete, "");
          }
     }

     pthread_mutex_unlock(&completion_lock);

     // TODO: merge with terminal thread re-drawing code
     struct timeval current_time;
     uint64_t elapsed = 0;

     // make sure the other view drawer is done before drawing
     pthread_mutex_lock(&draw_lock);

     // wait for our interval limit, before drawing
     do{
          gettimeofday(&current_time, NULL);
          elapsed = (current_time.tv_sec - thread_data->config_state->last_draw_time.tv_sec) * 1000000LL +
                    (current_time.tv_usec - thread_data->config_state->last_draw_time.tv_usec);
     }while(elapsed < DRAW_USEC_LIMIT);

     view_drawer(thread_data->config_state);
     pthread_mutex_unlock(&draw_lock);

     pthread_cleanup_pop(data);

     return NULL;
}

void clang_completion(ConfigState_t* config_state, Point_t start_completion)
{
     if(config_state->clang_complete_thread){
          pthread_cancel(config_state->clang_complete_thread);
          pthread_detach(config_state->clang_complete_thread);
          config_state->clang_complete_thread = 0;
     }

     if(auto_completing(&config_state->auto_complete)){
          auto_complete_end(&config_state->auto_complete);
     }

     ClangCompleteThreadData_t* thread_data = malloc(sizeof(*thread_data));
     if(!thread_data) return;
     thread_data->buffer_to_complete = config_state->tab_current->view_current->buffer;
     thread_data->start = start_completion;
     thread_data->cursor = config_state->tab_current->view_current->cursor;
     thread_data->clang_output_buffer = &config_state->clang_completion_buffer;
     thread_data->completion_buffer = config_state->completion_buffer;
     thread_data->auto_complete = &config_state->auto_complete;
     thread_data->config_state = config_state;
     int rc = pthread_create(&config_state->clang_complete_thread, NULL, clang_complete_thread, thread_data);
     if(rc != 0){
          ce_message("pthread_create() for clang auto complete failed");
     }
}
