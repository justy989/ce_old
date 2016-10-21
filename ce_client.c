#include "ce_client.h"
#include "ce_server.h"
#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

void view_drawer(const BufferNode_t* head, void* user_data);

typedef struct{
     ClientState_t* client_state;
     Server_t* server;
}ClientServer_t;

static bool _handle_load_file(NetworkId_t buffer, const char* filename, const char* file_str, void* user_data)
{
     ClientServer_t* client_server = user_data;
     ClientState_t* client_state = client_server->client_state;
     BufferNode_t* itr = client_state->buffer_list_head;
     while(itr){
          if(!strcmp(itr->buffer->name, filename)){
               break; // already open
          }
          itr = itr->next;
     }

     Buffer_t* to_load;
     if(!itr){
          // TODO: create a new buffer from file_str
#if 0
          to_load = calloc(1, sizeof(*to_load));
          if(!ce_load_string(to_load, file_str)){
          }
#endif
          ce_message("buffer was not already open");
          assert(0);
          return false;
     }
     else{
          to_load = itr->buffer;
     }
     to_load->network_id = buffer;
     ce_clear_lines(to_load);
     if(!ce_load_string(to_load, file_str)){
          ce_message("failed to load new network file %s", filename);
     }
     to_load->modified = false;
     assert(CLIENT_ID(buffer) == client_server->server->id);
     sem_post(client_state->command_sem);
     return true;
}

#define PARSE_ARGS \
     ClientServer_t* client_server = user_data; \
     ClientState_t* client_state = client_server->client_state; \
     Server_t* server = client_server->server; \
     Buffer_t* buf = id_to_buffer(client_state->buffer_list_head, buffer); \
     assert(buf); /* a client should not tell us about a buffer we don't know about */ \
     if(!buf) return false;

static bool _handle_set_cursor(NetworkId_t buffer, Point_t location, void* user_data)
{
     PARSE_ARGS
     // add to the buffer's list of cursors if it isn't already there
     CursorNode_t* head = client_state->cursor_list_head;
     while(head){
         if(head->network_id == CLIENT_ID(buffer)) break;
         head = head->next;
     }

     if(!head){
         // add the cursor to the list
        head = malloc(sizeof(*head));
        *head = (CursorNode_t){buffer, location, head, client_state->cursor_list_head};
        if(head->next) head->next->prev = head;
        client_state->cursor_list_head = head;
     }

     assert(CLIENT_ID(buffer) != server->id);
     view_drawer(client_state->buffer_list_head, client_state->config_user_data);
     // NOTE: we don't block on cursor movements in client_set_cursor, so we don't need to sem_post here
     return true;
}

static bool _handle_insert_char(NetworkId_t buffer, Point_t location, char c, void* user_data)
{
     PARSE_ARGS
     if(!ce_insert_char(buf, location, c)) return false;
     if(CLIENT_ID(buffer) == server->id) sem_post(client_state->command_sem);
     else view_drawer(client_state->buffer_list_head, client_state->config_user_data);
     return true;
}

static bool _handle_insert_char_readonly(NetworkId_t buffer, Point_t location, char c, void* user_data)
{
     PARSE_ARGS
     if(!ce_insert_char(buf, location, c)) return false;
     if(CLIENT_ID(buffer) == server->id) sem_post(client_state->command_sem);
     else view_drawer(client_state->buffer_list_head, client_state->config_user_data);
     return true;
}

static bool _handle_append_char(NetworkId_t buffer, char c, void* user_data)
{
     PARSE_ARGS
     if(!ce_append_char(buf, c)) return false;
     if(CLIENT_ID(buffer) == server->id) sem_post(client_state->command_sem);
     else view_drawer(client_state->buffer_list_head, client_state->config_user_data);
     return true;
}

static bool _handle_append_char_readonly(NetworkId_t buffer, char c, void* user_data)
{
     PARSE_ARGS
     if(!ce_append_char_readonly(buf, c)) return false;
     if(CLIENT_ID(buffer) == server->id) sem_post(client_state->command_sem);
     else view_drawer(client_state->buffer_list_head, client_state->config_user_data);
     return true;
}

