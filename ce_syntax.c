#include "ce_syntax.h"

#include <ctype.h>

static int iscapsvarchar(int c)
{
     return isupper(c) || c == '_' || isdigit(c);
}

static int64_t syntax_is_c_constant_number(const char* line, int64_t start_offset)
{
     const char* start = line + start_offset;
     const char* itr = start;
     int64_t count = 0;
     char ch = *itr;
     bool seen_decimal = false;
     bool seen_hex = false;
     bool seen_u = false;
     bool seen_digit = false;
     int seen_l = 0;

     while(ch != 0){
          if(isdigit(ch)){
               if(seen_u || seen_l) break;
               seen_digit = true;
               count++;
          }else if(!seen_decimal && ch == '.'){
               if(seen_u || seen_l) break;
               seen_decimal = true;
               count++;
          }else if(ch == 'f' && seen_decimal){
               if(seen_u || seen_l) break;
               count++;
               break;
          }else if(ch == '-' && itr == start){
               count++;
          }else if(ch == 'x' && itr == (start + 1)){
               seen_hex = true;
               count++;
          }else if((ch == 'u' || ch == 'U') && !seen_u){
               seen_u = true;
               count++;
          }else if((ch == 'l' || ch == 'L') && seen_l < 2){
               seen_l++;
               count++;
          }else if(seen_hex){
               if(seen_u || seen_l) break;

               bool valid_hex_char = false;

               switch(ch){
               default:
                    break;
               case 'a':
               case 'b':
               case 'c':
               case 'd':
               case 'e':
               case 'f':
               case 'A':
               case 'B':
               case 'C':
               case 'D':
               case 'E':
               case 'F':
                    count++;
                    valid_hex_char = true;
                    break;
               }

               if(!valid_hex_char) break;
          }else{
               break;
          }

          itr++;
          ch = *itr;
     }

     if(count == 1 && (start[0] == '-' || start[0] == '.')) return 0;
     if(!seen_digit) return 0;

     // check if the previous character is not a delimiter
     int64_t prev_index = start_offset - 1;
     if(prev_index >= 0 && (iscapsvarchar(line[prev_index]) || isalpha(line[prev_index]))) return 0;

     return count;
}

int64_t syntax_is_c_caps_var(const char* line, int64_t start_offset)
{
     const char* itr = line + start_offset;
     int64_t count = 0;
     for(char ch = *itr; iscapsvarchar(ch); ++itr){
          ch = *itr;
          count++;
     }

     if(!count) return 0;

     int64_t prev_index = start_offset - 1;

     // if the surrounding chars are letters, we haven't found a constant
     if(islower(*(itr - 1))) return 0;
     if(prev_index >= 0 && (iscapsvarchar(line[prev_index]) || isalpha(line[prev_index]))) return 0;

     return count - 1; // we over-counted on the last iteration
}

static bool likely_a_path(char c)
{
     return (isalnum(c) || c == '/' || c == '_' || c == '-' || c == '.' );
}

static int64_t syntax_is_c_fullpath(const char* line, int64_t start_offset)
{
     const char* itr = line + start_offset;
     int64_t count = 0;
     bool starts_with_slash = (line[start_offset] == '/');

     if(!starts_with_slash) return 0;

     // before the path should be blank
     if(start_offset && !isblank(line[start_offset-1])) return 0;

     while(itr){
          if(!likely_a_path(*itr)) break;
          itr++;
          count++;
     }

     itr--;

     if(starts_with_slash && count > 1) return count;

     return 0;
}

static int syntax_set_color(Syntax_t syntax, HighlightType_t highlight_type)
{
     standend();

     if(syntax < S_NORMAL_HIGHLIGHTED){
          switch(highlight_type){
          default:
               attron(COLOR_PAIR(syntax));
               break;
          case HL_VISUAL:
               attron(COLOR_PAIR(syntax + S_NORMAL_HIGHLIGHTED - 1));
               break;
          case HL_CURRENT_LINE:
               attron(COLOR_PAIR(syntax + S_NORMAL_CURRENT_LINE - 1));
               break;
          }
     }else{
          attron(COLOR_PAIR(syntax));
     }

     return syntax;
}

static CommentType_t syntax_is_c_comment(const char* line, int64_t start_offset, bool inside_string)
{
     if(inside_string) return CT_NONE;

     char ch = line[start_offset];

     if(ch == '/'){
          char next_ch = line[start_offset + 1];
          if(next_ch == '*'){
               return CT_BEGIN_MULTILINE;
          }else if(next_ch == '/'){
               return CT_SINGLE_LINE;
          }

          int64_t prev_index = start_offset - 1;
          if(prev_index >= 0 && line[prev_index] == '*'){
               return CT_END_MULTILINE;
          }
     }

     return CT_NONE;
}

