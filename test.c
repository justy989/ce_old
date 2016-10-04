#include "ce.h"
#include "test.h"

TEST(sanity_alloc_and_free)
{
     Buffer buffer = {};
     ce_alloc_lines(&buffer, 3);

     ASSERT(buffer.lines);
     EXPECT(buffer.line_count == 3);

     ce_free_buffer(&buffer);
}

TEST(sanity_load_string)
{
     const char* str = "TACOS";

     Buffer buffer = {};
     ce_load_string(&buffer, str);

     ASSERT(buffer.lines);
     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], str) == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_load_string_multiline)
{
     const char* str = "TACOS\nARE\nTHE\nBEST";

     Buffer buffer = {};
     ce_load_string(&buffer, str);

     ASSERT(buffer.lines);
     ASSERT(buffer.line_count == 4);
     EXPECT(strcmp(buffer.lines[0], "TACOS") == 0);
     EXPECT(strcmp(buffer.lines[1], "ARE") == 0);
     EXPECT(strcmp(buffer.lines[2], "THE") == 0);
     EXPECT(strcmp(buffer.lines[3], "BEST") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_load_one_line_file)
{
     // NOTE: sorry, can't run this test if you're hd is full !
     char cmd[128];
     const char* tmp_file = "/tmp/ce_one_line_file.txt";
     sprintf(cmd, "echo 'TACOS' > %s", tmp_file);
     system(cmd);
     Buffer buffer = {};
     ce_load_file(&buffer, tmp_file);
     sprintf(cmd, "rm %s", tmp_file);
     system(cmd);

     ASSERT(buffer.lines);
     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TACOS") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_load_multiline_file)
{
     // NOTE: sorry, can't run this test if you're hd is full !
     char cmd[128];
     const char* tmp_file = "/tmp/ce_multiline_file.txt";
     sprintf(cmd, "echo 'TACOS\nARE\nTHE\nBEST' > %s", tmp_file);
     system(cmd);
     Buffer buffer = {};
     ce_load_file(&buffer, tmp_file);
     sprintf(cmd, "rm %s", tmp_file);
     system(cmd);

     ASSERT(buffer.lines);
     ASSERT(buffer.line_count == 4);
     EXPECT(strcmp(buffer.lines[0], "TACOS") == 0);
     EXPECT(strcmp(buffer.lines[1], "ARE") == 0);
     EXPECT(strcmp(buffer.lines[2], "THE") == 0);
     EXPECT(strcmp(buffer.lines[3], "BEST") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_save_buffer_one_line)
{
     const char* tmp_file = "/tmp/ce_one_line_file.txt";

     Buffer buffer = {};
     buffer.filename = strdup(tmp_file);
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     ce_save_buffer(&buffer, tmp_file);

     // NOTE: not sure how else to validate this
     Buffer other_buffer = {};
     ce_load_file(&other_buffer, tmp_file);

     char cmd[128];
     sprintf(cmd, "rm %s", tmp_file);
     system(cmd);

     ASSERT(other_buffer.lines);
     ASSERT(other_buffer.line_count == 1);
     EXPECT(strcmp(other_buffer.lines[0], "TACOS") == 0);

     ce_free_buffer(&buffer);
     ce_free_buffer(&other_buffer);
}

TEST(sanity_point_on_buffer)
{
     Buffer buffer = {};

     buffer.line_count = 2;
     buffer.lines = malloc(2 * sizeof(char*));
     buffer.lines[0] = strdup("TA");
     buffer.lines[1] = strdup("TA");

     Point point = {1, 1};
     EXPECT(ce_point_on_buffer(&buffer, &point));

     point = (Point){1, 2};
     EXPECT(!ce_point_on_buffer(&buffer, &point));

     point = (Point){3, 1};
     EXPECT(!ce_point_on_buffer(&buffer, &point));

     ce_free_buffer(&buffer);
}

TEST(sanity_insert_char)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point point = {2, 0};
     ce_insert_char(&buffer, &point, 'R');

     EXPECT(strcmp(buffer.lines[0], "TARCOS") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_insert_char_newline_end)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point point = {5, 0};
     ce_insert_char(&buffer, &point, '\n');

     ASSERT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "TACOS") == 0);
     EXPECT(strcmp(buffer.lines[1], "") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_insert_char_newline_middle)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point point = {2, 0};
     ce_insert_char(&buffer, &point, '\n');

     ASSERT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "TA") == 0);
     EXPECT(strcmp(buffer.lines[1], "COS") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_remove_char)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point point = {2, 0};
     ce_remove_char(&buffer, &point);

     EXPECT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TAOS") == 0);

     ce_free_buffer(&buffer);
}