static bool _handle_remove_char(NetworkId_t buffer, Point_t location, void* user_data)
{
     PARSE_ARGS
     if(!ce_remove_char(buf, location)) return false;
     if(CLIENT_ID(buffer) == server->id) sem_post(client_state->command_sem);
     else view_drawer(client_state->buffer_list_head, client_state->config_user_data);
     return true;
}

static bool _handle_set_char(NetworkId_t buffer, Point_t location, char c, void* user_data)
{
     PARSE_ARGS
     if(!ce_set_char(buf, location, c)) return false;
     if(CLIENT_ID(buffer) == server->id) sem_post(client_state->command_sem);
     else view_drawer(client_state->buffer_list_head, client_state->config_user_data);
     return true;
}

static bool _handle_insert_string(NetworkId_t buffer, Point_t location, const char* string, void* user_data)
{
     PARSE_ARGS
     if(!ce_insert_string(buf, location, string)) return false;
     if(CLIENT_ID(buffer) == server->id) sem_post(client_state->command_sem);
     else view_drawer(client_state->buffer_list_head, client_state->config_user_data);
     return true;
}

static bool _handle_insert_string_readonly(NetworkId_t buffer, Point_t location, const char* string, void* user_data)
{
     PARSE_ARGS
     if(!ce_insert_string_readonly(buf, location, string)) return false;
     if(CLIENT_ID(buffer) == server->id) sem_post(client_state->command_sem);
     else view_drawer(client_state->buffer_list_head, client_state->config_user_data);
     return true;
}

static bool _handle_remove_string(NetworkId_t buffer, Point_t location, int64_t length, void* user_data)
{
     PARSE_ARGS
     if(!ce_remove_string(buf, location, length)) return false;
     if(CLIENT_ID(buffer) == server->id) sem_post(client_state->command_sem);
     else view_drawer(client_state->buffer_list_head, client_state->config_user_data);
     return true;
}

static bool _handle_prepend_string(NetworkId_t buffer, int64_t line, const char* string, void* user_data)
{
     PARSE_ARGS
     if(!ce_prepend_string(buf, line, string)) return false;
     if(CLIENT_ID(buffer) == server->id) sem_post(client_state->command_sem);
     else view_drawer(client_state->buffer_list_head, client_state->config_user_data);
     return true;
}

static bool _handle_append_string(NetworkId_t buffer, int64_t line, const char* string, void* user_data)
{
     PARSE_ARGS
     if(!ce_append_string(buf, line, string)) return false;
     if(CLIENT_ID(buffer) == server->id) sem_post(client_state->command_sem);
     else view_drawer(client_state->buffer_list_head, client_state->config_user_data);
     return true;
}

static bool _handle_append_string_readonly(NetworkId_t buffer, int64_t line, const char* string, void* user_data)
{
     PARSE_ARGS
     if(!ce_append_string_readonly(buf, line, string)) return false;
     if(CLIENT_ID(buffer) == server->id) sem_post(client_state->command_sem);
     else view_drawer(client_state->buffer_list_head, client_state->config_user_data);
     return true;
}

static bool _handle_insert_line(NetworkId_t buffer, int64_t line, const char* string, void* user_data)
{
     PARSE_ARGS
     if(!ce_insert_line(buf, line, string)) return false;
     if(CLIENT_ID(buffer) == server->id) sem_post(client_state->command_sem);
     else view_drawer(client_state->buffer_list_head, client_state->config_user_data);
     return true;
}

static bool _handle_insert_line_readonly(NetworkId_t buffer, int64_t line, const char* string, void* user_data)
{
     PARSE_ARGS
     if(!ce_insert_line_readonly(buf, line, string)) return false;
     if(CLIENT_ID(buffer) == server->id) sem_post(client_state->command_sem);
     else view_drawer(client_state->buffer_list_head, client_state->config_user_data);
     return true;
}