static void syntax_is_c_string_literal(const char* line, int64_t start_offset, int64_t line_len, bool* inside_string, char* last_quote_char)
{
     char ch = line[start_offset];

     if(ch == '"'){
          // ignore single quotes inside double quotes
          if(*inside_string){
               if(*last_quote_char == '\''){
                    return;
               }
               int64_t prev_char = start_offset - 1;
               if(prev_char >= 0 && line[prev_char] == '\\'){
                    int64_t prev_prev_char = prev_char - 1;
                    if(prev_prev_char >= 0 && line[prev_prev_char] != '\\'){
                         return;
                    }
               }
          }

          *inside_string = !*inside_string;
          if(*inside_string){
               *last_quote_char = ch;
          }
     }else if(ch == '\''){
          if(*inside_string){
               if(*last_quote_char == '"'){
                    return;
               }

               int64_t prev_char = start_offset - 1;
               if(prev_char >= 0 && line[prev_char] == '\\'){
                    int64_t prev_prev_char = prev_char - 1;
                    if(prev_prev_char >= 0 && line[prev_prev_char] != '\\'){
                         return;
                    }
               }

               *inside_string = false;
          }else{
               char next_char = line[start_offset + 1];
               int64_t next_next_index = start_offset + 2;
               char next_next_char = (next_next_index < line_len) ? line[next_next_index] : 0;

               if(next_char == '\\' || next_next_char == '\''){
                    *inside_string = true;
                    *last_quote_char = ch;
               }
          }
     }else if(ch == '<'){
          const char* itr = line + start_offset + 1;
          bool valid_system_header = true;

          while(*itr && *itr != '>'){
               if(isalnum(*itr)){
                    // pass
               }else if(*itr == '.'){
                    // pass
               }else if(*itr == '/'){
                    // pass
               }else{
                    valid_system_header = false;
               }
               itr++;
          }

          if(valid_system_header && *itr == '>'){
               *inside_string = true;
               *last_quote_char = ch;
          }
     }else if(ch == '>' && *last_quote_char == '<'){
          *inside_string = false;
     }
}

static int64_t match_keyword(const char* line, int64_t start_offset, const char** keywords, int64_t keyword_count)
{
     int64_t highlighting_left = 0;
     for(int64_t k = 0; k < keyword_count; ++k){
          int64_t keyword_len = strlen(keywords[k]);
          if(strncmp(line + start_offset, keywords[k], keyword_len) == 0){
               char pre_char = 0;
               char post_char = line[start_offset + keyword_len];
               if(start_offset > 0) pre_char = line[start_offset - 1];

               if(!isalnum(pre_char) && pre_char != '_' &&
                  !isalnum(post_char) && post_char != '_'){
                    highlighting_left = keyword_len;
                    break;
               }
          }
     }

     return highlighting_left;
}

static int64_t syntax_is_c_keyword(const char* line, int64_t start_offset)
{
     static const char* keywords [] = {
          "__thread",
          "auto",
          "case",
          "default",
          "do",
          "else",
          "enum",
          "extern",
          "false",
          "for",
          "if",
          "inline",
          "register",
          "sizeof",
          "static",
          "struct",
          "switch",
          "true",
          "typedef",
          "typeof",
          "union",
          "volatile",
          "while",
     };

     static const int keyword_count = sizeof(keywords) / sizeof(keywords[0]);

     return match_keyword(line, start_offset, keywords, keyword_count);
}

static int64_t syntax_is_c_control(const char* line, int64_t start_offset)
{
     static const char* keywords [] = {
          "break",
          "const",
          "continue",
          "goto",
          "return",
     };

     static const int keyword_count = sizeof(keywords) / sizeof(keywords[0]);

     return match_keyword(line, start_offset, keywords, keyword_count);
}

static int iscidentifier(int c)
{
     return isalnum(c) || c == '_';
}

static int64_t syntax_is_c_typename(const char* line, int64_t start_offset)
{
     // NOTE: simple rules for now:
     //       -if it is one of the c standard type names
     //       -ends in _t

     static const char* keywords [] = {
          "bool",
          "char",
          "double",
          "float",
          "int",
          "long",
          "short",
          "signed",
          "unsigned",
          "void",
     };

     static const int keyword_count = sizeof(keywords) / sizeof(keywords[0]);

     int64_t match = match_keyword(line, start_offset, keywords, keyword_count);
     if(match) return match;

     if(start_offset > 0 && iscidentifier(line[start_offset - 1])) return 0; // weed out middle of words

     // try valid identifier ending in '_t'
     const char* itr = line + start_offset;
     int64_t count = 0;
     for(char ch = *itr; iscidentifier(ch); ++itr){
          ch = *itr;
          count++;
     }

     if(!count) return 0;

     // we overcounted on the last iteration!
     count--;

     // reset itr before checking the final 2 characters
     itr = line + start_offset;
     if(count >= 2 && itr[count-2] == '_' && itr[count-1] == 't') return count;

#if 0
     // NOTE: Justin uses this while working on Bryte!
     if(isupper(*itr)){
          return count;
     }
#endif

     return 0;
}

