#include <assert.h>
#include <ctype.h>
#include <ftw.h>
#include <inttypes.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <poll.h>

#include "ce_config.h"
#include "ce_syntax.h"

#define SCROLL_LINES 1

const char* buffer_flag_string(Buffer_t* buffer)
{
     switch(buffer->status){
     default:
          break;
     case BS_READONLY:
          return "[RO] ";
     case BS_NEW_FILE:
          return "[NEW] ";
     case BS_MODIFIED:
          return "*";
     }

     return "";
}

void sigint_handler(int signal)
{
     ce_message("recieved signal %d", signal);
}

pthread_mutex_t draw_lock;
pthread_mutex_t view_input_save_lock;
pthread_mutex_t completion_lock;

int64_t count_digits(int64_t n)
{
     int count = 0;
     while(n > 0){
          n /= 10;
          count++;
     }

     return count;
}

bool string_all_digits(const char* string)
{
     for(const char* c = string; *c; c++){
          if(!isdigit(*c)) return false;
     }

     return true;
}

void view_drawer(void* user_data);

bool input_history_init(InputHistory_t* history)
{
     InputHistoryNode_t* node = calloc(1, sizeof(*node));
     if(!node){
          ce_message("%s() failed to malloc input history node", __FUNCTION__);
          return false;
     }

     history->head = node;
     history->tail = node;
     history->cur = node;

     return true;
}

bool input_history_commit_current(InputHistory_t* history)
{
     assert(history->tail);
     assert(history->cur);

     InputHistoryNode_t* node = calloc(1, sizeof(*node));
     if(!node){
          ce_message("%s() failed to malloc input history node", __FUNCTION__);
          return false;
     }

     history->tail->next = node;
     node->prev = history->tail;

     history->tail = node;
     history->cur = node;

     return true;
}

void input_history_free(InputHistory_t* history)
{
     while(history->head){
          free(history->head->entry);
          InputHistoryNode_t* tmp = history->head;
          history->head = history->head->next;
          free(tmp);
     }

     history->tail = NULL;
     history->cur = NULL;
}

bool input_history_next(InputHistory_t* history)
{
     if(!history->cur) return false;
     if(!history->cur->next) return false;

     history->cur = history->cur->next;
     return true;
}

bool input_history_prev(InputHistory_t* history)
{
     if(!history->cur) return false;
     if(!history->cur->prev) return false;

     history->cur = history->cur->prev;
     return true;
}

// location is {left_column, top_line} for the view
void scroll_view_to_location(BufferView_t* buffer_view, const Point_t* location){
     // TODO: should we be able to scroll the view above our first line?
     buffer_view->left_column = (location->x >= 0) ? location->x : 0;
     buffer_view->top_row = (location->y >= 0) ? location->y : 0;
}

void center_view(BufferView_t* view)
{
     int64_t view_height = view->bottom_right.y - view->top_left.y;
     Point_t location = (Point_t) {0, view->cursor.y - (view_height / 2)};
     scroll_view_to_location(view, &location);
}

void center_view_when_cursor_outside_portion(BufferView_t* view, float portion_start, float portion_end)
{
     int64_t view_height = view->bottom_right.y - view->top_left.y;
     Point_t location = (Point_t) {0, view->cursor.y - (view_height / 2)};

     int64_t current_scroll_y = view->cursor.y - view->top_row;
     float y_portion = ((float)(current_scroll_y) / (float)(view_height));

     if(y_portion < portion_start || y_portion > portion_end){
          scroll_view_to_location(view, &location);
     }
}

TabView_t* tab_view_insert(TabView_t* head)
{
     // find the tail
     TabView_t* itr = head; // if we use tab_current, we *may* be closer to the tail !
     while(itr->next) itr = itr->next;

     // create the new tab
     TabView_t* new_tab = calloc(1, sizeof(*new_tab));
     if(!new_tab){
          ce_message("failed to allocate tab");
          return NULL;
     }

     // attach to the end of the tail
     itr->next = new_tab;

     // allocate the view
     new_tab->view_head = calloc(1, sizeof(*new_tab->view_head));
     if(!new_tab->view_head){
          ce_message("failed to allocate new view for tab");
          return NULL;
     }

     return new_tab;
}

void tab_view_remove(TabView_t** head, TabView_t* view)
{
     if(!*head || !view) return;

     // TODO: free any open views
     TabView_t* tmp = *head;
     if(*head == view){
          *head = view->next;
          free(tmp);
          return;
     }

     while(tmp->next != view) tmp = tmp->next;
     tmp->next = view->next;
     free(view);
}

void tab_view_save_overrideable(TabView_t* tab)
{
     if(!tab->view_overrideable) return;

     tab->overriden_buffer = tab->view_overrideable->buffer;
     tab->overriden_buffer->cursor = tab->view_overrideable->cursor;
}

void tab_view_restore_overrideable(TabView_t* tab)
{
     if(!tab->view_overrideable) return;

     tab->view_overrideable->buffer = tab->overriden_buffer;
     tab->view_overrideable->cursor = tab->overriden_buffer->cursor;
     center_view(tab->view_overrideable);
}

bool auto_complete_insert(AutoComplete_t* auto_complete, const char* option, const char* description)
{
     CompleteNode_t* new_node = calloc(1, sizeof(*new_node));
     if(!new_node){
          ce_message("failed to allocate auto complete option");
          return false;
     }

     new_node->option = strdup(option);
     if(description) new_node->description = strdup(description);

     if(auto_complete->tail){
          auto_complete->tail->next = new_node;
          new_node->prev = auto_complete->tail;
     }

     auto_complete->tail = new_node;
     if(!auto_complete->head){
          auto_complete->head = new_node;
     }

     return true;
}

void auto_complete_start(AutoComplete_t* auto_complete, AutoCompleteType_t type, Point_t start)
{
     assert(start.x >= 0);
     auto_complete->start = start;
     auto_complete->type = type;
     auto_complete->current = auto_complete->head;
}

void auto_complete_end(AutoComplete_t* auto_complete)
{
     auto_complete->start.x = -1;
}

bool auto_completing(AutoComplete_t* auto_complete)
{
    return auto_complete->start.x >= 0;
}

int64_t string_common_beginning(const char* a, const char* b)
{
     size_t common = 0;

     while(*a && *b){
          if(*a == *b){
               common++;
          }else{
               break;
          }

          a++;
          b++;
     }

     return common;
}

// NOTE: allocates the string that is returned to the user
char* auto_complete_get_completion(AutoComplete_t* auto_complete, int64_t x)
{
     int64_t offset = x - auto_complete->start.x;
     if(offset < 0) return NULL;

     CompleteNode_t* itr = auto_complete->current;
     int64_t complete_len = strlen(auto_complete->current->option + offset);
     int64_t min_complete_len = complete_len;

     do{
          itr = itr->next;
          if(!itr) itr = auto_complete->head;
          int64_t option_len = strlen(itr->option);

          if(option_len <= offset) continue;
          if(auto_complete->type == ACT_EXACT){
               if(strncmp(auto_complete->current->option, itr->option, offset) != 0) continue;

               int64_t check_complete_len = string_common_beginning(auto_complete->current->option + offset, itr->option + offset);
               if(check_complete_len < min_complete_len) min_complete_len = check_complete_len;
          }
     }while(itr != auto_complete->current);

     if(min_complete_len) complete_len = min_complete_len;

     char* completion = malloc(complete_len + 1);
     strncpy(completion, auto_complete->current->option + offset, complete_len);
     completion[complete_len] = 0;

     return completion;
}

void auto_complete_free(AutoComplete_t* auto_complete)
{
     auto_complete_end(auto_complete);

     CompleteNode_t* itr = auto_complete->head;
     while(itr){
          CompleteNode_t* tmp = itr;
          itr = itr->next;
          free(tmp->option);
          free(tmp->description);
          free(tmp);
     }

     auto_complete->head = NULL;
     auto_complete->tail = NULL;
     auto_complete->current = NULL;
}

bool auto_complete_next(AutoComplete_t* auto_complete, const char* match)
{
     int64_t match_len = 0;
     if(match){
          match_len = strlen(match);
     }else{
          match = "";
     }
     CompleteNode_t* initial_node = auto_complete->current;

     do{
          auto_complete->current = auto_complete->current->next;
          if(!auto_complete->current) auto_complete->current = auto_complete->head;
          if(auto_complete->type == ACT_EXACT){
               if(strncmp(auto_complete->current->option, match, match_len) == 0) return true;
          }else if(auto_complete->type == ACT_OCCURANCE){
               if(strstr(auto_complete->current->option, match)) return true;
          }
     }while(auto_complete->current != initial_node);

     auto_complete_end(auto_complete);
     return false;
}

bool auto_complete_prev(AutoComplete_t* auto_complete, const char* match)
{
     int64_t match_len = 0;
     if(match){
          match_len = strlen(match);
     }else{
          match = "";
     }
     CompleteNode_t* initial_node = auto_complete->current;

     do{
          auto_complete->current = auto_complete->current->prev;
          if(!auto_complete->current) auto_complete->current = auto_complete->tail;
          if(auto_complete->type == ACT_EXACT){
               if(strncmp(auto_complete->current->option, match, match_len) == 0) return true;
          }else if(auto_complete->type == ACT_OCCURANCE){
               if(strstr(auto_complete->current->option, match)) return true;
          }
     }while(auto_complete->current != initial_node);

     auto_complete_end(auto_complete);
     return false;
}

void update_completion_buffer(Buffer_t* completion_buffer, AutoComplete_t* auto_complete, const char* match)
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


static bool string_ends_in_substring(const char* string, size_t string_len, const char* substring)
{
     size_t substring_len = strlen(substring);
     return string_len > substring_len && strcmp(string + (string_len - substring_len), substring) == 0;
}

bool initialize_buffer(Buffer_t* buffer){
     BufferState_t* buffer_state = calloc(1, sizeof(*buffer_state));
     if(!buffer_state){
          ce_message("failed to allocate buffer state.");
          return false;
     }

     BufferCommitNode_t* tail = calloc(1, sizeof(*tail));
     if(!tail){
          ce_message("failed to allocate commit history for buffer");
          free(buffer_state);
          return false;
     }

     buffer_state->commit_tail = tail;

     buffer->user_data = buffer_state;
     buffer->mark = (Point_t){-1, -1};

     if(buffer->name){
          int64_t name_len = strlen(buffer->name);
          if(string_ends_in_substring(buffer->name, name_len, ".c") ||
             string_ends_in_substring(buffer->name, name_len, ".h")){
               buffer->syntax_fn = syntax_highlight_c;
               buffer->syntax_user_data = malloc(sizeof(SyntaxC_t));
               buffer->type = BFT_C;
               if(!buffer->syntax_user_data){
                    ce_message("failed to allocate syntax user data for buffer");
                    free(buffer_state);
                    return false;
               }
          }else if(string_ends_in_substring(buffer->name, name_len, ".cpp") ||
                   string_ends_in_substring(buffer->name, name_len, ".hpp")){
               buffer->syntax_fn = syntax_highlight_cpp;
               buffer->syntax_user_data = malloc(sizeof(SyntaxCpp_t));
               buffer->type = BFT_CPP;
               if(!buffer->syntax_user_data){
                    ce_message("failed to allocate syntax user data for buffer");
                    free(buffer_state);
                    return false;
               }
          }else if(string_ends_in_substring(buffer->name, name_len, ".py")){
               buffer->syntax_fn = syntax_highlight_python;
               buffer->syntax_user_data = malloc(sizeof(SyntaxPython_t));
               buffer->type = BFT_PYTHON;
               if(!buffer->syntax_user_data){
                    ce_message("failed to allocate syntax user data for buffer");
                    free(buffer_state);
                    return false;
               }
          }else if(string_ends_in_substring(buffer->name, name_len, ".java")){
               buffer->syntax_fn = syntax_highlight_java;
               buffer->syntax_user_data = malloc(sizeof(SyntaxJava_t));
               buffer->type = BFT_JAVA;
               if(!buffer->syntax_user_data){
                    ce_message("failed to allocate syntax user data for buffer");
                    free(buffer_state);
                    return false;
               }
          }else if(string_ends_in_substring(buffer->name, name_len, ".sh")){
               buffer->syntax_fn = syntax_highlight_bash;
               buffer->syntax_user_data = malloc(sizeof(SyntaxBash_t));
               buffer->type = BFT_BASH;
               if(!buffer->syntax_user_data){
                    ce_message("failed to allocate syntax user data for buffer");
                    free(buffer_state);
                    return false;
               }
          }else if(string_ends_in_substring(buffer->name, name_len, ".cfg")){
               buffer->syntax_fn = syntax_highlight_config;
               buffer->syntax_user_data = malloc(sizeof(SyntaxConfig_t));
               buffer->type = BFT_CONFIG;
               if(!buffer->syntax_user_data){
                    ce_message("failed to allocate syntax user data for buffer");
                    free(buffer_state);
                    return false;
               }
          }else if(string_ends_in_substring(buffer->name, name_len, "COMMIT_EDITMSG") ||
                   string_ends_in_substring(buffer->name, name_len, ".patch") ||
                   string_ends_in_substring(buffer->name, name_len, ".diff")){
               buffer->syntax_fn = syntax_highlight_diff;
               buffer->syntax_user_data = malloc(sizeof(SyntaxDiff_t));
               buffer->type = BFT_DIFF;
               if(!buffer->syntax_user_data){
                    ce_message("failed to allocate syntax user data for buffer");
                    free(buffer_state);
                    return false;
               }
          }else if(buffer->line_count > 0){
               // check for '#!/bin/bash/python' type of file header
               if(strlen(buffer->lines[0]) > 1 && buffer->lines[0][0] == '#' && buffer->lines[0][1] == '!'){
                    if(strstr(buffer->lines[0], "python")){
                         buffer->syntax_fn = syntax_highlight_python;
                         buffer->syntax_user_data = malloc(sizeof(SyntaxPython_t));
                         buffer->type = BFT_PYTHON;
                         if(!buffer->syntax_user_data){
                              ce_message("failed to allocate syntax user data for buffer");
                              free(buffer_state);
                              return false;
                         }
                    }else if(strstr(buffer->lines[0], "/sh") ||
                             strstr(buffer->lines[0], "/bash")){
                         buffer->syntax_fn = syntax_highlight_bash;
                         buffer->syntax_user_data = malloc(sizeof(SyntaxBash_t));
                         buffer->type = BFT_BASH;
                         if(!buffer->syntax_user_data){
                              ce_message("failed to allocate syntax user data for buffer");
                              free(buffer_state);
                              return false;
                         }
                    }
               }
          }
     }

     if(!buffer->syntax_fn){
          buffer->syntax_fn = syntax_highlight_plain;
          buffer->syntax_user_data = malloc(sizeof(SyntaxPlain_t));
          buffer->type = BFT_PLAIN;
          if(!buffer->syntax_user_data){
               ce_message("failed to allocate syntax user data for buffer");
               free(buffer_state);
               return false;
          }
     }

     return true;
}

// NOTE: need to free the allocated str
char* str_from_file_stream(FILE* file){
     fseek(file, 0, SEEK_END);
     size_t file_size = ftell(file);
     fseek(file, 0, SEEK_SET);

     char* str = malloc(file_size + 1);
     fread(str, file_size, 1, file);
     str[file_size] = 0;
     return str;
}

// NOTE: we should return a BufferNode_t to be consistent with new_buffer_from_file?
Buffer_t* new_buffer_from_string(BufferNode_t* head, const char* name, const char* str){
     if(access(name, F_OK) == 0){
          ce_message("failed to create new buffer named '%s', file already exists.", name);
          return NULL;
     }

     Buffer_t* buffer = calloc(1, sizeof(*buffer));
     if(!buffer){
          ce_message("failed to allocate buffer");
          return NULL;
     }

     buffer->name = strdup(name);
     if(!buffer->name){
          ce_message("failed to allocate buffer name");
          free(buffer);
          return NULL;
     }

     if(!initialize_buffer(buffer)){
          free(buffer->name);
          free(buffer);
          return NULL;
     }

     if(str) ce_load_string(buffer, str);

     BufferNode_t* new_buffer_node = ce_append_buffer_to_list(head, buffer);
     if(!new_buffer_node){
          free(buffer->name);
          free(buffer->user_data);
          free(buffer);
          return NULL;
     }

     return buffer;
}

BufferNode_t* new_buffer_from_file(BufferNode_t* head, const char* filename)
{
     Buffer_t* buffer = calloc(1, sizeof(*buffer));
     if(!buffer){
          ce_message("failed to allocate buffer");
          return NULL;
     }

     LoadFileResult_t lfr = ce_load_file(buffer, filename);
     switch(lfr){
     default:
          assert(!"unsupported LoadFileResult_t");
          return false;
     case LF_DOES_NOT_EXIST:
          buffer->status = BS_NEW_FILE;
          buffer->name = strdup(filename);
          break;
     case LF_IS_DIRECTORY:
          return NULL;
     case LF_SUCCESS:
     {
          // adjust the filepath if it doesn't match our pwd
          char full_path[PATH_MAX + 1];
          char* res = realpath(filename, full_path);
          if(!res) break;
          free(buffer->name);
          char cwd[PATH_MAX + 1];
          if(getcwd(cwd, sizeof(cwd)) != NULL){
               size_t cwd_len = strlen(cwd);
               // if the file is in our current directory, only show part of the path
               if(strncmp(cwd, full_path, cwd_len) == 0){
                    buffer->name = strdup(full_path + cwd_len + 1);
               }else{
                    buffer->name = strdup(full_path);
               }
          }else{
               buffer->name = strdup(full_path);
          }
     } break;
     }

     if(!initialize_buffer(buffer)){
          free(buffer->filename);
          free(buffer);
     }

     BufferNode_t* new_buffer_node = ce_append_buffer_to_list(head, buffer);
     if(!new_buffer_node){
          free(buffer->filename);
          free(buffer->user_data);
          free(buffer);
          return NULL;
     }

     return new_buffer_node;
}

InputHistory_t* history_from_input_key(ConfigState_t* config_state)
{
     InputHistory_t* history = NULL;

     switch(config_state->input_key){
     default:
          break;
     case '/':
     case '?':
          history = &config_state->search_history;
          break;
     case ':':
          history = &config_state->command_history;
          break;
     }

     return history;
}

void input_start(ConfigState_t* config_state, const char* input_message, int input_key)
{
     ce_clear_lines(config_state->view_input->buffer);
     ce_alloc_lines(config_state->view_input->buffer, 1);
     config_state->input = true;
     config_state->view_input->cursor = (Point_t){0, 0};
     config_state->view_input->left_column = 0;
     config_state->view_input->top_row = 0;
     config_state->input_message = input_message;
     config_state->input_key = input_key;
     config_state->input_mode_save = config_state->vim_state.mode;
     if(config_state->vim_state.mode == VM_VISUAL_LINE || config_state->vim_state.mode == VM_VISUAL_RANGE){
          config_state->input_visual_save = config_state->vim_state.visual_start;
     }
     pthread_mutex_lock(&view_input_save_lock);
     config_state->tab_current->view_input_save = config_state->tab_current->view_current;
     pthread_mutex_unlock(&view_input_save_lock);
     config_state->tab_current->view_current = config_state->view_input;

     vim_enter_insert_mode(&config_state->vim_state, config_state->tab_current->view_current->buffer);

     // reset input history back to tail
     InputHistory_t* history = history_from_input_key(config_state);
     if(history) history->cur = history->tail;
}

void input_end(ConfigState_t* config_state)
{
     config_state->input = false;
     config_state->tab_current->view_current = config_state->tab_current->view_input_save;

     switch(config_state->input_mode_save){
     default:
     case VM_NORMAL:
          vim_enter_normal_mode(&config_state->vim_state);
          break;
     case VM_VISUAL_RANGE:
          vim_enter_visual_range_mode(&config_state->vim_state, config_state->input_visual_save);
          break;
     case VM_VISUAL_LINE:
          vim_enter_visual_line_mode(&config_state->vim_state, config_state->input_visual_save);
          break;
     }
}

void input_cancel(ConfigState_t* config_state)
{
     if(config_state->input_key == '/' || config_state->input_key == '?'){
          pthread_mutex_lock(&view_input_save_lock);
          config_state->tab_current->view_input_save->cursor = config_state->vim_state.search.start;
          pthread_mutex_unlock(&view_input_save_lock);
          center_view(config_state->tab_current->view_input_save);
     }else if(config_state->input_key == 6 ||
              config_state->input_key == ':'){
          if(config_state->tab_current->view_overrideable){
               tab_view_restore_overrideable(config_state->tab_current);
          }

          // free the search path so we can re-use it
          free(config_state->load_file_search_path);
          config_state->load_file_search_path = NULL;
     }

     input_end(config_state);
}

void override_buffer_in_view(TabView_t* tab_current, BufferView_t* view, Buffer_t* new_buffer, Buffer_t** buffer_before_override)
{
     if(tab_current->view_overrideable){
          tab_view_save_overrideable(tab_current);

          tab_current->view_overrideable->buffer = new_buffer;
          tab_current->view_overrideable->cursor = (Point_t){0, 0};
          center_view(tab_current->view_overrideable);
     }else{
          *buffer_before_override = view->buffer;
          (*buffer_before_override)->cursor = view->cursor;

          tab_current->view_current->buffer = new_buffer;
          tab_current->view_current->cursor = (Point_t){0, 0};
          tab_current->view_current->top_row = 0;
     }
}

#ifndef FTW_STOP
#define FTW_STOP 1
#define FTW_CONTINUE 0
#endif

typedef struct {
     const char* search_filename;
     BufferNode_t* head;
     BufferNode_t* new_node;
} NftwState_t;
__thread NftwState_t nftw_state;

