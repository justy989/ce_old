#include "ce_server.h"
#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <limits.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "ce.h"

void view_drawer(const BufferNode_t* head, void* user_data);
/* design thoughts:
Server side:
When a client connects, the server creates a piece of state to track that client. This state includes:
- the socket to send/receive data for that client on
- the views that the client has visible (these are updated as the user switches the displayed buffer, scrolls, changes tabs, etc...)
  Having the views stored on the server side lets us track and notify all clients when a buffer change occurs in a visible view.
I think that in the current design the server will need to move the client's view as we move the cursor around the buffer.
This is actually kind of annoying at seems like it will make things kind of complicated. I wonder if we should take some other approach here.

Client side:
- When a client opens a new view, it sends an NC_OPEN_VIEW command to the server and tells it the unique id for that view with
  which it will refer to the newly opened view in future commands.
  NOTE: the unique id is per-client. we can look in a per-client list of views on the server to lookup the view on future commands
- Views will be updated asynchronously as a client receives NC_REFRESH_VIEW commands from the server
  (this could occur due to a change requested by this client, or a change made to the view by another client)

UPDATE: I don't think we should implement a view based client-server model
Upsides to a library-based client server model that sends the whole buffer and library functions to call to modify the buffer:
- Very few code changes to the view code
- You have the whole buffer so you can implement features in the config that depend on information that is outside of the current view
 (for example if you wanted to implement search, or jump to next function, or something)
- Simple library-based client server model (Client sends a command to the server, server relays the command to all clients and they perform the action)
- It feels like this model doesn't limit the configs
Downsides:
- Send more data
- Have to send all changes to everyone (even out of view changes)
- Seems scary to have to keep the buffers in sync like this, but I'd like to try it first to see if it's actually an issue

Random thought:
- NC_OPEN_VIEW instead of tracking view info on the server just needs to create a new cursor to track with that view
- The client now needs to make sure that it only moves the cursor with API functions (shouldn't be a big deal to do)
*/

#if 0
// TODO: make an _read_str function which does appropriate error checking
#define READ_STR(buf) ({ \
     size_t read_ix = 0; \
     do{ \
          n_bytes = read(client->socket, &buf[read_ix], sizeof(buf[read_ix])); \
          assert(n_bytes > 0); \
          read_ix++; \
     } while(buf[read_ix-1] != '\0'); \
})


void _notify_file_opened(Client_t* client, const Buffer_t* new_buffer)
{
     // TODO: error handling
     NetworkCommand_t cmd = NC_OPEN_FILE;
     ssize_t sent_bytes = write(client->socket, &(cmd), sizeof(cmd));
     sent_bytes = write(client->socket, &(new_buffer->network_id), sizeof(new_buffer->network_id));
     sent_bytes = write(client->socket, new_buffer->filename, strlen(new_buffer->filename) + 1);
}
static void _refresh_view(Client_t* client, const Buffer_t* buffer, Point_t top_left, Point_t bottom_right)
{
     // TODO: error handling
     NetworkCommand_t cmd = NC_REFRESH_VIEW;
     ssize_t sent_bytes = write(client->socket, &(cmd), sizeof(cmd));
     ce_clamp_cursor(buffer, &top_left);
     ce_clamp_cursor(buffer, &bottom_right);
     char* view_str = ce_dupe_string(buffer, &top_left, &bottom_right);
     assert(view_str);
     sent_bytes = write(client->socket, view_str, strlen(view_str) + 1);
     free(view_str);
}
#endif

static void _close_client(ServerState_t* server_state, Client_t* client){
     struct sockaddr_in address;
     int addrlen;
     getpeername(client->socket, (struct sockaddr*)&address, (socklen_t*)&addrlen);
     ce_message("Client ip: %s, port: %d, disconnected", inet_ntoa(address.sin_addr), ntohs(address.sin_port));

     // remove the client from the list
     if(client->next) client->next->prev = client->prev;
     if(client->prev) client->prev->next = client->next;
     else{
          assert(server_state->client_list_head == client);
          server_state->client_list_head = client->next;
     }

     close(client->socket);
     free(client);
}