static int64_t syntax_is_c_preprocessor(const char* line, int64_t start_offset)
{
     static const char* keywords [] = {
          "define",
          "include",
          "undef",
          "ifdef",
          "ifndef",
          "if",
          "else",
          "elif",
          "endif",
          "error",
          "pragma",
          "push",
          "pop",
     };

     static const int keyword_count = sizeof(keywords) / sizeof(keywords[0]);

     // exit early if this isn't a preproc cmd
     if(line[start_offset] != '#') return 0;

     int64_t highlighting_left = 0;
     for(int64_t k = 0; k < keyword_count; ++k){
          // NOTE: I wish we could strlen at compile time ! Can we?
          int64_t keyword_len = strlen(keywords[k]);
          if(strncmp(line + start_offset + 1, keywords[k], keyword_len) == 0){
               highlighting_left = keyword_len + 1; // account for missing prepended #
               break;
          }
     }

     return highlighting_left;
}

static void syntax_determine_highlight(SyntaxHighlighterData_t* data, SyntaxHighlight_t* highlight)
{
     const char* buffer_line = data->buffer->lines[data->loc.y];

     if(ce_point_in_range(data->loc, data->buffer->highlight_start, data->buffer->highlight_end)){
          highlight->type = HL_VISUAL;
     }else{
          highlight->highlight_left--;

          if(highlight->highlight_left <= 0){
               if(data->highlight_line_type && data->loc.y == data->cursor.y){
                    highlight->type = HL_CURRENT_LINE;
               }else{
                    highlight->type = HL_OFF;
               }
          }

          if(data->highlight_regex){
               if(highlight->chars_til_highlight < 0){
                    int regex_rc = regexec(data->highlight_regex, buffer_line + data->loc.x, 1, highlight->regex_matches, 0);
                    if(regex_rc == 0) highlight->chars_til_highlight = highlight->regex_matches[0].rm_so;
               }

               if(highlight->chars_til_highlight == 0){
                    highlight->type = HL_VISUAL;
                    highlight->highlight_left = highlight->regex_matches[0].rm_eo - highlight->regex_matches[0].rm_so;
               }

               highlight->chars_til_highlight--;
          }
     }
}

static void syntax_calc_trailing_whitespace(SyntaxHighlighterData_t* data, int64_t* trailing_white_space_begin)
{
     const char* buffer_line = data->buffer->lines[data->loc.y];
     int64_t line_length = strlen(buffer_line);
     *trailing_white_space_begin = line_length;

     // NOTE: pre-pass to find trailing whitespace if it exists
     if(data->cursor.y != data->loc.y){
          for(int64_t c = line_length - 1; c >= 0; --c){
               if(isblank(buffer_line[c])){
                    (*trailing_white_space_begin)--;
               }else{
                    break;
               }
          }
     }
}

static void syntax_calc_matching_pair(SyntaxHighlighterData_t* data, Point_t* matched_pair)
{
     // is our cursor on something we can match?
     *matched_pair = (Point_t){-1, -1};
     if(ce_point_on_buffer(data->buffer, data->cursor)){
          char ch = 0;
          ch = ce_get_char_raw(data->buffer, data->cursor);
          switch(ch){
          default:
               break;
          case '{':
          case '}':
          case '(':
          case ')':
          case '[':
          case ']':
          case '<':
          case '>':
               *matched_pair = data->cursor;
               ce_move_cursor_to_matching_pair(data->buffer, matched_pair, ch);
               break;
          }
     }
}

