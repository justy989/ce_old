#ifndef CE_SYNTAX_H
#define CE_SYNTAX_H

#include "ce.h"

typedef enum {
     HL_OFF,
     HL_VISUAL,
     HL_CURRENT_LINE,
} HighlightType_t;

typedef struct{
     bool started;

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
} SyntaxC_t;

void syntax_highlight_c(const Buffer_t* buffer, Point_t top_left, Point_t bottom_right, Point_t cursor, Point_t loc,
                        regex_t* highlight_regex, HighlightLineType_t highlight_line_type, void* user_data);

#endif