static void _handle_load_file(ServerState_t* server_state, Client_t* client)
{
     char filename[PATH_MAX];
     if(!network_read_string(client->socket, filename, sizeof(filename))){
          assert(0);
          _close_client(server_state, client);
          return;
     }

     // use an existing buffer if we already have one
     BufferNode_t* itr = server_state->buffer_list_head;
     while(itr){
          if(!strcmp(itr->buffer->name, filename)){
               break; // already open
          }
          itr = itr->next;
     }

     Buffer_t* buffer;
     if(itr){
          buffer = itr->buffer;
     }
     else{
          // open the buffer on the server
          buffer = calloc(1, sizeof(*buffer));
          if(!ce_load_file(buffer, filename)){
               ce_message("server failed to load %s", filename);
               return;
          }

          buffer->network_id = server_state->current_id++;
          BufferNode_t* new_node = malloc(sizeof(*new_node));
          *new_node = (BufferNode_t){buffer, server_state->buffer_list_head->next};
          server_state->buffer_list_head->next = new_node;
     }

     // NOTE: we only respond to the current client with the load file command.
     // we do not send it to everyone... at least for the time being
     NetworkCommand_t cmd = NC_LOAD_FILE;
     if(!network_write(client->socket, &(cmd), sizeof(cmd))){
          _close_client(server_state, client);
          return;
     }

     if(!network_write(client->socket, &(buffer->network_id), sizeof(buffer->network_id))){
          _close_client(server_state, client);
          return;
     }

     if(!network_write(client->socket, buffer->filename, strlen(buffer->filename) + 1)){
          _close_client(server_state, client);
          return;
     }

     char* new_buffer_str = ce_dupe_buffer(buffer);
     if(!network_write(client->socket, new_buffer_str, strlen(new_buffer_str) + 1)){
          free(new_buffer_str);
          _close_client(server_state, client);
          return;
     }
     free(new_buffer_str);
}

#define PARSE_ARGS \
     ServerState_t* server_state = user_data; \
     Buffer_t* buf = id_to_buffer(server_state->buffer_list_head, buffer); \
     assert(buf); /* a client should not tell us about a buffer we don't know about */ \
     if(!buf) return false;

static bool _handle_insert_char(NetworkId_t buffer, Point_t location, char c, void* user_data)
{
     PARSE_ARGS
     if(!ce_insert_char(buf, location, c)) return false;

     // relay command to all clients
     Client_t* client = server_state->client_list_head;
     while(client){
          if(!network_insert_char(client->socket, buffer, location, c)){
               _close_client(server_state, client);
          }
          client = client->next;
     }
     return true;
}

static bool _handle_insert_char_readonly(NetworkId_t buffer, Point_t location, char c, void* user_data)
{
     PARSE_ARGS
     if(!ce_insert_char(buf, location, c)) return false;

     // relay command to all clients
     Client_t* client = server_state->client_list_head;
     while(client){
          if(!network_insert_char_readonly(client->socket, buffer, location, c)){
               _close_client(server_state, client);
          }
          client = client->next;
     }
     return true;
}

static bool _handle_append_char(NetworkId_t buffer, char c, void* user_data)
{
     PARSE_ARGS
     if(!ce_append_char(buf, c)) return false;

     // relay command to all clients
     Client_t* client = server_state->client_list_head;
     while(client){
          if(!network_append_char(client->socket, buffer, c)){
               _close_client(server_state, client);
          }
          client = client->next;
     }
     return true;
}