void syntax_highlight_c(SyntaxHighlighterData_t* data, void* user_data)
{
     if(!user_data) return;

     SyntaxC_t* syntax = user_data;

     // init if we haven't initted already
     switch(data->state){
     default:
          break;
     case SS_INITIALIZING:
     {
          memset(syntax, 0, sizeof(*syntax));

          // is our cursor on something we can match?
          syntax_calc_matching_pair(data, &syntax->matched_pair);

          // figure out of any multiline comments are earlier in the file offscreen
          syntax->inside_multiline_comment = false;
          int64_t last_line = data->bottom_right.y;
          if(last_line >= data->buffer->line_count) last_line = data->buffer->line_count;

          for(int64_t i = data->loc.y; i < last_line; ++i) {
               if(!data->buffer->lines[i][0]) continue;
               const char* buffer_line = data->buffer->lines[i];
               int64_t len = strlen(buffer_line);
               bool found_open_multiline_comment = false;

               for(int64_t c = 0; c < len; ++c){
                    if(buffer_line[c] == '/' && buffer_line[c + 1] == '*'){
                         found_open_multiline_comment = true;
                         break;
                    }

                    if(buffer_line[c] == '*' && buffer_line[c + 1] == '/'){
                         syntax->inside_multiline_comment = true;
                    }
               }

               if(found_open_multiline_comment) break;
          }

          syntax->highlight.chars_til_highlight = -1;

          if(data->line_number_type) syntax_set_color(S_LINE_NUMBERS, HL_OFF);
     } break;
     case SS_BEGINNING_OF_LINE:
     {
          syntax->inside_comment = false;
          syntax->inside_string = false;
          syntax->last_quote_char = 0;

          syntax->current_color = S_NORMAL;
          syntax->current_color_left = 0;

          syntax->highlight.type = HL_OFF;

          SyntaxHighlighterData_t data_copy = *data;
          data_copy.state = SS_CHARACTER;

          for(int64_t x = 0; x < data->loc.x; ++x){
               data_copy.loc = (Point_t){x, data->loc.y};
               syntax_highlight_c(&data_copy, user_data);
          }

          syntax_calc_trailing_whitespace(data, &syntax->trailing_whitespace_begin);

          if(data->loc.y == data->cursor.y){
               syntax->highlight.type = HL_CURRENT_LINE;
          }else{
               syntax->highlight.type = HL_OFF;
          }

          if(syntax->inside_multiline_comment){
               syntax->current_color = S_COMMENT;
          }

          syntax->current_color = syntax_set_color(syntax->current_color, syntax->highlight.type);
     } break;
     case SS_CHARACTER:
     {
          syntax_determine_highlight(data, &syntax->highlight);
          syntax->current_color = syntax_set_color(syntax->current_color, syntax->highlight.type);

          const char* line_to_print = data->buffer->lines[data->loc.y];
          int64_t print_line_length = strlen(line_to_print);

          // syntax highligh c things we recognize
          if(syntax->current_color_left == 0){
               if(!syntax->inside_string){
                    if((syntax->current_color_left = syntax_is_c_constant_number(line_to_print, data->loc.x))){
                         syntax->current_color = syntax_set_color(S_CONSTANT_NUMBER, syntax->highlight.type);
                    }else if((syntax->current_color_left = syntax_is_c_caps_var(line_to_print, data->loc.x))){
                         syntax->current_color = syntax_set_color(S_CONSTANT, syntax->highlight.type);
                    }

                    if(!syntax->current_color_left && !syntax->inside_comment && !syntax->inside_multiline_comment){
                         if((syntax->current_color_left = syntax_is_c_control(line_to_print, data->loc.x))){
                              syntax->current_color = syntax_set_color(S_CONTROL, syntax->highlight.type);
                         }else if((syntax->current_color_left = syntax_is_c_typename(line_to_print, data->loc.x))){
                              syntax->current_color = syntax_set_color(S_TYPE, syntax->highlight.type);
                         }else if((syntax->current_color_left = syntax_is_c_keyword(line_to_print, data->loc.x))){
                              syntax->current_color = syntax_set_color(S_KEYWORD, syntax->highlight.type);
                         }else if((syntax->current_color_left = syntax_is_c_preprocessor(line_to_print, data->loc.x))){
                              syntax->current_color = syntax_set_color(S_PREPROCESSOR, syntax->highlight.type);
                         }else if(syntax->matched_pair.x >= 0){
                              if(ce_points_equal(data->loc, data->cursor) || ce_points_equal(data->loc, syntax->matched_pair)){
                                   syntax->current_color = syntax_set_color(S_MATCHING_PARENS, syntax->highlight.type);
                              }else if(syntax->current_color == S_MATCHING_PARENS){
                                   syntax->current_color = syntax_set_color(S_NORMAL, syntax->highlight.type);
                              }
                         }
                    }

                    if(!syntax->current_color_left){
                         if((syntax->current_color_left = syntax_is_c_fullpath(line_to_print, data->loc.x))){
                              syntax->current_color = syntax_set_color(S_FILEPATH, syntax->highlight.type);
                         }
                    }
               }

               // highlight comments
               CommentType_t comment_type = syntax_is_c_comment(line_to_print, data->loc.x, syntax->inside_string);
               switch(comment_type){
               default:
                    break;
               case CT_SINGLE_LINE:
                    syntax->inside_comment = true;
                    syntax->current_color = syntax_set_color(S_COMMENT, syntax->highlight.type);
                    break;
               case CT_BEGIN_MULTILINE:
                    if(!syntax->inside_comment){
                         syntax->inside_multiline_comment = true;
                         syntax->current_color = syntax_set_color(S_COMMENT, syntax->highlight.type);
                    }
                    break;
               case CT_END_MULTILINE:
                    syntax->inside_multiline_comment = false;
                    syntax->current_color_left = 1;
                    break;
               }

               // highlight strings
               bool pre_quote_check = syntax->inside_string;
               syntax_is_c_string_literal(line_to_print, data->loc.x, print_line_length, &syntax->inside_string, &syntax->last_quote_char);

               // if inside_string has changed, update the color
               if(pre_quote_check != syntax->inside_string){
                    if(syntax->inside_string) syntax->current_color = syntax_set_color(S_STRING, syntax->highlight.type);
                    else syntax->current_color_left = 1;
               }
          }else{
               syntax->current_color_left--;

               // if no color is left, go back to what the color should be based on state
               if(syntax->current_color_left == 0){
                    syntax->current_color = syntax_set_color(S_NORMAL, syntax->highlight.type);

                    if(syntax->inside_comment || syntax->inside_multiline_comment){
                         syntax->current_color = syntax_set_color(S_COMMENT, syntax->highlight.type);
                    }else if(syntax->inside_string){
                         syntax->current_color = syntax_set_color(S_STRING, syntax->highlight.type);
                    }else if(syntax->matched_pair.x >= 0){
                         if(ce_points_equal(data->loc, data->cursor) || ce_points_equal(data->loc, syntax->matched_pair)){
                              syntax->current_color = syntax_set_color(S_MATCHING_PARENS, syntax->highlight.type);
                         }
                    }
               }
          }

          // highlight trailing whitespace
          if(data->loc.x >= syntax->trailing_whitespace_begin) syntax_set_color(S_TRAILING_WHITESPACE, syntax->highlight.type);
     } break;
     case SS_END_OF_LINE:
     {
          const char* buffer_line = data->buffer->lines[data->loc.y];
          int64_t line_length = strlen(buffer_line);

          // highlight the rest of the line, if configured
          if(data->cursor.y == data->loc.y && data->highlight_line_type == HLT_ENTIRE_LINE){
               syntax_set_color(S_NORMAL, HL_CURRENT_LINE);
               for(int64_t c = data->loc.x; c < data->bottom_right.x; ++c){
                    addch(' ');
               }
          }

          // highlight line numbers!
          syntax_set_color(S_LINE_NUMBERS, HL_OFF);

          // NOTE: post pass after the line to see if multiline comments begin or end
          for(int64_t c = data->loc.x; c < line_length; ++c){
               CommentType_t comment_type = syntax_is_c_comment(buffer_line, c, syntax->inside_string);
               switch(comment_type){
               default:
                    break;
               case CT_BEGIN_MULTILINE:
                    if(!syntax->inside_comment) syntax->inside_multiline_comment = true;
                    break;
               case CT_END_MULTILINE:
                    syntax->inside_multiline_comment = false;
                    break;
               }
          }
     } break;
     }
}

