#include "ce.h"
#include <assert.h>

typedef struct{
     bool insert;
     bool split;
     int64_t start_line;
     int last_key;
     BufferNode* current_buffer_node;
} ConfigState;

bool initializer(BufferNode* head, Point* terminal_dimensions, void** user_data)
{
     // NOTE: need to set these in this module
     g_message_buffer = head->buffer;
     g_terminal_dimensions = terminal_dimensions;

     // setup the config's state
     ConfigState* config_state = malloc(sizeof(*config_state));
     if(!config_state) return false;

     config_state->insert = false;
     config_state->split = false;
     config_state->start_line = 0;
     config_state->last_key = 0;
     config_state->current_buffer_node = head;

     *user_data = config_state;

     // setup state for the buffers
     while(head){
          Point* cursor = malloc(sizeof(Point));
          if(!cursor) return false;
          cursor->x = 0;
          cursor->y = 0;
          head->buffer->user_data = cursor;
          head = head->next;
     }

     return true;
}

bool destroyer(BufferNode* head, void* user_data)
{
     while(head){
          Point* cursor = head->buffer->user_data;
          free(cursor);
          head->buffer->user_data = NULL;
          head = head->next;
     }

     free(user_data);
     return true;
}

const char* weak_word_boundary_characters = " [](){}+->.,=!?\"'";
const char* strong_word_boundary_characters = " ";
// move cursor to next word boundary
static void advance_to_boundary(Buffer* buffer, Point* cursor, const char* boundary_str) {
     if(buffer->lines[cursor->y][0] == '\0') return;
     const char* search_char = &buffer->lines[cursor->y][cursor->x+1];
     assert(cursor->x <= (int64_t)strlen(buffer->lines[cursor->y]));
     if(search_char != '\0'){
          // search for a word boundary character after our current character
          const char* boundary_char = strpbrk(search_char, boundary_str);
          if(boundary_char){
               if(boundary_char == search_char){
                    cursor->x++;
                    advance_to_boundary(buffer, cursor, boundary_str);
               }
               else{
                    cursor->x += (boundary_char--) - search_char;
               }
               // we end at the character before the boundary
               // character if we did not start at a word boundary,
               // or the last boundary character if we started at a word boundary
               return;
          }
          else {
               cursor->x = strlen(buffer->lines[cursor->y])-1;
               return;
          }
     }
}

