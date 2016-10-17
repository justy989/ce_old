#include "ce_network.h"
#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#if 0
#define WRITE(var) ({ \
     ssize_t sent_bytes = write(client_state->server_socket, &(var), sizeof(var)); \
     assert(sent_bytes == sizeof(var)); \
})

bool ce_network_insert_string(ClientState_t* client_state, NetworkBufferId_t buffer_id, Point_t location, const char* string)
{
     assert(client_state->server_socket);
     NetworkCommand_t cmd = NC_INSERT_STRING;
     WRITE(cmd);
     WRITE(buffer_id);
     WRITE(location);
     ssize_t n_bytes_sent = write(client_state->server_socket, string, strlen(string) + 1);
     assert(n_bytes_sent == (ssize_t)strlen(string)+1);
     return true;
}

#define CLIENT_READ(var) \
({ \
     ssize_t n_bytes = read(client_state->server_socket, &var, sizeof(var)); \
     assert(n_bytes > 0); \
})
#define CLIENT_READ_STR(buf) ({ \
     size_t read_ix = 0; \
     do{ \
          ssize_t n_bytes = read(client_state->server_socket, &buf[read_ix], sizeof(buf[read_ix])); \
          assert(n_bytes > 0); \
          read_ix++; \
     } while(buf[read_ix-1] != '\0'); \
})

void ce_client_refresh_view(ClientState_t* client_state, Buffer_t* buffer)
{
     NetworkCommand_t cmd;
     CLIENT_READ(cmd);
     assert(cmd == NC_REFRESH_VIEW);
     char view_str[BUFSIZ];
     ce_clear_lines(buffer);
     {
     size_t read_ix = 0;
again:
          do{
               ssize_t n_bytes = read(client_state->server_socket, &view_str[read_ix], sizeof(view_str[read_ix]));
               assert(n_bytes > 0);
               if(view_str[read_ix] == '\n'){
                    view_str[read_ix] = 0;
                    ce_append_line(buffer, view_str);
                    read_ix = 0;
                    goto again;
               }
               else read_ix++;
          } while(view_str[read_ix-1] != '\0');
          ce_append_line(buffer, view_str);
     }
}

void ce_network_refresh_view(ClientState_t* client_state, BufferView_t* buffer_view)
{
     NetworkCommand_t cmd = NC_REFRESH_VIEW;
     WRITE(cmd);
     WRITE(buffer_view->buffer->network_id);
     WRITE(buffer_view->top_left);
     WRITE(buffer_view->bottom_right);

     // TODO: for now we will synchronously receive the view update.
     // eventually this will need to be out of band
     ce_client_refresh_view(client_state, buffer_view->buffer);
     // TODO: do I need to change the top_left and bottom_right stuff?
     buffer_view->top_row = 0;
}

bool ce_network_load_file(ClientState_t* client_state, Buffer_t* buffer, const char* filename)
{
     // send open file request
     NetworkCommand_t cmd = NC_OPEN_FILE;
     WRITE(cmd);
     ssize_t n_bytes_sent = write(client_state->server_socket, filename, strlen(filename) + 1);
     assert(n_bytes_sent == (ssize_t)strlen(filename)+1);

     // wait for open file response and assign the network id
     CLIENT_READ(cmd);
     assert(cmd == NC_OPEN_FILE);
     CLIENT_READ(buffer->network_id);
     char filename_str[BUFSIZ];
     CLIENT_READ_STR(filename_str);
     assert(!strcmp(filename_str, filename));
     buffer->name = strdup(filename);
     return true;
}
#endif





