#include "ce_server.h"
#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <limits.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
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

static Buffer_t* _id_to_buffer(BufferNode_t* head, const NetworkBufferId_t* buffer_id)
{
     while(head && head->buffer->network_id != buffer_id->id){
          head = head->next;
     }
     if(!head) return NULL;
     assert(head);
     if(buffer_id->cursor.x >= 0 && buffer_id->cursor.y >= 0)
          head->buffer->cursor = buffer_id->cursor;
     return head->buffer;
}

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

     // cleanup all client views
     {
          ClientView_t* view_itr = client->view_list_head;
          while(view_itr){
               ClientView_t* tmp = view_itr;
               view_itr = view_itr->next;
               free(tmp);
          }
     }

     close(client->socket);
     free(client);
}

#if 0
static void _handle_open_file(ServerState_t* server_state, Client_t* client)
{
     char filename[PATH_MAX];
     ssize_t n_bytes;
     READ_STR(filename);
     ce_message("Received OPEN_FILE %s", filename);
     Buffer_t* buffer = calloc(1, sizeof(*buffer));
     if(!ce_load_file(buffer, filename)){
          free(buffer);
          return;
     }
     buffer->network_id = server_state->current_id++;
     BufferNode_t* new_node = malloc(sizeof(*new_node));
     *new_node = (BufferNode_t){buffer, server_state->buffer_list_head->next};
     server_state->buffer_list_head->next = new_node;

     // notify all clients about the new buffer
     _notify_file_opened(client, buffer);
}

static void _handle_refresh_view(ServerState_t* server_state, Client_t* client)
{
     NetworkId_t id;
     if(!_read(client, &id, sizeof(id))) _close_client(server_state, client);
     // TODO: this should be the per-client view id
     NetworkBufferId_t buffer_id = {id, {-1,-1}};

     Point_t top_left;
     if(!_read(client, &top_left, sizeof(top_left))) _close_client(server_state, client);

     Point_t bottom_right;
     if(!_read(client, &bottom_right, sizeof(bottom_right))) _close_client(server_state, client);

     _refresh_view(client, _id_to_buffer(server_state->buffer_list_head, &buffer_id), top_left, bottom_right);
}

static void _handle_insert_string(ServerState_t* server_state, Client_t* client)
{
     ssize_t n_bytes;
     NetworkBufferId_t buffer_id;
     if(!_read(client, &buffer_id, sizeof(buffer_id))) _close_client(server_state, client);

     Point_t insert_loc;
     char to_insert[BUFSIZ]; // TODO: insert string by line (or as we fill up to_insert)
     READ_STR(to_insert);
     ce_insert_string(_id_to_buffer(server_state->buffer_list_head, &buffer_id), &insert_loc, to_insert);
}
#endif

static void _handle_client_command(ServerState_t* server_state, Client_t* client)
{
     NetworkCommand_t cmd = 0;
     if(!network_read(client->socket, &cmd, sizeof(cmd))){
          _close_client(server_state, client);
          return;
     }
     ce_message("Server received command %s", cmd_to_str(cmd));
#if 0
     switch(cmd){
          case NC_OPEN_FILE:
               ce_message("received open file");
               //_handle_open_file(server_state, client);
               break;
          case NC_REFRESH_VIEW:
               ce_message("received refresh view");
               //_handle_refresh_view(server_state, client);
               break;
          case NC_INSERT_STRING:
               ce_message("received insert string");
               //_handle_insert_string(server_state, client);
               break;
          default:
               ce_message("Received invalid command %d", cmd);
               break;
     }
#endif
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