bool key_handler(int key, BufferNode* head, void* user_data)
{
     ConfigState* config_state = user_data;
     Buffer* buffer = config_state->current_buffer_node->buffer;
     Point* cursor = buffer->user_data;

     config_state->last_key = key;

     if(config_state->insert){
          // TODO: should be a switch
          if(key == 27){ // escape
               config_state->insert = false;
               if(buffer->lines[cursor->y]){
                    int64_t line_len = strlen(buffer->lines[cursor->y]);
                    if(cursor->x == line_len){
                         cursor->x--;
                    }
               }
          }else if(key == 127){ // backspace
               if(buffer->line_count){
                    if(cursor->x == 0){
                         if(cursor->y != 0){
                              // remove the line and join the next line with the previous
                         }
                    }else{
                         if(ce_remove_char(buffer, cursor)){
                              cursor->x--;
                         }
                    }
               }
          }else if(key == 10){ // add empty line
               if(!buffer->line_count){
                    ce_alloc_lines(buffer, 1);
               }
               if(ce_insert_newline(buffer, cursor->y + 1)){
                    cursor->y++;
                    cursor->x = 0;
               }
          }else{ // insert
               if(buffer->line_count == 0) ce_alloc_lines(buffer, 1);

               if(ce_insert_char(buffer, cursor, key)) cursor->x++;
          }
     }else{
          switch(key){
          default:
               break;
          case 'q':
               return false; // exit !
          case 'j':
          {
               Point delta = {0, 1};
               ce_move_cursor(buffer, cursor, &delta);
          } break;
          case 'k':
          {
               Point delta = {0, -1};
               ce_move_cursor(buffer, cursor, &delta);
          } break;
          case 'e':
          {
               advance_to_boundary(buffer, cursor, weak_word_boundary_characters);
          } break;
          case 'E':
          {
               advance_to_boundary(buffer, cursor, strong_word_boundary_characters);
          } break;
          case 'h':
          {
               Point delta = {-1, 0};
               ce_move_cursor(buffer, cursor, &delta);
          } break;
          case 'l':
          {
               Point delta = {1, 0};
               ce_move_cursor(buffer, cursor, &delta);
          } break;
          case 'i':
               config_state->insert = true;
               break;
          case 'a':
               if(buffer->lines[cursor->y]){
                    cursor->x++;
               }
               config_state->insert = true;
               break;
          case 'd':
               // delete line
               if(buffer->line_count){
                    if(ce_remove_line(buffer, cursor->y)){
                         if(cursor->y >= buffer->line_count){
                              cursor->y = buffer->line_count - 1;
                         }
                    }
               }
               break;
          case 's':
               ce_save_buffer(buffer, buffer->filename);
               break;
          case 'v':
               config_state->split = !config_state->split;
               break;
          case 'b':
               config_state->current_buffer_node = config_state->current_buffer_node->next;
               if(!config_state->current_buffer_node){
                    config_state->current_buffer_node = head;
               }
               break;
          case 21: // Ctrl + d
          {
               Point delta = {0, -g_terminal_dimensions->y / 2};
               ce_move_cursor(buffer, cursor, &delta);
          } break;
          case 4: // Ctrl + u
          {
               Point delta = {0, g_terminal_dimensions->y / 2};
               ce_move_cursor(buffer, cursor, &delta);
          } break;
          }
     }

     return true;
}

void view_drawer(const BufferNode* head, void* user_data)
{
     (void)(head);
     ConfigState* config_state = user_data;
     Buffer* buffer = config_state->current_buffer_node->buffer;
     Point* cursor = buffer->user_data;

     // calculate the last line we can draw
     int64_t last_line = config_state->start_line + (g_terminal_dimensions->y - 2);

     // adjust the starting line based on where the cursor is
     if(cursor->y > last_line) config_state->start_line++;
     if(cursor->y < config_state->start_line) config_state->start_line--;

     // recalc the starting line
     last_line = config_state->start_line + (g_terminal_dimensions->y - 2);

     if(last_line > (buffer->line_count - 1)){
          last_line = buffer->line_count - 1;
          config_state->start_line = last_line - (g_terminal_dimensions->y - 2);
     }

     if(config_state->start_line < 0) config_state->start_line = 0;

     // print the range of lines we want to show
     Point buffer_top_left = {0, config_state->start_line};
     if(buffer->line_count){
          standend();
          Point term_top_left = {0, 0};
          Point term_bottom_right = {g_terminal_dimensions->x, g_terminal_dimensions->y - 1};
          if(!config_state->split){
               ce_draw_buffer(buffer, &term_top_left, &term_bottom_right, &buffer_top_left);
          }else{
               term_bottom_right.x = (g_terminal_dimensions->x / 2) - 1;
               ce_draw_buffer(buffer, &term_top_left, &term_bottom_right, &buffer_top_left);

               term_top_left.x = (g_terminal_dimensions->x / 2) + 1;
               term_bottom_right.x = g_terminal_dimensions->x;
               ce_draw_buffer(buffer, &term_top_left, &term_bottom_right, &buffer_top_left);
          }
     }

     attron(A_REVERSE);
     mvprintw(g_terminal_dimensions->y - 1, 0, "%s %s %d lines, key %d", config_state->insert ? "INSERT" : "NORMAL",
              buffer->filename, buffer->line_count, config_state->last_key);
     attroff(A_REVERSE);

     // reset the cursor
     move(cursor->y - buffer_top_left.y, cursor->x);
}