static bool _handle_remove_line(NetworkId_t buffer, int64_t line, void* user_data)
{
     PARSE_ARGS
     if(!ce_remove_line(buf, line)) return false;
     if(CLIENT_ID(buffer) == server->id) sem_post(client_state->command_sem);
     else view_drawer(client_state->buffer_list_head, client_state->config_user_data);
     return true;
}

static bool _handle_append_line(NetworkId_t buffer, const char* string, void* user_data)
{
     PARSE_ARGS
     if(!ce_append_line(buf, string)) return false;
     if(CLIENT_ID(buffer) == server->id) sem_post(client_state->command_sem);
     else view_drawer(client_state->buffer_list_head, client_state->config_user_data);
     return true;
}

static bool _handle_append_line_readonly(NetworkId_t buffer, const char* string, void* user_data)
{
     PARSE_ARGS
     if(!ce_append_line_readonly(buf, string)) return false;
     if(CLIENT_ID(buffer) == server->id) sem_post(client_state->command_sem);
     else view_drawer(client_state->buffer_list_head, client_state->config_user_data);
     return true;
}

static bool _handle_join_line(NetworkId_t buffer, int64_t line, void* user_data)
{
     PARSE_ARGS
     if(!ce_join_line(buf, line)) return false;
     if(CLIENT_ID(buffer) == server->id) sem_post(client_state->command_sem);
     else view_drawer(client_state->buffer_list_head, client_state->config_user_data);
     return true;
}

static bool _handle_insert_newline(NetworkId_t buffer, int64_t line, void* user_data)
{
     PARSE_ARGS
     if(!ce_insert_newline(buf, line)) return false;
     if(CLIENT_ID(buffer) == server->id) sem_post(client_state->command_sem);
     else view_drawer(client_state->buffer_list_head, client_state->config_user_data);
     return true;
}

