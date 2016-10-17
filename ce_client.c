#include "ce_client.h"
#include "ce_server.h"
#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <limits.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#if 0
static bool _read(Server_t* server, void* buf, size_t buf_len)
{
     // attempt to read buf_len bytes into buf. remove server on failure and return false
     ssize_t n_bytes_read = 0;
     do{
          ssize_t n_bytes = read(server->socket, buf + n_bytes_read, buf_len - n_bytes_read);
          if(n_bytes < 0){
               int err = errno; // useful for looking at errno in a coredump
               assert(n_bytes >= 0);
               ce_message("read() failed with error %s - Stopping client", strerror(err));
               pthread_exit(NULL);
          }
          else if(n_bytes == 0){
               // server closed connection
               return false;
          }
     } while(n_bytes_read < (ssize_t)buf_len);
     return true;
}
#endif

#if 0
static bool _handle_command(ClientState_t* client_state, Server_t* server)
{
     NetworkCommand_t cmd = 0;
     if(!_read(server, &cmd, sizeof(cmd))){
          _close_server(client_state, server);
          return;
     }
     switch(cmd){
          case NC_OPEN_FILE:
               ce_message("received open file");
               _handle_open_file(client_state, server);
               break;
          case NC_REFRESH_VIEW:
               ce_message("received refresh view");
               _handle_refresh_view(client_state, server);
               break;
          case NC_INSERT_STRING:
               ce_message("received insert string");
               _handle_insert_string(client_state, server);
               break;
          default:
               ce_message("Client received invalid command %d", cmd);
               break;
     }
     return true;
}
#endif

void* ce_client_listen(void* args)
{
     ClientState_t* client_state = args;
     BufferNode_t* head = client_state->buffer_list_head;
     assert(head);

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
                         //_handle_command(client_state, server_itr);
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

     // TODO: record client's network id
     ce_message("Client connected to %s:%d", server_ip, MAGIC_PORT);

     // add server to server list
     Server_t* new_server = malloc(sizeof(*new_server));
     *new_server = (Server_t){server_socket, 0, NULL, {client_state->server_list_head, NULL}};
     if(new_server->next) new_server->next->prev = new_server;
     client_state->server_list_head = new_server;
     return true;
}

bool ce_client_init(ClientState_t* client_state)
{
     if(!_server_connect(client_state, "127.0.0.1")) return false;

     // launch command handling thread
     pthread_create(&client_state->command_handling_thread, NULL, ce_client_listen, client_state);
     return true;
}

