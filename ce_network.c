#include "ce_network.h"
#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define READ(type, var) \
     type var; \
({ \
     n_bytes = read(server_state->client_socket, &var, sizeof(var)); \
     assert(n_bytes > 0); \
})

#define READ_STR(buf) ({ \
     size_t read_ix = 0; \
     do{ \
          n_bytes = read(server_state->client_socket, &buf[read_ix], sizeof(buf[read_ix])); \
          assert(n_bytes > 0); \
          read_ix++; \
     } while(buf[read_ix-1] != '\0'); \
})

Buffer_t* id_to_buffer(BufferNode_t* head, const NetworkBufferId_t* buffer_id)
{
     while(head && head->buffer->network_id != buffer_id->id){
          head = head->next;
     }
     if(!head) return NULL;
     assert(head);
     head->buffer->cursor = buffer_id->cursor;
     return head->buffer;
}

void* ce_server_listen(void* args)
{
     ServerState_t* server_state = args;
     BufferNode_t* head = server_state->buffer_list_head;

     // TODO: accept more than one connection and do it from a different thread?
     struct sockaddr_in client_addr = {};
     socklen_t client_len;
     server_state->client_socket = accept(server_state->server_socket, (struct sockaddr *)&client_addr, &client_len);
     assert(server_state->client_socket > 0);

     while(true){
          NetworkCommand_t cmd;
          ssize_t n_bytes = read(server_state->client_socket, &cmd, sizeof(cmd));
          assert(n_bytes > 0);
          // TODO: error checking for all read calls
          char in_str[BUFSIZ];
          switch(cmd){
               case NC_INSERT_STRING:
               {
                    READ(NetworkBufferId_t, buffer_id);
                    READ(Point_t, insert_loc);
                    READ_STR(in_str);
                    ce_insert_string(id_to_buffer(head, &buffer_id), &insert_loc, in_str);
               } break;
               default:
                    ce_message("received invalid command");
                    break;
          }
     }
     return NULL;
}

// TODO: make port configurable
#define MAGIC_PORT 12345
#define NUM_CONNECTIONS 1
bool ce_server_init(ServerState_t* server_state)
{
     // open tcp server
     server_state->server_socket = socket(AF_INET, SOCK_STREAM, 0);
     assert(server_state->server_socket > 0);
     int optval = 1;
     setsockopt(server_state->server_socket, SOL_SOCKET, SO_REUSEADDR,  (const void *)&optval , sizeof(int));

     struct sockaddr_in server_addr = {};
     server_addr.sin_family = AF_INET;
     server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
     server_addr.sin_port = htons((unsigned short)MAGIC_PORT);
     // associate the socket we created with the port we want to listen on
     if(bind(server_state->server_socket, (struct sockaddr *)&server_addr,  sizeof(server_addr)) < 0){
          assert(0);
     }
     if(listen(server_state->server_socket, NUM_CONNECTIONS) < 0) assert(0);
     // launch server
     pthread_create(&server_state->thread, NULL, ce_server_listen, server_state);
     return true;
}


bool ce_client_init(ClientState_t* client_state)
{
     client_state->server_socket = socket(AF_INET, SOCK_STREAM, 0);
     struct sockaddr_in server_addr = {};
     server_addr.sin_family = AF_INET;
     server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
     server_addr.sin_port = htons((unsigned short)MAGIC_PORT);
     // establish a connection with the server
     if(connect(client_state->server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) assert(0);
     return true;
}

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