int nftw_find_file(const char* fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
     (void)sb;
     if((typeflag == FTW_F || typeflag == FTW_SL) && !strcmp(&fpath[ftwbuf->base], nftw_state.search_filename)){
          nftw_state.new_node = new_buffer_from_file(nftw_state.head, nftw_state.search_filename);

          // update the buffer filename, we don't really want the basename, we want the full path from where we opened ce
          free(nftw_state.new_node->buffer->name);
          nftw_state.new_node->buffer->name = strdup(fpath + 2); // cut off initial './'
          return FTW_STOP;
     }
     return FTW_CONTINUE;
}

void free_buffer_state(BufferState_t* buffer_state)
{
     BufferCommitNode_t* itr = buffer_state->commit_tail;
     if(itr){
          while(itr->prev) itr = itr->prev;
          ce_commits_free(itr);
     }

     vim_marks_free(&buffer_state->vim_buffer_state.mark_head);
     free(buffer_state);
}

Buffer_t* open_file_buffer(BufferNode_t* head, const char* filename)
{
     struct stat new_file_stat;

     if(stat(filename, &new_file_stat) == 0){
          struct stat open_file_stat;
          BufferNode_t* itr = head;
          while(itr){
               // NOTE: should we cache inodes?
               if(stat(itr->buffer->name, &open_file_stat) == 0){
                    if(open_file_stat.st_ino == new_file_stat.st_ino){
                         return itr->buffer; // already open
                    }
               }

               itr = itr->next;
          }
     }else{
          // clang doesn't support nested functions so we need to deal with global state
          nftw_state.search_filename = filename;
          nftw_state.head = head;
          nftw_state.new_node = NULL;
          nftw(".", nftw_find_file, 20, FTW_CHDIR);
          if(nftw_state.new_node) return nftw_state.new_node->buffer;
     }

     BufferNode_t* node = new_buffer_from_file(head, filename);
     if(node) return node->buffer;

     return NULL;
}

bool delete_buffer_at_index(BufferNode_t** head, TabView_t* tab_head, int64_t buffer_index, TerminalNode_t** terminal_head, TerminalNode_t** terminal_current)
{
     // find which buffer the user wants to delete
     Buffer_t* delete_buffer = NULL;
     BufferNode_t* itr = *head;
     while(itr){
          if(buffer_index == 0){
               delete_buffer = itr->buffer;
               break;
          }
          buffer_index--;
          itr = itr->next;
     }

     if(!itr) return false;

     TerminalNode_t* term_itr = *terminal_head;
     TerminalNode_t* term_prev = NULL;

     while(term_itr){
          if(term_itr->buffer == delete_buffer) break;
          term_prev = term_itr;
          term_itr = term_itr->next;
     }

     if(term_itr){
          if(term_itr == *terminal_current) *terminal_current = NULL;

          pthread_cancel(term_itr->check_update_thread);
          pthread_join(term_itr->check_update_thread, NULL);
          terminal_free(&term_itr->terminal);

          if(term_prev){
               term_prev->next = term_itr->next;
          }else{
               (*terminal_head)->next = term_itr->next;
          }
     }

     ce_remove_buffer_from_list(head, delete_buffer);

     // change views that are showing this buffer for all tabs
     if(head){
          TabView_t* tab_itr = tab_head;
          while(tab_itr){
               ce_change_buffer_in_views(tab_itr->view_head, delete_buffer, (*head)->buffer);
               tab_itr = tab_itr->next;
          }
     }else{
          return false;
     }

     // free the buffer and it's state
     free_buffer_state(delete_buffer->user_data);
     ce_free_buffer(delete_buffer);

     return true;
}

void scroll_view_to_last_line(BufferView_t* view)
{
     view->top_row = view->buffer->line_count - (view->bottom_right.y - view->top_left.y);
     if(view->top_row < 0) view->top_row = 0;
}

void view_jump_insert(BufferViewState_t* view_state, const char* filepath, Point_t location)
{
     if(view_state == NULL) return;

     // update data
     int64_t next_index = view_state->jump_current + 1;

     if(next_index < JUMP_LIST_MAX - 1){
          strncpy(view_state->jumps[view_state->jump_current].filepath, filepath, PATH_MAX);
          view_state->jumps[view_state->jump_current].location = location;

          // clear all jumps afterwards
          for(int64_t i = next_index; i < JUMP_LIST_MAX; ++i){
               view_state->jumps[i].filepath[0] = 0;
          }

          // advance jump index
          view_state->jump_current = next_index;
     }else{
          for(int64_t i = 0; i < JUMP_LIST_MAX; ++i){
               view_state->jumps[i] = view_state->jumps[i + 1];
          }

          strncpy(view_state->jumps[view_state->jump_current].filepath, filepath, PATH_MAX);
          view_state->jumps[view_state->jump_current].location = location;
     }
}

void view_jump_to_previous(BufferView_t* view, BufferNode_t* buffer_head)
{
     BufferViewState_t* view_state = view->user_data;

     if(view_state->jump_current == 0) return; // history is empty

     int jump_index = view_state->jump_current - 1;
     Jump_t* jump = view_state->jumps + jump_index;
     if(!jump->filepath[0]) return; // the jump is empty

     // if this is our first jump, insert our current location
     if(view_state->jump_current < (JUMP_LIST_MAX)){
          Jump_t* forward_jump = view_state->jumps + view_state->jump_current + 1;

          if(!forward_jump->filepath[0]){
               view_jump_insert(view_state, view->buffer->filename, view->cursor);
          }
     }

     view_state->jump_current = jump_index;

     view->buffer = open_file_buffer(buffer_head, jump->filepath);
     view->cursor = jump->location;
     center_view(view);
}

void view_jump_to_next(BufferView_t* view, BufferNode_t* buffer_head)
{
     BufferViewState_t* view_state = view->user_data;

     if(view_state->jump_current >= JUMP_LIST_MAX - 1) return;

     int jump_index = view_state->jump_current + 1;
     Jump_t* jump = view_state->jumps + jump_index;
     if(!jump->filepath[0]) return; // the jump is empty

     view_state->jump_current = jump_index;

     view->buffer = open_file_buffer(buffer_head, jump->filepath);
     view->cursor = jump->location;
     center_view(view);
}

// NOTE: clear commits then create the initial required entry to restart
void reset_buffer_commits(BufferCommitNode_t** tail)
{
     BufferCommitNode_t* node = *tail;
     while(node->prev) node = node->prev;
     ce_commits_free(node);

     BufferCommitNode_t* new_tail = calloc(1, sizeof(*new_tail));
     if(!new_tail){
          ce_message("failed to allocate commit history for buffer");
          return;
     }

     *tail = new_tail;
}

// NOTE: modify last_jump if we succeed
bool goto_file_destination_in_buffer(BufferNode_t* head, Buffer_t* buffer, int64_t line, BufferView_t* head_view,
                                     BufferView_t* view, int64_t* last_jump, char* terminal_current_directory)
{
     if(!buffer->line_count) return false;

     assert(line >= 0);
     assert(line < buffer->line_count);

     char filename[BUFSIZ];
     char line_number_str[BUFSIZ];
     char column_number_str[BUFSIZ];

     column_number_str[0] = 0;

     // prepend the terminal current directory with a slash
     strncpy(filename, terminal_current_directory, BUFSIZ);
     int64_t filename_start = strlen(filename);
     filename[filename_start] = '/';
     filename_start++;

     // check for different file destination formats we understand
     if(buffer->lines[line][0] == '@' && buffer->lines[line][1] == '@'){
          // handle git diff format
          // '@@ -1633,9 +1636,26 @@ static int set_color(Syntax_t syntax, HighlightType_t highlight_type)'

          // search backward for a filename
          int64_t file_line = line - 1;
          for(;file_line >= 0; --file_line){
               if(buffer->lines[file_line][0] == '-' && buffer->lines[file_line][1] == '-' && buffer->lines[file_line][2] == '-'){
                    break;
               }else if(buffer->lines[file_line][0] == '+' && buffer->lines[file_line][1] == '+' && buffer->lines[file_line][2] == '+'){
                    break;
               }
          }

          if(file_line < 0) return false;

          // --- a/ce.c
          // +++ b/ce.c
          char* first_slash = strchr(buffer->lines[file_line], '/');
          if(!first_slash) return false;
          strncpy(filename + filename_start, first_slash + 1, BUFSIZ - filename_start);
          if(access(filename, F_OK) == -1) return false; // file does not exist

          char* plus = strchr(buffer->lines[line], '+');
          if(!plus) return false;
          char* comma = strchr(plus, ',');
          if(!comma) return false;

          int64_t line_number_len = comma - (plus + 1);
          assert(line_number_len < BUFSIZ);
          strncpy(line_number_str, plus + 1, line_number_len);
          line_number_str[line_number_len] = 0;
     }else if(buffer->lines[line][0] == '=' && buffer->lines[line][1] == '='){
          // handle valgrind format
          // '==7330==    by 0x638B16A: initializer (ce_config.c:1983)'
          char* open_paren = strchr(buffer->lines[line], '(');
          char* close_paren = strchr(buffer->lines[line], ')');
          if(!open_paren || !close_paren) return false;

          char* file_end = strchr(open_paren, ':');
          if(!file_end) return false;

          int64_t filename_len = file_end - (open_paren + 1);
          if(filename_len > (BUFSIZ - filename_start)) return false;
          strncpy(filename + filename_start, (open_paren + 1), filename_len);

          int64_t line_number_len = (close_paren - file_end) - 1;
          strncpy(line_number_str, file_end + 1, line_number_len);

          if(!string_all_digits(line_number_str)) return false;
     }else{
          // handle grep, and gcc formats
          // 'ce_config.c:1983:15'
          char* file_end = strpbrk(buffer->lines[line], ": ");
          if(!file_end) return false;
          if(buffer->lines[line][0] == '/') filename_start = 0; // if the buffer line starts with a '/', then overwrite the initial path
          int64_t filename_len = file_end - buffer->lines[line];
          if(filename_len > (BUFSIZ - filename_start)) return false;
          strncpy(filename + filename_start, buffer->lines[line], filename_len);
          filename[filename_start + filename_len] = 0;
          if(access(filename, F_OK) == -1) return false; // file does not exist

          char* line_number_begin_delim = NULL;
          char* line_number_end_delim = NULL;
          if(*file_end == ' '){
               // format: 'filepath search_symbol line '
               char* second_space = strchr(file_end + 1, ' ');
               if(!second_space) return false;
               line_number_begin_delim = second_space;
               line_number_end_delim = strchr(second_space + 1, ' ');
               if(!line_number_end_delim) return false;
          }else{
               // format: 'filepath:line:column:'
               line_number_begin_delim = file_end;
               char* second_colon = strchr(line_number_begin_delim + 1, ':');
               if(!second_colon) return false;
               line_number_end_delim = second_colon;
          }

          int64_t line_number_len = line_number_end_delim - (line_number_begin_delim + 1);
          strncpy(line_number_str, line_number_begin_delim + 1, line_number_len);
          line_number_str[line_number_len] = 0;

          if(!string_all_digits(line_number_str)) return false;

          char* third_colon = strchr(line_number_end_delim + 1, ':');
          if(third_colon){
               line_number_len = third_colon - (line_number_end_delim + 1);
               strncpy(column_number_str, line_number_end_delim + 1, line_number_len);
               column_number_str[line_number_len] = 0;

               if(!string_all_digits(column_number_str)) column_number_str[0] = 0;
          }
     }

     if(line_number_str[0]){
          Buffer_t* new_buffer = open_file_buffer(head, filename);
          if(new_buffer){
               view_jump_insert(view->user_data, view->buffer->filename, view->cursor);
               view->buffer = new_buffer;
               Point_t dst = {0, atoi(line_number_str) - 1}; // line numbers are 1 indexed
               ce_set_cursor(new_buffer, &view->cursor, dst);

               // check for optional column number
               if(column_number_str[0]){
                    dst.x = atoi(column_number_str) - 1; // column numbers are 1 indexed
                    assert(dst.x >= 0);
                    ce_set_cursor(new_buffer, &view->cursor, dst);
               }else{
                    ce_move_cursor_to_soft_beginning_of_line(new_buffer, &view->cursor);
               }

               center_view(view);
               BufferView_t* command_view = ce_buffer_in_view(head_view, buffer);
               if(command_view) command_view->top_row = line;
               *last_jump = line;
               return true;
          }
     }

     return false;
}

// TODO: rather than taking in config_state, I'd like to take in only the parts it needs, if it's too much, config_state is fine
void jump_to_next_shell_command_file_destination(BufferNode_t* head, ConfigState_t* config_state, bool forwards)
{
     TerminalNode_t* terminal_node = config_state->terminal_current;
     if(!terminal_node) return;

     Buffer_t* terminal_buffer = config_state->terminal_current->buffer;
     BufferView_t* terminal_view = ce_buffer_in_view(config_state->tab_current->view_head, terminal_buffer);

     if(!terminal_view){
          TerminalNode_t* term_itr = config_state->terminal_head;
          while(term_itr){
               terminal_view = ce_buffer_in_view(config_state->tab_current->view_head, term_itr->buffer);
               if(terminal_view){
                    terminal_buffer = term_itr->buffer;
                    config_state->terminal_current = term_itr;
                    break;
               }
               term_itr = term_itr->next;
          }
     }

     int64_t lines_checked = 0;
     int64_t delta = forwards ? 1 : -1;
     for(int64_t i = config_state->terminal_current->last_jump_location + delta; lines_checked < terminal_buffer->line_count;
         i += delta, lines_checked++){
          if(i == terminal_buffer->line_count && forwards){
               i = 0;
          }else if(i <= 0 && !forwards){
               i = terminal_buffer->line_count - 1;
          }

          char* terminal_current_directory = terminal_get_current_directory(&config_state->terminal_current->terminal);
          if(goto_file_destination_in_buffer(head, terminal_buffer, i, config_state->tab_current->view_head,
                                             config_state->tab_current->view_current, &config_state->terminal_current->last_jump_location,
                                             terminal_current_directory)){
               // NOTE: change the cursor, so when you go back to that buffer, your cursor is on the line we last jumped to
               terminal_buffer->cursor.x = 0;
               terminal_buffer->cursor.y = i;
               if(terminal_view) terminal_view->cursor = terminal_buffer->cursor;
               free(terminal_current_directory);
               break;
          }

          free(terminal_current_directory);
     }
}

void move_jump_location_to_end_of_output(TerminalNode_t* terminal_node)
{
     terminal_node->last_jump_location = terminal_node->buffer->line_count - 1;
}

void commit_input_to_history(Buffer_t* input_buffer, InputHistory_t* history)
{
     if(!input_buffer->line_count) return;

     char* saved = ce_dupe_buffer(input_buffer);

     if(!history->tail->prev || (history->tail->prev && strcmp(saved, history->tail->prev->entry) != 0)){
          history->tail->entry = saved;
          input_history_commit_current(history);
     }
}

void view_follow_cursor(BufferView_t* current_view, LineNumberType_t line_number_type)
{
     ce_follow_cursor(current_view->cursor, &current_view->left_column, &current_view->top_row,
                      current_view->bottom_right.x - current_view->top_left.x,
                      current_view->bottom_right.y - current_view->top_left.y,
                      current_view->bottom_right.x == (g_terminal_dimensions->x - 1),
                      current_view->bottom_right.y == (g_terminal_dimensions->y - 2),
                      line_number_type, current_view->buffer->line_count);
}

void view_follow_highlight(BufferView_t* current_view)
{
     ce_follow_cursor(current_view->buffer->highlight_start, &current_view->left_column, &current_view->top_row,
                      current_view->bottom_right.x - current_view->top_left.x,
                      current_view->bottom_right.y - current_view->top_left.y,
                      current_view->bottom_right.x == (g_terminal_dimensions->x - 1),
                      current_view->bottom_right.y == (g_terminal_dimensions->y - 2),
                      LNT_NONE, current_view->buffer->line_count);
}

// NOTE: stderr is redirected to stdout
pid_t bidirectional_popen(const char* cmd, int* in_fd, int* out_fd)
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


typedef struct{
     ConfigState_t* config_state;
     TerminalNode_t* terminal_node;
}TerminalCheckUpdateData_t;

void terminal_check_update_cleanup(void* data)
{
     // release locks we could be holding
     pthread_mutex_unlock(&draw_lock);

     free(data);
}

