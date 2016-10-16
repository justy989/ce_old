#ifndef CE_SERVER_H
#define CE_SERVER_H
#include "ce_network.h"
// server side state
typedef struct ClientView{
     NetworkId_t id; // view id's are unique per-client and generated on the client side
     Buffer_t* buffer;
     Point_t top_left;
     Point_t bottom_right;
     Point_t top_row;
     struct ClientView* next;
} ClientView_t;

typedef struct Client{
     int socket;
     ClientView_t* view_list_head;
     struct{ // client list
          struct Client* next;
          struct Client* prev;
     };
} Client_t;

typedef struct{
     NetworkId_t current_id; // used for generating unique network id's
     int server_socket;
     Client_t* client_list_head;
     BufferNode_t* buffer_list_head; // TODO: eventually make this a **
     pthread_t thread;
} ServerState_t;

// server side functions
bool ce_server_init(ServerState_t* server_state);

#endif // CE_SERVER_H
