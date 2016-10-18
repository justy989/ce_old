#include "ce_network.h"
#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define CMD_CASE(cmd) case cmd: return #cmd;
const char* cmd_to_str(NetworkCommand_t cmd)
{
     switch(cmd){
          CMD_CASE(NC_FREE_BUFFER)
          CMD_CASE(NC_ALLOC_LINES)
          CMD_CASE(NC_CLEAR_LINES)
          CMD_CASE(NC_CLEAR_LINES_READONLY)
          CMD_CASE(NC_LOAD_STRING)
          CMD_CASE(NC_LOAD_FILE)
          CMD_CASE(NC_INSERT_CHAR)
          CMD_CASE(NC_INSERT_CHAR_READONLY)
          CMD_CASE(NC_APPEND_CHAR)
          CMD_CASE(NC_APPEND_CHAR_READONLY)
          CMD_CASE(NC_REMOVE_CHAR)
          CMD_CASE(NC_SET_CHAR)
          CMD_CASE(NC_INSERT_STRING)
          CMD_CASE(NC_INSERT_STRING_READONLY)
          CMD_CASE(NC_REMOVE_STRING)
          CMD_CASE(NC_PREPEND_STRING)
          CMD_CASE(NC_APPEND_STRING)
          CMD_CASE(NC_APPEND_STRING_READONLY)
          CMD_CASE(NC_INSERT_LINE)
          CMD_CASE(NC_INSERT_LINE_READONLY)
          CMD_CASE(NC_REMOVE_LINE)
          CMD_CASE(NC_APPEND_LINE)
          CMD_CASE(NC_APPEND_LINE_READONLY)
          CMD_CASE(NC_JOIN_LINE)
          CMD_CASE(NC_INSERT_NEWLINE)
          CMD_CASE(NC_SAVE_BUFFER)
          default:
               return "invalid command";
     }
}

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



static void _free_buf(char** buf)
{
     free(*buf);
}

#define APPLY_READ(type, var) \
     type var; \
     if(!network_read(socket, &var, sizeof(var))){ return APPLY_FAILED; }

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

//typedef bool (*InsertCharFn_t) (NetworkBufferId_t buffer, Point_t location, char c, void* user_data);
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

//typedef bool (*RemoveCharFn_t) (NetworkBufferId_t buffer, Point_t location, void* user_data);
ApplyRC_t apply_remove_char(int socket, void* user_data, RemoveCharFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ(Point_t, location);
     return fn(buffer, location, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

//typedef bool (*SetCharFn_t) (NetworkBufferId_t buffer, Point_t location, char c, void* user_data);
ApplyRC_t apply_set_char(int socket, void* user_data, SetCharFn_t fn)
{
     APPLY_READ(NetworkBufferId_t, buffer);
     APPLY_READ(Point_t, location);
     APPLY_READ(char, c);
     return fn(buffer, location, c, user_data) ? APPLY_SUCCESS : APPLY_FAILED;
}

//typedef bool (*InsertStringFn_t) (NetworkBufferId_t buffer, Point_t location, const char* string, void* user_data);
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

//typedef bool (*RemoveStringFn_t) (NetworkBufferId_t buffer, Point_t location, int64_t length, void* user_data);
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







bool network_read(int socket, void* buf, size_t buf_len)
{
     // attempt to read buf_len bytes into buf. remove server on failure and return false
     ssize_t n_bytes_read = 0;
     do{
          ssize_t n_bytes = read(socket, buf + n_bytes_read, buf_len - n_bytes_read);
          if(n_bytes < 0){
               int err = errno; // useful for looking at errno in a coredump
               assert(n_bytes >= 0);
               ce_message("read() failed with error %s", strerror(err));
               return false;
          }
          else if(n_bytes == 0){
               // connection closed
               return false;
          }
          n_bytes_read += n_bytes;
     } while(n_bytes_read < (ssize_t)buf_len);
     return true;
}

// attempt to write buf_len bytes to the socket. return false on failure
bool network_write(int socket, const void* buf, size_t buf_len)
{
     ssize_t n_bytes_written = 0;
     do{
          ssize_t n_bytes = write(socket, buf + n_bytes_written, buf_len - n_bytes_written);
          if(n_bytes < 0){
               int err = errno; // useful for looking at errno in a coredump
               assert(n_bytes >= 0);
               ce_message("write() failed with error %s", strerror(err));
               return false;
          }
          else if(n_bytes == 0){
               // connection closed
               return false;
          }
          n_bytes_written += n_bytes;
     } while(n_bytes_written < (ssize_t)buf_len);
     return true;
}

#define NETWORK_WRITE(var) if(!network_write(socket, &var, sizeof(var))){ return false; }
#define NETWORK_WRITE_CMD(cmd) ({ \
     NetworkCommand_t _cmd = cmd; \
     NETWORK_WRITE(_cmd); \
})

#define NETWORK_WRITE_STR(var) if(!network_write(socket, var, strlen(var) + 1)){ return false; }