static bool _handle_append_char_readonly(NetworkId_t buffer, char c, void* user_data)
{
     PARSE_ARGS
     if(!ce_append_char_readonly(buf, c)) return false;

     // relay command to all clients
     Client_t* client = server_state->client_list_head;
     while(client){
          if(!network_append_char_readonly(client->socket, buffer, c)){
               _close_client(server_state, client);
          }
          client = client->next;
     }
     return true;
}

static bool _handle_remove_char(NetworkId_t buffer, Point_t location, void* user_data)
{
     PARSE_ARGS
     if(!ce_remove_char(buf, location)) return false;

     // relay command to all clients
     Client_t* client = server_state->client_list_head;
     while(client){
          if(!network_remove_char(client->socket, buffer, location)){
               _close_client(server_state, client);
          }
          client = client->next;
     }
     return true;
}

static bool _handle_set_char(NetworkId_t buffer, Point_t location, char c, void* user_data)
{
     PARSE_ARGS
     if(!ce_set_char(buf, location, c)) return false;

     // relay command to all clients
     Client_t* client = server_state->client_list_head;
     while(client){
          if(!network_set_char(client->socket, buffer, location, c)){
               _close_client(server_state, client);
          }
          client = client->next;
     }
     return true;
}

static bool _handle_insert_string(NetworkId_t buffer, Point_t location, const char* string, void* user_data)
{
     PARSE_ARGS
     if(!ce_insert_string(buf, location, string)) return false;

     // relay command to all clients
     Client_t* client = server_state->client_list_head;
     while(client){
          if(!network_insert_string(client->socket, buffer, location, string)){
               _close_client(server_state, client);
          }
          client = client->next;
     }
     return true;
}

static bool _handle_insert_string_readonly(NetworkId_t buffer, Point_t location, const char* string, void* user_data)
{
     PARSE_ARGS
     if(!ce_insert_string_readonly(buf, location, string)) return false;

     // relay command to all clients
     Client_t* client = server_state->client_list_head;
     while(client){
          if(!network_insert_string_readonly(client->socket, buffer, location, string)){
               _close_client(server_state, client);
          }
          client = client->next;
     }
     return true;
}

static bool _handle_remove_string(NetworkId_t buffer, Point_t location, int64_t length, void* user_data)
{
     PARSE_ARGS
     if(!ce_remove_string(buf, location, length)) return false;

     // relay command to all clients
     Client_t* client = server_state->client_list_head;
     while(client){
          if(!network_remove_string(client->socket, buffer, location, length)){
               _close_client(server_state, client);
          }
          client = client->next;
     }
     return true;
}

static bool _handle_prepend_string(NetworkId_t buffer, int64_t line, const char* string, void* user_data)
{
     PARSE_ARGS
     if(!ce_prepend_string(buf, line, string)) return false;

     // relay command to all clients
     Client_t* client = server_state->client_list_head;
     while(client){
          if(!network_prepend_string(client->socket, buffer, line, string)){
               _close_client(server_state, client);
          }
          client = client->next;
     }
     return true;
}

static bool _handle_append_string(NetworkId_t buffer, int64_t line, const char* string, void* user_data)
{
     PARSE_ARGS
     if(!ce_append_string(buf, line, string)) return false;

     // relay command to all clients
     Client_t* client = server_state->client_list_head;
     while(client){
          if(!network_append_string(client->socket, buffer, line, string)){
               _close_client(server_state, client);
          }
          client = client->next;
     }
     return true;
}

static bool _handle_append_string_readonly(NetworkId_t buffer, int64_t line, const char* string, void* user_data)
{
     PARSE_ARGS
     if(!ce_append_string_readonly(buf, line, string)) return false;

     // relay command to all clients
     Client_t* client = server_state->client_list_head;
     while(client){
          if(!network_append_string_readonly(client->socket, buffer, line, string)){
               _close_client(server_state, client);
          }
          client = client->next;
     }
     return true;
}