void* terminal_check_update(void* data)
{
     pthread_cleanup_push(terminal_check_update_cleanup, data);

     TerminalCheckUpdateData_t* check_update_data = data;
     ConfigState_t* config_state = check_update_data->config_state;
     Terminal_t* terminal = &check_update_data->terminal_node->terminal;
     struct timeval current_time;
     uint64_t elapsed = 0;

     while(terminal->is_alive){
          sem_wait(terminal->updated);

          BufferView_t* terminal_view = ce_buffer_in_view(config_state->tab_current->view_head, terminal->buffer);
          if(terminal_view){
               terminal_view->cursor = terminal->cursor;
               view_follow_cursor(terminal_view, LNT_NONE);
          }else{
               continue;
          }

          if(config_state->vim_state.mode == VM_INSERT && !terminal->is_alive){
               vim_enter_normal_mode(&config_state->vim_state);
          }

          // make sure the other view drawer is done before drawing
          pthread_mutex_lock(&draw_lock);

          // wait for our interval limit, before drawing
          do{
               gettimeofday(&current_time, NULL);
               elapsed = (current_time.tv_sec - config_state->last_draw_time.tv_sec) * 1000000LL +
                         (current_time.tv_usec - config_state->last_draw_time.tv_usec);
          }while(elapsed < DRAW_USEC_LIMIT);

          view_drawer(config_state);
          pthread_mutex_unlock(&draw_lock);
     }

     pthread_cleanup_pop(data);
     return NULL;
}

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
     snprintf(command, BUFSIZ, "%s %s %s -fsyntax-only -ferror-limit=1 %s - -Xclang -code-completion-at=-:%ld:%ld",
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
               update_completion_buffer(thread_data->completion_buffer, thread_data->auto_complete, match);
               free(match);
          }else{
               update_completion_buffer(thread_data->completion_buffer, thread_data->auto_complete, "");
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

TerminalNode_t* is_terminal_buffer(TerminalNode_t* terminal_head, Buffer_t* buffer)
{
     while(terminal_head){
          if(terminal_head->buffer == buffer) return terminal_head;

          terminal_head = terminal_head->next;
     }

     return NULL;
}

bool start_terminal_in_view(BufferView_t* buffer_view, TerminalNode_t* node, ConfigState_t* config_state)
{
     // TODO: create buffer_view_width() and buffer_view_height()
     int64_t width = buffer_view->bottom_right.x - buffer_view->top_left.x;
     int64_t height = buffer_view->bottom_right.y - buffer_view->top_left.y;

     if(!terminal_init(&node->terminal, width, height, node->buffer)){
          return false;
     }

     TerminalCheckUpdateData_t* check_update_data = calloc(1, sizeof(*check_update_data));
     check_update_data->config_state = config_state;
     check_update_data->terminal_node = node;

     int rc = pthread_create(&node->check_update_thread, NULL, terminal_check_update, check_update_data);
     if(rc != 0){
          ce_message("pthread_create() for terminal_check_update() failed");
          return false;
     }

     return true;
}

void split_view(BufferView_t* head_view, BufferView_t* current_view, bool horizontal, LineNumberType_t line_number_type)
{
     BufferView_t* new_view = ce_split_view(current_view, current_view->buffer, horizontal);
     if(new_view){
          Point_t top_left = {0, 0};
          Point_t bottom_right = {g_terminal_dimensions->x - 1, g_terminal_dimensions->y - 1};
          ce_calc_views(head_view, top_left, bottom_right);
          view_follow_cursor(current_view, line_number_type);
          new_view->cursor = current_view->cursor;
          new_view->top_row = current_view->top_row;
          new_view->left_column = current_view->left_column;

          BufferViewState_t* buffer_view_state = calloc(1, sizeof(*buffer_view_state));
          if(!buffer_view_state){
               ce_message("failed to allocate buffer view state");
          }else{
               new_view->user_data = buffer_view_state;
          }
     }
}

void switch_to_view_at_point(ConfigState_t* config_state, Point_t point)
{
     BufferView_t* next_view = NULL;

     if(point.x < 0) point.x = g_terminal_dimensions->x - 1;
     if(point.y < 0) point.y = g_terminal_dimensions->y - 1;
     if(point.x >= g_terminal_dimensions->x) point.x = 0;
     if(point.y >= g_terminal_dimensions->y) point.y = 0;

     if(config_state->input) next_view = ce_find_view_at_point(config_state->view_input, point);
     vim_stop_recording_macro(&config_state->vim_state);
     if(!next_view) next_view = ce_find_view_at_point(config_state->tab_current->view_head, point);

     if(next_view){
          // save view and cursor
          config_state->tab_current->view_previous = config_state->tab_current->view_current;
          config_state->tab_current->view_current->buffer->cursor = config_state->tab_current->view_current->cursor;
          config_state->tab_current->view_current = next_view;
          vim_enter_normal_mode(&config_state->vim_state);

          TerminalNode_t* terminal_node = is_terminal_buffer(config_state->terminal_head, next_view->buffer);
          if(terminal_node) config_state->terminal_current = terminal_node;
     }
}

void handle_mouse_event(ConfigState_t* config_state, Buffer_t* buffer, BufferView_t* buffer_view, Point_t* cursor)
{
     MEVENT event;
     if(getmouse(&event) == OK){
          bool enter_insert;
          if((enter_insert = config_state->vim_state.mode == VM_INSERT)){
               ce_clamp_cursor(buffer, cursor);
               vim_enter_normal_mode(&config_state->vim_state);
          }
#ifdef MOUSE_DIAG
          ce_message("0x%x", event.bstate);
          if(event.bstate & BUTTON1_PRESSED)
               ce_message("%s", "BUTTON1_PRESSED");
          else if(event.bstate & BUTTON1_RELEASED)
               ce_message("%s", "BUTTON1_RELEASED");
          else if(event.bstate & BUTTON1_CLICKED)
               ce_message("%s", "BUTTON1_CLICKED");
          else if(event.bstate & BUTTON1_DOUBLE_CLICKED)
               ce_message("%s", "BUTTON1_DOUBLE_CLICKED");
          else if(event.bstate & BUTTON1_TRIPLE_CLICKED)
               ce_message("%s", "BUTTON1_TRIPLE_CLICKED");
          else if(event.bstate & BUTTON2_PRESSED)
               ce_message("%s", "BUTTON2_PRESSED");
          else if(event.bstate & BUTTON2_RELEASED)
               ce_message("%s", "BUTTON2_RELEASED");
          else if(event.bstate & BUTTON2_CLICKED)
               ce_message("%s", "BUTTON2_CLICKED");
          else if(event.bstate & BUTTON2_DOUBLE_CLICKED)
               ce_message("%s", "BUTTON2_DOUBLE_CLICKED");
          else if(event.bstate & BUTTON2_TRIPLE_CLICKED)
               ce_message("%s", "BUTTON2_TRIPLE_CLICKED");
          else if(event.bstate & BUTTON3_PRESSED)
               ce_message("%s", "BUTTON3_PRESSED");
          else if(event.bstate & BUTTON3_RELEASED)
               ce_message("%s", "BUTTON3_RELEASED");
          else if(event.bstate & BUTTON3_CLICKED)
               ce_message("%s", "BUTTON3_CLICKED");
          else if(event.bstate & BUTTON3_DOUBLE_CLICKED)
               ce_message("%s", "BUTTON3_DOUBLE_CLICKED");
          else if(event.bstate & BUTTON3_TRIPLE_CLICKED)
               ce_message("%s", "BUTTON3_TRIPLE_CLICKED");
          else if(event.bstate & BUTTON4_PRESSED)
               ce_message("%s", "BUTTON4_PRESSED");
          else if(event.bstate & BUTTON4_RELEASED)
               ce_message("%s", "BUTTON4_RELEASED");
          else if(event.bstate & BUTTON4_CLICKED)
               ce_message("%s", "BUTTON4_CLICKED");
          else if(event.bstate & BUTTON4_DOUBLE_CLICKED)
               ce_message("%s", "BUTTON4_DOUBLE_CLICKED");
          else if(event.bstate & BUTTON4_TRIPLE_CLICKED)
               ce_message("%s", "BUTTON4_TRIPLE_CLICKED");
          else if(event.bstate & BUTTON_SHIFT)
               ce_message("%s", "BUTTON_SHIFT");
          else if(event.bstate & BUTTON_CTRL)
               ce_message("%s", "BUTTON_CTRL");
          else if(event.bstate & BUTTON_ALT)
               ce_message("%s", "BUTTON_ALT");
          else if(event.bstate & REPORT_MOUSE_POSITION)
               ce_message("%s", "REPORT_MOUSE_POSITION");
          else if(event.bstate & ALL_MOUSE_EVENTS)
               ce_message("%s", "ALL_MOUSE_EVENTS");
#endif
          if(event.bstate & BUTTON1_PRESSED){ // Left click OSX
               Point_t click = {event.x, event.y};
               switch_to_view_at_point(config_state, click);
               click = (Point_t) {event.x - (config_state->tab_current->view_current->top_left.x - config_state->tab_current->view_current->left_column),
                                  event.y - (config_state->tab_current->view_current->top_left.y - config_state->tab_current->view_current->top_row)};
               click.x -= ce_get_line_number_column_width(config_state->line_number_type, buffer->line_count, buffer_view->top_left.y, buffer_view->bottom_right.y);
               if(click.x < 0) click.x = 0;
               ce_set_cursor(config_state->tab_current->view_current->buffer,
                             &config_state->tab_current->view_current->cursor,
                             click);
          }
#ifdef SCROLL_SUPPORT
          // This feature is currently unreliable and is only known to work for Ryan :)
          else if(event.bstate & (BUTTON_ALT | BUTTON2_CLICKED)){
               Point_t next_line = {0, cursor->y + SCROLL_LINES};
               if(ce_point_on_buffer(buffer, &next_line)){
                    Point_t scroll_location = {0, buffer_view->top_row + SCROLL_LINES};
                    scroll_view_to_location(buffer_view, &scroll_location);
                    if(buffer_view->cursor.y < buffer_view->top_row)
                         ce_move_cursor(buffer, cursor, (Point_t){0, SCROLL_LINES});
               }
          }else if(event.bstate & BUTTON4_TRIPLE_CLICKED){
               Point_t next_line = {0, cursor->y - SCROLL_LINES};
               if(ce_point_on_buffer(buffer, &next_line)){
                    Point_t scroll_location = {0, buffer_view->top_row - SCROLL_LINES};
                    scroll_view_to_location(buffer_view, &scroll_location);
                    if(buffer_view->cursor.y > buffer_view->top_row + (buffer_view->bottom_right.y - buffer_view->top_left.y))
                         ce_move_cursor(buffer, cursor, (Point_t){0, -SCROLL_LINES});
               }
          }
#else
          (void) buffer;
          (void) buffer_view;
#endif
          // if we left insert and haven't switched views, enter insert mode
          if(enter_insert && config_state->tab_current->view_current == buffer_view){
               vim_enter_insert_mode(&config_state->vim_state, config_state->tab_current->view_current->buffer);
          }
     }
}

void half_page_up(BufferView_t* view)
{
     int64_t view_height = view->bottom_right.y - view->top_left.y;
     Point_t delta = { 0, -view_height / 2 };
     ce_move_cursor(view->buffer, &view->cursor, delta);
}

void half_page_down(BufferView_t* view)
{
     int64_t view_height = view->bottom_right.y - view->top_left.y;
     Point_t delta = { 0, view_height / 2 };
     ce_move_cursor(view->buffer, &view->cursor, delta);
}

bool iterate_history_input(ConfigState_t* config_state, bool previous)
{
     BufferState_t* buffer_state = config_state->view_input->buffer->user_data;
     InputHistory_t* history = history_from_input_key(config_state);
     if(!history) return false;

     bool success = false;

     if(previous){
          success = input_history_prev(history);
     }else{
          success = input_history_next(history);
     }

     if(success){
          ce_clear_lines(config_state->view_input->buffer);
          if(history->cur->entry) ce_append_string(config_state->view_input->buffer, 0, history->cur->entry);
          config_state->view_input->cursor = (Point_t){0, 0};
          ce_move_cursor_to_end_of_file(config_state->view_input->buffer, &config_state->view_input->cursor);
          reset_buffer_commits(&buffer_state->commit_tail);
     }

     return success;
}

void update_buffer_list_buffer(ConfigState_t* config_state, const BufferNode_t* head)
{
     char buffer_info[BUFSIZ];
     config_state->buffer_list_buffer.status = BS_NONE;
     ce_clear_lines(&config_state->buffer_list_buffer);

     // calc maxes of things we care about for formatting
     int64_t max_buffer_lines = 0;
     int64_t max_name_len = 0;
     int64_t buffer_count = 0;
     const BufferNode_t* itr = head;
     while(itr){
          if(max_buffer_lines < itr->buffer->line_count) max_buffer_lines = itr->buffer->line_count;
          int64_t name_len = strlen(itr->buffer->name);
          if(max_name_len < name_len) max_name_len = name_len;
          buffer_count++;
          itr = itr->next;
     }

     int64_t max_buffer_lines_digits = count_digits(max_buffer_lines);
     if(max_buffer_lines_digits < 5) max_buffer_lines_digits = 5; // account for "lines" string row header
     if(max_name_len < 11) max_name_len = 11; // account for "buffer name" string row header

     // build format string, OMG THIS IS SO UNREADABLE HOLY MOLY BATMAN
     char format_string[BUFSIZ];
     // build header
     snprintf(format_string, BUFSIZ, "// %%5s %%-%"PRId64"s %%-%"PRId64"s", max_name_len,
              max_buffer_lines_digits);
     snprintf(buffer_info, BUFSIZ, format_string, "flags", "buffer name", "lines");
     ce_append_line(&config_state->buffer_list_buffer, buffer_info);

     // build buffer info
     snprintf(format_string, BUFSIZ, "   %%5s %%-%"PRId64"s %%%"PRId64 PRId64, max_name_len, max_buffer_lines_digits);

     itr = head;
     while(itr){
          const char* buffer_flag_str = buffer_flag_string(itr->buffer);
          snprintf(buffer_info, BUFSIZ, format_string, buffer_flag_str, itr->buffer->name,
                   itr->buffer->line_count);
          ce_append_line(&config_state->buffer_list_buffer, buffer_info);
          itr = itr->next;
     }

     config_state->buffer_list_buffer.status = BS_READONLY;
}

void update_mark_list_buffer(ConfigState_t* config_state, const Buffer_t* buffer)
{
     char buffer_info[BUFSIZ];
     config_state->mark_list_buffer.status = BS_NONE;
     ce_clear_lines(&config_state->mark_list_buffer);

     snprintf(buffer_info, BUFSIZ, "// reg line");
     ce_append_line(&config_state->mark_list_buffer, buffer_info);

     int max_digits = 1;
     const VimMarkNode_t* itr = ((BufferState_t*)(buffer->user_data))->vim_buffer_state.mark_head;
     while(itr){
          int digits = count_digits(itr->location.y);
          if(digits > max_digits) max_digits = digits;
          itr = itr->next;
     }

     itr = ((BufferState_t*)(buffer->user_data))->vim_buffer_state.mark_head;
     while(itr){
          snprintf(buffer_info, BUFSIZ, "  '%c' %*"PRId64" %s",
                   itr->reg_char, max_digits, itr->location.y,
                   itr->location.y < buffer->line_count ? buffer->lines[itr->location.y] : "");
          ce_append_line(&config_state->mark_list_buffer, buffer_info);
          itr = itr->next;
     }

     config_state->mark_list_buffer.status = BS_READONLY;
}

void update_yank_list_buffer(ConfigState_t* config_state)
{
     char buffer_info[BUFSIZ];
     config_state->yank_list_buffer.status = BS_NONE;
     ce_clear_lines(&config_state->yank_list_buffer);

     const VimYankNode_t* itr = config_state->vim_state.yank_head;
     while(itr){
          snprintf(buffer_info, BUFSIZ, "// reg '%c'", itr->reg_char);
          ce_append_line(&config_state->yank_list_buffer, buffer_info);
          ce_append_line(&config_state->yank_list_buffer, itr->text);
          itr = itr->next;
     }

     config_state->yank_list_buffer.status = BS_READONLY;
}

void update_macro_list_buffer(ConfigState_t* config_state)
{
     char buffer_info[BUFSIZ];
     config_state->macro_list_buffer.status = BS_NONE;
     ce_clear_lines(&config_state->macro_list_buffer);

     ce_append_line(&config_state->macro_list_buffer, "// reg actions");

     const VimMacroNode_t* itr = config_state->vim_state.macro_head;
     while(itr){
          char* char_string = vim_command_string_to_char_string(itr->command);
          snprintf(buffer_info, BUFSIZ, "  '%c' %s", itr->reg, char_string);
          ce_append_line(&config_state->macro_list_buffer, buffer_info);
          free(char_string);
          itr = itr->next;
     }

     if(config_state->vim_state.recording_macro){
          ce_append_line(&config_state->macro_list_buffer, "");
          ce_append_line(&config_state->macro_list_buffer, "// recording:");

          int* int_cmd = ce_keys_get_string(config_state->vim_state.record_macro_head);

          if(int_cmd[0]){
               char* char_cmd = vim_command_string_to_char_string(int_cmd);
               if(char_cmd[0]){
                    ce_append_line(&config_state->macro_list_buffer, char_cmd);
               }

               free(char_cmd);
          }

          free(int_cmd);
     }

     ce_append_line(&config_state->macro_list_buffer, "");
     ce_append_line(&config_state->macro_list_buffer, "// escape conversions");
     ce_append_line(&config_state->macro_list_buffer, "// \\b -> KEY_BACKSPACE");
     ce_append_line(&config_state->macro_list_buffer, "// \\e -> KEY_ESCAPE");
     ce_append_line(&config_state->macro_list_buffer, "// \\r -> KEY_ENTER");
     ce_append_line(&config_state->macro_list_buffer, "// \\t -> KEY_TAB");
     ce_append_line(&config_state->macro_list_buffer, "// \\u -> KEY_UP");
     ce_append_line(&config_state->macro_list_buffer, "// \\d -> KEY_DOWN");
     ce_append_line(&config_state->macro_list_buffer, "// \\l -> KEY_LEFT");
     ce_append_line(&config_state->macro_list_buffer, "// \\i -> KEY_RIGHT");
     ce_append_line(&config_state->macro_list_buffer, "// \\\\ -> \\"); // HAHAHAHAHA

     config_state->macro_list_buffer.status = BS_READONLY;
}

Point_t get_cursor_on_terminal(const Point_t* cursor, const BufferView_t* buffer_view, LineNumberType_t line_number_type)
{
     Point_t p = {cursor->x - buffer_view->left_column + buffer_view->top_left.x,
                  cursor->y - buffer_view->top_row + buffer_view->top_left.y};
     p.x += ce_get_line_number_column_width(line_number_type, buffer_view->buffer->line_count, buffer_view->top_left.y,
                                            buffer_view->bottom_right.y);
     return p;
}

void get_terminal_view_rect(TabView_t* tab_head, Point_t* top_left, Point_t* bottom_right)
{
     *top_left = (Point_t){0, 0};
     *bottom_right = (Point_t){g_terminal_dimensions->x - 1, g_terminal_dimensions->y - 1};

     // if we have multiple tabs
     if(tab_head->next){
          top_left->y++;
     }
}

bool generate_auto_complete_files_in_dir(AutoComplete_t* auto_complete, const char* dir)
{
     struct dirent *node;
     DIR* os_dir = opendir(dir);
     if(!os_dir) return false;

     auto_complete_free(auto_complete);

     char tmp[BUFSIZ];
     struct stat info;
     while((node = readdir(os_dir)) != NULL){
          snprintf(tmp, BUFSIZ, "%s/%s", dir, node->d_name);
          stat(tmp, &info);
          if(S_ISDIR(info.st_mode)){
               snprintf(tmp, BUFSIZ, "%s/", node->d_name);
          }else{
               strncpy(tmp, node->d_name, BUFSIZ);
          }
          auto_complete_insert(auto_complete, tmp, NULL);
     }

     closedir(os_dir);

     if(!auto_complete->head) return false;
     return true;
}

bool calc_auto_complete_start_and_path(AutoComplete_t* auto_complete, const char* line, Point_t cursor,
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

               rc = generate_auto_complete_files_in_dir(auto_complete, path);
               free(path);
          }else{
               char* path = malloc(user_path_len + 1);
               if(!path){
                    ce_message("failed to alloc path");
                    return false;
               }

               memcpy(path, path_begin, user_path_len);

               path[user_path_len] = 0;

               rc = generate_auto_complete_files_in_dir(auto_complete, path);
               free(path);
          }
     }else{
          rc = generate_auto_complete_files_in_dir(auto_complete, start_path);
     }

     // set the start point if we generated files
     if(rc){
          if(last_slash){
               const char* completion = last_slash + 1;
               auto_complete_start(auto_complete, ACT_OCCURANCE, (Point_t){(last_slash - line) + 1, cursor.y});
               auto_complete_next(auto_complete, completion);
               update_completion_buffer(completion_buffer, auto_complete, completion);
          }else{
               auto_complete_start(auto_complete, ACT_OCCURANCE, (Point_t){(path_begin - line), cursor.y});
               auto_complete_next(auto_complete, path_begin);
               update_completion_buffer(completion_buffer, auto_complete, path_begin);
          }
     }

     pthread_mutex_unlock(&completion_lock);
     return rc;
}

