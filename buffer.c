#include "buffer.h"

#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>

static bool str_ends_in_substr(const char* string, size_t string_len, const char* substring)
{
     size_t substring_len = strlen(substring);
     return string_len > substring_len && strcmp(string + (string_len - substring_len), substring) == 0;
}

bool buffer_initialize(Buffer_t* buffer)
{
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
          if(str_ends_in_substr(buffer->name, name_len, ".c") ||
             str_ends_in_substr(buffer->name, name_len, ".h")){
               buffer->syntax_fn = syntax_highlight_c;
               buffer->syntax_user_data = malloc(sizeof(SyntaxC_t));
               buffer->type = BFT_C;
               if(!buffer->syntax_user_data){
                    ce_message("failed to allocate syntax user data for buffer");
                    free(buffer_state);
                    return false;
               }
          }else if(str_ends_in_substr(buffer->name, name_len, ".cpp") ||
                   str_ends_in_substr(buffer->name, name_len, ".cc") ||
                   str_ends_in_substr(buffer->name, name_len, ".hpp")){
               buffer->syntax_fn = syntax_highlight_cpp;
               buffer->syntax_user_data = malloc(sizeof(SyntaxCpp_t));
               buffer->type = BFT_CPP;
               if(!buffer->syntax_user_data){
                    ce_message("failed to allocate syntax user data for buffer");
                    free(buffer_state);
                    return false;
               }
          }else if(str_ends_in_substr(buffer->name, name_len, ".py")){
               buffer->syntax_fn = syntax_highlight_python;
               buffer->syntax_user_data = malloc(sizeof(SyntaxPython_t));
               buffer->type = BFT_PYTHON;
               if(!buffer->syntax_user_data){
                    ce_message("failed to allocate syntax user data for buffer");
                    free(buffer_state);
                    return false;
               }
          }else if(str_ends_in_substr(buffer->name, name_len, ".java")){
               buffer->syntax_fn = syntax_highlight_java;
               buffer->syntax_user_data = malloc(sizeof(SyntaxJava_t));
               buffer->type = BFT_JAVA;
               if(!buffer->syntax_user_data){
                    ce_message("failed to allocate syntax user data for buffer");
                    free(buffer_state);
                    return false;
               }
          }else if(str_ends_in_substr(buffer->name, name_len, ".sh")){
               buffer->syntax_fn = syntax_highlight_bash;
               buffer->syntax_user_data = malloc(sizeof(SyntaxBash_t));
               buffer->type = BFT_BASH;
               if(!buffer->syntax_user_data){
                    ce_message("failed to allocate syntax user data for buffer");
                    free(buffer_state);
                    return false;
               }
          }else if(str_ends_in_substr(buffer->name, name_len, ".cfg")){
               buffer->syntax_fn = syntax_highlight_config;
               buffer->syntax_user_data = malloc(sizeof(SyntaxConfig_t));
               buffer->type = BFT_CONFIG;
               if(!buffer->syntax_user_data){
                    ce_message("failed to allocate syntax user data for buffer");
                    free(buffer_state);
                    return false;
               }
          }else if(str_ends_in_substr(buffer->name, name_len, "COMMIT_EDITMSG") ||
                   str_ends_in_substr(buffer->name, name_len, ".patch") ||
                   str_ends_in_substr(buffer->name, name_len, ".diff")){
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

BufferNode_t* buffer_new_empty(BufferNode_t* head, const char* name)
{
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

     if(!buffer_initialize(buffer)){
          free(buffer->name);
          free(buffer);
          return NULL;
     }

     BufferNode_t* new_buffer_node = ce_append_buffer_to_list(head, buffer);
     if(!new_buffer_node){
          free(buffer->name);
          free(buffer->user_data);
          free(buffer);
          return NULL;
     }

     ce_alloc_lines(buffer, 1);
     return new_buffer_node;
}

BufferNode_t* buffer_create_from_file(BufferNode_t* head, const char* filename)
{
     struct stat new_file_stat;

     if(stat(filename, &new_file_stat) == 0){
          struct stat open_file_stat;
          BufferNode_t* itr = head;
          while(itr){
               // NOTE: should we cache inodes?
               if(stat(itr->buffer->name, &open_file_stat) == 0){
                    if(open_file_stat.st_ino == new_file_stat.st_ino){
                         return itr; // already open
                    }
               }

               itr = itr->next;
          }
     }

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

     if(!buffer_initialize(buffer)){
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

void buffer_state_free(BufferState_t* buffer_state)
{
     BufferCommitNode_t* itr = buffer_state->commit_tail;
     if(itr){
          while(itr->prev) itr = itr->prev;
          ce_commits_free(itr);
     }

     vim_marks_free(&buffer_state->vim_buffer_state.mark_head);
     free(buffer_state);
}

bool buffer_delete_at_index(BufferNode_t** head, TabView_t* tab_head, int64_t buffer_index, TerminalNode_t** terminal_head,
                            TerminalNode_t** terminal_current)
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
     buffer_state_free(delete_buffer->user_data);
     ce_free_buffer(delete_buffer);

     return true;
}
