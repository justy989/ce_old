#ifndef CE_NETWORK_H
#define CE_NETWORK_H
#include "ce.h"
#include <pthread.h>

#define MAGIC_PORT 12345
#define MAX_CONNECTIONS 5

typedef enum{
     NC_OPEN_FILE,
     NC_REFRESH_VIEW,
     NC_INSERT_STRING,
}NetworkCommand_t;

#if 0
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
#endif

typedef uint16_t NetworkId_t;
typedef struct{
     NetworkId_t id; // buffer id's are unique across all clients and are generated on the server side
     Point_t cursor; // we also read in the cursor position with every buffer id
} NetworkBufferId_t;

// client side state
typedef struct{
     int server_socket;
} ClientState_t;

// client side functions
bool ce_client_init(ClientState_t* client_state);
bool ce_network_insert_string(ClientState_t* client_state, NetworkBufferId_t buffer_id, Point_t location, const char* string);
bool ce_network_load_file(ClientState_t* client_state, Buffer_t* buffer, const char* filename);
void ce_network_refresh_view(ClientState_t* client_state, BufferView_t* buffer_view);

#endif // CE_NETWORK_H