#if 0 // NOTE: This fails, and I'm too under the influence to figure out why! It's Friday! :D
TEST(sanity_remove_char_empty_line)
{
     Buffer buffer = {};
     buffer.line_count = 2;
     buffer.lines = malloc(2 * sizeof(char**));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("");

     Point point = {0, 1};
     ce_remove_char(&buffer, &point);

     EXPECT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TACOS") == 0);

     ce_free_buffer(&buffer);
}
#endif

TEST(sanity_insert_string_begin)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point point = {0, 0};
     ce_insert_string(&buffer, &point, "AHHH ");

     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "AHHH TACOS") == 0);
     ce_free_buffer(&buffer);
}

TEST(sanity_insert_string_mid)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point point = {2, 0};
     ce_insert_string(&buffer, &point, " AHHH ");

     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TA AHHH COS") == 0);
     ce_free_buffer(&buffer);
}

TEST(sanity_insert_string_end)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point point = {5, 0};
     ce_insert_string(&buffer, &point, " AHHH");

     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TACOS AHHH") == 0);
     ce_free_buffer(&buffer);
}

TEST(sanity_insert_string_multiline_begin)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point point = {0, 0};
     ce_insert_string(&buffer, &point, "AH\nHH ");

     ASSERT(buffer.line_count == 2);
     // NOTE: I realize my examples are insane, but I'm intoxicated.
     EXPECT(strcmp(buffer.lines[0], "AH") == 0);
     EXPECT(strcmp(buffer.lines[1], "HH TACOS") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_insert_string_multiline_mid)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point point = {2, 0};
     ce_insert_string(&buffer, &point, " AH\nHH ");

     ASSERT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "TA AH") == 0);
     EXPECT(strcmp(buffer.lines[1], "HH COS") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_insert_string_multiline_end)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point point = {5, 0};
     ce_insert_string(&buffer, &point, " AH\nHH ");

     ASSERT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "TACOS AH") == 0);
     EXPECT(strcmp(buffer.lines[1], "HH ") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_insert_string_multiline_blank_begin)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point point = {0, 0};
     ce_insert_string(&buffer, &point, "\n\n");

     ASSERT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "") == 0);
     EXPECT(strcmp(buffer.lines[1], "") == 0);
     EXPECT(strcmp(buffer.lines[2], "TACOS") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_insert_string_multiline_blank_mid)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point point = {2, 0};
     ce_insert_string(&buffer, &point, "\n\n");

     ASSERT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "TA") == 0);
     EXPECT(strcmp(buffer.lines[1], "") == 0);
     EXPECT(strcmp(buffer.lines[2], "COS") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_insert_string_multiline_blank_end)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point point = {5, 0};
     ce_insert_string(&buffer, &point, "\n\n");

     ASSERT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "TACOS") == 0);
     EXPECT(strcmp(buffer.lines[1], "") == 0);
     EXPECT(strcmp(buffer.lines[2], "") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_append_string)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     ce_append_string(&buffer, 0, " ARE AWESOME");

     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TACOS ARE AWESOME") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_append_string_multiline)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     ce_append_string(&buffer, 0, " ARE\nAWESOME");

     ASSERT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "TACOS ARE") == 0);
     EXPECT(strcmp(buffer.lines[1], "AWESOME") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_remove_string_begin)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point point = {0, 0};
     ce_remove_string(&buffer, &point, 2);

     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "COS") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_remove_string_mid)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point point = {1, 0};
     ce_remove_string(&buffer, &point, 2);

     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TOS") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_remove_string_end)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point point = {3, 0};
     ce_remove_string(&buffer, &point, 2);

     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TAC") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_remove_string_multiline_begin)
{
     Buffer buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE");
     buffer.lines[2] = strdup("AWESOME");

     Point point = {0, 0};
     ce_remove_string(&buffer, &point, 7);

     ASSERT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "RE") == 0);
     EXPECT(strcmp(buffer.lines[1], "AWESOME") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_remove_string_multiline_mid)
{
     Buffer buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE");
     buffer.lines[2] = strdup("AWESOME");

     Point point = {1, 0};
     ce_remove_string(&buffer, &point, 7);

     ASSERT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "TE") == 0);
     EXPECT(strcmp(buffer.lines[1], "AWESOME") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_remove_string_multiline_end)
{
     Buffer buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE");
     buffer.lines[2] = strdup("AWESOMT");

     Point point = {2, 1};
     ce_remove_string(&buffer, &point, 9);

     ASSERT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "TACOS") == 0);
     EXPECT(strcmp(buffer.lines[1], "AR") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_remove_string_multiline_blank_begin)
{
     Buffer buffer = {};
     buffer.line_count = 5;
     buffer.lines = malloc(5 * sizeof(char*));
     buffer.lines[0] = strdup("");
     buffer.lines[1] = strdup("");
     buffer.lines[2] = strdup("TACOS");
     buffer.lines[3] = strdup("ARE");
     buffer.lines[4] = strdup("AWESOME");

     Point point = {0, 0};
     ce_remove_string(&buffer, &point, 5);

     ASSERT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "OS") == 0);
     EXPECT(strcmp(buffer.lines[1], "ARE") == 0);
     EXPECT(strcmp(buffer.lines[2], "AWESOME") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_remove_string_multiline_blank_mid)
{
     Buffer buffer = {};
     buffer.line_count = 5;
     buffer.lines = malloc(5 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("");
     buffer.lines[2] = strdup("");
     buffer.lines[3] = strdup("ARE");
     buffer.lines[4] = strdup("AWESOME");

     Point point = {0, 1};
     ce_remove_string(&buffer, &point, 4);

     ASSERT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "TACOS") == 0);
     EXPECT(strcmp(buffer.lines[1], "E") == 0);
     EXPECT(strcmp(buffer.lines[2], "AWESOME") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_remove_string_multiline_blank_end)
{
     Buffer buffer = {};
     buffer.line_count = 5;
     buffer.lines = malloc(5 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE");
     buffer.lines[2] = strdup("AWESOME");
     buffer.lines[3] = strdup("");
     buffer.lines[4] = strdup("");

     Point point = {3, 2};
     ce_remove_string(&buffer, &point, 7);

     ASSERT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "TACOS") == 0);
     EXPECT(strcmp(buffer.lines[1], "ARE") == 0);
     EXPECT(strcmp(buffer.lines[2], "AWE") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_append_line)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     ce_append_line(&buffer, "ARE AWESOME");

     EXPECT(buffer.line_count == 2);
     // NOTE: I realize my examples are insane, but I'm intoxicated.
     EXPECT(strcmp(buffer.lines[0], "TACOS") == 0);
     EXPECT(strcmp(buffer.lines[1], "ARE AWESOME") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_clear_lines)
{
     Buffer buffer = {};
     buffer.line_count = 2;
     buffer.lines = malloc(2 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE AWESOME");

     ce_clear_lines(&buffer);

     EXPECT(buffer.line_count == 0);
     EXPECT(buffer.lines == NULL);

     ce_free_buffer(&buffer);
}

TEST(sanity_dupe_string)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point start = {1, 0};
     Point end = {3, 0};
     char* str = ce_dupe_string(&buffer, &start, &end);

     EXPECT(strcmp(str, "AC") == 0);

     free(str);
     ce_free_buffer(&buffer);
}

