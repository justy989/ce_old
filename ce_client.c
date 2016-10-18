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
#include <inttypes.h>

static bool _handle_command(ClientState_t* client_state, Server_t* server)
{
     (void)client_state;
     NetworkCommand_t cmd = 0;
     if(!network_read(server->socket, &cmd, sizeof(cmd))){
          // TODO: close_server
          //_close_server(client_state, server);
          return false;;
     }

     ce_message("Client received command %s", cmd_to_str(cmd));

#if 0
     switch(cmd){
          default:
               break;
     }
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
                         _handle_command(client_state, server_itr);
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

     ce_message("Client connected to %s:%d with id %"PRIu16, server_ip, MAGIC_PORT, new_server->id);

     return true;
}

bool ce_client_init(ClientState_t* client_state)
{
     if(!_server_connect(client_state, "127.0.0.1")) return false;

     // launch command handling thread
     pthread_create(&client_state->command_handling_thread, NULL, ce_client_listen, client_state);
     return true;
}