bool confirm_action(ConfigState_t* config_state, BufferNode_t* head)
{
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Buffer_t* buffer = buffer_view->buffer;
     Point_t* cursor = &buffer_view->cursor;
     BufferState_t* buffer_state = buffer->user_data;

     if(config_state->input && buffer_view == config_state->view_input){
          input_end(config_state);

          // update convenience vars
          buffer_view = config_state->tab_current->view_current;
          buffer = config_state->tab_current->view_current->buffer;
          cursor = &config_state->tab_current->view_current->cursor;
          buffer_state = buffer->user_data;

          switch(config_state->input_key) {
          default:
               break;
          case 'q':
               if(!config_state->view_input->buffer->line_count) break;

               if(tolower(config_state->view_input->buffer->lines[0][0]) == 'y'){
                    config_state->quit = true;
               }
               return true;
          case 2: // Ctrl + b
          {
               if(!config_state->view_input->buffer->line_count) break;

               // if auto complete has a current matching value, overwrite what the user wrote with that completion
               if(auto_completing(&config_state->auto_complete) && config_state->auto_complete.current){
                    int64_t len = strlen(config_state->view_input->buffer->lines[0]);
                    if(!ce_remove_string(config_state->view_input->buffer, (Point_t){0, 0}, len)) break;
                    if(!ce_insert_string(config_state->view_input->buffer, (Point_t){0, 0}, config_state->auto_complete.current->option)) break;
               }

               BufferNode_t* itr = head;

               while(itr){
                    if(strcmp(itr->buffer->name, config_state->view_input->buffer->lines[0]) == 0){
                         config_state->tab_current->view_current->buffer = itr->buffer;
                         config_state->tab_current->view_current->cursor = itr->buffer->cursor;
                         center_view(config_state->tab_current->view_current);
                         break;
                    }
                    itr = itr->next;
               }

               // return whether we switched to a buffer or not
               return true;
          }
          case 6: // Ctrl + f
          {
               if(!config_state->view_input->buffer->line_count) break;

               bool switched_to_open_file = false;

               // if auto complete has a current matching value, overwrite what the user wrote with that completion
               if(auto_completing(&config_state->auto_complete) && config_state->auto_complete.current){
                    char* last_slash = strrchr(config_state->view_input->buffer->lines[0], '/');
                    int64_t offset = 0;
                    if(last_slash) offset = (last_slash - config_state->view_input->buffer->lines[0]) + 1;

                    int64_t len = strlen(config_state->view_input->buffer->lines[0] + offset);
                    if(!ce_remove_string(config_state->view_input->buffer, (Point_t){offset, 0}, len)) break;
                    if(!ce_insert_string(config_state->view_input->buffer, (Point_t){offset, 0}, config_state->auto_complete.current->option)) break;
               }

               // load the buffer, either from the current working dir, or from another base filepath
               Buffer_t* new_buffer = NULL;
               if(config_state->load_file_search_path && config_state->view_input->buffer->lines[0][0] != '/'){
                    char path[BUFSIZ];
                    snprintf(path, BUFSIZ, "%s/%s", config_state->load_file_search_path, config_state->view_input->buffer->lines[0]);
                    new_buffer = open_file_buffer(head, path);
               }else{
                    new_buffer = open_file_buffer(head, config_state->view_input->buffer->lines[0]);
               }

               if(!switched_to_open_file && new_buffer){
                    config_state->tab_current->view_current->buffer = new_buffer;
                    config_state->tab_current->view_current->cursor = (Point_t){0, 0};
                    switched_to_open_file = true;
               }

               // free the search path so we can re-use it
               free(config_state->load_file_search_path);
               config_state->load_file_search_path = NULL;

               if(!switched_to_open_file){
                    config_state->tab_current->view_current->buffer = head->buffer; // message buffer
                    config_state->tab_current->view_current->cursor = (Point_t){0, 0};
               }

               if(config_state->tab_current->overriden_buffer){
                    tab_view_restore_overrideable(config_state->tab_current);
               }

               return true;
          } break;
          case '/':
               if(!config_state->view_input->buffer->line_count) break;

               commit_input_to_history(config_state->view_input->buffer, &config_state->search_history);
               vim_yank_add(&config_state->vim_state.yank_head, '/', strdup(config_state->view_input->buffer->lines[0]), YANK_NORMAL);
               view_jump_insert(buffer_view->user_data, buffer->filename, *cursor);
               return true;
          case '?':
               if(!config_state->view_input->buffer->line_count) break;

               commit_input_to_history(config_state->view_input->buffer, &config_state->search_history);
               vim_yank_add(&config_state->vim_state.yank_head, '/', strdup(config_state->view_input->buffer->lines[0]), YANK_NORMAL);
               return true;
          case 'R':
          {
               if(!config_state->view_input->buffer->line_count) break; // NOTE: unsure if this is correct

               VimYankNode_t* yank = vim_yank_find(config_state->vim_state.yank_head, '/');
               if(!yank) break;

               const char* search_str = yank->text;
               // NOTE: allow empty string to replace search
               int64_t search_len = strlen(search_str);
               if(!search_len) break;

               char* replace_str = ce_dupe_buffer(config_state->view_input->buffer);
               int64_t replace_len = strlen(replace_str);
               Point_t begin = config_state->tab_current->view_input_save->buffer->highlight_start;
               Point_t end = config_state->tab_current->view_input_save->buffer->highlight_end;
               if(end.x < 0) ce_move_cursor_to_end_of_file(config_state->tab_current->view_input_save->buffer, &end);

               Point_t match = {};
               int64_t match_len = 0;
               int64_t replace_count = 0;
               while(ce_find_regex(buffer, begin, &config_state->vim_state.search.regex, &match, &match_len, CE_DOWN)){
                    if(ce_point_after(match, end)) break;
                    Point_t end_match = match;
                    ce_advance_cursor(buffer, &end_match, match_len - 1);
                    char* searched_dup = ce_dupe_string(buffer, match, end_match);
                    if(!ce_remove_string(buffer, match, match_len)) break;
                    if(replace_len){
                         if(!ce_insert_string(buffer, match, replace_str)) break;
                    }
                    ce_commit_change_string(&buffer_state->commit_tail, match, match, match, strdup(replace_str),
                                            searched_dup, BCC_KEEP_GOING);
                    begin = match;
                    replace_count++;
               }

               if(buffer_state->commit_tail) buffer_state->commit_tail->commit.chain = BCC_STOP;

               if(replace_count){
                    ce_message("replaced %" PRId64 " matches", replace_count);
               }else{
                    ce_message("no matches found to replace");
               }

               *cursor = begin;
               center_view(buffer_view);
               free(replace_str);
               return true;
          } break;
          case '@':
          {
               if(!config_state->view_input->buffer->lines) break;

               int64_t line = config_state->tab_current->view_input_save->cursor.y - 1; // account for buffer list row header
               if(line < 0) break;

               VimMacroNode_t* macro = vim_macro_find(config_state->vim_state.macro_head, config_state->editting_register);
               if(!macro) break;

               free(macro->command);
               int* new_macro_string = vim_char_string_to_command_string(config_state->view_input->buffer->lines[0]);

               if(new_macro_string){
                    macro->command = new_macro_string;
               }else{
                    ce_message("invalid editted macro string");
               }

               config_state->editting_register = 0;
               return true;
          } break;
          case 'y':
          {
               int64_t line = config_state->tab_current->view_input_save->cursor.y;
               if(line < 0) break;

               VimYankNode_t* yank = vim_yank_find(config_state->vim_state.yank_head, config_state->editting_register);
               if(!yank) break;

               char* new_yank = ce_dupe_buffer(config_state->view_input->buffer);
               free((char*)(yank->text));
               yank->text = new_yank;
               config_state->editting_register = 0;
          } break;
          case ':':
          {
               if(config_state->tab_current->view_overrideable){
                    tab_view_restore_overrideable(config_state->tab_current);
               }

               if(!config_state->view_input->buffer->line_count) break;

               commit_input_to_history(config_state->view_input->buffer, &config_state->command_history);

               bool alldigits = true;
               const char* itr = config_state->view_input->buffer->lines[0];
               while(*itr){
                    if(!isdigit(*itr)){
                         alldigits = false;
                         break;
                    }
                    itr++;
               }

               if(alldigits){
                    // goto line
                    int64_t line = atoi(config_state->view_input->buffer->lines[0]);
                    if(line > 0){
                         *cursor = (Point_t){0, line - 1};
                         ce_move_cursor_to_soft_beginning_of_line(buffer, cursor);
                         center_view(buffer_view);
                         view_jump_insert(buffer_view->user_data, buffer_view->buffer->filename, buffer_view->cursor);
                    }
               }else{
                    // if auto complete has a current matching value, overwrite what the user wrote with that completion
                    if(auto_completing(&config_state->auto_complete) && config_state->auto_complete.current){
                         int64_t len = strlen(config_state->view_input->buffer->lines[0]);
                         if(!ce_remove_string(config_state->view_input->buffer, (Point_t){0, 0}, len)) break;
                         if(!ce_insert_string(config_state->view_input->buffer, (Point_t){0, 0}, config_state->auto_complete.current->option)) break;
                    }

                    // run all commands in the input buffer
                    Command_t command = {};
                    if(!command_parse(&command, config_state->view_input->buffer->lines[0])){
                         ce_message("failed to parse command: '%s'", config_state->view_input->buffer->lines[0]);
                    }else{
                         ce_command* command_func = NULL;
                         for(int64_t i = 0; i < config_state->command_entry_count; ++i){
                              CommandEntry_t* entry = config_state->command_entries + i;
                              if(strcmp(entry->name, command.name) == 0){
                                   command_func = entry->func;
                                   break;
                              }
                         }

                         if(command_func){
                              CommandData_t command_data = {config_state, head};
                              command_func(&command, &command_data);
                         }else{
                              ce_message("unknown command: '%s'", command.name);
                         }
                    }
               }

               return true;
          } break; // NOTE: Ahhh the unreachable break in it's natural habitat
          }
     }else if(buffer_view->buffer == &config_state->buffer_list_buffer){
          int64_t line = cursor->y - 1; // account for buffer list row header
          if(line < 0) return false;
          BufferNode_t* itr = head;

          while(line > 0){
               itr = itr->next;
               if(!itr) return false;
               line--;
          }

          if(!itr) return false;

          buffer_view->buffer = itr->buffer;
          buffer_view->cursor = itr->buffer->cursor;
          center_view(buffer_view);

          TerminalNode_t* terminal_node = is_terminal_buffer(config_state->terminal_head, itr->buffer);
          if(terminal_node){
               int64_t new_width = buffer_view->bottom_right.x - buffer_view->top_left.x;
               int64_t new_height = buffer_view->bottom_right.y - buffer_view->top_left.y;
               terminal_resize(&terminal_node->terminal, new_width, new_height);
               config_state->terminal_current = terminal_node;
          }

          return true;
     }else if(buffer_view->buffer == &config_state->mark_list_buffer){
          int64_t line = cursor->y - 1; // account for buffer list row header
          if(line < 0) return false;
          VimMarkNode_t* itr = ((BufferState_t*)(config_state->buffer_before_query->user_data))->vim_buffer_state.mark_head;

          while(line > 0){
               itr = itr->next;
               if(!itr) return false;
               line--;
          }

          if(!itr) return false;

          buffer_view->buffer = config_state->buffer_before_query;
          buffer_view->cursor.y = itr->location.y;
          ce_move_cursor_to_soft_beginning_of_line(buffer_view->buffer, &buffer_view->cursor);
          center_view(buffer_view);

          if(config_state->tab_current->view_overrideable){
               tab_view_restore_overrideable(config_state->tab_current);
          }

          return true;
     }else if(buffer_view->buffer == &config_state->macro_list_buffer){
          int64_t line = cursor->y - 1; // account for buffer list row header
          if(line < 0) return false;
          VimMacroNode_t* itr = config_state->vim_state.macro_head;

          while(line > 0){
               itr = itr->next;
               if(!itr) return false;
               line--;
          }

          if(!itr) return false;

          input_start(config_state, "Edit Macro", '@');
          config_state->editting_register = itr->reg;
          vim_enter_normal_mode(&config_state->vim_state);
          char* char_command = vim_command_string_to_char_string(itr->command);
          ce_insert_string(config_state->view_input->buffer, (Point_t){0,0}, char_command);
          free(char_command);
          return true;
     }else if(buffer_view->buffer == &config_state->yank_list_buffer){
          int64_t line = cursor->y;
          if(line < 0) return false;

          VimYankNode_t* itr = config_state->vim_state.yank_head;
          while(itr){
               line -= (ce_count_string_lines(itr->text) + 1);
               if(line < 0) break;

               itr = itr->next;
          }

          input_start(config_state, "Edit Yank", 'y');
          config_state->editting_register = itr->reg_char;
          vim_enter_normal_mode(&config_state->vim_state);
          ce_insert_string(config_state->view_input->buffer, (Point_t){0,0}, itr->text);
          return true;
     }else{
          TerminalNode_t* terminal_node = is_terminal_buffer(config_state->terminal_head, buffer_view->buffer);
          if(terminal_node){
               BufferView_t* view_to_change = buffer_view;
               if(config_state->tab_current->view_previous) view_to_change = config_state->tab_current->view_previous;

               char* terminal_current_directory = terminal_get_current_directory(&terminal_node->terminal);
               if(goto_file_destination_in_buffer(head, buffer_view->buffer, cursor->y,
                                                  config_state->tab_current->view_head, view_to_change,
                                                  &config_state->terminal_current->last_jump_location,
                                                  terminal_current_directory)){
                    config_state->tab_current->view_current = view_to_change;
               }
               free(terminal_current_directory);
               return true;
          }
     }

     return false;
}

void draw_view_statuses(BufferView_t* view, BufferView_t* current_view, BufferView_t* overrideable_view, VimMode_t vim_mode, int last_key,
                        char recording_macro, TerminalNode_t* terminal_current)
{
     Buffer_t* buffer = view->buffer;
     if(view->next_horizontal) draw_view_statuses(view->next_horizontal, current_view, overrideable_view, vim_mode, last_key, recording_macro, terminal_current);
     if(view->next_vertical) draw_view_statuses(view->next_vertical, current_view, overrideable_view, vim_mode, last_key, recording_macro, terminal_current);

     // NOTE: mode names need space at the end for OCD ppl like me
     static const char* mode_names[] = {
          "NORMAL ",
          "INSERT ",
          "VISUAL ",
          "VISUAL LINE ",
          "VISUAL BLOCK ",
     };

     attron(COLOR_PAIR(S_BORDERS));
     move(view->bottom_right.y, view->top_left.x);
     for(int i = view->top_left.x; i < view->bottom_right.x; ++i) addch(ACS_HLINE);
     int right_status_offset = 0;
     if(view->bottom_right.x == (g_terminal_dimensions->x - 1)){
          addch(ACS_HLINE);
          right_status_offset = 1;
     }

     // TODO: handle case where filename is too long to fit in the status bar
     attron(COLOR_PAIR(S_VIEW_STATUS));
     mvprintw(view->bottom_right.y, view->top_left.x + 1, " %s%s%s ",
              view == current_view ? mode_names[vim_mode] : "", buffer_flag_string(buffer), buffer->filename);
#if 0 // NOTE: useful to show key presses when debugging
     if(view == current_view) printw("%s %d ", keyname(last_key), last_key);
#endif
     if(view == overrideable_view) printw("^ ");
     if(terminal_current && view->buffer == terminal_current->buffer) printw("$ ");
     if(view == current_view && recording_macro) printw("RECORDING %c ", recording_macro);
     int64_t row = view->cursor.y + 1;
     int64_t column = view->cursor.x + 1;
     int64_t digits_in_line = count_digits(row);
     digits_in_line += count_digits(column);
     mvprintw(view->bottom_right.y, (view->bottom_right.x - (digits_in_line + 5)) + right_status_offset,
              " %"PRId64", %"PRId64" ", column, row);
}

void resize_terminal_if_in_view(BufferView_t* view_head, TerminalNode_t* terminal_head)
{
     while(terminal_head){
          BufferView_t* term_view = ce_buffer_in_view(view_head, terminal_head->buffer);
          if(term_view){
               int64_t new_width = term_view->bottom_right.x - term_view->top_left.x;
               int64_t new_height = term_view->bottom_right.y - term_view->top_left.y;
               terminal_resize(&terminal_head->terminal, new_width, new_height);
          }

          terminal_head = terminal_head->next;
     }
}

bool run_command_on_terminal_in_view(TerminalNode_t* terminal_head, BufferView_t* view_head, const char* command)
{
     TerminalNode_t* term_itr = terminal_head;
     while(term_itr){
          if(ce_buffer_in_view(view_head, term_itr->buffer)){
               while(*command){
                    terminal_send_key(&term_itr->terminal, *command);
                    command++;
               }
               move_jump_location_to_end_of_output(term_itr);
               terminal_send_key(&term_itr->terminal, KEY_ENTER);
               return true;
          }

          term_itr = term_itr->next;
     }

     return false;
}

#define RELOAD_BUFFER_HELP "usage: reload_buffer"

void command_reload_buffer(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message(RELOAD_BUFFER_HELP);
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Buffer_t* buffer = buffer_view->buffer;

     if(access(buffer->filename, R_OK) != 0){
          ce_message("failed to read %s: %s", buffer->filename, strerror(errno));
          return;
     }

     // reload file
     if(buffer->status == BS_READONLY){
          // NOTE: maybe ce_clear_lines shouldn't care about readonly
          ce_clear_lines_readonly(buffer);
     }else{
          ce_clear_lines(buffer);
     }

     ce_load_file(buffer, buffer->filename);
     ce_clamp_cursor(buffer, &buffer_view->cursor);
}

static void command_syntax_help()
{
     ce_message("usage: syntax [string]");
     ce_message(" supported styles: 'c', 'cpp', 'python', 'config', 'diff', 'plain'");
}

