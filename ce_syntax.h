#ifndef CE_SYNTAX_H
#define CE_SYNTAX_H

#include "ce.h"

typedef enum {
     S_NORMAL = 1,
     S_KEYWORD,
     S_TYPE,
     S_CONTROL,
     S_COMMENT,
     S_STRING,
     S_CONSTANT,
     S_CONSTANT_NUMBER,
     S_MATCHING_PARENS,
     S_PREPROCESSOR,
     S_FILEPATH,
     S_DIFF_ADDED,
     S_DIFF_REMOVED,
     S_DIFF_HEADER,

     // NOTE: highlighted and current line groups must be in the same order as base syntax enums
     S_NORMAL_HIGHLIGHTED,
     S_KEYWORD_HIGHLIGHTED,
     S_TYPE_HIGHLIGHTED,
     S_CONTROL_HIGHLIGHTED,
     S_COMMENT_HIGHLIGHTED,
     S_STRING_HIGHLIGHTED,
     S_CONSTANT_HIGHLIGHTED,
     S_CONSTANT_NUMBER_HIGHLIGHTED,
     S_MATCHING_PARENS_HIGHLIGHTED,
     S_PREPROCESSOR_HIGHLIGHTED,
     S_FILEPATH_HIGHLIGHTED,
     S_DIFF_ADDED_HIGHLIGHTED,
     S_DIFF_REMOVED_HIGHLIGHTED,
     S_DIFF_HEADER_HIGHLIGHTED,

     S_NORMAL_CURRENT_LINE,
     S_KEYWORD_CURRENT_LINE,
     S_TYPE_CURRENT_LINE,
     S_CONTROL_CURRENT_LINE,
     S_COMMENT_CURRENT_LINE,
     S_STRING_CURRENT_LINE,
     S_CONSTANT_CURRENT_LINE,
     S_CONSTANT_NUMBER_CURRENT_LINE,
     S_MATCHING_PARENS_CURRENT_LINE,
     S_PREPROCESSOR_CURRENT_LINE,
     S_FILEPATH_CURRENT_LINE,
     S_DIFF_ADDED_CURRENT_LINE,
     S_DIFF_REMOVED_CURRENT_LINE,
     S_DIFF_HEADER_CURRENT_LINE,

     S_LINE_NUMBERS,

     S_TRAILING_WHITESPACE,

     S_BORDERS,

     S_TAB_NAME,
     S_CURRENT_TAB_NAME,

     S_VIEW_STATUS,
     S_INPUT_STATUS,

     S_AUTO_COMPLETE,
} Syntax_t;


typedef enum{
     HL_OFF,
     HL_VISUAL,
     HL_CURRENT_LINE,
}HighlightType_t;

typedef struct{
     bool inside_multiline_comment;
     bool inside_comment;
     bool inside_string;
     char last_quote_char;

     bool diff_seen_header;

     HighlightType_t highlight_type;
     int64_t chars_til_highlighted_word;
     int64_t highlighting_left;

     int current_color;
     int64_t current_color_left;

     int64_t begin_trailing_whitespace;

     Point_t matched_pair;
     regmatch_t regex_matches[1];
}SyntaxC_t;

void syntax_highlight_c(const Buffer_t* buffer, Point_t top_left, Point_t bottom_right, Point_t cursor, Point_t loc,
                        const regex_t* highlight_regex, LineNumberType_t line_number_type, HighlightLineType_t highlight_line_type,
                        void* user_data, bool first_call);

#endif