// attempt to read buf_len bytes into buf. return false on failure
static bool _read(int socket, void* buf, size_t buf_len)
{
     ssize_t n_bytes_read = 0;
     do{
          ssize_t n_bytes = read(socket, buf + n_bytes_read, buf_len - n_bytes_read);
          if(n_bytes < 0){
               int err = errno; // useful for looking at errno in a coredump
               assert(n_bytes >= 0);
               ce_message("read() failed with error %s", strerror(err));
               pthread_exit(NULL);
          }
          else if(n_bytes == 0){
               // server closed connection
               return false;
          }
     } while(n_bytes_read < (ssize_t)buf_len);
     return true;
}

static void _free_buf(char** buf)
{
     free(*buf);
}

#define APPLY_READ(type, var) \
     type var; \
     if(!_read(socket, &var, sizeof(var))){ return APPLY_FAILED; }

#define APPLY_READ_STR(var) \
     /* free the string when the variable goes out of scope */ \
     char* var __attribute__((__cleanup__(_free_buf))) = malloc(BUFSIZ); \
     assert(var); \
     { \
          size_t read_ix = 0; \
          do{ \
               if((read_ix % BUFSIZ) == 0){ \
                    /* extend the allocation */ \
                    size_t num_blocks = read_ix / BUFSIZ; \
                    num_blocks++; \
                    var = realloc(var, BUFSIZ*num_blocks); \
                    assert(var); \
               } \
               APPLY_READ(char, _c); \
               var[read_ix] = _c; \
               read_ix++; \
          } while(var[read_ix-1] != '\0'); \
     }

// apply functions with network arguments
//typedef void (*FreeBufferFn_t) (NetworkBufferId_t buffer, void* user_data);
ApplyRC_t apply_free_buffer(int socket, void* user_data, FreeBufferFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     fn(buffer, user_data);
     return APPLY_SUCCESS;
}

