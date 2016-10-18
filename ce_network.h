#ifndef CE_NETWORK_H
#define CE_NETWORK_H
#include "ce.h"
#include <pthread.h>

#define MAGIC_PORT 12345
#define MAX_CONNECTIONS 5

typedef enum{
     NC_FREE_BUFFER,
     NC_ALLOC_LINES,
     NC_CLEAR_LINES,
     NC_CLEAR_LINES_READONLY,
     NC_LOAD_STRING,
     NC_LOAD_FILE,
     NC_INSERT_CHAR,
     NC_INSERT_CHAR_READONLY,
     NC_APPEND_CHAR,
     NC_APPEND_CHAR_READONLY,
     NC_REMOVE_CHAR,
     NC_SET_CHAR,
     NC_INSERT_STRING,
     NC_INSERT_STRING_READONLY,
     NC_REMOVE_STRING,
     NC_PREPEND_STRING,
     NC_APPEND_STRING,
     NC_APPEND_STRING_READONLY,
     NC_INSERT_LINE,
     NC_INSERT_LINE_READONLY,
     NC_REMOVE_LINE,
     NC_APPEND_LINE,
     NC_APPEND_LINE_READONLY,
     NC_JOIN_LINE,
     NC_INSERT_NEWLINE,
     NC_SAVE_BUFFER,
}NetworkCommand_t;

const char* cmd_to_str(NetworkCommand_t cmd);

typedef uint16_t NetworkId_t;
typedef struct{
     NetworkId_t id; // buffer id's are unique across all clients and are generated on the server side
} NetworkBufferId_t;

// apply functions with arguments read from the specified socket
typedef enum {
     APPLY_SUCCESS,
     APPLY_FAILED,
     APPLY_SOCKET_DISCONNECTED,
} ApplyRC_t;

typedef void (*FreeBufferFn_t)         (NetworkBufferId_t buffer, void* user_data);
ApplyRC_t apply_free_buffer            (int socket, void* user_data, FreeBufferFn_t fn);

typedef bool (*AllocLinesFn_t)         (NetworkBufferId_t buffer, int64_t line_count, void* user_data);
ApplyRC_t apply_alloc_lines            (int socket, void* user_data, AllocLinesFn_t fn);

typedef bool (*ClearLinesFn_t)         (NetworkBufferId_t buffer, void* user_data);
ApplyRC_t apply_clear_lines            (int socket, void* user_data, ClearLinesFn_t fn);
ApplyRC_t apply_clear_lines_readonly   (int socket, void* user_data, ClearLinesFn_t fn);

typedef bool (*LoadStringFn_t)         (NetworkBufferId_t buffer, const char* string, void* user_data);
ApplyRC_t apply_load_string            (int socket, void* user_data, LoadStringFn_t fn);

typedef bool (*LoadFileFn_t)           (NetworkBufferId_t buffer, const char* filename, void* user_data);
ApplyRC_t apply_load_file              (int socket, void* user_data, LoadFileFn_t fn);

typedef bool (*InsertCharFn_t)         (NetworkBufferId_t buffer, Point_t location, char c, void* user_data);
ApplyRC_t apply_insert_char            (int socket, void* user_data, InsertCharFn_t fn);
ApplyRC_t apply_insert_char_readonly   (int socket, void* user_data, InsertCharFn_t fn);

typedef bool (*AppendCharFn_t)         (NetworkBufferId_t buffer, char c, void* user_data);
ApplyRC_t apply_append_char            (int socket, void* user_data, AppendCharFn_t fn);
ApplyRC_t apply_append_char_readonly   (int socket, void* user_data, AppendCharFn_t fn);

typedef bool (*RemoveCharFn_t)         (NetworkBufferId_t buffer, Point_t location, void* user_data);
ApplyRC_t apply_remove_char            (int socket, void* user_data, RemoveCharFn_t fn);

typedef bool (*SetCharFn_t)            (NetworkBufferId_t buffer, Point_t location, char c, void* user_data);
ApplyRC_t apply_set_char               (int socket, void* user_data, SetCharFn_t fn);

typedef bool (*InsertStringFn_t)       (NetworkBufferId_t buffer, Point_t location, const char* string, void* user_data);
ApplyRC_t apply_insert_string          (int socket, void* user_data, InsertStringFn_t fn);
ApplyRC_t apply_insert_string_readonly (int socket, void* user_data, InsertStringFn_t fn);

typedef bool (*RemoveStringFn_t)       (NetworkBufferId_t buffer, Point_t location, int64_t length, void* user_data);
ApplyRC_t apply_remove_string          (int socket, void* user_data, RemoveStringFn_t fn);

