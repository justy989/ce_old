#ifndef CE_NETWORK_H
#define CE_NETWORK_H
#include "ce.h"
#include <pthread.h>

typedef enum{
     NC_INSERT_STRING,
}NetworkCommand_t;

typedef struct{
     Point_t cursor;
     bool modified;
     bool readonly;
     union {
          char* filename;
          char* name;
     };
     void* user_data;
} NetworkBuffer_t;
typedef uint16_t NetworkId_t;
typedef struct{
     NetworkId_t id;
     Point_t cursor; // we also read in the cursor position with every buffer id
} NetworkBufferId_t;

typedef struct{
     int server_socket;
     // TODO: make the network buffer list a linked list
     int16_t buffer_count;
     NetworkBuffer_t** network_buffers;
} ClientState_t;

typedef struct{
     int server_socket;
     int client_socket; // TODO: allow for more than one
     BufferNode_t* buffer_list_head; // TODO: eventually make this a **
     pthread_t thread;
} ServerState_t;

// server side functions
bool ce_server_init(ServerState_t* server_state);

// client side functions
bool ce_client_init(ClientState_t* client_state);
bool ce_network_insert_string(ClientState_t* client_state, NetworkBufferId_t buffer_id, Point_t location, const char* string);
#endif // CE_NETWORK_H