//typedef bool (*AllocLinesFn_t) (NetworkBufferId_t buffer, int64_t line_count, void* user_data);
ApplyRC_t apply_alloc_lines(int socket, void* user_data, AllocLinesFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ(int64_t, line_count);
     return fn(buffer, line_count, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

//typedef bool (*ClearLinesFn_t) (NetworkBufferId_t buffer, void* user_data);
ApplyRC_t apply_clear_lines(int socket, void* user_data, ClearLinesFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     return fn(buffer, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

ApplyRC_t apply_clear_lines_readonly(int socket, void* user_data, ClearLinesFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     return fn(buffer, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

//typedef bool (*LoadStringFn_t) (NetworkBufferId_t buffer, const char* string, void* user_data);
ApplyRC_t apply_load_string(int socket, void* user_data, LoadStringFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ_STR(string);
     return fn(buffer, string, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

//typedef bool (*LoadFileFn_t) (NetworkBufferId_t buffer, const char* filename, void* user_data);
ApplyRC_t apply_load_file(int socket, void* user_data, LoadFileFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ_STR(filename);
     return fn(buffer, filename, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

//typedef bool (*InsertCharFn_t) (NetworkBufferId_t buffer, Point location, char c, void* user_data);
ApplyRC_t apply_insert_char(int socket, void* user_data, InsertCharFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ(Point_t, location);
     APPLY_READ(char, c);
     return fn(buffer, location, c, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

ApplyRC_t apply_insert_char_readonly(int socket, void* user_data, InsertCharFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ(Point_t, location);
     APPLY_READ(char, c);
     return fn(buffer, location, c, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

//typedef bool (*AppendCharFn_t) (NetworkBufferId_t buffer, char c, void* user_data);
ApplyRC_t apply_append_char(int socket, void* user_data, AppendCharFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ(char, c);
     return fn(buffer, c, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

ApplyRC_t apply_append_char_readonly(int socket, void* user_data, AppendCharFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ(char, c);
     return fn(buffer, c, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

//typedef bool (*RemoveCharFn_t) (NetworkBufferId_t buffer, Point location, void* user_data);
ApplyRC_t apply_remove_char(int socket, void* user_data, RemoveCharFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ(Point_t, location);
     return fn(buffer, location, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

//typedef bool (*SetCharFn_t) (NetworkBufferId_t buffer, Point location, char c, void* user_data);
ApplyRC_t apply_set_char(int socket, void* user_data, SetCharFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ(Point_t, location);
     APPLY_READ(char, c);
     return fn(buffer, location, c, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

//typedef bool (*InsertStringFn_t) (NetworkBufferId_t buffer, Point location, const char* string, void* user_data);
ApplyRC_t apply_insert_string(int socket, void* user_data, InsertStringFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ(Point_t, location);
     APPLY_READ_STR(string);
     return fn(buffer, location, string, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

ApplyRC_t apply_insert_string_readonly(int socket, void* user_data, InsertStringFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ(Point_t, location);
     APPLY_READ_STR(string);
     return fn(buffer, location, string, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

//typedef bool (*RemoveStringFn_t) (NetworkBufferId_t buffer, Point location, int64_t length, void* user_data);
ApplyRC_t apply_remove_string(int socket, void* user_data, RemoveStringFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ(Point_t, location);
     APPLY_READ(int64_t, length);
     return fn(buffer, location, length, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

//typedef bool (*PrependStringFn_t) (NetworkBufferId_t buffer, int64_t line, const char* string, void* user_data);
ApplyRC_t apply_prepend_string(int socket, void* user_data, PrependStringFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ(int64_t, line);
     APPLY_READ_STR(string);
     return fn(buffer, line, string, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

//typedef bool (*AppendStringFn_t) (NetworkBufferId_t buffer, int64_t line, const char* string, void* user_data);
ApplyRC_t apply_append_string(int socket, void* user_data, AppendStringFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ(int64_t, line);
     APPLY_READ_STR(string);
     return fn(buffer, line, string, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

ApplyRC_t apply_append_string_readonly(int socket, void* user_data, AppendStringFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ(int64_t, line);
     APPLY_READ_STR(string);
     return fn(buffer, line, string, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

//typedef bool (*InsertLineFn_t) (NetworkBufferId_t buffer, int64_t line, const char* string, void* user_data);
ApplyRC_t apply_insert_line(int socket, void* user_data, InsertLineFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ(int64_t, line);
     APPLY_READ_STR(string);
     return fn(buffer, line, string, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

ApplyRC_t apply_insert_line_readonly(int socket, void* user_data, InsertLineFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ(int64_t, line);
     APPLY_READ_STR(string);
     return fn(buffer, line, string, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

//typedef bool (*RemoveLineFn_t) (NetworkBufferId_t buffer, int64_t line, void* user_data);
ApplyRC_t apply_remove_line(int socket, void* user_data, RemoveLineFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ(int64_t, line);
     return fn(buffer, line, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

//typedef bool (*AppendLineFn_t) (NetworkBufferId_t buffer, int64_t line, const char* string, void* user_data);
ApplyRC_t apply_append_line(int socket, void* user_data, AppendLineFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ(int64_t, line);
     APPLY_READ_STR(string);
     return fn(buffer, line, string, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

ApplyRC_t apply_append_line_readonly(int socket, void* user_data, AppendLineFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ(int64_t, line);
     APPLY_READ_STR(string);
     return fn(buffer, line, string, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

//typedef bool (*JoinLineFn_t) (NetworkBufferId_t buffer, int64_t line, void* user_data);
ApplyRC_t apply_join_line(int socket, void* user_data, JoinLineFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ(int64_t, line);
     return fn(buffer, line, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

//typedef bool (*InsertNewlineFn_t) (NetworkBufferId_t buffer, int64_t line, void* user_data);
ApplyRC_t apply_insert_newline(int socket, void* user_data, InsertNewlineFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ(int64_t, line);
     return fn(buffer, line, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

//typedef bool (*SaveBufferFn_t) (NetworkBufferId_t buffer, const char* filename, void* user_data);
ApplyRC_t apply_save_buffer(int socket, void* user_data, SaveBufferFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ_STR(filename);
     return fn(buffer, filename, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}
