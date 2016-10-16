#include "ce_network.h"
#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

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