int64_t syntax_is_python_keyword(const char* line, int64_t start_offset)
{
     static const char* keywords [] = {
          "and",
          "del",
          "from",
          "not",
          "while",
          "as",
          "elif",
          "global",
          "or",
          "with",
          "assert",
          "else",
          "if",
          "pass",
          "import",
          "print",
          "class",
          "exec",
          "in",
          "finally",
          "is",
          "def",
          "for",
          "lambda",
          "self",
     };

     static const int keyword_count = sizeof(keywords) / sizeof(keywords[0]);

     return match_keyword(line, start_offset, keywords, keyword_count);
}

int64_t syntax_is_python_control(const char* line, int64_t start_offset)
{
     static const char* keywords [] = {
          "yield",
          "break",
          "except",
          "raise",
          "continue",
          "finally",
          "return",
          "try",
     };

     static const int keyword_count = sizeof(keywords) / sizeof(keywords[0]);

     return match_keyword(line, start_offset, keywords, keyword_count);
}

int64_t syntax_is_python_comment(const char* line, int64_t start_offset)
{
     if(line[start_offset] == '#'){
          int64_t line_len = strlen(line);
          return line_len - start_offset;
     }

     return 0;
}

void syntax_is_python_string(const char* line, int64_t start_offset, char* is_string)
{
     if(*is_string){
          if(line[start_offset] == *is_string){
               *is_string = 0;
          }
     }else{
          if(line[start_offset] == '\'' || line[start_offset] == '"'){
               *is_string = line[start_offset];
          }
     }
}

