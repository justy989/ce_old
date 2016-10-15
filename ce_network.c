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
     if(buffer_id->cursor.x >= 0 && buffer_id->cursor.y >= 0)
          head->buffer->cursor = buffer_id->cursor;
     return head->buffer;
}

void ce_server_file_opened(ServerState_t* server_state, const Buffer_t* new_buffer)
{
     // TODO: error handling
     NetworkCommand_t cmd = NC_OPEN_FILE;
     ssize_t sent_bytes = write(server_state->client_socket, &(cmd), sizeof(cmd));
     sent_bytes = write(server_state->client_socket, &(new_buffer->network_id), sizeof(new_buffer->network_id));
     sent_bytes = write(server_state->client_socket, new_buffer->filename, strlen(new_buffer->filename) + 1);
}
void ce_server_refresh_view(ServerState_t* server_state, const Buffer_t* buffer, Point_t top_left, Point_t bottom_right)
{
     ce_message("sending view to client");
     // TODO: error handling
     NetworkCommand_t cmd = NC_REFRESH_VIEW;
     ssize_t sent_bytes = write(server_state->client_socket, &(cmd), sizeof(cmd));
     ce_message("view string to client");
     ce_clamp_cursor(buffer, &top_left);
     ce_clamp_cursor(buffer, &bottom_right);
     ce_message("top left is %lld, %lld", top_left.x, top_left.y);
     ce_message("bottom right is %lld, %lld", bottom_right.x, bottom_right.y);
     char* view_str = ce_dupe_string(buffer, &top_left, &bottom_right);
     assert(view_str);
     sent_bytes = write(server_state->client_socket, view_str, strlen(view_str) + 1);
     ce_message("view string sent to client");
     free(view_str);
}

void* ce_server_listen(void* args)
{
     ServerState_t* server_state = args;
     BufferNode_t* head = server_state->buffer_list_head;
     assert(head);
     NetworkId_t current_buffer_id = 1;

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
               case NC_OPEN_FILE:
               {
                    READ_STR(in_str);
                    Buffer_t* buffer = calloc(1, sizeof(*buffer));
                    if(!ce_load_file(buffer, in_str)){
                         free(buffer);
                         continue;
                    }
                    buffer->network_id = current_buffer_id++;
                    // TODO: error handling
                    BufferNode_t* new_node = malloc(sizeof(*new_node));
                    *new_node = (BufferNode_t){buffer, head->next};
                    head->next = new_node;

                    // notify all clients about the new buffer
                    ce_server_file_opened(server_state, buffer);
               } break;
               case NC_REFRESH_VIEW:
               {
                    ce_message("received refresh view");
                    READ(NetworkId_t, id);
                    NetworkBufferId_t buffer_id = {id, {-1,-1}};
                    ce_message("parsed buffer_id");
                    READ(Point_t, top_left);
                    ce_message("parsed top left");
                    READ(Point_t, bottom_right);
                    ce_message("parsed top right");
                    ce_server_refresh_view(server_state, id_to_buffer(head, &buffer_id), top_left, bottom_right);
               } break;
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

#define CLIENT_READ(var) \
({ \
     ssize_t n_bytes = read(client_state->server_socket, &var, sizeof(var)); \
     assert(n_bytes > 0); \
})
#define CLIENT_READ_STR(buf) ({ \
     size_t read_ix = 0; \
     do{ \
          ssize_t n_bytes = read(client_state->server_socket, &buf[read_ix], sizeof(buf[read_ix])); \
          assert(n_bytes > 0); \
          read_ix++; \
     } while(buf[read_ix-1] != '\0'); \
})

void ce_client_refresh_view(ClientState_t* client_state, Buffer_t* buffer)
{
     NetworkCommand_t cmd;
     CLIENT_READ(cmd);
     assert(cmd == NC_REFRESH_VIEW);
     char view_str[BUFSIZ];
     ce_clear_lines(buffer);
     {
     size_t read_ix = 0;
again:
          do{
               ssize_t n_bytes = read(client_state->server_socket, &view_str[read_ix], sizeof(view_str[read_ix]));
               assert(n_bytes > 0);
               if(view_str[read_ix] == '\n'){
                    view_str[read_ix] = 0;
                    ce_append_line(buffer, view_str);
                    read_ix = 0;
                    goto again;
               }
               else read_ix++;
          } while(view_str[read_ix-1] != '\0');
          ce_append_line(buffer, view_str);
     }
}

void ce_network_refresh_view(ClientState_t* client_state, BufferView_t* buffer_view)
{
     NetworkCommand_t cmd = NC_REFRESH_VIEW;
     WRITE(cmd);
     WRITE(buffer_view->buffer->network_id);
     WRITE(buffer_view->top_left);
     WRITE(buffer_view->bottom_right);
     ce_message("view %lld, %lld", buffer_view->bottom_right.x, buffer_view->bottom_right.y);

     // TODO: for now we will synchronously receive the view update.
     // eventually this will need to be out of band
     ce_client_refresh_view(client_state, buffer_view->buffer);
     // TODO: do I need to change the top_left and bottom_right stuff?
     buffer_view->top_row = 0;
}

bool ce_network_load_file(ClientState_t* client_state, Buffer_t* buffer, const char* filename)
{
     // send open file request
     NetworkCommand_t cmd = NC_OPEN_FILE;
     WRITE(cmd);
     ssize_t n_bytes_sent = write(client_state->server_socket, filename, strlen(filename) + 1);
     assert(n_bytes_sent == (ssize_t)strlen(filename)+1);

     // wait for open file response and assign the network id
     CLIENT_READ(cmd);
     assert(cmd == NC_OPEN_FILE);
     CLIENT_READ(buffer->network_id);
     char filename_str[BUFSIZ];
     CLIENT_READ_STR(filename_str);
     assert(!strcmp(filename_str, filename));
     buffer->name = strdup(filename);
     return true;
}