static bool _handle_insert_line(NetworkId_t buffer, int64_t line, const char* string, void* user_data)
{
     PARSE_ARGS
     if(!ce_insert_line(buf, line, string)) return false;

     // relay command to all clients
     Client_t* client = server_state->client_list_head;
     while(client){
          if(!network_insert_line(client->socket, buffer, line, string)){
               _close_client(server_state, client);
          }
          client = client->next;
     }
     return true;
}

static bool _handle_insert_line_readonly(NetworkId_t buffer, int64_t line, const char* string, void* user_data)
{
     PARSE_ARGS
     if(!ce_insert_line_readonly(buf, line, string)) return false;

     // relay command to all clients
     Client_t* client = server_state->client_list_head;
     while(client){
          if(!network_insert_line_readonly(client->socket, buffer, line, string)){
               _close_client(server_state, client);
          }
          client = client->next;
     }
     return true;
}

static bool _handle_remove_line(NetworkId_t buffer, int64_t line, void* user_data)
{
     PARSE_ARGS
     if(!ce_remove_line(buf, line)) return false;

     // relay command to all clients
     Client_t* client = server_state->client_list_head;
     while(client){
          if(!network_remove_line(client->socket, buffer, line)){
               _close_client(server_state, client);
          }
          client = client->next;
     }
     return true;
}

static bool _handle_append_line(NetworkId_t buffer, const char* string, void* user_data)
{
     PARSE_ARGS
     if(!ce_append_line(buf, string)) return false;

     // relay command to all clients
     Client_t* client = server_state->client_list_head;
     while(client){
          if(!network_append_line(client->socket, buffer, string)){
               _close_client(server_state, client);
          }
          client = client->next;
     }
     return true;
}

static bool _handle_append_line_readonly(NetworkId_t buffer, const char* string, void* user_data)
{
     PARSE_ARGS
     if(!ce_append_line_readonly(buf, string)) return false;

     // relay command to all clients
     Client_t* client = server_state->client_list_head;
     while(client){
          if(!network_append_line_readonly(client->socket, buffer, string)){
               _close_client(server_state, client);
          }
          client = client->next;
     }
     return true;
}

static bool _handle_join_line(NetworkId_t buffer, int64_t line, void* user_data)
{
     PARSE_ARGS
     if(!ce_join_line(buf, line)) return false;

     // relay command to all clients
     Client_t* client = server_state->client_list_head;
     while(client){
          if(!network_join_line(client->socket, buffer, line)){
               _close_client(server_state, client);
          }
          client = client->next;
     }
     return true;
}

static bool _handle_insert_newline(NetworkId_t buffer, int64_t line, void* user_data)
{
     PARSE_ARGS
     if(!ce_insert_newline(buf, line)) return false;

     // relay command to all clients
     Client_t* client = server_state->client_list_head;
     while(client){
          if(!network_insert_newline(client->socket, buffer, line)){
               _close_client(server_state, client);
          }
          client = client->next;
     }
     return true;
}

