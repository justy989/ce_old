#include "ce.h"
#include "assert.h"

bool g_insert = false;
bool g_split = false;
int64_t g_start_line = 0;
int g_last_key = 0;
BufferNode* g_current_buffer_node = NULL;

bool initializer(BufferNode* head, Buffer* message_buffer, Point* terminal_dimensions)
{
     g_message_buffer = message_buffer;
     g_terminal_dimensions = terminal_dimensions;
     g_current_buffer_node = head;

     while(head){
          Point* cursor = malloc(sizeof(Point));
          cursor->x = 0;
          cursor->y = 0;
          head->buffer->user_data = cursor;
          head = head->next;
     }

     return true;
}

bool destroyer(BufferNode* head)
{
     while(head){
          Point* cursor = head->buffer->user_data;
          free(cursor);
          head->buffer->user_data = NULL;
          head = head->next;
     }

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

bool key_handler(int key, BufferNode* head)
{
     Buffer* buffer = g_current_buffer_node->buffer;
     Point* cursor = buffer->user_data;

     g_last_key = key;

     if(g_insert){
          // TODO: should be a switch
          if(key == 27){ // escape
               g_insert = false;
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
               g_insert = true;
               break;
          case 'a':
               if(buffer->lines[cursor->y]){
                    cursor->x++;
               }
               g_insert = true;
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
               g_split = !g_split;
               break;
          case 'b':
               g_current_buffer_node = g_current_buffer_node->next;
               if(!g_current_buffer_node){
                    g_current_buffer_node = head;
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

void view_drawer(const BufferNode* head)
{
     (void)(head);
     Buffer* buffer = g_current_buffer_node->buffer;
     Point* cursor = buffer->user_data;

     // calculate the last line we can draw
     int64_t last_line = g_start_line + (g_terminal_dimensions->y - 2);

     // adjust the starting line based on where the cursor is
     if(cursor->y > last_line) g_start_line++;
     if(cursor->y < g_start_line) g_start_line--;

     // recalc the starting line
     last_line = g_start_line + (g_terminal_dimensions->y - 2);

     if(last_line > (buffer->line_count - 1)){
          last_line = buffer->line_count - 1;
          g_start_line = last_line - (g_terminal_dimensions->y - 2);
     }

     if(g_start_line < 0) g_start_line = 0;

     // print the range of lines we want to show
     Point buffer_top_left = {0, g_start_line};
     if(buffer->line_count){
          standend();
          Point term_top_left = {0, 0};
          Point term_bottom_right = {g_terminal_dimensions->x, g_terminal_dimensions->y - 1};
          if(!g_split){
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
     mvprintw(g_terminal_dimensions->y - 1, 0, "%s %d lines, key %d", buffer->filename, buffer->line_count, g_last_key);
     attroff(A_REVERSE);

     // reset the cursor
     move(cursor->y - buffer_top_left.y, cursor->x);
}
