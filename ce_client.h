#ifndef CE_CLIENT_H
#define CE_CLIENT_H
#include "ce_network.h"
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
     BufferNode_t* buffer_list_head;
     pthread_t command_handling_thread;
} ClientState_t;

// client side functions
bool ce_client_init(ClientState_t* client_state);


#endif // CE_CLIENT_H
