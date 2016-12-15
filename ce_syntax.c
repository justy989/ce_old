#include "ce_syntax.h"

#include <ctype.h>

static int iscapsvarchar(int c)
{
     return isupper(c) || c == '_' || isdigit(c);
}

int64_t syntax_is_c_constant_number(const char* line, int64_t start_offset)
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

int64_t syntax_is_c_fullpath(const char* line, int64_t start_offset)
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
     } else {
          attron(COLOR_PAIR(syntax));
     }

     return syntax;
}

CommentType_t syntax_is_c_comment(const char* line, int64_t start_offset, bool inside_string)
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

void syntax_is_c_string_literal(const char* line, int64_t start_offset, int64_t line_len, bool* inside_string, char* last_quote_char)
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

int64_t syntax_is_c_keyword(const char* line, int64_t start_offset)
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

int64_t syntax_is_c_control(const char* line, int64_t start_offset)
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

int64_t syntax_is_c_typename(const char* line, int64_t start_offset)
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

int64_t syntax_is_c_preprocessor(const char* line, int64_t start_offset)
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

void syntax_highlight_c(const Buffer_t* buffer, Point_t top_left, Point_t bottom_right, Point_t cursor, Point_t loc,
                        regex_t* highlight_regex, HighlightLineType_t highlight_line_type, void* user_data)
{
     (void)(cursor);
     (void)(highlight_line_type);

     SyntaxC_t* syntax = user_data;

     bool diff_header = buffer->lines[loc.y][0] == '@' && buffer->lines[loc.y][1] == '@';
     if(diff_header) syntax->diff_seen_header = true;
     bool diff_add = syntax->diff_seen_header && buffer->lines[loc.y][0] == '+';
     bool diff_remove = syntax->diff_seen_header && buffer->lines[loc.y][0] == '-';

     // init if we haven't initted already
     if(!syntax->started){
          // look for any diff headers earlier in the file
          for(int64_t i = loc.y - 1; i >= 0; --i){
               if(buffer->lines[i][0] == '@' && buffer->lines[i][1] == '@'){
                    syntax->diff_seen_header = true;
                    break;
               }
          }

          // is our cursor on something we can match?
          syntax->matched_pair = (Point_t){-1, -1};
          if(ce_point_on_buffer(buffer, cursor)){
               char ch = 0;
               ch = ce_get_char_raw(buffer, cursor);
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
                    syntax->matched_pair = cursor;
                    ce_move_cursor_to_matching_pair(buffer, &syntax->matched_pair, ch);
                    break;
               }
          }

          syntax->inside_multiline_comment = false;
          for(int64_t i = loc.y; i <= bottom_right.y; ++i) {
               if(!buffer->lines[i][0]) continue;
               const char* buffer_line = buffer->lines[i];
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

          syntax->started = true;
     }

     // is this a new line?
     if(loc.x == top_left.x){
          int pre_line_color = 0;

          syntax->inside_comment = false;
          syntax->inside_string = false;
          syntax->last_quote_char = 0;

          syntax->current_color = 0;
          syntax->current_color_left = 0;

          if(highlight_regex){
               int regex_rc = regexec(highlight_regex, buffer->lines[loc.y], 1, syntax->regex_matches, 0);
               if(regex_rc == 0) syntax->chars_til_highlighted_word = syntax->regex_matches[0].rm_so;
          }

          const char* buffer_line = buffer->lines[loc.y];
          int64_t line_length = strlen(buffer_line);
          syntax->begin_trailing_whitespace = line_length;

          // NOTE: pre-pass to find trailing whitespace if it exists
          if(cursor.y != loc.y){
               for(int64_t c = line_length - 1; c >= 0; --c){
                    if(isblank(buffer_line[c])){
                         syntax->begin_trailing_whitespace--;
                    }else{
                         break;
                    }
               }
          }

          // NOTE: pre-pass check for comments and strings out of view
          for(int64_t c = 0; c < loc.x; ++c){
               CommentType_t comment_type = syntax_is_c_comment(buffer_line, c, syntax->inside_string);
               switch(comment_type){
               default:
                    break;
               case CT_SINGLE_LINE:
                    syntax->inside_comment = true;
                    break;
               case CT_BEGIN_MULTILINE:
                    if(!syntax->inside_comment) syntax->inside_multiline_comment = true;
                    break;
               case CT_END_MULTILINE:
                    syntax->inside_multiline_comment = false;
                    break;
               }

               if(highlight_regex && syntax->chars_til_highlighted_word == 0){
                    syntax->highlighting_left = syntax->regex_matches[0].rm_eo - syntax->regex_matches[0].rm_so;
                    syntax->highlight_type = HL_VISUAL;
               }else if(syntax->highlight_type){
                    syntax->highlighting_left--;

                    if(!syntax->highlighting_left){
                         if(highlight_regex){
                              int regex_rc = regexec(highlight_regex, buffer->lines[loc.y], 1, syntax->regex_matches, 0);
                              if(regex_rc == 0) syntax->chars_til_highlighted_word = syntax->regex_matches[0].rm_so;
                         }

                         if(syntax->chars_til_highlighted_word == 0){
                              syntax->highlighting_left = syntax->regex_matches[0].rm_eo - syntax->regex_matches[0].rm_so;
                              syntax->highlight_type = HL_VISUAL;
                         }else{
                              syntax->highlight_type = HL_OFF;
                         }
                    }
               }

               syntax_is_c_string_literal(buffer_line, c, line_length, &syntax->inside_string, &syntax->last_quote_char);

               // subtract from what is left of the keyword if we found a keyword earlier
               if(syntax->current_color_left){
                    syntax->current_color_left--;
               }else{
                    if(!syntax->inside_string){
                         int64_t keyword_left = 0;

                         if(!keyword_left){
                              keyword_left = syntax_is_c_constant_number(buffer_line, c);
                              if(keyword_left){
                                   syntax->current_color_left = keyword_left;
                                   pre_line_color = S_CONSTANT_NUMBER;
                              }
                         }

                         if(!keyword_left){
                              keyword_left = syntax_is_c_caps_var(buffer_line, c);
                              if(keyword_left){
                                   syntax->current_color_left = keyword_left;
                                   pre_line_color = S_CONSTANT;
                              }
                         }

                         if(!syntax->inside_comment && !syntax->inside_multiline_comment){
                              if(!keyword_left){
                                   keyword_left = syntax_is_c_control(buffer_line, c);
                                   if(keyword_left){
                                        syntax->current_color_left = keyword_left;
                                        pre_line_color = S_CONTROL;
                                   }
                              }

                              if(!keyword_left){
                                   keyword_left = syntax_is_c_typename(buffer_line, c);
                                   if(keyword_left){
                                        syntax->current_color_left = keyword_left;
                                        pre_line_color = S_TYPE;
                                   }
                              }

                              if(!keyword_left){
                                   keyword_left = syntax_is_c_keyword(buffer_line, c);
                                   if(keyword_left){
                                        syntax->current_color_left = keyword_left;
                                        pre_line_color = S_KEYWORD;
                                   }
                              }

                              if(!keyword_left){
                                   keyword_left = syntax_is_c_preprocessor(buffer_line, c);
                                   if(keyword_left){
                                        syntax->current_color_left = keyword_left;
                                        pre_line_color = S_PREPROCESSOR;
                                   }
                              }
                         }

                         if(!keyword_left){
                              keyword_left = syntax_is_c_fullpath(buffer_line, c);
                              if(keyword_left){
                                   syntax->current_color_left = keyword_left;
                                   pre_line_color = S_FILEPATH;
                              }
                         }
                    }
               }

               syntax->chars_til_highlighted_word--;
          }

          syntax->current_color = syntax_set_color(S_NORMAL, syntax->highlight_type);

          if(syntax->inside_comment || syntax->inside_multiline_comment){
               syntax->current_color = syntax_set_color(S_COMMENT, syntax->highlight_type);
          }else if(syntax->inside_string){
               syntax->current_color = syntax_set_color(S_STRING, syntax->highlight_type);
          }else if(diff_add){
               syntax->current_color = syntax_set_color(S_DIFF_ADDED, syntax->highlight_type);
          }else if(diff_remove){
               syntax->current_color = syntax_set_color(S_DIFF_REMOVED, syntax->highlight_type);
          }else if(diff_header){
               syntax->current_color = syntax_set_color(S_DIFF_HEADER, syntax->highlight_type);
          }else if(syntax->current_color_left){
               syntax->current_color = syntax_set_color(pre_line_color, syntax->highlight_type);
          }
     }

     // highlight current character
     if(ce_point_in_range(loc, buffer->highlight_start, buffer->highlight_end)){
          syntax->highlight_type = HL_VISUAL;
          syntax_set_color(syntax->current_color, syntax->highlight_type);
     }else{
          if(highlight_regex && syntax->chars_til_highlighted_word == 0){
               syntax->highlighting_left = syntax->regex_matches[0].rm_eo - syntax->regex_matches[0].rm_so;
               syntax->highlight_type = HL_VISUAL;
               syntax_set_color(syntax->current_color, syntax->highlight_type);
          }else if(syntax->highlight_type){
               if(syntax->highlighting_left){
                    syntax->highlighting_left--;

                    if(!syntax->highlighting_left){

                         if(highlight_regex){
                              int regex_rc = regexec(highlight_regex, buffer->lines[loc.y] + loc.x, 1, syntax->regex_matches, 0);
                              if(regex_rc == 0){
                                   syntax->chars_til_highlighted_word = syntax->regex_matches[0].rm_so;
                              }
                         }

                         if(syntax->chars_til_highlighted_word == 0){
                              syntax->highlighting_left = syntax->regex_matches[0].rm_eo - syntax->regex_matches[0].rm_so;
                              syntax->highlight_type = HL_VISUAL;
                         }else if(cursor.y == loc.y && highlight_line_type != HLT_NONE){
                              syntax->highlight_type = HL_CURRENT_LINE;
                         }else{
                              syntax->highlight_type = HL_OFF;
                         }
                         syntax_set_color(syntax->current_color, syntax->highlight_type);
                    }
               }else if(cursor.y == loc.y && highlight_line_type != HLT_NONE){
                    syntax->highlight_type = HL_CURRENT_LINE;
                    syntax_set_color(syntax->current_color, syntax->highlight_type);
               }else{
                    syntax->highlight_type = HL_OFF;
                    syntax_set_color(syntax->current_color, syntax->highlight_type);
               }
          }else if(cursor.y == loc.y && highlight_line_type != HLT_NONE){
               syntax->highlight_type = HL_CURRENT_LINE;
               syntax_set_color(syntax->current_color, syntax->highlight_type);
          }
     }

     char* line_to_print = buffer->lines[loc.y] + top_left.x;
     int64_t print_line_length = strlen(line_to_print); // TODO: clamp by view width

     // syntax highlighting
     if(syntax->current_color_left == 0){
          if(!syntax->inside_string){
               syntax->current_color_left = syntax_is_c_constant_number(line_to_print, loc.x);
               if(syntax->current_color_left){
                    syntax->current_color = syntax_set_color(S_CONSTANT_NUMBER, syntax->highlight_type);
               }

               if(!syntax->current_color_left){
                    syntax->current_color_left = syntax_is_c_caps_var(line_to_print, loc.x);
                    if(syntax->current_color_left){
                         syntax->current_color = syntax_set_color(S_CONSTANT, syntax->highlight_type);
                    }
               }

               if(!syntax->inside_comment && !syntax->inside_multiline_comment){
                    if(!syntax->current_color_left){
                         syntax->current_color_left = syntax_is_c_control(line_to_print, loc.x);
                         if(syntax->current_color_left){
                              syntax->current_color = syntax_set_color(S_CONTROL, syntax->highlight_type);
                         }
                    }

                    if(!syntax->current_color_left){
                         syntax->current_color_left = syntax_is_c_typename(line_to_print, loc.x);
                         if(syntax->current_color_left){
                              syntax->current_color = syntax_set_color(S_TYPE, syntax->highlight_type);
                         }
                    }

                    if(!syntax->current_color_left){
                         syntax->current_color_left = syntax_is_c_keyword(line_to_print, loc.x);
                         if(syntax->current_color_left){
                              syntax->current_color = syntax_set_color(S_KEYWORD, syntax->highlight_type);
                         }
                    }

                    if(!syntax->current_color_left){
                         syntax->current_color_left = syntax_is_c_preprocessor(line_to_print, loc.x);
                         if(syntax->current_color_left){
                              syntax->current_color = syntax_set_color(S_PREPROCESSOR, syntax->highlight_type);
                         }
                    }

                    if(!syntax->current_color_left && syntax->matched_pair.x >= 0){
                         if(ce_points_equal(loc, cursor) || ce_points_equal(loc, syntax->matched_pair)){
                              syntax->current_color = syntax_set_color(S_MATCHING_PARENS, syntax->highlight_type);
                         }else if(syntax->current_color == S_MATCHING_PARENS){
                              syntax->current_color = syntax_set_color(S_NORMAL, syntax->highlight_type);
                         }
                    }
               }

               if(!syntax->current_color_left){
                    syntax->current_color_left = syntax_is_c_fullpath(line_to_print, loc.x);
                    if(syntax->current_color_left){
                         syntax->current_color = syntax_set_color(S_FILEPATH, syntax->highlight_type);
                    }
               }
          }

          CommentType_t comment_type = syntax_is_c_comment(line_to_print, loc.x, syntax->inside_string);
          switch(comment_type){
          default:
               break;
          case CT_SINGLE_LINE:
               syntax->inside_comment = true;
               syntax->current_color = syntax_set_color(S_COMMENT, syntax->highlight_type);
               break;
          case CT_BEGIN_MULTILINE:
               if(!syntax->inside_comment){
                    syntax->inside_multiline_comment = true;
                    syntax->current_color = syntax_set_color(S_COMMENT, syntax->highlight_type);
               }
               break;
          case CT_END_MULTILINE:
               syntax->inside_multiline_comment = false;
               syntax->current_color_left = 1;
               break;
          }

          bool pre_quote_check = syntax->inside_string;
          syntax_is_c_string_literal(line_to_print, loc.x, print_line_length, &syntax->inside_string, &syntax->last_quote_char);

          // if inside_string has changed, update the color
          if(pre_quote_check != syntax->inside_string){
               if(syntax->inside_string) syntax->current_color = syntax_set_color(S_STRING, syntax->highlight_type);
               else syntax->current_color_left = 1;
          }
     }else{
          syntax->current_color_left--;
          if(syntax->current_color_left == 0){
               syntax->current_color = syntax_set_color(S_NORMAL, syntax->highlight_type);

               if(syntax->inside_comment || syntax->inside_multiline_comment){
                    syntax->current_color = syntax_set_color(S_COMMENT, syntax->highlight_type);
               }else if(syntax->inside_string){
                    syntax->current_color = syntax_set_color(S_STRING, syntax->highlight_type);
               }else if(diff_add){
                    syntax->current_color = syntax_set_color(S_DIFF_ADDED, syntax->highlight_type);
               }else if(diff_remove){
                    syntax->current_color = syntax_set_color(S_DIFF_REMOVED, syntax->highlight_type);
               }else if(diff_header){
                    syntax->current_color = syntax_set_color(S_DIFF_HEADER, syntax->highlight_type);
               }else if(syntax->matched_pair.x >= 0){
                    if(ce_points_equal(loc, cursor) || ce_points_equal(loc, syntax->matched_pair)){
                         syntax->current_color = syntax_set_color(S_MATCHING_PARENS, syntax->highlight_type);
                    }
               }
          }
     }

     if(loc.x >= syntax->begin_trailing_whitespace){
          syntax->current_color = syntax_set_color(S_TRAILING_WHITESPACE, syntax->highlight_type);
     }

     syntax->chars_til_highlighted_word--;
}
