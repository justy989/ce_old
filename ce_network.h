#ifndef CE_NETWORK_H
#define CE_NETWORK_H
#include "ce.h"
#include <pthread.h>

typedef enum{
     NC_OPEN_FILE,
     NC_REFRESH_VIEW,
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
void ce_server_refresh_view(ServerState_t* server_state, const Buffer_t* buffer, Point_t top_left, Point_t bottom_right);
//void ce_server_file_opened(ServerState_t* server_state, Buffer_t* new_buffer);

// client side functions
bool ce_client_init(ClientState_t* client_state);
bool ce_network_insert_string(ClientState_t* client_state, NetworkBufferId_t buffer_id, Point_t location, const char* string);
bool ce_network_load_file(ClientState_t* client_state, Buffer_t* buffer, const char* filename);
void ce_network_refresh_view(ClientState_t* client_state, BufferView_t* buffer_view);

#endif // CE_NETWORK_H