static bool _handle_command(ClientState_t* client_state, Server_t* server)
{
     NetworkCommand_t cmd = 0;
     if(!network_read(server->socket, &cmd, sizeof(cmd))){
          // TODO: close_server. NOTE: _close_server should also post to the command semaphore
          //_close_server(client_state, server);
          return false;
     }

#ifdef DEBUG_NETWORK
     ce_message("Client received command %s", cmd_to_str(cmd));
#endif
     ClientServer_t client_server = {client_state, server};
     client_state->command_rc = true;
     switch(cmd){
     case NC_FAILED:
          client_state->command_rc = false;
          sem_post(client_state->command_sem);
          break;
     case NC_FREE_BUFFER:
          break;
     case NC_ALLOC_LINES:
          break;
     case NC_CLEAR_LINES:
          break;
     case NC_CLEAR_LINES_READONLY:
          break;
     case NC_LOAD_STRING:
          break;
     case NC_LOAD_FILE:
          apply_load_file(server->socket, &client_server, _handle_load_file);
          break;
     case NC_SET_CURSOR:
          if(apply_set_cursor(server->socket, &client_server, _handle_set_cursor) == APPLY_SOCKET_DISCONNECTED){
               //_disconnect_client(&client_server, client);
               return false;
          }
          break;
     case NC_INSERT_CHAR:
          if(apply_insert_char(server->socket, &client_server, _handle_insert_char) == APPLY_SOCKET_DISCONNECTED){
               //_disconnect_client(&client_server, client);
               return false;
          }
          break;
     case NC_INSERT_CHAR_READONLY:
          if(apply_insert_char_readonly(server->socket, &client_server, _handle_insert_char_readonly) == APPLY_SOCKET_DISCONNECTED){
               //_disconnect_client(&client_server, client);
               return false;
          }
          break;
     case NC_APPEND_CHAR:
          if(apply_append_char(server->socket, &client_server, _handle_append_char) == APPLY_SOCKET_DISCONNECTED){
               //_disconnect_client(&client_server, client);
               return false;
          }
          break;
     case NC_APPEND_CHAR_READONLY:
          if(apply_append_char_readonly(server->socket, &client_server, _handle_append_char_readonly) == APPLY_SOCKET_DISCONNECTED){
               //_disconnect_client(&client_server, client);
               return false;
          }
          break;
     case NC_REMOVE_CHAR:
          if(apply_remove_char(server->socket, &client_server, _handle_remove_char) == APPLY_SOCKET_DISCONNECTED){
               //_disconnect_client(&client_server, client);
               return false;
          }
          break;
     case NC_SET_CHAR:
          if(apply_set_char(server->socket, &client_server, _handle_set_char) == APPLY_SOCKET_DISCONNECTED){
               //_disconnect_client(&client_server, client);
               return false;
          }
          break;
     case NC_INSERT_STRING:
          if(apply_insert_string(server->socket, &client_server, _handle_insert_string) == APPLY_SOCKET_DISCONNECTED){
               //_disconnect_client(&client_server, client);
               return false;
          }
          break;
     case NC_INSERT_STRING_READONLY:
          if(apply_insert_string_readonly(server->socket, &client_server, _handle_insert_string_readonly) == APPLY_SOCKET_DISCONNECTED){
               //_disconnect_client(&client_server, client);
               return false;
          }
          break;
     case NC_REMOVE_STRING:
          if(apply_remove_string(server->socket, &client_server, _handle_remove_string) == APPLY_SOCKET_DISCONNECTED){
               //_disconnect_client(&client_server, client);
               return false;
          }
          break;
     case NC_PREPEND_STRING:
          if(apply_prepend_string(server->socket, &client_server, _handle_prepend_string) == APPLY_SOCKET_DISCONNECTED){
               //_disconnect_client(&client_server, client);
               return false;
          }
          break;
     case NC_APPEND_STRING:
          if(apply_append_string(server->socket, &client_server, _handle_append_string) == APPLY_SOCKET_DISCONNECTED){
               //_disconnect_client(&client_server, client);
               return false;
          }
          break;
     case NC_APPEND_STRING_READONLY:
          if(apply_append_string_readonly(server->socket, &client_server, _handle_append_string_readonly) == APPLY_SOCKET_DISCONNECTED){
               //_disconnect_client(&client_server, client);
               return false;
          }
          break;
     case NC_INSERT_LINE:
          if(apply_insert_line(server->socket, &client_server, _handle_insert_line) == APPLY_SOCKET_DISCONNECTED){
               //_disconnect_client(&client_server, client);
               return false;
          }
          break;
     case NC_INSERT_LINE_READONLY:
          if(apply_insert_line_readonly(server->socket, &client_server, _handle_insert_line_readonly) == APPLY_SOCKET_DISCONNECTED){
               //_disconnect_client(&client_server, client);
               return false;
          }
          break;
     case NC_REMOVE_LINE:
          if(apply_remove_line(server->socket, &client_server, _handle_remove_line) == APPLY_SOCKET_DISCONNECTED){
               //_disconnect_client(&client_server, client);
               return false;
          }
          break;
     case NC_APPEND_LINE:
          if(apply_append_line(server->socket, &client_server, _handle_append_line) == APPLY_SOCKET_DISCONNECTED){
               //_disconnect_client(&client_server, client);
               return false;
          }
          break;
     case NC_APPEND_LINE_READONLY:
          if(apply_append_line_readonly(server->socket, &client_server, _handle_append_line_readonly) == APPLY_SOCKET_DISCONNECTED){
               //_disconnect_client(&client_server, client);
               return false;
          }
          break;
     case NC_JOIN_LINE:
          if(apply_join_line(server->socket, &client_server, _handle_join_line) == APPLY_SOCKET_DISCONNECTED){
               //_disconnect_client(&client_server, client);
               return false;
          }
          break;
     case NC_INSERT_NEWLINE:
          if(apply_insert_newline(server->socket, &client_server, _handle_insert_newline) == APPLY_SOCKET_DISCONNECTED){
               //_disconnect_client(&client_server, client);
               return false;
          }
          break;
     case NC_SAVE_BUFFER:
          break;
     default:
          assert(0);
     }
#if 0
     // TODO: we want to redraw the view whenever a command originates from another client
     view_drawer(client_state->buffer_list_head, client_state->config_user_data);
#endif
     return true;
}