static void _handle_client_command(ServerState_t* server_state, Client_t* client)
{
     NetworkCommand_t cmd = 0;
     if(!network_read(client->socket, &cmd, sizeof(cmd))){
          _close_client(server_state, client);
          return;
     }
     ce_message("Server received command %s", cmd_to_str(cmd));
     switch(cmd){
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
          _handle_load_file(server_state, client);
          break;
     case NC_INSERT_CHAR:
          if(apply_insert_char(client->socket, server_state, _handle_insert_char) == APPLY_SOCKET_DISCONNECTED){
               _close_client(server_state, client);
               return;
          }
          break;
     case NC_INSERT_CHAR_READONLY:
          if(apply_insert_char_readonly(client->socket, server_state, _handle_insert_char_readonly) == APPLY_SOCKET_DISCONNECTED){
               _close_client(server_state, client);
               return;
          }
          break;
     case NC_APPEND_CHAR:
          if(apply_append_char(client->socket, server_state, _handle_append_char) == APPLY_SOCKET_DISCONNECTED){
               _close_client(server_state, client);
               return;
          }
          break;
     case NC_APPEND_CHAR_READONLY:
          if(apply_append_char_readonly(client->socket, server_state, _handle_append_char_readonly) == APPLY_SOCKET_DISCONNECTED){
               _close_client(server_state, client);
               return;
          }
          break;
     case NC_REMOVE_CHAR:
          if(apply_remove_char(client->socket, server_state, _handle_remove_char) == APPLY_SOCKET_DISCONNECTED){
               _close_client(server_state, client);
               return;
          }
          break;
     case NC_SET_CHAR:
          if(apply_set_char(client->socket, server_state, _handle_set_char) == APPLY_SOCKET_DISCONNECTED){
               _close_client(server_state, client);
               return;
          }
          break;
     case NC_INSERT_STRING:
          if(apply_insert_string(client->socket, server_state, _handle_insert_string) == APPLY_SOCKET_DISCONNECTED){
               _close_client(server_state, client);
               return;
          }
          break;
     case NC_INSERT_STRING_READONLY:
          if(apply_insert_string_readonly(client->socket, server_state, _handle_insert_string_readonly) == APPLY_SOCKET_DISCONNECTED){
               _close_client(server_state, client);
               return;
          }
          break;
     case NC_REMOVE_STRING:
          if(apply_remove_string(client->socket, server_state, _handle_remove_string) == APPLY_SOCKET_DISCONNECTED){
               _close_client(server_state, client);
               return;
          }
          break;
     case NC_PREPEND_STRING:
          if(apply_prepend_string(client->socket, server_state, _handle_prepend_string) == APPLY_SOCKET_DISCONNECTED){
               _close_client(server_state, client);
               return;
          }
          break;
     case NC_APPEND_STRING:
          if(apply_append_string(client->socket, server_state, _handle_append_string) == APPLY_SOCKET_DISCONNECTED){
               _close_client(server_state, client);
               return;
          }
          break;
     case NC_APPEND_STRING_READONLY:
          if(apply_append_string_readonly(client->socket, server_state, _handle_append_string_readonly) == APPLY_SOCKET_DISCONNECTED){
               _close_client(server_state, client);
               return;
          }
          break;
     case NC_INSERT_LINE:
          if(apply_insert_line(client->socket, server_state, _handle_insert_line) == APPLY_SOCKET_DISCONNECTED){
               _close_client(server_state, client);
               return;
          }
          break;
     case NC_INSERT_LINE_READONLY:
          if(apply_insert_line_readonly(client->socket, server_state, _handle_insert_line_readonly) == APPLY_SOCKET_DISCONNECTED){
               _close_client(server_state, client);
               return;
          }
          break;
     case NC_REMOVE_LINE:
          if(apply_remove_line(client->socket, server_state, _handle_remove_line) == APPLY_SOCKET_DISCONNECTED){
               _close_client(server_state, client);
               return;
          }
          break;
     case NC_APPEND_LINE:
          if(apply_append_line(client->socket, server_state, _handle_append_line) == APPLY_SOCKET_DISCONNECTED){
               _close_client(server_state, client);
               return;
          }
          break;
     case NC_APPEND_LINE_READONLY:
          if(apply_append_line_readonly(client->socket, server_state, _handle_append_line_readonly) == APPLY_SOCKET_DISCONNECTED){
               _close_client(server_state, client);
               return;
          }
          break;
     case NC_JOIN_LINE:
          if(apply_join_line(client->socket, server_state, _handle_join_line) == APPLY_SOCKET_DISCONNECTED){
               _close_client(server_state, client);
               return;
          }
          break;
     case NC_INSERT_NEWLINE:
          if(apply_insert_newline(client->socket, server_state, _handle_insert_newline) == APPLY_SOCKET_DISCONNECTED){
               _close_client(server_state, client);
               return;
          }
          break;
     case NC_SAVE_BUFFER:
          break;
     }
     view_drawer(server_state->buffer_list_head, server_state->config_user_data);
}

