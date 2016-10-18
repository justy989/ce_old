#ifndef CE_SERVER_H
#define CE_SERVER_H
#include "ce_network.h"
// server side state
typedef struct Client{
     int socket;
     NetworkId_t id; // unique id for this client
     void* config_user_data;
     struct{ // client list
          struct Client* next;
          struct Client* prev;
     };
} Client_t;

typedef struct{
     int server_socket;
     NetworkId_t current_client_id;
     NetworkId_t current_buffer_id; // used for generating unique network id's
     void* config_user_data;
     Client_t* client_list_head;
     BufferNode_t* buffer_list_head; // TODO: eventually make this a **
     pthread_t thread;
} ServerState_t;

// server side functions
bool ce_server_init(ServerState_t* server_state);

#endif // CE_SERVER_H