void command_syntax(Command_t* command, void* user_data)
{
     if(command->arg_count != 1){
          command_syntax_help();
          return;
     }

     if(command->args[0].type != CAT_STRING){
          command_syntax_help();
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Buffer_t* buffer = buffer_view->buffer;

     if(strcmp(command->args[0].string, "c") == 0){
          ce_message("syntax 'c' now on %s", buffer->filename);
          buffer->syntax_fn = syntax_highlight_c;
          free(buffer->syntax_user_data);
          buffer->syntax_user_data = malloc(sizeof(SyntaxC_t));
          buffer->type = BFT_C;
     }else if(strcmp(command->args[0].string, "cpp") == 0){
          ce_message("syntax 'cpp' now on %s", buffer->filename);
          buffer->syntax_fn = syntax_highlight_cpp;
          free(buffer->syntax_user_data);
          buffer->syntax_user_data = malloc(sizeof(SyntaxCpp_t));
          buffer->type = BFT_CPP;
     }else if(strcmp(command->args[0].string, "python") == 0){
          ce_message("syntax 'python' now on %s", buffer->filename);
          buffer->syntax_fn = syntax_highlight_python;
          free(buffer->syntax_user_data);
          buffer->syntax_user_data = malloc(sizeof(SyntaxPython_t));
          buffer->type = BFT_PYTHON;
     }else if(strcmp(command->args[0].string, "java") == 0){
          ce_message("syntax 'java' now on %s", buffer->filename);
          buffer->syntax_fn = syntax_highlight_java;
          free(buffer->syntax_user_data);
          buffer->syntax_user_data = malloc(sizeof(SyntaxJava_t));
          buffer->type = BFT_JAVA;
     }else if(strcmp(command->args[0].string, "config") == 0){
          ce_message("syntax 'config' now on %s", buffer->filename);
          buffer->syntax_fn = syntax_highlight_config;
          free(buffer->syntax_user_data);
          buffer->syntax_user_data = malloc(sizeof(SyntaxConfig_t));
          buffer->type = BFT_CONFIG;
     }else if(strcmp(command->args[0].string, "diff") == 0){
          ce_message("syntax 'diff' now on %s", buffer->filename);
          buffer->syntax_fn = syntax_highlight_diff;
          free(buffer->syntax_user_data);
          buffer->syntax_user_data = malloc(sizeof(SyntaxDiff_t));
          buffer->type = BFT_DIFF;
     }else if(strcmp(command->args[0].string, "plain") == 0){
          ce_message("syntax 'plain' now on %s", buffer->filename);
          buffer->syntax_fn = syntax_highlight_plain;
          free(buffer->syntax_user_data);
          buffer->syntax_user_data = malloc(sizeof(SyntaxPlain_t));
          buffer->type = BFT_PLAIN;
     }else{
          ce_message("unknown syntax '%s'", command->args[0].string);
     }
}

#define NOH_HELP "usage: noh"

void command_noh(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message(NOH_HELP);
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     config_state->do_not_highlight_search = true;
}

static void command_line_number_help()
{
     ce_message("usage: line_number [string]");
     ce_message(" supported modes: 'none', 'absolute', 'relative', 'both'");
}

void command_line_number(Command_t* command, void* user_data)
{
     if(command->arg_count != 1){
          command_line_number_help();
          return;
     }

     if(command->args[0].type != CAT_STRING){
          command_line_number_help();
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     if(strcmp(command->args[0].string, "none") == 0){
          config_state->line_number_type = LNT_NONE;
     }else if(strcmp(command->args[0].string, "absolute") == 0){
          config_state->line_number_type = LNT_ABSOLUTE;
     }else if(strcmp(command->args[0].string, "relative") == 0){
          config_state->line_number_type = LNT_RELATIVE;
     }else if(strcmp(command->args[0].string, "both") == 0){
          config_state->line_number_type = LNT_RELATIVE_AND_ABSOLUTE;
     }
}

static void command_highlight_line_help()
{
     ce_message("usage: highlight_line [string]");
     ce_message(" supported modes: 'none', 'text', 'entire'");
}

void command_highlight_line(Command_t* command, void* user_data)
{
     if(command->arg_count != 1){
          command_highlight_line_help();
          return;
     }

     if(command->args[0].type != CAT_STRING){
          command_highlight_line_help();
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     if(strcmp(command->args[0].string, "none") == 0){
          config_state->highlight_line_type = HLT_NONE;
     }else if(strcmp(command->args[0].string, "text") == 0){
          config_state->highlight_line_type = HLT_TO_END_OF_TEXT;
     }else if(strcmp(command->args[0].string, "entire") == 0){
          config_state->highlight_line_type = HLT_ENTIRE_LINE;
     }
}

#define NEW_BUFFER_HELP "usage: new_buffer [optional filename]"

void command_new_buffer(Command_t* command, void* user_data)
{
     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;

     if(command->arg_count == 0){
          Buffer_t* new_buffer = new_buffer_from_string(*config_state->save_buffer_head, "unnamed", NULL);
          if(new_buffer){
               ce_alloc_lines(new_buffer, 1);
               config_state->tab_current->view_current->buffer = new_buffer;
               config_state->tab_current->view_current->cursor = (Point_t){0, 0};
          }
     }else if(command->arg_count == 1){
          Buffer_t* new_buffer = new_buffer_from_string(*config_state->save_buffer_head, command->args[0].string, NULL);
          if(new_buffer){
               ce_alloc_lines(new_buffer, 1);
               config_state->tab_current->view_current->buffer = new_buffer;
               config_state->tab_current->view_current->cursor = (Point_t){0, 0};
          }
     }else{
          ce_message(NEW_BUFFER_HELP);
          return;
     }
}

#define RENAME_HELP "usage: rename [string]"

void command_rename(Command_t* command, void* user_data)
{
     if(command->arg_count != 1){
          ce_message(RENAME_HELP);
          return;
     }

     if(command->args[0].type != CAT_STRING){
          ce_message(RENAME_HELP);
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Buffer_t* buffer = buffer_view->buffer;

     if(buffer->name) free(buffer->name);
     buffer->name = strdup(command->args[0].string);
     if(buffer->status != BS_READONLY){
          buffer->status = BS_MODIFIED;
     }
}

#define BUFFERS_HELP "usage: buffers"

void command_buffers(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message(BUFFERS_HELP);
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Buffer_t* buffer = buffer_view->buffer;
     Point_t* cursor = &buffer_view->cursor;

     view_jump_insert(buffer_view->user_data, buffer->filename, *cursor);

     buffer->cursor = config_state->tab_current->view_current->cursor;

     // try to find a better place to put the cursor to start
     BufferNode_t* itr = command_data->head;
     int64_t buffer_index = 1;
     bool found_good_buffer_index = false;
     while(itr){
          if(itr->buffer->status != BS_READONLY && !ce_buffer_in_view(config_state->tab_current->view_head, itr->buffer)){
               found_good_buffer_index = true;
               break;
          }
          itr = itr->next;
          buffer_index++;
     }

     update_buffer_list_buffer(config_state, command_data->head);
     config_state->tab_current->view_current->buffer->cursor = *cursor;
     config_state->tab_current->view_current->buffer = &config_state->buffer_list_buffer;
     config_state->tab_current->view_current->top_row = 0;
     config_state->tab_current->view_current->cursor = (Point_t){0, found_good_buffer_index ? buffer_index : 1};
}

#define MACRO_BACKSLASHES_HELP "usage: macro_backslashes"

void command_macro_backslashes(Command_t* command, void* user_data)
{
     if(command->arg_count != 0){
          ce_message(MACRO_BACKSLASHES_HELP);
          return;
     }

     CommandData_t* command_data = (CommandData_t*)(user_data);
     ConfigState_t* config_state = command_data->config_state;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Buffer_t* buffer = buffer_view->buffer;
     BufferState_t* buffer_state = buffer->user_data;
     Point_t* cursor = &buffer_view->cursor;

     if(config_state->vim_state.mode != VM_VISUAL_LINE) return;

     int64_t start_line = 0;
     int64_t end_line = 0;

     // sort points
     if(cursor->y > config_state->vim_state.visual_start.y){
          start_line = config_state->vim_state.visual_start.y;
          end_line = cursor->y;
     }else{
          start_line = cursor->y;
          end_line = config_state->vim_state.visual_start.y;
     }

     // figure out longest line TODO: after slurping spaces and backslashes
     int64_t line_count = end_line - start_line + 1;
     int64_t longest_line = 0;

     for(int64_t i = 0; i < line_count; ++i){
          int64_t line_len = strlen(buffer->lines[i + start_line]);
          if(line_len > longest_line) longest_line = line_len;
     }

     // insert whitespace and backslash on every line to make it the same length
     for(int64_t i = 0; i < line_count; ++i){
          int64_t line = i + start_line;
          int64_t line_len = strlen(buffer->lines[line]);
          int64_t space_len = longest_line - line_len + 1;
          Point_t loc = {line_len, line};
          for(int64_t s = 0; s < space_len; ++s){
               ce_insert_char(buffer, loc, ' ');
               ce_commit_insert_string(&buffer_state->commit_tail, loc, *cursor, *cursor, strdup(" "), BCC_KEEP_GOING);
               loc.x++;
          }
          ce_insert_char(buffer, loc, '\\');
          ce_commit_insert_string(&buffer_state->commit_tail, loc, *cursor, *cursor, strdup("\\"), BCC_KEEP_GOING);
     }
}

void clang_completion(ConfigState_t* config_state, Point_t start_completion)
{
     if(config_state->clang_complete_thread){
          pthread_cancel(config_state->clang_complete_thread);
          pthread_join(config_state->clang_complete_thread, NULL);
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

bool initializer(BufferNode_t** head, Point_t* terminal_dimensions, int argc, char** argv, void** user_data)
{
     // NOTE: need to set these in this module
     g_terminal_dimensions = terminal_dimensions;

     // setup the config's state
     ConfigState_t* config_state = calloc(1, sizeof(*config_state));
     if(!config_state){
          ce_message("failed to allocate config state");
          return false;
     }

     config_state->tab_head = calloc(1, sizeof(*config_state->tab_head));
     if(!config_state->tab_head){
          ce_message("failed to allocate tab");
          return false;
     }

     config_state->tab_current = config_state->tab_head;

     config_state->tab_head->view_head = calloc(1, sizeof(*config_state->tab_head->view_head));
     if(!config_state->tab_head->view_head){
          ce_message("failed to allocate buffer view");
          return false;
     }

     BufferViewState_t* buffer_view_state = calloc(1, sizeof(*buffer_view_state));
     if(!buffer_view_state){
          ce_message("failed to allocate buffer view state");
          return false;
     }

     config_state->tab_head->view_head->user_data = buffer_view_state;

     config_state->view_input = calloc(1, sizeof(*config_state->view_input));
     if(!config_state->view_input){
          ce_message("failed to allocate buffer view for input");
          return false;
     }

     config_state->view_auto_complete = calloc(1, sizeof(*config_state->view_auto_complete));
     if(!config_state->view_auto_complete){
          ce_message("failed to allocate buffer view for auto complete");
          return false;
     }

     // setup input buffer
     ce_alloc_lines(&config_state->input_buffer, 1);
     initialize_buffer(&config_state->input_buffer);
     config_state->input_buffer.name = strdup("[input]");
     config_state->input_buffer.absolutely_no_line_numbers_under_any_circumstances = true;
     config_state->view_input->buffer = &config_state->input_buffer;

     // setup clang completion buffer
     initialize_buffer(&config_state->clang_completion_buffer);
     config_state->clang_completion_buffer.name = strdup("[clang completion]");

     // setup buffer list buffer
     config_state->buffer_list_buffer.name = strdup("[buffers]");
     initialize_buffer(&config_state->buffer_list_buffer);
     config_state->buffer_list_buffer.status = BS_READONLY;
     config_state->buffer_list_buffer.absolutely_no_line_numbers_under_any_circumstances = true;
     config_state->buffer_list_buffer.syntax_fn = syntax_highlight_c;
     config_state->buffer_list_buffer.syntax_user_data = realloc(config_state->buffer_list_buffer.syntax_user_data, sizeof(SyntaxC_t));
     config_state->buffer_list_buffer.type = BFT_C;

     config_state->mark_list_buffer.name = strdup("[marks]");
     initialize_buffer(&config_state->mark_list_buffer);
     config_state->mark_list_buffer.status = BS_READONLY;
     config_state->mark_list_buffer.absolutely_no_line_numbers_under_any_circumstances = true;
     config_state->mark_list_buffer.syntax_fn = syntax_highlight_c;
     config_state->mark_list_buffer.syntax_user_data = realloc(config_state->mark_list_buffer.syntax_user_data, sizeof(SyntaxC_t));
     config_state->mark_list_buffer.type = BFT_C;

     config_state->yank_list_buffer.name = strdup("[yanks]");
     initialize_buffer(&config_state->yank_list_buffer);
     config_state->yank_list_buffer.status = BS_READONLY;
     config_state->yank_list_buffer.absolutely_no_line_numbers_under_any_circumstances = true;
     config_state->yank_list_buffer.syntax_fn = syntax_highlight_c;
     config_state->yank_list_buffer.syntax_user_data = realloc(config_state->yank_list_buffer.syntax_user_data, sizeof(SyntaxC_t));
     config_state->yank_list_buffer.type = BFT_C;

     config_state->macro_list_buffer.name = strdup("[macros]");
     initialize_buffer(&config_state->macro_list_buffer);
     config_state->macro_list_buffer.status = BS_READONLY;
     config_state->macro_list_buffer.absolutely_no_line_numbers_under_any_circumstances = true;
     config_state->macro_list_buffer.syntax_fn = syntax_highlight_c;
     config_state->macro_list_buffer.syntax_user_data = realloc(config_state->macro_list_buffer.syntax_user_data, sizeof(SyntaxC_t));
     config_state->macro_list_buffer.type = BFT_C;

     // if we reload, the completionbuffer may already exist, don't recreate it
     BufferNode_t* itr = *head;
     while(itr){
          if(strcmp(itr->buffer->name, "[completions]") == 0){
               config_state->completion_buffer = itr->buffer;
               break;
          }
          itr = itr->next;
     }

     if(!config_state->completion_buffer){
          config_state->completion_buffer = calloc(1, sizeof(*config_state->completion_buffer));
          config_state->completion_buffer->name = strdup("[completions]");
          config_state->completion_buffer->status = BS_READONLY;
          config_state->completion_buffer->absolutely_no_line_numbers_under_any_circumstances = true;
          config_state->completion_buffer->syntax_fn = syntax_highlight_c;
          config_state->completion_buffer->syntax_user_data = realloc(config_state->completion_buffer->syntax_user_data, sizeof(SyntaxC_t));
          config_state->completion_buffer->type = BFT_C;
          BufferNode_t* new_buffer_node = ce_append_buffer_to_list(*head, config_state->completion_buffer);
          if(!new_buffer_node){
               ce_message("failed to add shell command buffer to list");
               return false;
          }
     }

     config_state->view_auto_complete->buffer = config_state->completion_buffer;

     *user_data = config_state;

     // setup state for each buffer
     itr = *head;
     while(itr){
          initialize_buffer(itr->buffer);
          itr = itr->next;
     }

     config_state->tab_current->view_head->buffer = (*head)->buffer;
     config_state->tab_current->view_current = config_state->tab_current->view_head;

     for(int i = 0; i < argc; ++i){
          BufferNode_t* node = new_buffer_from_file(*head, argv[i]);

          // if we loaded a file, set the view to point at the file
          if(node){
               if(i == 0){
                    config_state->tab_current->view_current->buffer = node->buffer;
               }else{
                    BufferView_t* buffer_view = ce_split_view(config_state->tab_current->view_head, node->buffer, true);
                    BufferViewState_t* buffer_view_state = calloc(1, sizeof(*buffer_view_state));
                    if(!buffer_view_state){
                         ce_message("failed to allocate buffer view state");
                         return false;
                    }

                    buffer_view->user_data = buffer_view_state;
               }
          }
     }

     // update view boundaries now that we have split them
     Point_t top_left;
     Point_t bottom_right;
     get_terminal_view_rect(config_state->tab_head, &top_left, &bottom_right);
     ce_calc_views(config_state->tab_current->view_head, top_left, bottom_right);

     config_state->line_number_type = LNT_NONE;
     config_state->highlight_line_type = HLT_ENTIRE_LINE;

     config_state->max_auto_complete_height = 10;

#if 0
     // enable mouse events
     mousemask(~((mmask_t)0), NULL);
     mouseinterval(0);
#endif

     input_history_init(&config_state->search_history);
     input_history_init(&config_state->command_history);

     // setup colors for syntax highlighting
     init_pair(S_NORMAL, COLOR_FOREGROUND, COLOR_BACKGROUND);
     init_pair(S_KEYWORD, COLOR_BLUE, COLOR_BACKGROUND);
     init_pair(S_TYPE, COLOR_BRIGHT_BLUE, COLOR_BACKGROUND);
     init_pair(S_CONTROL, COLOR_YELLOW, COLOR_BACKGROUND);
     init_pair(S_COMMENT, COLOR_GREEN, COLOR_BACKGROUND);
     init_pair(S_STRING, COLOR_RED, COLOR_BACKGROUND);
     init_pair(S_CONSTANT, COLOR_MAGENTA, COLOR_BACKGROUND);
     init_pair(S_CONSTANT_NUMBER, COLOR_MAGENTA, COLOR_BACKGROUND);
     init_pair(S_MATCHING_PARENS, COLOR_BRIGHT_WHITE, COLOR_BACKGROUND);
     init_pair(S_PREPROCESSOR, COLOR_BRIGHT_MAGENTA, COLOR_BACKGROUND);
     init_pair(S_FILEPATH, COLOR_BLUE, COLOR_BACKGROUND);
     init_pair(S_BLINK, COLOR_BRIGHT_WHITE, COLOR_BACKGROUND);
     init_pair(S_DIFF_ADDED, COLOR_GREEN, COLOR_BACKGROUND);
     init_pair(S_DIFF_REMOVED, COLOR_RED, COLOR_BACKGROUND);
     init_pair(S_DIFF_HEADER, COLOR_BRIGHT_WHITE, COLOR_BACKGROUND);

     init_pair(S_NORMAL_HIGHLIGHTED, COLOR_FOREGROUND, COLOR_WHITE);
     init_pair(S_KEYWORD_HIGHLIGHTED, COLOR_BLUE, COLOR_WHITE);
     init_pair(S_TYPE_HIGHLIGHTED, COLOR_BRIGHT_BLUE, COLOR_WHITE);
     init_pair(S_CONTROL_HIGHLIGHTED, COLOR_YELLOW, COLOR_WHITE);
     init_pair(S_COMMENT_HIGHLIGHTED, COLOR_GREEN, COLOR_WHITE);
     init_pair(S_STRING_HIGHLIGHTED, COLOR_RED, COLOR_WHITE);
     init_pair(S_CONSTANT_HIGHLIGHTED, COLOR_MAGENTA, COLOR_WHITE);
     init_pair(S_CONSTANT_NUMBER_HIGHLIGHTED, COLOR_MAGENTA, COLOR_WHITE);
     init_pair(S_MATCHING_PARENS_HIGHLIGHTED, COLOR_BRIGHT_WHITE, COLOR_WHITE);
     init_pair(S_PREPROCESSOR_HIGHLIGHTED, COLOR_BRIGHT_MAGENTA, COLOR_WHITE);
     init_pair(S_FILEPATH_HIGHLIGHTED, COLOR_BLUE, COLOR_WHITE);
     init_pair(S_BLINK_HIGHLIGHTED, COLOR_BRIGHT_WHITE, COLOR_WHITE);
     init_pair(S_DIFF_ADDED_HIGHLIGHTED, COLOR_GREEN, COLOR_WHITE);
     init_pair(S_DIFF_REMOVED_HIGHLIGHTED, COLOR_RED, COLOR_WHITE);
     init_pair(S_DIFF_HEADER_HIGHLIGHTED, COLOR_BRIGHT_WHITE, COLOR_WHITE);

     init_pair(S_NORMAL_CURRENT_LINE, COLOR_FOREGROUND, COLOR_BRIGHT_BLACK);
     init_pair(S_KEYWORD_CURRENT_LINE, COLOR_BLUE, COLOR_BRIGHT_BLACK);
     init_pair(S_TYPE_CURRENT_LINE, COLOR_BRIGHT_BLUE, COLOR_BRIGHT_BLACK);
     init_pair(S_CONTROL_CURRENT_LINE, COLOR_YELLOW, COLOR_BRIGHT_BLACK);
     init_pair(S_COMMENT_CURRENT_LINE, COLOR_GREEN, COLOR_BRIGHT_BLACK);
     init_pair(S_STRING_CURRENT_LINE, COLOR_RED, COLOR_BRIGHT_BLACK);
     init_pair(S_CONSTANT_CURRENT_LINE, COLOR_MAGENTA, COLOR_BRIGHT_BLACK);
     init_pair(S_CONSTANT_NUMBER_CURRENT_LINE, COLOR_MAGENTA, COLOR_BRIGHT_BLACK);
     init_pair(S_MATCHING_PARENS_CURRENT_LINE, COLOR_BRIGHT_WHITE, COLOR_BRIGHT_BLACK);
     init_pair(S_PREPROCESSOR_CURRENT_LINE, COLOR_BRIGHT_MAGENTA, COLOR_BRIGHT_BLACK);
     init_pair(S_FILEPATH_CURRENT_LINE, COLOR_BLUE, COLOR_BRIGHT_BLACK);
     init_pair(S_BLINK_CURRENT_LINE, COLOR_BRIGHT_WHITE, COLOR_BRIGHT_BLACK);
     init_pair(S_DIFF_ADDED_CURRENT_LINE, COLOR_GREEN, COLOR_BRIGHT_BLACK);
     init_pair(S_DIFF_REMOVED_CURRENT_LINE, COLOR_RED, COLOR_BRIGHT_BLACK);
     init_pair(S_DIFF_HEADER_CURRENT_LINE, COLOR_BRIGHT_WHITE, COLOR_BRIGHT_BLACK);

     init_pair(S_LINE_NUMBERS, COLOR_WHITE, COLOR_BACKGROUND);

     init_pair(S_TRAILING_WHITESPACE, COLOR_FOREGROUND, COLOR_RED);

     init_pair(S_BORDERS, COLOR_WHITE, COLOR_BACKGROUND);

     init_pair(S_TAB_NAME, COLOR_WHITE, COLOR_BACKGROUND);
     init_pair(S_CURRENT_TAB_NAME, COLOR_CYAN, COLOR_BACKGROUND);

     init_pair(S_VIEW_STATUS, COLOR_CYAN, COLOR_BACKGROUND);
     init_pair(S_INPUT_STATUS, COLOR_RED, COLOR_BACKGROUND);
     init_pair(S_AUTO_COMPLETE, COLOR_WHITE, COLOR_BACKGROUND);

     define_key(NULL, KEY_BACKSPACE);   // Blow away backspace
     define_key("\x7F", KEY_BACKSPACE); // Backspace  (127) (0x7F) ASCII "DEL" Delete
     define_key("\x15", KEY_NPAGE);     // ctrl + d    (21) (0x15) ASCII "NAK" Negative Acknowledgement
     define_key("\x04", KEY_PPAGE);     // ctrl + u     (4) (0x04) ASCII "EOT" End of Transmission
     define_key("\x11", KEY_CLOSE);     // ctrl + q    (17) (0x11) ASCII "DC1" Device Control 1
     define_key("\x12", KEY_REDO);      // ctrl + r    (18) (0x12) ASCII "DC2" Device Control 2
     define_key("\x17", KEY_SAVE);      // ctrl + w    (23) (0x17) ASCII "ETB" End of Transmission Block
     define_key(NULL, KEY_ENTER);       // Blow away enter
     define_key("\x0D", KEY_ENTER);     // Enter       (13) (0x0D) ASCII "CR"  NL Carriage Return

     pthread_mutex_init(&draw_lock, NULL);

     auto_complete_end(&config_state->auto_complete);
     config_state->vim_state.insert_start = (Point_t){-1, -1};

     // init commands
     {
          // create a stack array so we can have the compiler track the number of elements
          CommandEntry_t command_entries[] = {
               {command_buffers, "buffers"},
               {command_highlight_line, "highlight_line"},
               {command_line_number, "line_number"},
               {command_macro_backslashes, "macro_backslashes"},
               {command_new_buffer, "new_buffer"},
               {command_noh, "noh"},
               {command_reload_buffer, "reload_buffer"},
               {command_rename, "rename"},
               {command_syntax, "syntax"},
          };

          // init and copy from our stack array
          config_state->command_entry_count = sizeof(command_entries) / sizeof(command_entries[0]);
          config_state->command_entries = malloc(config_state->command_entry_count * sizeof(*config_state->command_entries));
          for(int64_t i = 0; i < config_state->command_entry_count; ++i){
               config_state->command_entries[i] = command_entries[i];
          }
     }

     // read in state file if it exists
     // TODO: load this into a buffer instead of dealing with the freakin scanf nonsense
     {
          char path[128];
          snprintf(path, 128, "%s/%s", getenv("HOME"), ".ce");

          Buffer_t scrap_buffer = {};
          if(ce_load_file(&scrap_buffer, path) && scrap_buffer.line_count){
               ce_message("restoring state from %s", path);

               int yank_lines = atoi(scrap_buffer.lines[0]);

               Point_t start = {0, 1};
               Point_t end = {0, start.y + (yank_lines - 1)};

               if(yank_lines){
                    end.x = ce_last_index(scrap_buffer.lines[end.y]);

                    char* search_pattern = ce_dupe_string(&scrap_buffer, start, end);
                    vim_yank_add(&config_state->vim_state.yank_head, '/', search_pattern, YANK_NORMAL);
                    int rc = regcomp(&config_state->vim_state.search.regex, search_pattern, REG_EXTENDED);
                    if(rc != 0){
                         char error_buffer[BUFSIZ];
                         regerror(rc, &config_state->vim_state.search.regex, error_buffer, BUFSIZ);
                         ce_message("regcomp() failed: '%s'", error_buffer);
                    }else{
                         config_state->vim_state.search.valid_regex = true;
                         ce_message("  search pattern '%s'", search_pattern);
                    }
               }

               int64_t next_line = end.y + 1;

               if(next_line < scrap_buffer.line_count){
                    ce_message("  file cursor positions");
               }

               for(int64_t i = next_line; i < scrap_buffer.line_count; ++i){
                    char* space = scrap_buffer.lines[i];
                    while(*space && *space != ' ') space++;
                    if(*space != ' ') break;

                    char* filename = scrap_buffer.lines[i];
                    ssize_t filename_len = space - filename;
                    int line_number = atoi(space + 1);

                    BufferNode_t* itr = *head;
                    while(itr){
                         if(strncmp(itr->buffer->name, filename, filename_len) == 0){
                              ce_message(" '%s' at line %d", itr->buffer->name, line_number);
                              itr->buffer->cursor.y = line_number;
                              ce_move_cursor_to_soft_beginning_of_line(itr->buffer, &itr->buffer->cursor);
                              BufferView_t* view = ce_buffer_in_view(config_state->tab_current->view_head, itr->buffer);
                              if(view){
                                   view->cursor = view->buffer->cursor;
                                   center_view(view);
                              }
                              break;
                         }
                         itr = itr->next;
                    }
               }

               ce_free_buffer(&scrap_buffer);
          }
     }

     // register ctrl + c signal handler
     struct sigaction sa = {};
     sa.sa_handler = sigint_handler;
     sigemptyset(&sa.sa_mask);
     if(sigaction(SIGINT, &sa, NULL) == -1){
          ce_message("failed to register ctrl+c (SIGINT) signal handler.");
     }

     // ignore sigpipe for when things like clang completion error our
     signal(SIGPIPE, SIG_IGN);

     if(pthread_mutex_trylock(&draw_lock) == 0){
          view_drawer(*user_data);
          pthread_mutex_unlock(&draw_lock);
     }

     return true;
}

bool destroyer(BufferNode_t** head, void* user_data)
{
     ConfigState_t* config_state = user_data;

     // write out file with some state we can use to restore
     {
          char path[128];
          snprintf(path, 128, "%s/%s", getenv("HOME"), ".ce");

          FILE* out_file = fopen(path, "w");
          if(out_file){
               // write out last searched text
               VimYankNode_t* yank = vim_yank_find(config_state->vim_state.yank_head, '/');
               if(yank){
                    int64_t line_count = ce_count_string_lines(yank->text);
                    fprintf(out_file, "%"PRId64"\n", line_count);
                    fprintf(out_file, "%s\n", yank->text);
               }else{
                    fprintf(out_file, "0\n");
               }

               // TODO: vim last command

               // write out all buffers and cursor positions
               BufferNode_t* itr = *head;
               while(itr){
                    if(itr->buffer->status != BS_READONLY){
                         // TODO: handle all tabs
                         BufferView_t* view = ce_buffer_in_view(config_state->tab_current->view_head, itr->buffer);
                         if(view){
                              fprintf(out_file, "%s %"PRId64"\n", itr->buffer->name, view->cursor.y);
                         }else{
                              fprintf(out_file, "%s %"PRId64"\n", itr->buffer->name, itr->buffer->cursor.y);
                         }
                    }
                    itr = itr->next;
               }

               fclose(out_file);
          }
     }

     if(config_state->terminal_head){
          TerminalColorPairNode_t* color_itr = terminal_color_pairs_head;

          while(color_itr){
               TerminalColorPairNode_t* tmp = color_itr;
               color_itr = color_itr->next;
               free(tmp);
          }
     }

     TerminalNode_t* term_itr = config_state->terminal_head;
     while(term_itr){
          if(term_itr->check_update_thread){
               pthread_cancel(term_itr->check_update_thread);
               pthread_join(term_itr->check_update_thread, NULL);
               terminal_free(&term_itr->terminal);
          }

          TerminalNode_t* tmp = term_itr;
          term_itr = term_itr->next;
          free(tmp);
     }

     config_state->terminal_head = NULL;

     BufferNode_t* itr = *head;
     while(itr){
          free_buffer_state(itr->buffer->user_data);
          itr->buffer->user_data = NULL;

          free(itr->buffer->syntax_user_data);
          itr->buffer->syntax_user_data = NULL;

          itr = itr->next;
     }

     TabView_t* tab_itr = config_state->tab_head;
     while(tab_itr){
          if(tab_itr->view_head){
               ce_free_views(&tab_itr->view_head);
          }
          tab_itr = tab_itr->next;
     }

     tab_itr = config_state->tab_head;
     while(tab_itr){
          TabView_t* tmp = tab_itr;
          tab_itr = tab_itr->next;
          free(tmp);
     }

     // input buffer
     {
          free_buffer_state(config_state->input_buffer.user_data);
          free(config_state->input_buffer.syntax_user_data);
          ce_free_buffer(&config_state->input_buffer);
          free(config_state->view_input);
          free(config_state->view_auto_complete);
     }

     free_buffer_state(config_state->clang_completion_buffer.user_data);
     free(config_state->clang_completion_buffer.syntax_user_data);
     ce_free_buffer(&config_state->clang_completion_buffer);

     free_buffer_state(config_state->buffer_list_buffer.user_data);
     free(config_state->buffer_list_buffer.syntax_user_data);
     ce_free_buffer(&config_state->buffer_list_buffer);

     free_buffer_state(config_state->mark_list_buffer.user_data);
     free(config_state->mark_list_buffer.syntax_user_data);
     ce_free_buffer(&config_state->mark_list_buffer);

     free_buffer_state(config_state->yank_list_buffer.user_data);
     free(config_state->yank_list_buffer.syntax_user_data);
     ce_free_buffer(&config_state->yank_list_buffer);

     free_buffer_state(config_state->macro_list_buffer.user_data);
     free(config_state->macro_list_buffer.syntax_user_data);
     ce_free_buffer(&config_state->macro_list_buffer);

     free(config_state->command_entries);

     // history
     input_history_free(&config_state->search_history);
     input_history_free(&config_state->command_history);

     pthread_mutex_destroy(&draw_lock);

     auto_complete_free(&config_state->auto_complete);

     free(config_state->vim_state.last_insert_command);

     ce_keys_free(&config_state->vim_state.command_head);
     ce_keys_free(&config_state->vim_state.record_macro_head);

     if(config_state->vim_state.search.valid_regex){
          regfree(&config_state->vim_state.search.regex);
     }

     vim_yanks_free(&config_state->vim_state.yank_head);

     vim_macros_free(&config_state->vim_state.macro_head);

     VimMacroCommitNode_t* macro_commit_head = config_state->vim_state.macro_commit_current;
     while(macro_commit_head && macro_commit_head->prev) macro_commit_head = macro_commit_head->prev;
     vim_macro_commits_free(&macro_commit_head);

     free(config_state);
     return true;
}

bool key_handler(int key, BufferNode_t** head, void* user_data)
{
     ConfigState_t* config_state = user_data;
     Buffer_t* buffer = config_state->tab_current->view_current->buffer;
     BufferState_t* buffer_state = buffer->user_data;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Point_t* cursor = &config_state->tab_current->view_current->cursor;

     bool handled_key = false;

     if(key == KEY_RESIZE){
          Point_t top_left = {0, 0};
          Point_t bottom_right = {g_terminal_dimensions->x - 1, g_terminal_dimensions->y - 1};
          ce_calc_views(config_state->tab_current->view_head, top_left, bottom_right);
          resize_terminal_if_in_view(buffer_view, config_state->terminal_head);
          handled_key = true;
     }

     config_state->save_buffer_head = head;

     if(config_state->vim_state.mode != VM_INSERT){
          switch(config_state->last_key){
          default:
               break;
          case 'z':
          {
               Point_t location;
               handled_key = true;
               switch(key){
               default:
                    handled_key = false;
                    break;
               case 't':
                    location = (Point_t){0, cursor->y};
                    scroll_view_to_location(buffer_view, &location);
                    break;
               case 'z': {
                    center_view(buffer_view);
                    // reset key so we don't come in here on the very next iteration
                    key = 0;
               } break;
               case 'b': {
                    // move current line to bottom of view
                    location = (Point_t){0, cursor->y - buffer_view->bottom_right.y};
                    scroll_view_to_location(buffer_view, &location);
               } break;
               }
          } break;
          case 'g':
          {
               handled_key = true;

               switch(key){
               default:
                    handled_key = false;
                    break;
               case 't':
                    if(config_state->tab_current->next){
                         config_state->tab_current = config_state->tab_current->next;
                    }else{
                         config_state->tab_current = config_state->tab_head;
                    }
                    break;
               case 'T':
                    if(config_state->tab_current == config_state->tab_head){
                         // find tail
                         TabView_t* itr = config_state->tab_head;
                         while(itr->next) itr = itr->next;
                         config_state->tab_current = itr;
                    }else{
                         TabView_t* itr = config_state->tab_head;
                         while(itr && itr->next != config_state->tab_current) itr = itr->next;

                         // what if we don't find our current tab and hit the end of the list?!
                         assert(itr);
                         config_state->tab_current = itr;
                    }
                    break;
               case 'f':
               {
                    if(!buffer->lines[cursor->y]) break;
                    Point_t word_start;
                    Point_t word_end;
                    if(!ce_get_word_at_location(buffer, *cursor, &word_start, &word_end)) break;

                    // expand left to pick up the beginning of a path
                    char check_word_char = 0;
                    while(true){
                         Point_t save_word_start = word_start;

                         if(!ce_move_cursor_to_beginning_of_word(buffer, &word_start, true)) break;
                         if(!ce_get_char(buffer, word_start, &check_word_char)) break;
                         // TODO: probably need more rules for matching filepaths
                         if(isalpha(check_word_char) || isdigit(check_word_char) || check_word_char == '/'){
                              continue;
                         }else{
                              word_start = save_word_start;
                              break;
                         }
                    }

                    // expand right to pick up the full path
                    while(true){
                         Point_t save_word_end = word_end;

                         if(!ce_move_cursor_to_end_of_word(buffer, &word_end, true)) break;
                         if(!ce_get_char(buffer, word_end, &check_word_char)) break;
                         // TODO: probably need more rules for matching filepaths
                         if(isalpha(check_word_char) || isdigit(check_word_char) || check_word_char == '/'){
                              continue;
                         }else{
                              word_end = save_word_end;
                              break;
                         }
                    }

                    word_end.x++;

                    char period = 0;
                    if(!ce_get_char(buffer, word_end, &period)) break;
                    if(period != '.') break;
                    Point_t extension_start;
                    Point_t extension_end;
                    if(!ce_get_word_at_location(buffer, (Point_t){word_end.x + 1, word_end.y}, &extension_start, &extension_end)) break;
                    extension_end.x++;
                    char filename[PATH_MAX];
                    snprintf(filename, PATH_MAX, "%.*s.%.*s",
                             (int)(word_end.x - word_start.x), buffer->lines[word_start.y] + word_start.x,
                             (int)(extension_end.x - extension_start.x), buffer->lines[extension_start.y] + extension_start.x);

                    if(access(filename, F_OK) != 0){
                         ce_message("no such file: '%s' to go to", filename);
                         break;
                    }

                    BufferNode_t* node = new_buffer_from_file(*head, filename);
                    if(node){
                         buffer_view->buffer = node->buffer;
                         buffer_view->cursor = (Point_t){0, 0};
                    }
               } break;
               case 'd':
               {
                    Point_t word_start;
                    Point_t word_end;
                    if(!ce_get_word_at_location(buffer, *cursor, &word_start, &word_end)) break;
                    assert(word_start.y == word_end.y);
                    int len = (word_end.x - word_start.x) + 1;

                    char command[BUFSIZ];
                    snprintf(command, BUFSIZ, "cscope -L1%*.*s", len, len, buffer->lines[cursor->y] + word_start.x);
                    run_command_on_terminal_in_view(config_state->terminal_head, config_state->tab_current->view_head, command);
               } break;
               case 'b':
               {
                    char command[BUFSIZ];
                    strncpy(command, "make", BUFSIZ);
                    run_command_on_terminal_in_view(config_state->terminal_head, config_state->tab_current->view_head, command);
               } break;
               case 'm':
               {
                    char command[BUFSIZ];
                    strncpy(command, "make clean", BUFSIZ);
                    run_command_on_terminal_in_view(config_state->terminal_head, config_state->tab_current->view_head, command);
               } break;
#if 0
               // NOTE: useful for debugging
               case 'a':
                    config_state->tab_current->view_current->buffer = &config_state->clang_completion_buffer;
                    break;
#endif
               case 'r':
                    clear();
                    break;
               case 'v':
                    config_state->tab_current->view_overrideable = config_state->tab_current->view_current;
                    config_state->tab_current->overriden_buffer = NULL;
                    break;
               case 'q':
               {
                    uint64_t unsaved_buffers = 0;
                    BufferNode_t* itr = *head;
                    while(itr){
                         if(itr->buffer->status == BS_MODIFIED) unsaved_buffers++;
                         itr = itr->next;
                    }

                    if(unsaved_buffers){
                         input_start(config_state, "Unsaved buffers... Quit anyway? (y/n)", key);
                         break;
                    }

                    // quit !
                    return false;
               }
               }

               if(handled_key){
                    key = 0;
                    ce_keys_free(&config_state->vim_state.command_head);
               }
          } break;
          case 'm':
          case '"':
          {
               if(!isprint(key)) break;

               if(key == '?'){
                    update_mark_list_buffer(config_state, buffer);

                    override_buffer_in_view(config_state->tab_current, config_state->tab_current->view_current, &config_state->mark_list_buffer, &config_state->buffer_before_query);

                    ce_keys_free(&config_state->vim_state.command_head);

                    handled_key = true;
                    key = 0;
               }
          } break;
          case 'y':
               if(!isprint(key)) break;

               if(key == '?'){
                    update_yank_list_buffer(config_state);

                    override_buffer_in_view(config_state->tab_current, config_state->tab_current->view_current, &config_state->yank_list_buffer, &config_state->buffer_before_query);

                    ce_keys_free(&config_state->vim_state.command_head);
                    handled_key = true;
                    key = 0;
               }
               break;
          case 'Z':
               switch(config_state->last_key){
               default:
                    break;
               case 'Z':
                    ce_save_buffer(buffer, buffer->filename);
                    return false; // quit
               }
               break;
          case 'q':
          case '@':
               if(key == '?'){
                    update_macro_list_buffer(config_state);

                    override_buffer_in_view(config_state->tab_current, config_state->tab_current->view_current, &config_state->macro_list_buffer, &config_state->buffer_before_query);

                    ce_keys_free(&config_state->vim_state.command_head);
                    handled_key = true;
                    key = 0;
               }
               break;
#if 0 // useful for debugging commit history
          case '!':
               ce_commits_dump(buffer_state->commit_tail);
               break;
#endif
#if 0 // useful for debugging macro commit history
          case '!':
          {
               MacroCommitNode_t* head = config_state->vim_state.macro_commit_current;
               while(head->prev) head = head->prev;
               macro_commits_dump(head);
          } break;
#endif
          case KEY_ENTER:
          case -1:
          {
               TerminalNode_t* terminal_node = is_terminal_buffer(config_state->terminal_head, buffer);
               if(terminal_node){
                    move_jump_location_to_end_of_output(terminal_node);
                    terminal_send_key(&terminal_node->terminal, key);
                    config_state->vim_state.mode = VM_INSERT;
                    buffer_view->cursor = terminal_node->terminal.cursor;
                    view_follow_cursor(buffer_view, config_state->line_number_type);
                    handled_key = true;
                    key = 0;
               }
          } break;
          }
     }else{
          TerminalNode_t* terminal_node = is_terminal_buffer(config_state->terminal_head, buffer);
          if(terminal_node){
               if(key != KEY_ESCAPE){
                    Terminal_t* terminal = &terminal_node->terminal;
                    buffer_view->cursor = terminal->cursor;

                    if(key == KEY_ENTER) move_jump_location_to_end_of_output(terminal_node);

                    if(terminal_send_key(terminal, key)){
                         handled_key = true;
                         key = 0;
                         if(buffer_view->cursor.x < (terminal->width - 1)){
                              buffer_view->cursor.x++;
                         }
                         view_follow_cursor(buffer_view, LNT_NONE);
                    }
               }
          }else{
               switch(key){
               default:
                    break;
               case KEY_TAB:
                    if(auto_completing(&config_state->auto_complete)){
                         if(config_state->auto_complete.type == ACT_EXACT){
                              char* complete = auto_complete_get_completion(&config_state->auto_complete, cursor->x);
                              int64_t complete_len = strlen(complete);
                              if(ce_insert_string(buffer, *cursor, complete)){
                                   Point_t save_cursor = *cursor;
                                   ce_move_cursor(buffer, cursor, (Point_t){complete_len, 0});
                                   cursor->x++;
                                   ce_commit_insert_string(&buffer_state->commit_tail, save_cursor, save_cursor, *cursor, complete, BCC_KEEP_GOING);
                              }else{
                                   free(complete);
                              }
                         }else if(config_state->auto_complete.type == ACT_OCCURANCE){
                              int64_t complete_len = strlen(config_state->auto_complete.current->option);
                              int64_t line_len = strlen(buffer->lines[config_state->auto_complete.start.y]);
                              char* removed = ce_dupe_string(buffer, config_state->auto_complete.start,
                                                             (Point_t){config_state->auto_complete.start.x + line_len - 1, config_state->auto_complete.start.y});
                              if(ce_remove_string(buffer, config_state->auto_complete.start, line_len)){
                                   ce_commit_remove_string(&buffer_state->commit_tail, config_state->auto_complete.start, *cursor, *cursor, removed, BCC_KEEP_GOING);
                                   if(ce_insert_string(buffer, config_state->auto_complete.start, config_state->auto_complete.current->option)){
                                        Point_t save_cursor = *cursor;
                                        cursor->x = config_state->auto_complete.start.x + complete_len;
                                        char* inserted = strdup(config_state->auto_complete.current->option);
                                        ce_commit_insert_string(&buffer_state->commit_tail, config_state->auto_complete.start, save_cursor, *cursor, inserted, BCC_KEEP_GOING);
                                   }
                              }
                         }

                         update_completion_buffer(config_state->completion_buffer, &config_state->auto_complete, buffer->lines[config_state->auto_complete.start.y]);

                         if(config_state->input){
                              switch(config_state->input_key){
                              default:
                                   break;
                              case 6: // Ctrl + f
                                   calc_auto_complete_start_and_path(&config_state->auto_complete,
                                                                     buffer->lines[cursor->y],
                                                                     *cursor,
                                                                     config_state->completion_buffer,
                                                                     config_state->load_file_search_path);
                                   break;
                              }
                         }

                         handled_key = true;
                         key = 0;
                    }
                    break;
               case KEY_REDO:
               {
                    VimYankNode_t* yank = vim_yank_find(config_state->vim_state.yank_head, '"');

                    if(!yank) break;

                    if(ce_insert_string(buffer, *cursor, yank->text)){
                         ce_commit_insert_string(&buffer_state->commit_tail, *cursor, *cursor,
                                                 *cursor, strdup(yank->text), BCC_STOP);
                         handled_key = true;
                         key = 0;
                    }

                    ce_advance_cursor(buffer, cursor, strlen(yank->text));
               } break;
               }
          }
     }

     if(!handled_key){
          if(key == KEY_ENTER){
               if(confirm_action(config_state, *head)){
                    ce_keys_free(&config_state->vim_state.command_head);
                    key = 0;
                    handled_key = true;
               }
          }
     }

     if(!handled_key){
          Point_t save_cursor = *cursor;
          VimKeyHandlerResult_t vkh_result = vim_key_handler(key, &config_state->vim_state, config_state->tab_current->view_current->buffer,
                                                             &config_state->tab_current->view_current->cursor, &buffer_state->commit_tail,
                                                             &buffer_state->vim_buffer_state, false);
          switch(vkh_result.type){
          default:
               break;
          case VKH_HANDLED_KEY:
               if(config_state->vim_state.mode == VM_INSERT){
                    if(config_state->input){
                         switch(key){
                         default:
                              break;
                         case KEY_UP:
                              if(iterate_history_input(config_state, true)){
                                   if(buffer->line_count && buffer->lines[cursor->y][0]) cursor->x++;
                              }
                              break;
                         case KEY_DOWN:
                              if(iterate_history_input(config_state, false)){
                                   if(buffer->line_count && buffer->lines[cursor->y][0]) cursor->x++;
                              }
                              break;
                         }

                         switch(config_state->input_key){
                         default:
                              break;
                         case 6: // load file
                              calc_auto_complete_start_and_path(&config_state->auto_complete,
                                                                buffer->lines[cursor->y],
                                                                *cursor,
                                                                config_state->completion_buffer,
                                                                config_state->load_file_search_path);
                              break;
                         case ':': // command
                         case 2: // Ctrl + b
                         {
                              Point_t end = {cursor->x - 1, cursor->y};
                              if(end.x < 0) end.x = 0;
                              char* match = "";

                              pthread_mutex_lock(&completion_lock);
                              if(auto_completing(&config_state->auto_complete)){
                                   if(!ce_points_equal(config_state->auto_complete.start, *cursor)) match = ce_dupe_string(buffer, config_state->auto_complete.start, end);
                                   if(strstr(config_state->auto_complete.current->option, match) == NULL){
                                        auto_complete_next(&config_state->auto_complete, match);
                                   }
                              }else{
                                   auto_complete_start(&config_state->auto_complete, ACT_OCCURANCE, (Point_t){0, cursor->y});
                                   if(!ce_points_equal(config_state->auto_complete.start, *cursor)) match = ce_dupe_string(buffer, config_state->auto_complete.start, end);
                                   auto_complete_next(&config_state->auto_complete, match);
                              }

                              update_completion_buffer(config_state->completion_buffer, &config_state->auto_complete, match);
                              pthread_mutex_unlock(&completion_lock);
                              if(!ce_points_equal(config_state->auto_complete.start, end))free(match);
                         } break;
                         }
                    }else if(buffer->type == BFT_C || buffer->type == BFT_CPP){
                         if(auto_completing(&config_state->auto_complete)){
                              Point_t end = {cursor->x - 1, cursor->y};
                              if(end.x < 0) end.x = 0;
                              char* match = "";
                              match = ce_dupe_string(buffer, config_state->auto_complete.start, end);
                              size_t match_len = 0;
                              if(match) match_len = strlen(match);
                              if(config_state->auto_complete.current && strncmp(config_state->auto_complete.current->option, match, match_len) == 0){
                                   // pass
                              }else{
                                   pthread_mutex_lock(&completion_lock);
                                   auto_complete_next(&config_state->auto_complete, match);
                                   update_completion_buffer(config_state->completion_buffer, &config_state->auto_complete, match);
                                   pthread_mutex_unlock(&completion_lock);
                              }

                              free(match);
                         }

                         switch(key){
                         default:
                              // check if key is a valid c identifier
                              if((isalnum(key) || key == '_' ) && !auto_completing(&config_state->auto_complete)){
                                   char prev_char = 0;
                                   if(cursor->x > 1 && ce_get_char(buffer, (Point_t){cursor->x - 2, cursor->y}, &prev_char)){
                                        if(!isalnum(prev_char) && prev_char != '_'){
                                             clang_completion(config_state, (Point_t){cursor->x - 1, cursor->y});
                                        }
                                   }
                              }
                              break;
                         case '>':
                         {
                              // only continue to complete if we type the full '->'
                              char prev_char = 0;
                              if(cursor->x > 1 && ce_get_char(buffer, (Point_t){cursor->x - 2, cursor->y}, &prev_char)){
                                   if(prev_char != '-'){
                                        break;
                                   }
                              }else{
                                   break;
                              }

                              // intentional fallthrough
                         }
                         case '.':
                              clang_completion(config_state, *cursor);
                              break;
                         }
                    }
               }
               break;
          case VKH_COMPLETED_ACTION_FAILURE:
               if(vkh_result.completed_action.change.type == VCT_DELETE &&
                  config_state->tab_current->view_current->buffer == &config_state->buffer_list_buffer){
                    VimActionRange_t action_range;
                    if(vim_action_get_range(&vkh_result.completed_action, buffer, cursor, &config_state->vim_state,
                                            &buffer_state->vim_buffer_state, &action_range)){
                         int64_t delete_index = action_range.sorted_start->y - 1;
                         int64_t buffers_to_delete = (action_range.sorted_end->y - action_range.sorted_start->y) + 1;

                         // TODO: what if you delete the terminal buffer!

                         for(int64_t b = 0; b < buffers_to_delete; ++b){
                              if(!delete_buffer_at_index(head, config_state->tab_head, delete_index,
                                                         &config_state->terminal_head, &config_state->terminal_current)){
                                   return false; // quit !
                              }
                         }

                         update_buffer_list_buffer(config_state, *head);

                         if(cursor->y >= config_state->buffer_list_buffer.line_count){
                              cursor->y = config_state->buffer_list_buffer.line_count - 1;
                         }

                         vim_enter_normal_mode(&config_state->vim_state);
                         ce_keys_free(&config_state->vim_state.command_head);
                    }
               }else if(vkh_result.completed_action.change.type == VCT_PASTE_BEFORE ||
                        vkh_result.completed_action.change.type == VCT_PASTE_AFTER){
                    TerminalNode_t* terminal_node = is_terminal_buffer(config_state->terminal_head, config_state->tab_current->view_current->buffer);
                    if(terminal_node){
                         Terminal_t* terminal = &terminal_node->terminal;
                         char reg = vkh_result.completed_action.change.reg ? vkh_result.completed_action.change.reg : '"';
                         VimYankNode_t* yank = vim_yank_find(config_state->vim_state.yank_head, reg);
                         if(yank){
                              const char* itr = yank->text;
                              while(*itr){
                                   terminal_send_key(terminal, *itr);
                                   itr++;
                              }
                         }
                    }
               }
               break;
          case VKH_COMPLETED_ACTION_SUCCESS:
          {
               if(config_state->vim_state.mode == VM_INSERT){
                    TerminalNode_t* terminal_node = is_terminal_buffer(config_state->terminal_head, buffer_view->buffer);
                    if(terminal_node){
                         buffer_view->cursor = terminal_node->terminal.cursor;
                         view_follow_cursor(buffer_view, config_state->line_number_type);
                    }
               }else if(vkh_result.completed_action.motion.type == VMT_SEARCH ||
                        vkh_result.completed_action.motion.type == VMT_SEARCH_WORD_UNDER_CURSOR){
                    config_state->do_not_highlight_search = false;
                    center_view_when_cursor_outside_portion(buffer_view, 0.15f, 0.85f);
                    view_jump_insert(buffer_view->user_data, buffer_view->buffer->filename, save_cursor);
               }else if(vkh_result.completed_action.motion.type == VMT_GOTO_MARK){
                    config_state->do_not_highlight_search = false;
                    center_view_when_cursor_outside_portion(buffer_view, 0.15f, 0.85f);
                    view_jump_insert(buffer_view->user_data, buffer_view->buffer->filename, save_cursor);
               }else if(vkh_result.completed_action.motion.type == VMT_BEGINNING_OF_FILE ||
                        vkh_result.completed_action.motion.type == VMT_END_OF_FILE){
                    view_jump_insert(buffer_view->user_data, buffer_view->buffer->filename, save_cursor);
               }else if(vkh_result.completed_action.change.type == VCT_YANK){
                    VimActionRange_t action_range;
                    if(vim_action_get_range(&vkh_result.completed_action, buffer, cursor, &config_state->vim_state,
                                            &buffer_state->vim_buffer_state, &action_range)){
                         buffer->blink = true;
                         buffer->highlight_start = *action_range.sorted_start;
                         buffer->highlight_end = *action_range.sorted_end;
                    }
               }else if(vkh_result.completed_action.change.type == VCT_SUBSTITUTE){
                    VimYankNode_t* yank = vim_yank_find(config_state->vim_state.yank_head,
                                                        vkh_result.completed_action.change.reg ? vkh_result.completed_action.change.reg : '"');
                    if(yank){
                         VimActionRange_t action_range;
                         if(vim_action_get_range(&vkh_result.completed_action, buffer, cursor, &config_state->vim_state,
                                                 &buffer_state->vim_buffer_state, &action_range)){
                              buffer->blink = true;
                              buffer->highlight_start = *action_range.sorted_start;
                              buffer->highlight_end = buffer->highlight_start;
                              int64_t len = strlen(yank->text) - 1;
                              ce_advance_cursor(buffer, &buffer->highlight_end, len);
                         }
                    }
               }else if(vkh_result.completed_action.change.type == VCT_PASTE_BEFORE){
                    VimYankNode_t* yank = vim_yank_find(config_state->vim_state.yank_head,
                                                        vkh_result.completed_action.change.reg ? vkh_result.completed_action.change.reg : '"');
                    if(yank){
                         switch(yank->mode){
                         default:
                              break;
                         case YANK_NORMAL:
                         {
                              buffer->blink = true;
                              buffer->highlight_start = *cursor;
                              buffer->highlight_end = buffer->highlight_start;
                              int64_t len = strlen(yank->text) - 1;
                              ce_advance_cursor(buffer, &buffer->highlight_end, len);
                         } break;
                         case YANK_LINE:
                         {
                              buffer->blink = true;
                              buffer->highlight_start = (Point_t){0, cursor->y};
                              buffer->highlight_end = buffer->highlight_start;
                              int64_t len = strlen(yank->text) - 1;
                              ce_advance_cursor(buffer, &buffer->highlight_end, len);
                         } break;
                         }
                    }
               }else if(vkh_result.completed_action.change.type == VCT_PASTE_AFTER){
                    VimYankNode_t* yank = vim_yank_find(config_state->vim_state.yank_head,
                                                        vkh_result.completed_action.change.reg ? vkh_result.completed_action.change.reg : '"');

                    switch(yank->mode){
                    default:
                         break;
                    case YANK_NORMAL:
                    {
                         buffer->blink = true;
                         buffer->highlight_end = *cursor;
                         buffer->highlight_start = buffer->highlight_end;
                         int64_t len = strlen(yank->text) - 1;
                         ce_advance_cursor(buffer, &buffer->highlight_start, -len);
                    } break;
                    case YANK_LINE:
                    {
                         buffer->blink = true;
                         buffer->highlight_start = (Point_t){0, cursor->y};
                         buffer->highlight_end = buffer->highlight_start;
                         int64_t len = strlen(yank->text) - 1;
                         ce_advance_cursor(buffer, &buffer->highlight_end, len);
                    } break;
                    }
               }

               // don't save 'g' if we completed an action with it, this ensures we don't use it in the next update
               if(key == 'g') key = 0;
          } break;
          case VKH_UNHANDLED_KEY:
               switch(key){
               case KEY_MOUSE:
                    handle_mouse_event(config_state, buffer, buffer_view, cursor);
                    break;
               case KEY_DC:
                    // TODO: with our current insert mode undo implementation we can't support this
                    // ce_remove_char(buffer, cursor);
                    break;
               case 14: // Ctrl + n
                    if(auto_completing(&config_state->auto_complete)){
                         Point_t end = {cursor->x - 1, cursor->y};
                         if(end.x < 0) end.x = 0;
                         char* match = "";
                         if(!ce_points_equal(config_state->auto_complete.start, *cursor)) match = ce_dupe_string(buffer, config_state->auto_complete.start, end);
                         auto_complete_next(&config_state->auto_complete, match);
                         update_completion_buffer(config_state->completion_buffer, &config_state->auto_complete,
                                                  match);
                         if(!ce_points_equal(config_state->auto_complete.start, *cursor)) free(match);
                         break;
                    }

                    if(config_state->input){
                         if(iterate_history_input(config_state, false)){
                              if(buffer->line_count && buffer->lines[cursor->y][0]) cursor->x++;
                              vim_enter_normal_mode(&config_state->vim_state);
                         }
                    }
                    break;
               case 16: // Ctrl + p
                    if(auto_completing(&config_state->auto_complete)){
                         Point_t end = {cursor->x - 1, cursor->y};
                         if(end.x < 0) end.x = 0;
                         char* match = "";
                         if(!ce_points_equal(config_state->auto_complete.start, *cursor)) match = ce_dupe_string(buffer, config_state->auto_complete.start, end);
                         auto_complete_prev(&config_state->auto_complete, match);
                         update_completion_buffer(config_state->completion_buffer, &config_state->auto_complete,
                                                  match);
                         if(!ce_points_equal(config_state->auto_complete.start, *cursor)) free(match);
                         break;
                    }

                    if(config_state->input){
                         if(iterate_history_input(config_state, true)){
                              if(buffer->line_count && buffer->lines[cursor->y][0]) cursor->x++;
                              vim_enter_normal_mode(&config_state->vim_state);
                         }
                    }
                    break;
               }

               if(config_state->vim_state.mode != VM_INSERT){
                    switch(key){
                    default:
                         break;
                    case '.':
                    {
                         vim_action_apply(&config_state->vim_state.last_action, buffer, cursor, &config_state->vim_state,
                                          &buffer_state->commit_tail, &buffer_state->vim_buffer_state);

                         if(config_state->vim_state.mode != VM_INSERT || !config_state->vim_state.last_insert_command ||
                            config_state->vim_state.last_action.change.type == VCT_PLAY_MACRO) break;

                         if(!vim_enter_insert_mode(&config_state->vim_state, config_state->tab_current->view_current->buffer)) break;

                         int* cmd_itr = config_state->vim_state.last_insert_command;
                         while(*cmd_itr){
                              vim_key_handler(*cmd_itr, &config_state->vim_state, config_state->tab_current->view_current->buffer,
                                              &config_state->tab_current->view_current->cursor, &buffer_state->commit_tail,
                                              &buffer_state->vim_buffer_state, true);
                              cmd_itr++;
                         }

                         vim_enter_normal_mode(&config_state->vim_state);
                         ce_keys_free(&config_state->vim_state.command_head);
                         if(buffer_state->commit_tail) buffer_state->commit_tail->commit.chain = BCC_STOP;
                    } break;
                    case KEY_ESCAPE:
                         if(config_state->input) input_cancel(config_state);
                         break;
                    case KEY_SAVE:
                         ce_save_buffer(buffer, buffer->filename);
                         break;
                    case KEY_CLOSE: // Ctrl + q
                    {
                         if(config_state->input){
                              input_cancel(config_state);
                              break;
                         }

                         if(config_state->vim_state.recording_macro){
                              vim_stop_recording_macro(&config_state->vim_state);
                              break;
                         }

                         if(config_state->tab_current == config_state->tab_head &&
                            config_state->tab_current->next == NULL &&
                            config_state->tab_current->view_current == config_state->tab_current->view_head &&
                            config_state->tab_current->view_current->next_horizontal == NULL &&
                            config_state->tab_current->view_current->next_vertical == NULL ){
                                 break;
                         }

                         Point_t save_cursor_on_terminal = get_cursor_on_terminal(cursor, buffer_view, config_state->line_number_type);
                         config_state->tab_current->view_current->buffer->cursor = config_state->tab_current->view_current->cursor;

                         if(ce_remove_view(&config_state->tab_current->view_head, config_state->tab_current->view_current)){
                              if(config_state->tab_current->view_current == config_state->tab_current->view_overrideable){
                                   config_state->tab_current->view_overrideable = NULL;
                              }

                              // if head is NULL, then we have removed the view head, and there were no other views, head is NULL
                              if(!config_state->tab_current->view_head){
                                   if(config_state->tab_current->next){
                                        config_state->tab_current->next = config_state->tab_current->next;
                                        TabView_t* tmp = config_state->tab_current;
                                        config_state->tab_current = config_state->tab_current->next;
                                        tab_view_remove(&config_state->tab_head, tmp);
                                        break;
                                   }else{

                                        TabView_t* itr = config_state->tab_head;
                                        while(itr && itr->next != config_state->tab_current) itr = itr->next;
                                        assert(itr);
                                        assert(itr->next == config_state->tab_current);
                                        tab_view_remove(&config_state->tab_head, config_state->tab_current);
                                        config_state->tab_current = itr;
                                        break;
                                   }
                              }

                              if(config_state->tab_current->view_current == config_state->tab_current->view_previous){
                                   config_state->tab_current->view_previous = NULL;
                              }

                              Point_t top_left;
                              Point_t bottom_right;
                              get_terminal_view_rect(config_state->tab_head, &top_left, &bottom_right);

                              ce_calc_views(config_state->tab_current->view_head, top_left, bottom_right);
                              BufferView_t* new_view = ce_find_view_at_point(config_state->tab_current->view_head, save_cursor_on_terminal);
                              if(new_view){
                                   config_state->tab_current->view_current = new_view;
                              }else{
                                   config_state->tab_current->view_current = config_state->tab_current->view_head;
                              }

                              resize_terminal_if_in_view(config_state->tab_current->view_head, config_state->terminal_head);
                         }
                    } break;
                    case 2: // Ctrl + b
                         if(config_state->input && config_state->tab_current->view_current == config_state->view_input){
                              // pass
                         }else{
                              pthread_mutex_lock(&completion_lock);
                              auto_complete_free(&config_state->auto_complete);
                              BufferNode_t* itr = *head;
                              while(itr){
                                   auto_complete_insert(&config_state->auto_complete, itr->buffer->name, NULL);
                                   itr = itr->next;
                              }
                              auto_complete_start(&config_state->auto_complete, ACT_OCCURANCE, (Point_t){0, 0});
                              update_completion_buffer(config_state->completion_buffer, &config_state->auto_complete, NULL);
                              pthread_mutex_unlock(&completion_lock);

                              input_start(config_state, "Switch Buffer", key);
                         }
                         break;
                    case 'u':
                         if(buffer_state->commit_tail && buffer_state->commit_tail->commit.type != BCT_NONE){
                              ce_commit_undo(buffer, &buffer_state->commit_tail, cursor);
                              if(buffer_state->commit_tail->commit.type == BCT_NONE){
                                   buffer->status = BS_NONE;
                              }
                         }

                         // if we are recording a macro, kill the last command we entered from the key list
                         if(config_state->vim_state.recording_macro){
                              VimMacroCommitNode_t* last_macro_commit = config_state->vim_state.macro_commit_current->prev;
                              if(last_macro_commit){
                                   do{
                                        KeyNode_t* itr = config_state->vim_state.record_macro_head;
                                        if(last_macro_commit->command_begin){
                                             KeyNode_t* prev = NULL;
                                             while(itr && itr != last_macro_commit->command_begin){
                                                  prev = itr;
                                                  itr = itr->next;
                                             }

                                             if(itr){
                                                  // free the keys from our macro recording
                                                  ce_keys_free(&itr);
                                                  if(prev){
                                                       prev->next = NULL;
                                                  }else{
                                                       config_state->vim_state.record_macro_head = NULL;
                                                  }
                                             }
                                        }else{
                                             // NOTE: not sure this case can get hit anymore
                                             ce_keys_free(&itr);
                                             config_state->vim_state.record_macro_head = NULL;
                                        }

                                        if(!last_macro_commit->chain) break;

                                        last_macro_commit = last_macro_commit->prev;
                                   }while(last_macro_commit);

                                   config_state->vim_state.macro_commit_current = last_macro_commit;
                              }else{
                                   vim_stop_recording_macro(&config_state->vim_state);
                              }
                         }
                         break;
                    case KEY_REDO:
                    {
                         if(buffer_state->commit_tail && buffer_state->commit_tail->next){
                              if(ce_commit_redo(buffer, &buffer_state->commit_tail, cursor)){
                                   if(config_state->vim_state.recording_macro){
                                        do{
                                             KeyNode_t* itr = config_state->vim_state.macro_commit_current->command_copy;
                                             KeyNode_t* new_command_begin = NULL;
                                             while(itr){
                                                  KeyNode_t* new_key = ce_keys_push(&config_state->vim_state.record_macro_head, itr->key);
                                                  if(!new_command_begin) new_command_begin = new_key;
                                                  itr = itr->next;
                                             }

                                             itr = config_state->vim_state.record_macro_head;
                                             while(itr->next) itr = itr->next;

                                             config_state->vim_state.macro_commit_current->command_begin = new_command_begin;
                                             config_state->vim_state.macro_commit_current = config_state->vim_state.macro_commit_current->next;
                                        }while(config_state->vim_state.macro_commit_current && config_state->vim_state.macro_commit_current->prev->chain);
                                   }
                              }
                         }

                    } break;
                    case 'H':
                    {
                         // move cursor to top line of view
                         Point_t location = {cursor->x, buffer_view->top_row};
                         ce_set_cursor(buffer, cursor, location);
                    } break;
                    case 'M':
                    {
                         // move cursor to middle line of view
                         int64_t view_height = buffer_view->bottom_right.y - buffer_view->top_left.y;
                         Point_t location = {cursor->x, buffer_view->top_row + (view_height/2)};
                         ce_set_cursor(buffer, cursor, location);
                    } break;
                    case 'L':
                    {
                         // move cursor to bottom line of view
                         int64_t view_height = buffer_view->bottom_right.y - buffer_view->top_left.y;
                         Point_t location = {cursor->x, buffer_view->top_row + view_height};
                         ce_set_cursor(buffer, cursor, location);
                    } break;
                    case 'z':
                    break;
                    case KEY_NPAGE:
                    {
                         half_page_up(config_state->tab_current->view_current);
                    } break;
                    case KEY_PPAGE:
                    {
                         half_page_down(config_state->tab_current->view_current);
                    } break;
                    case ':':
                    {
                         pthread_mutex_lock(&completion_lock);
                         auto_complete_free(&config_state->auto_complete);
                         for(int64_t i = 0; i < config_state->command_entry_count; ++i){
                              auto_complete_insert(&config_state->auto_complete, config_state->command_entries[i].name, NULL);
                         }
                         auto_complete_start(&config_state->auto_complete, ACT_OCCURANCE, (Point_t){0, 0});
                         update_completion_buffer(config_state->completion_buffer, &config_state->auto_complete, NULL);
                         pthread_mutex_unlock(&completion_lock);
                         input_start(config_state, "Command", key);
                    } break;
                    case '/':
                    {
                         input_start(config_state, "Regex Search", key);
                         config_state->vim_state.search.direction = CE_DOWN;
                         config_state->vim_state.search.start = *cursor;
                         break;
                    }
                    case '?':
                    {
                         input_start(config_state, "Reverse Regex Search", key);
                         config_state->vim_state.search.direction = CE_UP;
                         config_state->vim_state.search.start = *cursor;
                         break;
                    }
                    case '=':
                    {
#if 0
                         if(config_state->movement_keys[0] == MOVEMENT_CONTINUE) return true;
                         else{
                              int64_t begin_format_line;
                              int64_t end_format_line;
                              switch(key){
                              case '=':
                                   begin_format_line = cursor->y;
                                   end_format_line = cursor->y;
                                   break;
                              case 'G':
                                   begin_format_line = cursor->y;
                                   end_format_line = buffer->line_count-1;
                                   break;
                              default:
                                   clear_keys(config_state);
                                   return true;
                              }

                              // TODO support undo with clang-format
                              ce_commits_free(buffer_state->commit_tail);
                              buffer_state->commit_tail = NULL;

                              int in_fds[2]; // 0 = child stdin
                              int out_fds[2]; // 1 = child stdout
                              if(pipe(in_fds) == -1 || pipe(out_fds) == -1){
                                   ce_message("pipe failed %s", strerror(errno));
                                   return true;
                              }
                              pid_t pid = fork();
                              if(pid == -1){
                                   ce_message("fork failed %s", strerror(errno));
                                   return true;
                              }
                              if(pid == 0){
                                   // child process
                                   close(in_fds[1]); // close parent fds
                                   close(out_fds[0]);

                                   close(0); // close stdin
                                   close(1); // close stdout
                                   dup(in_fds[0]); // new stdin
                                   dup(out_fds[1]); // new stdout
                                   int64_t cursor_position = cursor->x+1;
                                   for(int64_t y_itr = 0; y_itr < cursor->y; y_itr++){
                                        cursor_position += strlen(buffer->lines[y_itr]);
                                   }
                                   char* cursor_arg;
                                   asprintf(&cursor_arg, "-cursor=%"PRId64, cursor_position);

                                   char* line_arg;
                                   asprintf(&line_arg, "-lines=%"PRId64":%"PRId64, begin_format_line+1, end_format_line+1);

                                   int ret __attribute__((unused)) = execlp("clang-format", "clang-format", line_arg, cursor_arg, (char *)NULL);
                                   assert(ret != -1);
                                   exit(1); // we should never reach here
                              }

                              // parent process
                              close(in_fds[0]); // close child fds
                              close(out_fds[1]);

                              FILE* child_stdin = fdopen(in_fds[1], "w");
                              FILE* child_stdout = fdopen(out_fds[0], "r");
                              assert(child_stdin);
                              assert(child_stdout);

                              for(int i = 0; i < buffer->line_count; i++){
                                   if(fputs(buffer->lines[i], child_stdin) == EOF || fputc('\n', child_stdin) == EOF){
                                        ce_message("issue with fputs");
                                        return true;
                                   }
                              }
                              fclose(child_stdin);
                              close(in_fds[1]);

                              char formatted_line_buf[BUFSIZ];
                              formatted_line_buf[0] = 0;

                              // read cursor position
                              fgets(formatted_line_buf, BUFSIZ, child_stdout);
                              int cursor_position = -1;
                              sscanf(formatted_line_buf, "{ \"Cursor\": %d", &cursor_position);

                              // blow away all lines in the file
                              for(int64_t i = buffer->line_count - 1; i >= 0; i--){
                                   ce_remove_line(buffer, i);
                              }

                              for(int64_t i = 0; ; i++){
                                   if(fgets(formatted_line_buf, BUFSIZ, child_stdout) == NULL) break;
                                   size_t new_line_len = strlen(formatted_line_buf) - 1;
                                   assert(formatted_line_buf[new_line_len] == '\n');
                                   formatted_line_buf[new_line_len] = 0;
                                   ce_insert_line(buffer, i, formatted_line_buf);
#if 0
                                   if(cursor_position > 0){
                                        cursor_position -= new_line_len+1;
                                        if(cursor_position <= 0){
                                             Point_t new_cursor_location = {-cursor_position, i};
                                             ce_message("moving cursor to %ld", -cursor_position);
                                             ce_set_cursor(buffer, cursor, &new_cursor_location);
                                        }
                                   }
#endif
                              }
                              cursor->x = 0;
                              cursor->y = 0;
                              if(!ce_advance_cursor(buffer, cursor, cursor_position - 1))
                                   ce_message("failed to advance cursor");

#if 0
                              // TODO: use -output-replacements-xml to support undo
                              char* formatted_line = strdup(formatted_line_buf);
                              // save the current line in undo history
                              Point_t delete_begin = {0, cursor->y};
                              char* save_string = ce_dupe_line(buffer, cursor->y);
                              if(!ce_remove_line(buffer, cursor->y)){
                                   ce_message("ce_remove_string failed");
                                   return true;
                              }
                              ce_insert_string(buffer, &delete_begin, formatted_line);
                              ce_commit_change_string(&buffer_state->commit_tail, &delete_begin, cursor, cursor, formatted_line, save_string);
#endif

                              fclose(child_stdout);
                              close(in_fds[0]);

                              // wait for the child process to complete
                              int wstatus;
                              do {
                                   pid_t w = waitpid(pid, &wstatus, WUNTRACED | WCONTINUED);
                                   if (w == -1) {
                                        perror("waitpid");
                                        exit(EXIT_FAILURE);
                                   }

                                   if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) != 0) {
                                        ce_message("clang-format process exited, status=%d\n", WEXITSTATUS(wstatus));
                                   } else if (WIFSIGNALED(wstatus)) {
                                        ce_message("clang-format process killed by signal %d\n", WTERMSIG(wstatus));
                                   } else if (WIFSTOPPED(wstatus)) {
                                        ce_message("clang-format process stopped by signal %d\n", WSTOPSIG(wstatus));
                                   } else if (WIFCONTINUED(wstatus)) {
                                        ce_message("clang-format process continued\n");
                                   }
                              } while (!WIFEXITED(wstatus) && !WIFSIGNALED(wstatus));
                              config_state->command_key = '\0';
                         }
#endif
                    } break;
                    case 24: // Ctrl + x
                         if(config_state->terminal_current){
                              // revive terminal if it is dead !
                              if(!config_state->terminal_current->terminal.is_alive){
                                   pthread_cancel(config_state->terminal_current->terminal.reader_thread);
                                   pthread_join(config_state->terminal_current->terminal.reader_thread, NULL);

                                   if(!start_terminal_in_view(buffer_view, config_state->terminal_current, config_state)){
                                        break;
                                   }
                              }

                              BufferView_t* terminal_view = ce_buffer_in_view(config_state->tab_current->view_head,
                                                                              config_state->terminal_current->buffer);
                              if(terminal_view){
                                   // if terminal is already in view
                                   config_state->tab_current->view_previous = config_state->tab_current->view_current; // save previous view
                                   config_state->tab_current->view_current = terminal_view;
                                   buffer_view = terminal_view;
                              }else if(config_state->tab_current->view_overrideable){
                                   // if an overrideable view exists
                                   tab_view_save_overrideable(config_state->tab_current);
                                   config_state->tab_current->view_current = config_state->tab_current->view_overrideable;
                                   buffer_view = config_state->tab_current->view_current;
                                   buffer_view->buffer = config_state->terminal_current->buffer;
                              }else{
                                   // otherwise use the current view
                                   buffer->cursor = buffer_view->cursor; // save cursor before switching
                                   buffer_view->buffer = config_state->terminal_current->buffer;
                              }

                              buffer_view->cursor = config_state->terminal_current->terminal.cursor;
                              buffer_view->top_row = 0;
                              buffer_view->left_column = 0;
                              view_follow_cursor(buffer_view, config_state->line_number_type);
                              config_state->vim_state.mode = VM_INSERT;
                              break;
                         }

                         // intentionally fall through if there was no terminal available
                    case 1: // Ctrl + a
                    {
                         TerminalNode_t* node = calloc(1, sizeof(*node));
                         if(!node) break;

                         // setup terminal buffer buffer_state
                         BufferState_t* terminal_buffer_state = calloc(1, sizeof(*terminal_buffer_state));
                         if(!terminal_buffer_state){
                              ce_message("failed to allocate buffer state.");
                              return false;
                         }

                         node->buffer = calloc(1, sizeof(*node->buffer));
                         node->buffer->absolutely_no_line_numbers_under_any_circumstances = true;
                         node->buffer->user_data = terminal_buffer_state;

                         TerminalHighlight_t* terminal_highlight_data = calloc(1, sizeof(TerminalHighlight_t));
                         terminal_highlight_data->terminal = &node->terminal;
                         node->buffer->syntax_fn = terminal_highlight;
                         node->buffer->syntax_user_data = terminal_highlight_data;

                         BufferNode_t* new_buffer_node = ce_append_buffer_to_list(*head, node->buffer);
                         if(!new_buffer_node){
                              ce_message("failed to add shell command buffer to list");
                              break;
                         }

                         if(!start_terminal_in_view(buffer_view, node, config_state)){
                              break;
                         }

                         if(config_state->tab_current->view_overrideable){
                              tab_view_save_overrideable(config_state->tab_current);
                              config_state->tab_current->view_current = config_state->tab_current->view_overrideable;
                              buffer_view = config_state->tab_current->view_current;
                         }else{
                              buffer->cursor = buffer_view->cursor; // save cursor before switching
                         }

                         buffer_view->buffer = node->buffer;

                         // append the node to the list
                         int64_t id = 1;
                         if(config_state->terminal_head){
                              TerminalNode_t* itr = config_state->terminal_head;
                              while(itr->next){
                                   itr = itr->next;
                                   id++;
                              }
                              id++;
                              itr->next = node;
                         }else{
                              config_state->terminal_head = node;
                         }

                         // name terminal
                         char buffer_name[64];
                         snprintf(buffer_name, 64, "[terminal %" PRId64 "]", id);
                         node->buffer->name = strdup(buffer_name);

                         config_state->terminal_current = node;
                         config_state->vim_state.mode = VM_INSERT;
                    } break;
                    case 14: // Ctrl + n
                         if(config_state->input) break;

                         jump_to_next_shell_command_file_destination(*head, config_state, true);
                         break;
                    case 16: // Ctrl + p
                         if(config_state->input) break;

                         jump_to_next_shell_command_file_destination(*head, config_state, false);
                         break;
                    case 6: // Ctrl + f
                    {
                         assert(config_state->load_file_search_path == NULL);

                         buffer->cursor = buffer_view->cursor;

                         input_start(config_state, "Load File", key);

                         // when searching for a file, see if we would like to use a path other than the one ce was run at.
                         TerminalNode_t* terminal_node = is_terminal_buffer(config_state->terminal_head, buffer);
                         if(terminal_node){
                              // if we are looking at a terminal, use the terminal's cwd
                              config_state->load_file_search_path = terminal_get_current_directory(&terminal_node->terminal);
                         }else{
                              // if our file has a relative path in it, use that
                              char* last_slash = strrchr(buffer->filename, '/');
                              if(last_slash){
                                   int64_t path_len = last_slash - buffer->filename;
                                   config_state->load_file_search_path = malloc(path_len + 1);
                                   strncpy(config_state->load_file_search_path, buffer->filename, path_len);
                                   config_state->load_file_search_path[path_len] = 0;
                              }
                         }

                         calc_auto_complete_start_and_path(&config_state->auto_complete,
                                                           config_state->view_input->buffer->lines[0],
                                                           *cursor,
                                                           config_state->completion_buffer,
                                                           config_state->load_file_search_path);
                         if(config_state->tab_current->view_overrideable){
                              tab_view_save_overrideable(config_state->tab_current);

                              config_state->tab_current->view_overrideable->buffer = config_state->completion_buffer;
                              config_state->tab_current->view_overrideable->cursor = (Point_t){0, 0};
                              center_view(config_state->tab_current->view_overrideable);
                         }
                    } break;
                    case 20: // Ctrl + t
                    {
                         TabView_t* new_tab = tab_view_insert(config_state->tab_head);
                         if(!new_tab) break;

                         // copy view attributes from the current view
                         *new_tab->view_head = *config_state->tab_current->view_current;
                         new_tab->view_head->next_horizontal = NULL;
                         new_tab->view_head->next_vertical = NULL;
                         new_tab->view_current = new_tab->view_head;

                         config_state->tab_current = new_tab;
                    } break;
                    case 'R':
                         if(config_state->vim_state.mode == VM_VISUAL_RANGE || config_state->vim_state.mode == VM_VISUAL_LINE){
                              input_start(config_state, "Visual Replace", key);
                         }else{
                              input_start(config_state, "Replace", key);
                         }
                    break;
                    case 8: // Ctrl + h
                    {
                         Point_t point = {config_state->tab_current->view_current->top_left.x - 2, // account for window separator
                                         cursor->y - config_state->tab_current->view_current->top_row + config_state->tab_current->view_current->top_left.y};
                         switch_to_view_at_point(config_state, point);
                    } break;
                    case 10: // Ctrl + j
                    {
                         Point_t point = {cursor->x - config_state->tab_current->view_current->left_column + config_state->tab_current->view_current->top_left.x,
                                          config_state->tab_current->view_current->bottom_right.y + 2}; // account for window separator
                         switch_to_view_at_point(config_state, point);
                    } break;
                    case 11: // Ctrl + k
                    {
                         Point_t point = {cursor->x - config_state->tab_current->view_current->left_column + config_state->tab_current->view_current->top_left.x,
                                          config_state->tab_current->view_current->top_left.y - 2};
                         switch_to_view_at_point(config_state, point);
                    } break;
                    case 12: // Ctrl + l
                    {
                         Point_t point = {config_state->tab_current->view_current->bottom_right.x + 2, // account for window separator
                                          cursor->y - config_state->tab_current->view_current->top_row + config_state->tab_current->view_current->top_left.y};
                         switch_to_view_at_point(config_state, point);
                    } break;
                    case 19: // Ctrl + s
                    {
                         split_view(config_state->tab_current->view_head, config_state->tab_current->view_current, false, config_state->line_number_type);
                         resize_terminal_if_in_view(config_state->tab_current->view_head, config_state->terminal_head);
                    } break;
                    case 22: // Ctrl + v
                    {
                         split_view(config_state->tab_current->view_head, config_state->tab_current->view_current, true, config_state->line_number_type);
                         resize_terminal_if_in_view(config_state->tab_current->view_head, config_state->terminal_head);
                    } break;
                    case 15: // ctrl + o
                         view_jump_to_previous(buffer_view, *head);
                         handled_key = true;
                         break;
                    case 9: // ctrl + i (also tab)
                         view_jump_to_next(buffer_view, *head);
                         handled_key = true;
                         break;
                    }
               }else{
                    switch(key){
                    default:
                         break;
                    case 25: // Ctrl + y
                    {
                         Point_t beginning_of_word = *cursor;
                         char cur_char = 0;
                         if(ce_get_char(buffer, (Point_t){cursor->x - 1, cursor->y}, &cur_char)){
                              if(isalpha(cur_char) || cur_char == '_'){
                                   ce_move_cursor_to_beginning_of_word(buffer, &beginning_of_word, true);
                              }
                         }
                         clang_completion(config_state, beginning_of_word);
                    } break;
                    }
               }
               break;
          }
     }

     // incremental search
     if(config_state->input && (config_state->input_key == '/' || config_state->input_key == '?')){
          if(config_state->view_input->buffer->lines == NULL){
               pthread_mutex_lock(&view_input_save_lock);
               config_state->tab_current->view_input_save->cursor = config_state->vim_state.search.start;
               pthread_mutex_unlock(&view_input_save_lock);
          }else{
               size_t search_len = strlen(config_state->view_input->buffer->lines[0]);
               if(search_len){
                    int rc = regcomp(&config_state->vim_state.search.regex, config_state->view_input->buffer->lines[0], REG_EXTENDED);
                    if(rc == 0){
                         config_state->do_not_highlight_search = false;
                         config_state->vim_state.search.valid_regex = true;

                         Point_t match = {};
                         int64_t match_len = 0;
                         if(config_state->view_input->buffer->lines[0][0] &&
                            ce_find_regex(config_state->tab_current->view_input_save->buffer,
                                          config_state->vim_state.search.start, &config_state->vim_state.search.regex, &match,
                                          &match_len, config_state->vim_state.search.direction)){
                              pthread_mutex_lock(&view_input_save_lock);
                              ce_set_cursor(config_state->tab_current->view_input_save->buffer,
                                            &config_state->tab_current->view_input_save->cursor, match);
                              pthread_mutex_unlock(&view_input_save_lock);
                              center_view(config_state->tab_current->view_input_save);
                         }else{
                              pthread_mutex_lock(&view_input_save_lock);
                              config_state->tab_current->view_input_save->cursor = config_state->vim_state.search.start;
                              pthread_mutex_unlock(&view_input_save_lock);
                              center_view(config_state->tab_current->view_input_save);
                         }
                    }else{
                         config_state->vim_state.search.valid_regex = false;
     #if DEBUG
                         // NOTE: this might be too noisy in practice
                         char error_buffer[BUFSIZ];
                         regerror(rc, &regex, error_buffer, BUFSIZ);
                         ce_message("regcomp() failed: '%s'", error_buffer);
     #endif
                    }
               }else{
                    config_state->vim_state.search.valid_regex = false;
               }
          }
     }

     if(config_state->vim_state.mode != VM_INSERT){
          auto_complete_end(&config_state->auto_complete);
     }

     if(config_state->quit) return false;

     config_state->last_key = key;

     if(ce_buffer_in_view(config_state->tab_current->view_head, &config_state->buffer_list_buffer)){
          update_buffer_list_buffer(config_state, *head);
     }

     if(config_state->tab_current->view_current->buffer != &config_state->mark_list_buffer &&
        ce_buffer_in_view(config_state->tab_current->view_head, &config_state->mark_list_buffer)){
          update_mark_list_buffer(config_state, buffer);
     }

     if(ce_buffer_in_view(config_state->tab_current->view_head, &config_state->yank_list_buffer)){
          update_yank_list_buffer(config_state);
     }

     if(ce_buffer_in_view(config_state->tab_current->view_head, &config_state->macro_list_buffer)){
          update_macro_list_buffer(config_state);
     }

     // grab the draw lock so we can draw
     if(pthread_mutex_trylock(&draw_lock) == 0){
          view_drawer(user_data);
          pthread_mutex_unlock(&draw_lock);
     }

     return true;
}

void view_drawer(void* user_data)
{
     // clear all lines in the terminal
     erase();

     ConfigState_t* config_state = user_data;
     Buffer_t* buffer = config_state->tab_current->view_current->buffer;
     BufferState_t* buffer_state = buffer->user_data;
     BufferView_t* buffer_view = config_state->tab_current->view_current;
     Point_t* cursor = &config_state->tab_current->view_current->cursor;

     Point_t top_left;
     Point_t bottom_right;
     get_terminal_view_rect(config_state->tab_head, &top_left, &bottom_right);
     ce_calc_views(config_state->tab_current->view_head, top_left, bottom_right);

     // TODO: don't draw over borders!
     LineNumberType_t line_number_type = config_state->line_number_type;
     if(buffer->absolutely_no_line_numbers_under_any_circumstances){
          line_number_type = LNT_NONE;
     }

     Point_t terminal_cursor = {};

     Point_t input_top_left = {};
     Point_t input_bottom_right = {};
     Point_t auto_complete_top_left = {};
     Point_t auto_complete_bottom_right = {};
     if(config_state->input){
          int64_t input_view_height = config_state->view_input->buffer->line_count;
          if(input_view_height) input_view_height--;
          pthread_mutex_lock(&view_input_save_lock);
          input_top_left = (Point_t){config_state->tab_current->view_input_save->top_left.x,
                                     (config_state->tab_current->view_input_save->bottom_right.y - input_view_height) - 1};
          input_bottom_right = config_state->tab_current->view_input_save->bottom_right;
          pthread_mutex_unlock(&view_input_save_lock);
          if(input_top_left.y < 1) input_top_left.y = 1; // clamp to growing to 1, account for input message
          if(input_bottom_right.y == g_terminal_dimensions->y - 2){
               input_top_left.y++;
               input_bottom_right.y++; // account for bottom status bar
          }
          ce_calc_views(config_state->view_input, input_top_left, input_bottom_right);
          pthread_mutex_lock(&view_input_save_lock);
          config_state->tab_current->view_input_save->bottom_right.y = input_top_left.y - 1;
          pthread_mutex_unlock(&view_input_save_lock);

          // update cursor based on view size changing
          view_follow_cursor(buffer_view, line_number_type);
          terminal_cursor = get_cursor_on_terminal(cursor, buffer_view, line_number_type);

          if(auto_completing(&config_state->auto_complete)){
               int64_t auto_complete_view_height = config_state->view_auto_complete->buffer->line_count;
               if(auto_complete_view_height > config_state->max_auto_complete_height) auto_complete_view_height = config_state->max_auto_complete_height;
               auto_complete_top_left = (Point_t){input_top_left.x, (input_top_left.y - auto_complete_view_height) - 1};
               if(auto_complete_top_left.y < 0) auto_complete_top_left.y = 0; // account for separator line
               auto_complete_bottom_right = (Point_t){input_bottom_right.x, input_top_left.y - 1};
               ce_calc_views(config_state->view_auto_complete, auto_complete_top_left, auto_complete_bottom_right);
               view_follow_highlight(config_state->view_auto_complete);
          }
     }else if(auto_completing(&config_state->auto_complete)){
          view_follow_cursor(buffer_view, line_number_type);
          terminal_cursor = get_cursor_on_terminal(cursor, buffer_view, line_number_type);

          int64_t auto_complete_view_height = config_state->view_auto_complete->buffer->line_count;
          if(auto_complete_view_height > config_state->max_auto_complete_height) auto_complete_view_height = config_state->max_auto_complete_height;
          auto_complete_top_left = (Point_t){config_state->tab_current->view_current->top_left.x,
                                             (config_state->tab_current->view_current->bottom_right.y - auto_complete_view_height) - 1};
          if(auto_complete_top_left.y <= terminal_cursor.y) auto_complete_top_left.y = terminal_cursor.y + 2;
          auto_complete_bottom_right = (Point_t){config_state->tab_current->view_current->bottom_right.x,
                                                 config_state->tab_current->view_current->bottom_right.y};
          ce_calc_views(config_state->view_auto_complete, auto_complete_top_left, auto_complete_bottom_right);
          view_follow_highlight(config_state->view_auto_complete);
     }else{
          view_follow_cursor(buffer_view, line_number_type);
          terminal_cursor = get_cursor_on_terminal(cursor, buffer_view, line_number_type);
     }

     // setup highlight
     if(config_state->vim_state.mode == VM_VISUAL_RANGE){
          const Point_t* start = &config_state->vim_state.visual_start;
          const Point_t* end = &config_state->tab_current->view_current->cursor;

          ce_sort_points(&start, &end);

          buffer->highlight_start = *start;
          buffer->highlight_end = *end;
     }else if(config_state->vim_state.mode == VM_VISUAL_LINE){
          int64_t start_line = config_state->vim_state.visual_start.y;
          int64_t end_line = config_state->tab_current->view_current->cursor.y;

          if(start_line > end_line){
               int64_t tmp = start_line;
               start_line = end_line;
               end_line = tmp;
          }

          buffer->highlight_start = (Point_t){0, start_line};
          buffer->highlight_end = (Point_t){strlen(config_state->tab_current->view_current->buffer->lines[end_line]), end_line};
     }else if(!buffer->blink){
          buffer->highlight_start = (Point_t){0, 0};
          buffer->highlight_end = (Point_t){-1, 0};
     }

     Point_t* vim_mark = vim_mark_find(buffer_state->vim_buffer_state.mark_head, '0');
     if(vim_mark) buffer->mark = *vim_mark;

     const char* search = NULL;
     if(config_state->input && (config_state->input_key == '/' || config_state->input_key == '?') &&
        config_state->view_input->buffer->lines && config_state->view_input->buffer->lines[0][0]){
          search = config_state->view_input->buffer->lines[0];
     }else{
          VimYankNode_t* yank = vim_yank_find(config_state->vim_state.yank_head, '/');
          if(yank) search = yank->text;
     }

     HighlightLineType_t highlight_line_type = config_state->highlight_line_type;

     // turn off highlighting the current line when in visual mode
     if(config_state->vim_state.mode == VM_VISUAL_RANGE || config_state->vim_state.mode == VM_VISUAL_LINE){
          highlight_line_type = HLT_NONE;
     }

     regex_t* highlight_regex = NULL;
     if(!config_state->do_not_highlight_search && config_state->vim_state.search.valid_regex){
          highlight_regex = &config_state->vim_state.search.regex;
     }

     // NOTE: always draw from the head
     ce_draw_views(config_state->tab_current->view_head, highlight_regex, config_state->line_number_type, highlight_line_type);

     // draw input status
     if(auto_completing(&config_state->auto_complete)){
          move(auto_complete_top_left.y - 1, auto_complete_top_left.x);

          attron(COLOR_PAIR(S_BORDERS));
          for(int i = auto_complete_top_left.x; i < auto_complete_bottom_right.x; ++i) addch(ACS_HLINE);
          // if we are at the edge of the terminal, draw the inclusing horizontal line. We
          if(auto_complete_bottom_right.x == g_terminal_dimensions->x - 1) addch(ACS_HLINE);

          // clear auto complete buffer section
          for(int y = auto_complete_top_left.y; y <= auto_complete_bottom_right.y; ++y){
               move(y, auto_complete_top_left.x);
               for(int x = auto_complete_top_left.x; x <= auto_complete_bottom_right.x; ++x){
                    addch(' ');
               }
          }

          ce_draw_views(config_state->view_auto_complete, NULL, LNT_NONE, HLT_NONE);
     }

     draw_view_statuses(config_state->tab_current->view_head, config_state->tab_current->view_current,
                        config_state->tab_current->view_overrideable, config_state->vim_state.mode, config_state->last_key,
                        config_state->vim_state.recording_macro, config_state->terminal_current);

     if(config_state->input){
          if(config_state->view_input == config_state->tab_current->view_current){
               move(input_top_left.y - 1, input_top_left.x);

               attron(COLOR_PAIR(S_BORDERS));
               for(int i = input_top_left.x; i < input_bottom_right.x; ++i) addch(ACS_HLINE);
               // if we are at the edge of the terminal, draw the inclusing horizontal line. We
               if(input_bottom_right.x == g_terminal_dimensions->x - 1) addch(ACS_HLINE);

               attron(COLOR_PAIR(S_INPUT_STATUS));
               mvprintw(input_top_left.y - 1, input_top_left.x + 1, " %s ", config_state->input_message);
          }

          standend();
          // clear input buffer section
          for(int y = input_top_left.y; y <= input_bottom_right.y; ++y){
               move(y, input_top_left.x);
               for(int x = input_top_left.x; x <= input_bottom_right.x; ++x){
                    addch(' ');
               }
          }

          ce_draw_views(config_state->view_input, NULL, LNT_NONE, HLT_NONE);
          draw_view_statuses(config_state->view_input, config_state->tab_current->view_current,
                             NULL, config_state->vim_state.mode, config_state->last_key,
                             config_state->vim_state.recording_macro, config_state->terminal_current);
     }

     // draw auto complete
     if(auto_completing(&config_state->auto_complete) && config_state->auto_complete.current && config_state->auto_complete.type == ACT_EXACT){
          move(terminal_cursor.y, terminal_cursor.x);
          int64_t offset = cursor->x - config_state->auto_complete.start.x;
          if(offset >= 0){
               const char* option = config_state->auto_complete.current->option + offset;
               attron(COLOR_PAIR(S_AUTO_COMPLETE));
               while(*option){
                    addch(*option);
                    option++;
               }
          }
     }

     // draw tab line
     if(config_state->tab_head->next){
          // clear tab line with inverses
          move(0, 0);
          attron(COLOR_PAIR(S_BORDERS));
          for(int i = 0; i < g_terminal_dimensions->x; ++i) addch(ACS_HLINE);
          for(int i = 0; i < g_terminal_dimensions->x; ++i){
               Point_t p = {i, 0};
               ce_connect_border_lines(p);
          }

          TabView_t* tab_itr = config_state->tab_head;
          move(0, 1);

          // draw before current tab
          attron(COLOR_PAIR(S_TAB_NAME));
          addch(' ');
          while(tab_itr && tab_itr != config_state->tab_current){
               printw(tab_itr->view_current->buffer->name);
               addch(' ');
               tab_itr = tab_itr->next;
          }

          // draw current tab
          assert(tab_itr == config_state->tab_current);
          attron(COLOR_PAIR(S_CURRENT_TAB_NAME));
          printw(tab_itr->view_current->buffer->name);
          addch(' ');

          // draw rest of tabs
          attron(COLOR_PAIR(S_TAB_NAME));
          tab_itr = tab_itr->next;
          while(tab_itr){
               printw(tab_itr->view_current->buffer->name);
               addch(' ');
               tab_itr = tab_itr->next;
          }
     }

     // reset the cursor
     move(terminal_cursor.y, terminal_cursor.x);

     // update the screen with what we drew
     refresh();

     // clear mark if one existed
     if(vim_mark) buffer->mark = (Point_t){-1, -1};
     if(buffer->blink) buffer->blink = false;

     gettimeofday(&config_state->last_draw_time, NULL);
}