void syntax_is_python_docstring(const char* line, int64_t start_offset, char* is_docstring)
{
     int64_t line_len = strlen(line);

     if((line_len > start_offset + 2) &&
        (line[start_offset] == '\'' || line[start_offset] == '"') &&
        line[start_offset] == line[start_offset + 1] && line[start_offset] == line[start_offset + 2]){
          if(*is_docstring){
               *is_docstring = 0;
          }else{
               *is_docstring = line[start_offset];
          }
     }
}

void syntax_highlight_python(SyntaxHighlighterData_t* data, void* user_data)
{
     SyntaxPython_t* syntax = user_data;

     switch(data->state){
     default:
          break;
     case SS_INITIALIZING:
     {
          syntax->inside_docstring = 0;
          syntax->inside_string = 0;
          syntax->current_color = 0;
          syntax->current_color_left = 0;
          syntax->highlight.type = HL_OFF;

          for(int64_t y = 0; y < data->loc.y; ++y){
               const char* buffer_line = data->buffer->lines[y];
               int64_t line_len = strlen(buffer_line);

               for(int64_t x = 0; x < line_len; ++x){
                    syntax_is_python_docstring(buffer_line, x, &syntax->inside_docstring);
               }
          }

          // is our cursor on something we can match?
          syntax_calc_matching_pair(data, &syntax->matched_pair);
     } break;
     case SS_BEGINNING_OF_LINE:
     {
          syntax->inside_string = 0;
          syntax->current_color = S_NORMAL;
          syntax->current_color_left = 0;

          // lie to me !
          SyntaxHighlighterData_t data_copy = *data;
          data_copy.state = SS_CHARACTER;

          for(int64_t x = 0; x < data->loc.x; ++x){
               data_copy.loc = (Point_t){x, data->loc.y};
               syntax_highlight_python(&data_copy, user_data);
          }

          syntax_calc_trailing_whitespace(data, &syntax->trailing_whitespace_begin);

          if(data->loc.y == data->cursor.y){
               syntax->highlight.type = HL_CURRENT_LINE;
          }else{
               syntax->highlight.type = HL_OFF;
          }

          syntax->current_color = syntax_set_color(syntax->current_color, syntax->highlight.type);
     } break;
     case SS_CHARACTER:
     {
          syntax_determine_highlight(data, &syntax->highlight);
          syntax->current_color = syntax_set_color(syntax->current_color, syntax->highlight.type);

          const char* buffer_line = data->buffer->lines[data->loc.y];

          if(!syntax->inside_string && !syntax->inside_docstring){
               if(syntax->current_color_left){
                    syntax->current_color_left--;
               }else{
                    if((syntax->current_color_left = syntax_is_python_keyword(buffer_line, data->loc.x))){
                         syntax->current_color = syntax_set_color(S_KEYWORD, syntax->highlight.type);
                    }else if((syntax->current_color_left = syntax_is_python_control(buffer_line, data->loc.x))){
                         syntax->current_color = syntax_set_color(S_CONTROL, syntax->highlight.type);
                    }else if((syntax->current_color_left = syntax_is_c_caps_var(buffer_line, data->loc.x))){
                         syntax->current_color = syntax_set_color(S_CONSTANT, syntax->highlight.type);
                    }else if((syntax->current_color_left = syntax_is_c_constant_number(buffer_line, data->loc.x))){
                         syntax->current_color = syntax_set_color(S_CONSTANT, syntax->highlight.type);
                    }else if((syntax->current_color_left = syntax_is_python_comment(buffer_line, data->loc.x))){
                         syntax->current_color = syntax_set_color(S_COMMENT, syntax->highlight.type);
                    }else if(syntax->matched_pair.x >= 0){
                         if(ce_points_equal(data->loc, data->cursor) || ce_points_equal(data->loc, syntax->matched_pair)){
                              syntax->current_color = syntax_set_color(S_MATCHING_PARENS, syntax->highlight.type);
                         }
                    }
               }
          }

          if(syntax->current_color_left <= 0){
               bool inside_docstring = syntax->inside_docstring;
               syntax_is_python_docstring(buffer_line, data->loc.x, &syntax->inside_docstring);

               if(inside_docstring || syntax->inside_docstring){
                    syntax->current_color = syntax_set_color(S_STRING, syntax->highlight.type);
               }else{
                    bool inside_string = syntax->inside_string;
                    syntax_is_python_string(buffer_line, data->loc.x, &syntax->inside_string);

                    if(inside_string || syntax->inside_string){
                         syntax->current_color = syntax_set_color(S_STRING, syntax->highlight.type);
                    }else if(syntax->matched_pair.x >= 0){
                         if(ce_points_equal(data->loc, data->cursor) || ce_points_equal(data->loc, syntax->matched_pair)){
                              syntax->current_color = syntax_set_color(S_MATCHING_PARENS, syntax->highlight.type);
                         }else{
                              syntax->current_color = syntax_set_color(S_NORMAL, syntax->highlight.type);
                         }
                    }else{
                         syntax->current_color = syntax_set_color(S_NORMAL, syntax->highlight.type);
                    }
               }
          }

          if(data->loc.x >= syntax->trailing_whitespace_begin) syntax_set_color(S_TRAILING_WHITESPACE, syntax->highlight.type);
     } break;
     case SS_END_OF_LINE:
          if(data->cursor.y == data->loc.y && data->highlight_line_type == HLT_ENTIRE_LINE){
               syntax->current_color = syntax_set_color(S_NORMAL, HL_CURRENT_LINE);
               for(int64_t c = data->loc.x; c < data->bottom_right.x; ++c){
                    addch(' ');
               }
          }

          // highlight line numbers!
          syntax_set_color(S_LINE_NUMBERS, HL_OFF);
          break;
     }
}