void* ce_client_listen(void* args)
{
     ClientState_t* client_state = args;

     fd_set server_fds;
     int max_fd; // select needs this
     while(true){
          // build the fd_set to select on
          {
               FD_ZERO(&server_fds);
               max_fd = 0;
               Server_t* server_itr = client_state->server_list_head;
               while(server_itr){
                    // listen for commands on all client sockets
                    FD_SET(server_itr->socket, &server_fds);
                    max_fd = CE_MAX(max_fd, server_itr->socket); // update max_fd if necessary
                    server_itr = server_itr->next;
               }
          }

          // block until something happens on one of our sockets
          int event = select( max_fd + 1 , &server_fds , NULL , NULL , NULL);
          if(event < 0){
               int err = errno;
               if(err == EINTR) continue; // interrupted by signal. try again
               ce_message("Select error: %s - Stopping client", strerror(err));
               pthread_exit(NULL); // TODO: pthread_cleanup_push()
          }

          // check for commands
          {
               Server_t* server_itr = client_state->server_list_head;
               while(server_itr){
                    if(FD_ISSET(server_itr->socket, &server_fds)){
                         if(!_handle_command(client_state, server_itr)){
                              // TODO: hanle error
                         }
                    }
                    server_itr = server_itr->next;
               }
          }
     }
     return NULL;
}

