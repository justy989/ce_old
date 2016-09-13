#include "ce.h"

bool g_insert = false;
bool g_split = false;
int64_t g_start_line = 0;

bool initializer(Buffer* message_buffer, Point* terminal_dimensions)
{
     g_message_buffer = message_buffer;
     g_terminal_dimensions = terminal_dimensions;

     return true;
}

bool key_handler(int key, BufferNode* head, Point* cursor)
{
     Buffer* buffer = head->buffer;
     if(head->next) buffer = head->next->buffer;

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
               if(cursor->y < (buffer->line_count - 1)){
                    cursor->y++;
               }

               if(buffer->lines[cursor->y]){
                    int64_t line_length = strlen(buffer->lines[cursor->y]);

                    if(cursor->x >= line_length){
                         cursor->x = line_length - 1;
                         if(cursor->x < 0){
                              cursor->x = 0;
                         }
                    }
               }else{
                    cursor->x = 0;
               }
          } break;
          case 'k':
          {
               if(cursor->y > 0){
                    cursor->y--;
               }

               if(buffer->lines[cursor->y]){
                    int64_t line_length = strlen(buffer->lines[cursor->y]);

                    if(cursor->x >= line_length){
                         cursor->x = line_length - 1;
                         if(cursor->x < 0){
                              cursor->x = 0;
                         }
                    }
               }else{
                    cursor->x = 0;
               }
          } break;
          case 'h':
          if(cursor->x > 0){
               cursor->x--;
          }
          break;
          case 'l':
          {
               if(buffer->lines[cursor->y]){
                    int64_t line_length = strlen(buffer->lines[cursor->y]);
                    if(cursor->x < (line_length - 1)){
                         cursor->x++;
                    }
               }
          } break;
          case 'i':
               g_insert = true;
               break;
          case '':
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
          }
     }

     return true;
}

void view_drawer(const BufferNode* head, const Point* cursor)
{
     Buffer* buffer = head->buffer;
     if(head->next) buffer = head->next->buffer;

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
     mvprintw(g_terminal_dimensions->y - 1, 0, "%s %d lines", buffer->filename, buffer->line_count);
     attroff(A_REVERSE);

     // reset the cursor
     move(cursor->y - buffer_top_left.y, cursor->x);
}