int64_t syntax_is_config_keyword(const char* line, int64_t start_offset)
{
     static const char* keywords [] = {
          "true",
          "false",
     };

     static const int keyword_count = sizeof(keywords) / sizeof(keywords[0]);

     return match_keyword(line, start_offset, keywords, keyword_count);
}

void syntax_highlight_config(SyntaxHighlighterData_t* data, void* user_data)
{
     SyntaxConfig_t* syntax = user_data;

     switch(data->state){
     default:
          break;
     case SS_INITIALIZING:
     {
          syntax->inside_string = 0;
          syntax->current_color = 0;
          syntax->current_color_left = 0;
          syntax->highlight.type = HL_OFF;

          // is our cursor on something we can match?
          syntax_calc_matching_pair(data, &syntax->matched_pair);
     } break;
     case SS_BEGINNING_OF_LINE:
     {
          syntax->inside_string = 0;
          syntax->current_color = S_NORMAL;
          syntax->current_color_left = 0;

          // lie to me !
          SyntaxHighlighterData_t data_copy = *data;
          data_copy.state = SS_CHARACTER;

          for(int64_t x = 0; x < data->loc.x; ++x){
               data_copy.loc = (Point_t){x, data->loc.y};
               syntax_highlight_config(&data_copy, user_data);
          }

          if(data->loc.y == data->cursor.y){
               syntax->highlight.type = HL_CURRENT_LINE;
          }else{
               syntax->highlight.type = HL_OFF;
          }

          syntax_calc_trailing_whitespace(data, &syntax->trailing_whitespace_begin);

          syntax->current_color = syntax_set_color(syntax->current_color, syntax->highlight.type);
     } break;
     case SS_CHARACTER:
     {
          syntax_determine_highlight(data, &syntax->highlight);
          syntax->current_color = syntax_set_color(syntax->current_color, syntax->highlight.type);

          const char* buffer_line = data->buffer->lines[data->loc.y];

          if(!syntax->inside_string){
               if(syntax->current_color_left){
                    syntax->current_color_left--;
               }else{
                    if((syntax->current_color_left = syntax_is_config_keyword(buffer_line, data->loc.x))){
                         syntax->current_color = syntax_set_color(S_KEYWORD, syntax->highlight.type);
                    }else if((syntax->current_color_left = syntax_is_c_caps_var(buffer_line, data->loc.x))){
                         syntax->current_color = syntax_set_color(S_CONSTANT, syntax->highlight.type);
                    }else if((syntax->current_color_left = syntax_is_c_constant_number(buffer_line, data->loc.x))){
                         syntax->current_color = syntax_set_color(S_CONSTANT, syntax->highlight.type);
                    }else if((syntax->current_color_left = syntax_is_python_comment(buffer_line, data->loc.x))){
                         syntax->current_color = syntax_set_color(S_COMMENT, syntax->highlight.type);
                    }else if(syntax->matched_pair.x >= 0){
                         if(ce_points_equal(data->loc, data->cursor) || ce_points_equal(data->loc, syntax->matched_pair)){
                              syntax->current_color = syntax_set_color(S_MATCHING_PARENS, syntax->highlight.type);
                         }
                    }
               }
          }

          if(syntax->current_color_left <= 0){
               bool inside_string = syntax->inside_string;
               syntax_is_python_string(buffer_line, data->loc.x, &syntax->inside_string);

               if(inside_string || syntax->inside_string){
                    syntax->current_color = syntax_set_color(S_STRING, syntax->highlight.type);
               }else if(syntax->matched_pair.x >= 0){
                    if(ce_points_equal(data->loc, data->cursor) || ce_points_equal(data->loc, syntax->matched_pair)){
                         syntax->current_color = syntax_set_color(S_MATCHING_PARENS, syntax->highlight.type);
                    }else{
                         syntax->current_color = syntax_set_color(S_NORMAL, syntax->highlight.type);
                    }
               }else{
                    syntax->current_color = syntax_set_color(S_NORMAL, syntax->highlight.type);
               }
          }

          if(data->loc.x >= syntax->trailing_whitespace_begin) syntax_set_color(S_TRAILING_WHITESPACE, syntax->highlight.type);
     } break;
     case SS_END_OF_LINE:
          if(data->cursor.y == data->loc.y && data->highlight_line_type == HLT_ENTIRE_LINE){
               syntax->current_color = syntax_set_color(S_NORMAL, HL_CURRENT_LINE);
               for(int64_t c = data->loc.x; c < data->bottom_right.x; ++c){
                    addch(' ');
               }
          }

          // highlight line numbers!
          syntax_set_color(S_LINE_NUMBERS, HL_OFF);
          break;
     }
}