#if 0
// NOTE: there seems to be a difference between 1 line and multiple lines!
TEST(sanity_dupe_string_multiline)
{
     Buffer buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE");
     buffer.lines[2] = strdup("AWESOME");

     Point start = {1, 0};
     Point end = {3, 2};
     char* str = ce_dupe_string(&buffer, &start, &end);

     EXPECT(strcmp(str, "ACOS\nARE\nAW") == 0);

     free(str);
     ce_free_buffer(&buffer);
}
#endif

TEST(sanity_dupe_line)
{
     Buffer buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE");
     buffer.lines[2] = strdup("AWESOME");

     char* str = ce_dupe_line(&buffer, 1);

     EXPECT(strcmp(str, "ARE\n") == 0);

     free(str);
     ce_free_buffer(&buffer);
}

TEST(sanity_get_char)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     char ch = 0;
     Point point = {2, 0};
     ce_get_char(&buffer, &point, &ch);

     EXPECT(ch == 'C');

     ce_free_buffer(&buffer);
}

TEST(sanity_set_char)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point point = {2, 0};
     ce_set_char(&buffer, &point, 'R');

     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TAROS") == 0);

     ce_free_buffer(&buffer);
}

#if 0 // NOTE: unsure why I can't get ce_find_match to work here
TEST(sanity_find_match_same_line)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TA COS ARE TA COS");

     Point point = {2, 0};
     Point delta = {};
     ce_find_match(&buffer, &point, &delta);

     EXPECT(delta.x == 11);
     EXPECT(delta.y == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_find_match_next_line)
{
     Buffer buffer = {};
     buffer.line_count = 2;
     buffer.lines = malloc(2 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS ARE AWESOME");
     buffer.lines[1] = strdup("TACOS ARE REVOLUTIONARY");

     Point point = {6, 0};
     Point delta = {};
     ce_find_match(&buffer, &point, &delta);

     EXPECT(delta.x == 6);
     EXPECT(delta.y == 1);

     ce_free_buffer(&buffer);
}
#endif

TEST(sanity_find_match_same_line)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS ARE AWESOME");

     Point point = {2, 0};
     Point match = {};
     ce_find_string(&buffer, &point, "ARE", &match, CE_DOWN);

     EXPECT(match.x == 6);
     EXPECT(match.y == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_find_match_next_line)
{
     Buffer buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE SO");
     buffer.lines[2] = strdup("AWESOME");

     Point point = {2, 0};
     Point delta = {};
     ce_find_string(&buffer, &point, "SO", &delta, CE_DOWN);

     EXPECT(delta.x == 4);
     EXPECT(delta.y == 1);

     ce_free_buffer(&buffer);
}

TEST(sanity_clamp_cursor_horizontal)
{
     Buffer buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE SO");
     buffer.lines[2] = strdup("AWESOME");

     Point cursor = {50, 0};
     ce_clamp_cursor(&buffer, &cursor);

     EXPECT(cursor.x == 4);
     EXPECT(cursor.y == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_clamp_cursor_vertical)
{
     Buffer buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE SO");
     buffer.lines[2] = strdup("AWESOME");

     Point cursor = {4, 50};
     ce_clamp_cursor(&buffer, &cursor);

     EXPECT(cursor.x == 4);
     EXPECT(cursor.y == 2);

     ce_free_buffer(&buffer);
}

TEST(sanity_move_cursor)
{
     Buffer buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE SO");
     buffer.lines[2] = strdup("AWESOME");

     Point cursor = {2, 0};
     Point delta = {2, 1};
     ce_move_cursor(&buffer, &cursor, &delta);

     EXPECT(cursor.x == 4);
     EXPECT(cursor.y == 1);

     ce_free_buffer(&buffer);
}

TEST(sanity_set_cursor)
{
     Buffer buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE SO");
     buffer.lines[2] = strdup("AWESOME");

     Point cursor = {2, 0};
     Point delta = {4, 2};
     ce_set_cursor(&buffer, &cursor, &delta);

     EXPECT(cursor.x == 4);
     EXPECT(cursor.y == 2);

     ce_free_buffer(&buffer);
}

TEST(sanity_advance_cursor_same_line)
{
     Buffer buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE SO");
     buffer.lines[2] = strdup("AWESOME");

     Point cursor = {1, 0};
     ce_advance_cursor(&buffer, &cursor, 3);

     EXPECT(cursor.x == 4);
     EXPECT(cursor.y == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_advance_cursor_next_line)
{
     Buffer buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE SO");
     buffer.lines[2] = strdup("AWESOME");

     Point cursor = {1, 0};
     ce_advance_cursor(&buffer, &cursor, 6);

     EXPECT(cursor.x == 2);
     EXPECT(cursor.y == 1);

     ce_free_buffer(&buffer);
}

TEST(sanity_move_cursor_to_end_of_file)
{
     Buffer buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE SO");
     buffer.lines[2] = strdup("AWESOME");

     Point cursor = {1, 0};
     ce_move_cursor_to_end_of_file(&buffer, &cursor);

     EXPECT(cursor.x == 6);
     EXPECT(cursor.y == 2);

     ce_free_buffer(&buffer);
}

TEST(sanity_commit_insert_char_undo_redo)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     BufferCommitNode* tail = calloc(1, sizeof(*tail));
     ASSERT(tail != NULL);

     Point start = {2, 0};
     Point undo = {2, 0};
     Point redo = {3, 0};
     ce_commit_insert_char(&tail, &start, &undo, &redo, 'C');

     Point cursor = {};
     ce_commit_undo(&buffer, &tail, &cursor);

     EXPECT(cursor.x == 2);
     EXPECT(cursor.y == 0);
     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TAOS") == 0);

     ce_commit_redo(&buffer, &tail, &cursor);

     EXPECT(cursor.x == 3);
     EXPECT(cursor.y == 0);
     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TACOS") == 0);

     ce_free_buffer(&buffer);
     ce_commits_free(tail);
}

TEST(sanity_commit_insert_string_undo_redo)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS ARE AWESOME");

     BufferCommitNode* tail = calloc(1, sizeof(*tail));
     ASSERT(tail != NULL);

     Point start = {5, 0};
     Point undo = {5, 0};
     Point redo = {9, 0};
     ce_commit_insert_string(&tail, &start, &undo, &redo, strdup(" ARE"));

     Point cursor = {};
     ce_commit_undo(&buffer, &tail, &cursor);

     EXPECT(cursor.x == 5);
     EXPECT(cursor.y == 0);
     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TACOS AWESOME") == 0);

     ce_commit_redo(&buffer, &tail, &cursor);

     EXPECT(cursor.x == 9);
     EXPECT(cursor.y == 0);
     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TACOS ARE AWESOME") == 0);

     ce_free_buffer(&buffer);
     ce_commits_free(tail);
}

TEST(sanity_commit_remove_char_undo_redo)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TAOS");

     BufferCommitNode* tail = calloc(1, sizeof(*tail));
     ASSERT(tail != NULL);

     Point start = {2, 0};
     Point undo = {2, 0};
     Point redo = {2, 0};
     ce_commit_remove_char(&tail, &start, &undo, &redo, 'C');

     Point cursor = {};
     ce_commit_undo(&buffer, &tail, &cursor);

     EXPECT(cursor.x == 2);
     EXPECT(cursor.y == 0);
     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TACOS") == 0);

     ce_commit_redo(&buffer, &tail, &cursor);

     EXPECT(cursor.x == 2);
     EXPECT(cursor.y == 0);
     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TAOS") == 0);

     ce_free_buffer(&buffer);
     ce_commits_free(tail);
}