typedef bool (*PrependStringFn_t)      (NetworkBufferId_t buffer, int64_t line, const char* string, void* user_data);
ApplyRC_t apply_prepend_string         (int socket, void* user_data, PrependStringFn_t fn);

typedef bool (*AppendStringFn_t)       (NetworkBufferId_t buffer, int64_t line, const char* string, void* user_data);
ApplyRC_t apply_append_string          (int socket, void* user_data, AppendStringFn_t fn);
ApplyRC_t apply_append_string_readonly (int socket, void* user_data, AppendStringFn_t fn);

typedef bool (*InsertLineFn_t)         (NetworkBufferId_t buffer, int64_t line, const char* string, void* user_data);
ApplyRC_t apply_insert_line            (int socket, void* user_data, InsertLineFn_t fn);
ApplyRC_t apply_insert_line_readonly   (int socket, void* user_data, InsertLineFn_t fn);

typedef bool (*RemoveLineFn_t)         (NetworkBufferId_t buffer, int64_t line, void* user_data);
ApplyRC_t apply_remove_line            (int socket, void* user_data, RemoveLineFn_t fn);

typedef bool (*AppendLineFn_t)         (NetworkBufferId_t buffer, int64_t line, const char* string, void* user_data);
ApplyRC_t apply_append_line            (int socket, void* user_data, AppendLineFn_t fn);
ApplyRC_t apply_append_line_readonly   (int socket, void* user_data, AppendLineFn_t fn);

typedef bool (*JoinLineFn_t)           (NetworkBufferId_t buffer, int64_t line, void* user_data);
ApplyRC_t apply_join_line              (int socket, void* user_data, JoinLineFn_t fn);

typedef bool (*InsertNewlineFn_t)      (NetworkBufferId_t buffer, int64_t line, void* user_data);
ApplyRC_t apply_insert_newline         (int socket, void* user_data, InsertNewlineFn_t fn);

typedef bool (*SaveBufferFn_t)         (NetworkBufferId_t buffer, const char* filename, void* user_data);
ApplyRC_t apply_save_buffer            (int socket, void* user_data, SaveBufferFn_t fn);


bool network_free_buffer            (int socket, NetworkBufferId_t buffer);
bool network_alloc_lines            (int socket, NetworkBufferId_t buffer, int64_t line_count);
bool network_clear_lines            (int socket, NetworkBufferId_t buffer);
bool network_clear_lines_readonly   (int socket, NetworkBufferId_t buffer);
bool network_load_string            (int socket, NetworkBufferId_t buffer, const char* string);
bool network_load_file              (int socket, const char* filename);
bool network_insert_char            (int socket, NetworkBufferId_t buffer, Point_t location, char c);
bool network_insert_char_readonly   (int socket, NetworkBufferId_t buffer, Point_t location, char c);
bool network_append_char            (int socket, NetworkBufferId_t buffer, char c);
bool network_append_char_readonly   (int socket, NetworkBufferId_t buffer, char c);
bool network_remove_char            (int socket, NetworkBufferId_t buffer, Point_t location);
bool network_set_char               (int socket, NetworkBufferId_t buffer, Point_t location, char c);
bool network_insert_string          (int socket, NetworkBufferId_t buffer, Point_t location, const char* string);
bool network_insert_string_readonly (int socket, NetworkBufferId_t buffer, Point_t location, const char* string);
bool network_remove_string          (int socket, NetworkBufferId_t buffer, Point_t location, int64_t length);
bool network_prepend_string         (int socket, NetworkBufferId_t buffer, int64_t line, const char* string);
bool network_append_string          (int socket, NetworkBufferId_t buffer, int64_t line, const char* string);
bool network_append_string_readonly (int socket, NetworkBufferId_t buffer, int64_t line, const char* string);
bool network_insert_line            (int socket, NetworkBufferId_t buffer, int64_t line, const char* string);
bool network_insert_line_readonly   (int socket, NetworkBufferId_t buffer, int64_t line, const char* string);
bool network_remove_line            (int socket, NetworkBufferId_t buffer, int64_t line);
bool network_append_line            (int socket, NetworkBufferId_t buffer, int64_t line, const char* string);
bool network_append_line_readonly   (int socket, NetworkBufferId_t buffer, int64_t line, const char* string);
bool network_join_line              (int socket, NetworkBufferId_t buffer, int64_t line);
bool network_insert_newline         (int socket, NetworkBufferId_t buffer, int64_t line);
bool network_save_buffer            (int socket, NetworkBufferId_t buffer, const char* filename);

bool network_write(int socket, const void* buf, size_t buf_len);
bool network_read(int socket, void* buf, size_t buf_len);
#endif // CE_NETWORK_H
