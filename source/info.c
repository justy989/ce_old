#include "info.h"
#include "misc.h"

void info_update_buffer_list_buffer(Buffer_t* buffer_list_buffer, const BufferNode_t* head)
{
     char buffer_info[BUFSIZ];
     buffer_list_buffer->status = BS_NONE;
     ce_clear_lines(buffer_list_buffer);

     // calc maxes of things we care about for formatting
     int64_t max_buffer_lines = 0;
     int64_t max_name_len = 0;
     int64_t buffer_count = 0;
     const BufferNode_t* itr = head;
     while(itr){
          if(max_buffer_lines < itr->buffer->line_count) max_buffer_lines = itr->buffer->line_count;
          int64_t name_len = strlen(itr->buffer->name);
          if(max_name_len < name_len) max_name_len = name_len;
          buffer_count++;
          itr = itr->next;
     }

     int64_t max_buffer_lines_digits = misc_count_digits(max_buffer_lines);
     if(max_buffer_lines_digits < 5) max_buffer_lines_digits = 5; // account for "lines" string row header
     if(max_name_len < 11) max_name_len = 11; // account for "buffer name" string row header

     // build format string, OMG THIS IS SO UNREADABLE HOLY MOLY BATMAN
     char format_string[BUFSIZ];
     // build header
     snprintf(format_string, BUFSIZ, "// %%5s %%-%"PRId64"s %%-%"PRId64"s", max_name_len,
              max_buffer_lines_digits);
     snprintf(buffer_info, BUFSIZ, format_string, "flags", "buffer name", "lines");
     ce_append_line(buffer_list_buffer, buffer_info);

     // build buffer info
     snprintf(format_string, BUFSIZ, "   %%5s %%-%"PRId64"s %%%"PRId64 PRId64, max_name_len, max_buffer_lines_digits);

     itr = head;
     while(itr){
          const char* buffer_flag_str = misc_buffer_flag_string(itr->buffer);
          snprintf(buffer_info, BUFSIZ, format_string, buffer_flag_str, itr->buffer->name,
                   itr->buffer->line_count);
          ce_append_line(buffer_list_buffer, buffer_info);
          itr = itr->next;
     }

     buffer_list_buffer->status = BS_READONLY;
}

void info_update_mark_list_buffer(Buffer_t* mark_list_buffer, const Buffer_t* buffer)
{
     char buffer_info[BUFSIZ];
     mark_list_buffer->status = BS_NONE;
     ce_clear_lines(mark_list_buffer);

     snprintf(buffer_info, BUFSIZ, "// reg line");
     ce_append_line(mark_list_buffer, buffer_info);

     int max_digits = 1;
     const VimMarkNode_t* itr = ((BufferState_t*)(buffer->user_data))->vim_buffer_state.mark_head;
     while(itr){
          int digits = misc_count_digits(itr->location.y);
          if(digits > max_digits) max_digits = digits;
          itr = itr->next;
     }

     itr = ((BufferState_t*)(buffer->user_data))->vim_buffer_state.mark_head;
     while(itr){
          snprintf(buffer_info, BUFSIZ, "  '%c' %*"PRId64" %s",
                   itr->reg_char, max_digits, itr->location.y,
                   itr->location.y < buffer->line_count ? buffer->lines[itr->location.y] : "");
          ce_append_line(mark_list_buffer, buffer_info);
          itr = itr->next;
     }

     mark_list_buffer->status = BS_READONLY;
}

void info_update_yank_list_buffer(Buffer_t* yank_list_buffer, const VimYankNode_t* yank_head)
{
     char buffer_info[BUFSIZ];
     yank_list_buffer->status = BS_NONE;
     ce_clear_lines(yank_list_buffer);

     const VimYankNode_t* itr = yank_head;
     while(itr){
          snprintf(buffer_info, BUFSIZ, "// reg '%c'", itr->reg_char);
          ce_append_line(yank_list_buffer, buffer_info);
          ce_append_line(yank_list_buffer, itr->text);
          itr = itr->next;
     }

     yank_list_buffer->status = BS_READONLY;
}

void info_update_macro_list_buffer(Buffer_t* macro_list_buffer, const VimState_t* vim_state)
{
     char buffer_info[BUFSIZ];
     macro_list_buffer->status = BS_NONE;
     ce_clear_lines(macro_list_buffer);

     ce_append_line(macro_list_buffer, "// reg actions");

     const VimMacroNode_t* itr = vim_state->macro_head;
     while(itr){
          char* char_string = vim_command_string_to_char_string(itr->command);
          snprintf(buffer_info, BUFSIZ, "  '%c' %s", itr->reg, char_string);
          ce_append_line(macro_list_buffer, buffer_info);
          free(char_string);
          itr = itr->next;
     }

     if(vim_state->recording_macro){
          ce_append_line(macro_list_buffer, "");
          ce_append_line(macro_list_buffer, "// recording:");

          int* int_cmd = ce_keys_get_string(vim_state->record_macro_head);

          if(int_cmd[0]){
               char* char_cmd = vim_command_string_to_char_string(int_cmd);
               if(char_cmd[0]){
                    ce_append_line(macro_list_buffer, char_cmd);
               }

               free(char_cmd);
          }

          free(int_cmd);
     }

     ce_append_line(macro_list_buffer, "");
     ce_append_line(macro_list_buffer, "// escape conversions");
     ce_append_line(macro_list_buffer, "// \\b -> KEY_BACKSPACE");
     ce_append_line(macro_list_buffer, "// \\e -> KEY_ESCAPE");
     ce_append_line(macro_list_buffer, "// \\r -> KEY_ENTER");
     ce_append_line(macro_list_buffer, "// \\t -> KEY_TAB");
     ce_append_line(macro_list_buffer, "// \\u -> KEY_UP");
     ce_append_line(macro_list_buffer, "// \\d -> KEY_DOWN");
     ce_append_line(macro_list_buffer, "// \\l -> KEY_LEFT");
     ce_append_line(macro_list_buffer, "// \\i -> KEY_RIGHT");
     ce_append_line(macro_list_buffer, "// \\\\ -> \\"); // HAHAHAHAHA

     macro_list_buffer->status = BS_READONLY;
}