// write network functions with network arguments
//typedef void (*FreeBufferFn_t) (NetworkBufferId_t buffer, void* user_data);
bool network_free_buffer(int socket, NetworkBufferId_t buffer)
{
     NETWORK_WRITE_CMD(NC_FREE_BUFFER);
     NETWORK_WRITE(buffer);
     return true;
}

//typedef bool (*AllocLinesFn_t) (NetworkBufferId_t buffer, int64_t line_count, void* user_data);
bool network_alloc_lines(int socket, NetworkBufferId_t buffer, int64_t line_count)
{
     NETWORK_WRITE_CMD(NC_ALLOC_LINES);
     NETWORK_WRITE(buffer);
     NETWORK_WRITE(line_count);
     return true;
}

//typedef bool (*ClearLinesFn_t) (NetworkBufferId_t buffer, void* user_data);
bool network_clear_lines(int socket, NetworkBufferId_t buffer)
{
     NETWORK_WRITE_CMD(NC_CLEAR_LINES);
     NETWORK_WRITE(buffer);
     return true;
}

bool network_clear_lines_readonly(int socket, NetworkBufferId_t buffer)
{
     NETWORK_WRITE_CMD(NC_CLEAR_LINES_READONLY);
     NETWORK_WRITE(buffer);
     return true;
}

//typedef bool (*LoadStringFn_t) (NetworkBufferId_t buffer, const char* string, void* user_data);
bool network_load_string(int socket, NetworkBufferId_t buffer, const char* string)
{
     NETWORK_WRITE_CMD(NC_LOAD_STRING);
     NETWORK_WRITE(buffer);
     NETWORK_WRITE_STR(string);
     return true;
}

//typedef bool (*LoadFileFn_t) (NetworkBufferId_t buffer, const char* filename, void* user_data);
bool network_load_file(int socket, const char* filename)
{
     NETWORK_WRITE_CMD(NC_LOAD_FILE);
     NETWORK_WRITE_STR(filename);
     return true;
}

//typedef bool (*InsertCharFn_t) (NetworkBufferId_t buffer, Point_t location, char c, void* user_data);
bool network_insert_char(int socket, NetworkBufferId_t buffer, Point_t location, char c)
{
     NETWORK_WRITE_CMD(NC_INSERT_CHAR);
     NETWORK_WRITE(buffer);
     NETWORK_WRITE(location);
     NETWORK_WRITE(c);
     return true;
}

bool network_insert_char_readonly(int socket, NetworkBufferId_t buffer, Point_t location, char c)
{
     NETWORK_WRITE_CMD(NC_INSERT_CHAR_READONLY);
     NETWORK_WRITE(buffer);
     NETWORK_WRITE(location);
     NETWORK_WRITE(c);
     return true;
}

//typedef bool (*AppendCharFn_t) (NetworkBufferId_t buffer, char c, void* user_data);
bool network_append_char(int socket, NetworkBufferId_t buffer, char c)
{
     NETWORK_WRITE_CMD(NC_APPEND_CHAR);
     NETWORK_WRITE(buffer);
     NETWORK_WRITE(c);
     return true;
}

bool network_append_char_readonly(int socket, NetworkBufferId_t buffer, char c)
{
     NETWORK_WRITE_CMD(NC_APPEND_CHAR_READONLY);
     NETWORK_WRITE(buffer);
     NETWORK_WRITE(c);
     return true;
}

//typedef bool (*RemoveCharFn_t) (NetworkBufferId_t buffer, Point_t location, void* user_data);
bool network_remove_char(int socket, NetworkBufferId_t buffer, Point_t location)
{
     NETWORK_WRITE_CMD(NC_REMOVE_CHAR);
     NETWORK_WRITE(buffer);
     NETWORK_WRITE(location);
     return true;
}

//typedef bool (*SetCharFn_t) (NetworkBufferId_t buffer, Point_t location, char c, void* user_data);
bool network_set_char(int socket, NetworkBufferId_t buffer, Point_t location, char c)
{
     NETWORK_WRITE_CMD(NC_SET_CHAR);
     NETWORK_WRITE(buffer);
     NETWORK_WRITE(location);
     NETWORK_WRITE(c);
     return true;
}

//typedef bool (*InsertStringFn_t) (NetworkBufferId_t buffer, Point_t location, const char* string, void* user_data);
bool network_insert_string(int socket, NetworkBufferId_t buffer, Point_t location, const char* string)
{
     NETWORK_WRITE_CMD(NC_INSERT_STRING);
     NETWORK_WRITE(buffer);
     NETWORK_WRITE(location);
     NETWORK_WRITE_STR(string);
     return true;
}

bool network_insert_string_readonly(int socket, NetworkBufferId_t buffer, Point_t location, const char* string)
{
     NETWORK_WRITE_CMD(NC_INSERT_STRING_READONLY);
     NETWORK_WRITE(buffer);
     NETWORK_WRITE(location);
     NETWORK_WRITE_STR(string);
     return true;
}