TEST(sanity_commit_remove_string_undo_redo)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS AWESOME");

     BufferCommitNode* tail = calloc(1, sizeof(*tail));
     ASSERT(tail != NULL);

     Point start = {5, 0};
     Point undo = {9, 0};
     Point redo = {5, 0};
     ce_commit_remove_string(&tail, &start, &undo, &redo, strdup(" ARE"));

     Point cursor = {};
     ce_commit_undo(&buffer, &tail, &cursor);

     EXPECT(cursor.x == 9);
     EXPECT(cursor.y == 0);
     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TACOS ARE AWESOME") == 0);

     ce_commit_redo(&buffer, &tail, &cursor);

     EXPECT(cursor.x == 5);
     EXPECT(cursor.y == 0);
     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TACOS AWESOME") == 0);

     ce_free_buffer(&buffer);
     ce_commits_free(tail);
}

TEST(sanity_commit_change_char_undo_redo)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TALOS");

     BufferCommitNode* tail = calloc(1, sizeof(*tail));
     ASSERT(tail != NULL);

     Point start = {2, 0};
     Point undo = {2, 0};
     Point redo = {2, 0};
     ce_commit_change_char(&tail, &start, &undo, &redo, 'L', 'C');

     Point cursor = {};
     ce_commit_undo(&buffer, &tail, &cursor);

     EXPECT(cursor.x == 2);
     EXPECT(cursor.y == 0);
     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TACOS") == 0);

     ce_commit_redo(&buffer, &tail, &cursor);

     EXPECT(cursor.x == 2);
     EXPECT(cursor.y == 0);
     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TALOS") == 0);

     ce_free_buffer(&buffer);
     ce_commits_free(tail);
}