static bool _server_connect(ClientState_t* client_state, const char* server_ip)
{
     int server_socket = 0;
     if((server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0){
          ce_message("socket() failed with error: %s", strerror(errno));
          return false;
     }

     struct sockaddr_in server_addr = {};
     server_addr.sin_family = AF_INET;
     server_addr.sin_addr.s_addr = inet_addr(server_ip);
     server_addr.sin_port = htons((unsigned short)MAGIC_PORT);

     // establish a connection with the server
     if(connect(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
          ce_message("connect() failed with error: %s", strerror(errno));
          return false;
     }

     // add server to server list
     Server_t* new_server = malloc(sizeof(*new_server));
     *new_server = (Server_t){server_socket, 0, NULL, {client_state->server_list_head, NULL}};
     if(new_server->next) new_server->next->prev = new_server;
     client_state->server_list_head = new_server;

     // read the network id from the server
     if(!network_read(new_server->socket, &new_server->id, sizeof(new_server->id))){
          free(new_server);
          return false;
     }

     ce_message("Client connected to %s:%d with id %"PRIx64, server_ip, MAGIC_PORT, new_server->id);

     return true;
}

bool ce_client_init(ClientState_t* client_state, const char* server_addr)
{
     char sem_name[] = "COMMAND_SEM";
     sem_unlink(sem_name);
     client_state->command_sem = sem_open(sem_name, O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 0);
     sem_unlink(sem_name);
     assert(client_state->command_sem != SEM_FAILED);

     if(!_server_connect(client_state, server_addr)) return false;

     // launch command handling thread
     pthread_create(&client_state->command_thread, NULL, ce_client_listen, client_state);
     return true;
}

// client command
bool client_free_buffer(ClientState_t* client_state, Server_t* server, NetworkId_t buffer)
{
     if(!network_free_buffer(server->socket, buffer)) return false;
     // TODO: handle sem_wait errors (including signal interrupts)
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_alloc_lines(ClientState_t* client_state, Server_t* server, NetworkId_t buffer, int64_t line_count)
{
     if(!network_alloc_lines(server->socket, buffer, line_count)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_clear_lines(ClientState_t* client_state, Server_t* server, NetworkId_t buffer)
{
     if(!network_clear_lines(server->socket, buffer)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_clear_lines_readonly(ClientState_t* client_state, Server_t* server, NetworkId_t buffer)
{
     if(!network_clear_lines_readonly(server->socket, buffer)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_load_string(ClientState_t* client_state, Server_t* server, NetworkId_t buffer, const char* string)
{
     if(!network_load_string(server->socket, buffer, string)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_load_file(ClientState_t* client_state, Server_t* server, const char* filename)
{
     if(!network_load_file(server->socket, filename)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_insert_char(ClientState_t* client_state, Server_t* server, NetworkId_t buffer, Point_t location, char c)
{
     if(!network_insert_char(server->socket, buffer, location, c)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_insert_char_readonly(ClientState_t* client_state, Server_t* server, NetworkId_t buffer, Point_t location, char c)
{
     if(!network_insert_char_readonly(server->socket, buffer, location, c)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_append_char(ClientState_t* client_state, Server_t* server, NetworkId_t buffer, char c)
{
     if(!network_append_char(server->socket, buffer, c)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_append_char_readonly(ClientState_t* client_state, Server_t* server, NetworkId_t buffer, char c)
{
     if(!network_append_char_readonly(server->socket, buffer, c)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_remove_char(ClientState_t* client_state, Server_t* server, NetworkId_t buffer, Point_t location)
{
     if(!network_remove_char(server->socket, buffer, location)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_set_char(ClientState_t* client_state, Server_t* server, NetworkId_t buffer, Point_t location, char c)
{
     if(!network_set_char(server->socket, buffer, location, c)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_insert_string(ClientState_t* client_state, Server_t* server, NetworkId_t buffer, Point_t location, const char* string)
{
     if(!network_insert_string(server->socket, buffer, location, string)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_insert_string_readonly(ClientState_t* client_state, Server_t* server, NetworkId_t buffer, Point_t location, const char* string)
{
     if(!network_insert_string_readonly(server->socket, buffer, location, string)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_remove_string(ClientState_t* client_state, Server_t* server, NetworkId_t buffer, Point_t location, int64_t length)
{
     if(!network_remove_string(server->socket, buffer, location, length)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_prepend_string(ClientState_t* client_state, Server_t* server, NetworkId_t buffer, int64_t line, const char* string)
{
     if(!network_prepend_string(server->socket, buffer, line, string)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_append_string(ClientState_t* client_state, Server_t* server, NetworkId_t buffer, int64_t line, const char* string)
{
     if(!network_append_string(server->socket, buffer, line, string)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_append_string_readonly(ClientState_t* client_state, Server_t* server, NetworkId_t buffer, int64_t line, const char* string)
{
     if(!network_append_string_readonly(server->socket, buffer, line, string)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_insert_line(ClientState_t* client_state, Server_t* server, NetworkId_t buffer, int64_t line, const char* string)
{
     if(!network_insert_line(server->socket, buffer, line, string)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_insert_line_readonly(ClientState_t* client_state, Server_t* server, NetworkId_t buffer, int64_t line, const char* string)
{
     if(!network_insert_line_readonly(server->socket, buffer, line, string)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_remove_line(ClientState_t* client_state, Server_t* server, NetworkId_t buffer, int64_t line)
{
     if(!network_remove_line(server->socket, buffer, line)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_append_line(ClientState_t* client_state, Server_t* server, NetworkId_t buffer, const char* string)
{
     if(!network_append_line(server->socket, buffer, string)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_append_line_readonly(ClientState_t* client_state, Server_t* server, NetworkId_t buffer, const char* string)
{
     if(!network_append_line_readonly(server->socket, buffer, string)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_join_line(ClientState_t* client_state, Server_t* server, NetworkId_t buffer, int64_t line)
{
     if(!network_join_line(server->socket, buffer, line)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_insert_newline(ClientState_t* client_state, Server_t* server, NetworkId_t buffer, int64_t line)
{
     if(!network_insert_newline(server->socket, buffer, line)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_save_buffer(ClientState_t* client_state, Server_t* server, NetworkId_t buffer, const char* filename)
{
     if(!network_save_buffer(server->socket, buffer, filename)) return false;
     sem_wait(client_state->command_sem);
     return client_state->command_rc;
}

bool client_set_cursor(ClientState_t* client_state __attribute__((unused)), Server_t* server, NetworkId_t buffer, Point_t location)
{
     if(!network_set_cursor(server->socket, buffer, location)) return false;
     return true;
}


