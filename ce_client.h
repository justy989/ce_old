#ifndef CE_CLIENT_H
#define CE_CLIENT_H
#include "ce_network.h"
#include <semaphore.h>
// this file contains logic for parsing and applying ce network commands

// client side state
typedef struct Server{
     int socket;
     NetworkId_t id; // id used by the server to identify this client
     BufferNode_t* buffer_list_head; // list of buffers that pertain to this server
     struct{ // server list
          struct Server* next;
          struct Server* prev;
     };
} Server_t;

typedef struct{
     Server_t* server_list_head; // TODO: eventually support connecting to multiple servers?
     void* config_user_data;
     BufferNode_t* buffer_list_head;
     CursorNode_t* cursor_list_head;
     pthread_t command_thread;
     bool command_rc;
     sem_t* command_sem; // clients wait on this when they send a command and post
                         // when they receive a command that originated from this client
} ClientState_t;

// client side functions
bool ce_client_init(ClientState_t* client_state, const char* server_addr);

bool client_free_buffer            (ClientState_t* client_state, Server_t* server, NetworkId_t buffer);
bool client_alloc_lines            (ClientState_t* client_state, Server_t* server, NetworkId_t buffer, int64_t line_count);
bool client_clear_lines            (ClientState_t* client_state, Server_t* server, NetworkId_t buffer);
bool client_clear_lines_readonly   (ClientState_t* client_state, Server_t* server, NetworkId_t buffer);
bool client_load_string            (ClientState_t* client_state, Server_t* server, NetworkId_t buffer, const char* string);
bool client_load_file              (ClientState_t* client_state, Server_t* server, const char* filename);
bool client_insert_char            (ClientState_t* client_state, Server_t* server, NetworkId_t buffer, Point_t location, char c);
bool client_insert_char_readonly   (ClientState_t* client_state, Server_t* server, NetworkId_t buffer, Point_t location, char c);
bool client_append_char            (ClientState_t* client_state, Server_t* server, NetworkId_t buffer, char c);
bool client_append_char_readonly   (ClientState_t* client_state, Server_t* server, NetworkId_t buffer, char c);
bool client_remove_char            (ClientState_t* client_state, Server_t* server, NetworkId_t buffer, Point_t location);
bool client_set_char               (ClientState_t* client_state, Server_t* server, NetworkId_t buffer, Point_t location, char c);
bool client_insert_string          (ClientState_t* client_state, Server_t* server, NetworkId_t buffer, Point_t location, const char* string);
bool client_insert_string_readonly (ClientState_t* client_state, Server_t* server, NetworkId_t buffer, Point_t location, const char* string);
bool client_remove_string          (ClientState_t* client_state, Server_t* server, NetworkId_t buffer, Point_t location, int64_t length);
bool client_prepend_string         (ClientState_t* client_state, Server_t* server, NetworkId_t buffer, int64_t line, const char* string);
bool client_append_string          (ClientState_t* client_state, Server_t* server, NetworkId_t buffer, int64_t line, const char* string);
bool client_append_string_readonly (ClientState_t* client_state, Server_t* server, NetworkId_t buffer, int64_t line, const char* string);
bool client_insert_line            (ClientState_t* client_state, Server_t* server, NetworkId_t buffer, int64_t line, const char* string);
bool client_insert_line_readonly   (ClientState_t* client_state, Server_t* server, NetworkId_t buffer, int64_t line, const char* string);
bool client_remove_line            (ClientState_t* client_state, Server_t* server, NetworkId_t buffer, int64_t line);
bool client_append_line            (ClientState_t* client_state, Server_t* server, NetworkId_t buffer, const char* string);
bool client_append_line_readonly   (ClientState_t* client_state, Server_t* server, NetworkId_t buffer, const char* string);
bool client_join_line              (ClientState_t* client_state, Server_t* server, NetworkId_t buffer, int64_t line);
bool client_insert_newline         (ClientState_t* client_state, Server_t* server, NetworkId_t buffer, int64_t line);
bool client_save_buffer            (ClientState_t* client_state, Server_t* server, NetworkId_t buffer, const char* filename);
bool client_set_cursor             (ClientState_t* client_state, Server_t* server, NetworkId_t buffer, Point_t location);

#endif // CE_CLIENT_H