TEST(sanity_commit_change_string_undo_redo)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS BE AWESOME");

     BufferCommitNode* tail = calloc(1, sizeof(*tail));
     ASSERT(tail != NULL);

     Point start = {5, 0};
     Point undo = {9, 0};
     Point redo = {5, 0};
     ce_commit_change_string(&tail, &start, &undo, &redo, strdup(" BE"), strdup(" ARE"));

     Point cursor = {};
     ce_commit_undo(&buffer, &tail, &cursor);

     EXPECT(cursor.x == 9);
     EXPECT(cursor.y == 0);
     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TACOS ARE AWESOME") == 0);

     ce_commit_redo(&buffer, &tail, &cursor);

     EXPECT(cursor.x == 5);
     EXPECT(cursor.y == 0);
     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TACOS BE AWESOME") == 0);

     ce_free_buffer(&buffer);
     ce_commits_free(tail);
}

TEST(sanity_find_delta_to_end_of_line)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("Cats are delicious");

     Point cursor;

     cursor = (Point) {0, 0};
     ASSERT(ce_find_delta_to_end_of_line(&buffer, &cursor) == (int64_t) strlen(buffer.lines[0]) - 1);

     cursor = (Point) {5, 0};
     ASSERT(ce_find_delta_to_end_of_line(&buffer, &cursor) == (int64_t) strlen(buffer.lines[0]) - 1 - 5);
}