void* ce_server_listen(void* args)
{
     ServerState_t* server_state = args;
     BufferNode_t* head = server_state->buffer_list_head;
     assert(head);

     fd_set read_fds;
     int max_fd; // select needs this
     while(true){
          // build the fd_set to select on
          {
               FD_ZERO(&read_fds);
               // listen for new connections
               FD_SET(server_state->server_socket, &read_fds);
               max_fd = server_state->server_socket;
               Client_t* client_itr = server_state->client_list_head;
               while(client_itr){
                    // listen for commands on all client sockets
                    FD_SET(client_itr->socket, &read_fds);
                    max_fd = CE_MAX(max_fd, client_itr->socket); // update max_fd if necessary
                    client_itr = client_itr->next;
               }
          }

          // block until something happens on one of our sockets
          int event = select( max_fd + 1 , &read_fds , NULL , NULL , NULL);
          if(event < 0){
               int err = errno;
               if(err == EINTR) continue; // interrupted by signal. try again
               ce_message("Select error: %s - Stopping server", strerror(err));
               pthread_exit(NULL); // TODO: pthread_cleanup_push()
          }

          // check for client commands
          {
               Client_t* client_itr = server_state->client_list_head;
               while(client_itr){
                    if(FD_ISSET(client_itr->socket, &read_fds)){
                         _handle_client_command(server_state, client_itr);
                    }
                    client_itr = client_itr->next;
               }
          }

          if(FD_ISSET(server_state->server_socket, &read_fds)){
               // new client connection
               struct sockaddr_in client_addr = {};
               socklen_t client_len;
               int client_socket = accept(server_state->server_socket, (struct sockaddr *)&client_addr, &client_len);
               if(client_socket < 0){
                    // TODO: is this the behavior we want
                    ce_message("accept() failed with error %s - Stopping server", strerror(errno));
                    pthread_exit(NULL);
               }
               ce_message("Accepted connection from ip: %s, port: %d", inet_ntoa(client_addr.sin_addr) , ntohs(client_addr.sin_port));

               // start tracking the new client. insert at list head
               Client_t* new_client = malloc(sizeof(*new_client));
               *new_client = (Client_t){client_socket, server_state->current_id++, NULL, {server_state->client_list_head, NULL}};
               if(new_client->next) new_client->next->prev = new_client;
               server_state->client_list_head = new_client;

               // notify the new client of its network id
               network_write(new_client->socket, &new_client->id, sizeof(new_client->id));
          }
     }
     return NULL;
}

bool ce_server_init(ServerState_t* server_state)
{
     server_state->current_id = 1; // start uid's at 1
     // open tcp server socket
     if((server_state->server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0){
          ce_message("socket() failed with error: %s", strerror(errno));
          return false;
     }

     int optval = 1;
     // let us reuse the socket address immediately when our process dies
     if(setsockopt(server_state->server_socket, SOL_SOCKET, SO_REUSEADDR,  (const void *)&optval , sizeof(int)) < 0){
          ce_message("setsockopt() failed with error: %s", strerror(errno));
          return false;
     }

     // associate the socket we created with the port we want to listen on
     struct sockaddr_in server_addr = {};
     server_addr.sin_family = AF_INET;
     server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
     server_addr.sin_port = htons((unsigned short)MAGIC_PORT);

     if(bind(server_state->server_socket, (struct sockaddr *)&server_addr,  sizeof(server_addr)) < 0){
          ce_message("bind() failed with error: %s", strerror(errno));
          return false;
     }

     if(listen(server_state->server_socket, MAX_CONNECTIONS) < 0){
          ce_message("listen() failed with error: %s", strerror(errno));
          return false;
     }
     // launch server
     pthread_create(&server_state->thread, NULL, ce_server_listen, server_state);
     return true;
}