void syntax_highlight_plain(SyntaxHighlighterData_t* data, void* user_data)
{
     SyntaxPlain_t* syntax = user_data;

     switch(data->state){
     default:
          break;
     case SS_INITIALIZING:
     {
          syntax->highlight.type = HL_OFF;
     } break;
     case SS_BEGINNING_OF_LINE:
     {
          if(data->loc.y == data->cursor.y){
               syntax->highlight.type = HL_CURRENT_LINE;
          }else{
               syntax->highlight.type = HL_OFF;
          }

          syntax_set_color(S_NORMAL, syntax->highlight.type);
     } break;
     case SS_CHARACTER:
     {
          syntax_determine_highlight(data, &syntax->highlight);
          syntax_set_color(S_NORMAL, syntax->highlight.type);
     } break;
     case SS_END_OF_LINE:
          if(data->cursor.y == data->loc.y && data->highlight_line_type == HLT_ENTIRE_LINE){
               syntax_set_color(S_NORMAL, HL_CURRENT_LINE);
               for(int64_t c = data->loc.x; c < data->bottom_right.x; ++c){
                    addch(' ');
               }
          }

          // highlight line numbers!
          syntax_set_color(S_LINE_NUMBERS, HL_OFF);
          break;
     }
}

void syntax_highlight_diff(SyntaxHighlighterData_t* data, void* user_data)
{
     SyntaxDiff_t* syntax = user_data;

     switch(data->state){
     default:
          break;
     case SS_INITIALIZING:
     {
          syntax->highlight.type = HL_OFF;
     } break;
     case SS_BEGINNING_OF_LINE:
     {
          if(data->loc.y == data->cursor.y){
               syntax->highlight.type = HL_CURRENT_LINE;
          }else{
               syntax->highlight.type = HL_OFF;
          }

          const char* buffer_line = data->buffer->lines[data->loc.y];

          if(buffer_line[0] == '-'){
               syntax->current_color = syntax_set_color(S_DIFF_REMOVED, syntax->highlight.type);
          }else if(buffer_line[0] == '+'){
               syntax->current_color = syntax_set_color(S_DIFF_ADDED, syntax->highlight.type);
          }else if(buffer_line[0] == '@' && buffer_line[1] == '@'){
               syntax->current_color = syntax_set_color(S_DIFF_HEADER, syntax->highlight.type);
          }else{
               syntax->current_color = syntax_set_color(S_NORMAL, syntax->highlight.type);
          }
     } break;
     case SS_CHARACTER:
     {
          syntax_determine_highlight(data, &syntax->highlight);
          syntax_set_color(syntax->current_color, syntax->highlight.type);
     } break;
     case SS_END_OF_LINE:
          if(data->cursor.y == data->loc.y && data->highlight_line_type == HLT_ENTIRE_LINE){
               syntax_set_color(S_NORMAL, HL_CURRENT_LINE);
               for(int64_t c = data->loc.x; c < data->bottom_right.x; ++c){
                    addch(' ');
               }
          }

          // highlight line numbers!
          syntax_set_color(S_LINE_NUMBERS, HL_OFF);
          break;
     }
}