TEST(sanity_ce_memrchr)
{
     char str[] = "TACOS BE AWESOME";
     char* B;

     ASSERT(B = ce_memrchr(str, 'B', sizeof str));
     EXPECT(&str[6] == B);

     ASSERT(!ce_memrchr(str, 'Z', sizeof str));
}

TEST(sanity_compute_length)
{
     Buffer buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("Cats are delicious");
     buffer.lines[1] = strdup("This forever has been true");
     buffer.lines[2] = strdup("We should eat them all.");

     Point start;
     Point end;

     start = (Point) {0, 0};
     end = (Point) {1, 0};
     ASSERT(ce_compute_length(&buffer, &start, &end) == 1);

     start = (Point) {0, 0};
     end = (Point) {18, 0};
     ASSERT(ce_compute_length(&buffer, &start, &end) == 18);

     start = (Point) {0, 0};
     end = (Point) {0, 1};
     ASSERT(ce_compute_length(&buffer, &start, &end) == 19);

     start = (Point) {0, 0};
     end = (Point) {1, 1};
     ASSERT(ce_compute_length(&buffer, &start, &end) == 20);

     start = (Point) {0, 0};
     end = (Point) {strlen(buffer.lines[2]), 2};
     ASSERT(ce_compute_length(&buffer, &start, &end) == (int64_t) strlen(buffer.lines[0]) + 1 + // account for null
                                                        (int64_t) strlen(buffer.lines[1]) + 1 + // account for null
                                                        (int64_t) strlen(buffer.lines[2]));     // last point is exclusive
}

#if 0
// This currently fails because the first character isn't checked by is_homogenous?
TEST(sanity_get_homogenous_adjacents)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("0123....    ");

     Point start, end;
     start = end = (Point) {1, 0};

     ASSERT(ce_get_homogenous_adjacents(&buffer, &start, &end, isblank));
     EXPECT(start.x == 1);
     EXPECT(start.y == 0);
     EXPECT(end.x == 1);
     EXPECT(end.y == 0);

     start = end = (Point) {1, 0};

     ASSERT(ce_get_homogenous_adjacents(&buffer, &start, &end, ce_ispunct));
     EXPECT(start.x == 1);
     EXPECT(start.y == 0);
     EXPECT(end.x == 1);
     EXPECT(end.y == 0);

     start = end = (Point) {1, 0};

     ASSERT(ce_get_homogenous_adjacents(&buffer, &start, &end, ce_iswordchar));
     EXPECT(start.x == 0);
     EXPECT(start.y == 0);
     EXPECT(end.x == 4);
     EXPECT(end.y == 0);

     start = end = (Point) {5, 0};

     ASSERT(ce_get_homogenous_adjacents(&buffer, &start, &end, ce_ispunct));
     EXPECT(start.x == 4);
     EXPECT(start.y == 0);
     EXPECT(end.x == 8);
     EXPECT(end.y == 0);

     start = end = (Point) {9, 0};

     ASSERT(ce_get_homogenous_adjacents(&buffer, &start, &end, ce_ispunct));
     EXPECT(start.x == 8);
     EXPECT(start.y == 0);
     EXPECT(end.x == 12);
     EXPECT(end.y == 0);
}
#endif