//typedef bool (*RemoveStringFn_t) (NetworkBufferId_t buffer, Point_t location, int64_t length, void* user_data);
bool network_remove_string(int socket, NetworkBufferId_t buffer, Point_t location, int64_t length)
{
     NETWORK_WRITE_CMD(NC_REMOVE_STRING);
     NETWORK_WRITE(buffer);
     NETWORK_WRITE(location);
     NETWORK_WRITE(length);
     return true;
}

//typedef bool (*PrependStringFn_t) (NetworkBufferId_t buffer, int64_t line, const char* string, void* user_data);
bool network_prepend_string(int socket, NetworkBufferId_t buffer, int64_t line, const char* string)
{
     NETWORK_WRITE_CMD(NC_PREPEND_STRING);
     NETWORK_WRITE(buffer);
     NETWORK_WRITE(line);
     NETWORK_WRITE_STR(string);
     return true;
}

//typedef bool (*AppendStringFn_t) (NetworkBufferId_t buffer, int64_t line, const char* string, void* user_data);
bool network_append_string(int socket, NetworkBufferId_t buffer, int64_t line, const char* string)
{
     NETWORK_WRITE_CMD(NC_APPEND_STRING);
     NETWORK_WRITE(buffer);
     NETWORK_WRITE(line);
     NETWORK_WRITE_STR(string);
     return true;
}

bool network_append_string_readonly(int socket, NetworkBufferId_t buffer, int64_t line, const char* string)
{
     NETWORK_WRITE_CMD(NC_APPEND_STRING_READONLY);
     NETWORK_WRITE(buffer);
     NETWORK_WRITE(line);
     NETWORK_WRITE_STR(string);
     return true;
}

//typedef bool (*InsertLineFn_t) (NetworkBufferId_t buffer, int64_t line, const char* string, void* user_data);
bool network_insert_line(int socket, NetworkBufferId_t buffer, int64_t line, const char* string)
{
     NETWORK_WRITE_CMD(NC_INSERT_LINE);
     NETWORK_WRITE(buffer);
     NETWORK_WRITE(line);
     NETWORK_WRITE_STR(string);
     return true;
}

bool network_insert_line_readonly(int socket, NetworkBufferId_t buffer, int64_t line, const char* string)
{
     NETWORK_WRITE_CMD(NC_INSERT_LINE_READONLY);
     NETWORK_WRITE(buffer);
     NETWORK_WRITE(line);
     NETWORK_WRITE_STR(string);
     return true;
}

//typedef bool (*RemoveLineFn_t) (NetworkBufferId_t buffer, int64_t line, void* user_data);
bool network_remove_line(int socket, NetworkBufferId_t buffer, int64_t line)
{
     NETWORK_WRITE_CMD(NC_REMOVE_LINE);
     NETWORK_WRITE(buffer);
     NETWORK_WRITE(line);
     return true;
}

//typedef bool (*AppendLineFn_t) (NetworkBufferId_t buffer, int64_t line, const char* string, void* user_data);
bool network_append_line(int socket, NetworkBufferId_t buffer, int64_t line, const char* string)
{
     NETWORK_WRITE_CMD(NC_APPEND_LINE);
     NETWORK_WRITE(buffer);
     NETWORK_WRITE(line);
     NETWORK_WRITE_STR(string);
     return true;
}

bool network_append_line_readonly(int socket, NetworkBufferId_t buffer, int64_t line, const char* string)
{
     NETWORK_WRITE_CMD(NC_APPEND_LINE_READONLY);
     NETWORK_WRITE(buffer);
     NETWORK_WRITE(line);
     NETWORK_WRITE_STR(string);
     return true;
}

//typedef bool (*JoinLineFn_t) (NetworkBufferId_t buffer, int64_t line, void* user_data);
bool network_join_line(int socket, NetworkBufferId_t buffer, int64_t line)
{
     NETWORK_WRITE_CMD(NC_JOIN_LINE);
     NETWORK_WRITE(buffer);
     NETWORK_WRITE(line);
     return true;
}

//typedef bool (*InsertNewlineFn_t) (NetworkBufferId_t buffer, int64_t line, void* user_data);
bool network_insert_newline(int socket, NetworkBufferId_t buffer, int64_t line)
{
     NETWORK_WRITE_CMD(NC_INSERT_NEWLINE);
     NETWORK_WRITE(buffer);
     NETWORK_WRITE(line);
     return true;
}

//typedef bool (*SaveBufferFn_t) (NetworkBufferId_t buffer, const char* filename, void* user_data);
bool network_save_buffer(int socket, NetworkBufferId_t buffer, const char* filename)
{
     NETWORK_WRITE_CMD(NC_SAVE_BUFFER);
     NETWORK_WRITE(buffer);
     NETWORK_WRITE_STR(filename);
     return true;
}