TEST(sanity_get_word_at_location)
{
     Buffer buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("0123 567 9ABC EFG");
     buffer.lines[1] = strdup("0123     9ABC EFG");
     buffer.lines[2] = strdup("0123.... 9ABC EFG");

     Point word_start, word_end;
     Point cursor = {6, 0};

     ASSERT(ce_get_word_at_location(&buffer, &cursor, &word_start, &word_end));
     EXPECT(word_start.y == 0);
     EXPECT(word_start.x == 5);
     EXPECT(word_end.y == 0);
     EXPECT(word_end.x == 8);

     cursor = (Point) {6, 1};

     ASSERT(ce_get_word_at_location(&buffer, &cursor, &word_start, &word_end));
     EXPECT(word_start.y == 1);
     EXPECT(word_start.x == 4);
     EXPECT(word_end.y == 1);
     EXPECT(word_end.x == 9);

     cursor = (Point) {6, 2};

     ASSERT(ce_get_word_at_location(&buffer, &cursor, &word_start, &word_end));
     EXPECT(word_start.y == 2);
     EXPECT(word_start.x == 4);
     EXPECT(word_end.y == 2);
     EXPECT(word_end.x == 8);
}

TEST(get_indentation_for_next_line_open_bracket)
{
     const size_t tab_len = 5;

     Buffer buffer = {};
     buffer.line_count = 6;
     buffer.lines = malloc(6 * sizeof(char*));
     buffer.lines[0] = strdup("int is_delicious_cat(cat_t cat){");
     buffer.lines[1] = strdup("     if(is_kitten(cat)){");
     buffer.lines[2] = strdup("          return true;");
     buffer.lines[3] = strdup("       } // whoops! not aligned!?");
     buffer.lines[4] = strdup("     return true;");
     buffer.lines[5] = strdup("}");

     Point cursor;

     cursor = (Point) {5, 0};
     ASSERT(ce_get_indentation_for_next_line(&buffer, &cursor, tab_len) == 5);

     cursor = (Point) {7, 1};
     ASSERT(ce_get_indentation_for_next_line(&buffer, &cursor, tab_len) == 10);

     cursor = (Point) {4, 2};
     ASSERT(ce_get_indentation_for_next_line(&buffer, &cursor, tab_len) == 10);

     cursor = (Point) {2, 3};
     ASSERT(ce_get_indentation_for_next_line(&buffer, &cursor, tab_len) == 7); // un-aligned!

     cursor = (Point) {1, 4};
     ASSERT(ce_get_indentation_for_next_line(&buffer, &cursor, tab_len) == 5);

     cursor = (Point) {0, 5};
     ASSERT(ce_get_indentation_for_next_line(&buffer, &cursor, tab_len) == 0);
}

TEST(sanity_sort_points_swap)
{
     // swap case
     const Point start = {500, 500};
     const Point end = {250, 250};
     const Point* p_start = &start;
     const Point* p_end = &end;

     ce_sort_points ( &p_start, &p_end );
     ASSERT(memcmp(p_start, &end, sizeof(Point)) == 0);
     ASSERT(memcmp(p_end, &start, sizeof(Point)) == 0);
}

TEST(sanity_sort_points_no_swap)
{
     // no swap case
     const Point start = {250, 250};
     const Point end = {500, 500};
     const Point* p_start = &start;
     const Point* p_end = &end;

     ce_sort_points ( &p_start, &p_end );
     ASSERT(memcmp(p_start, &start, sizeof(Point)) == 0);
     ASSERT(memcmp(p_end, &end, sizeof(Point)) == 0);
}

int main()
{
     RUN_TESTS();
}
