#include <execinfo.h>
#include <signal.h>

#include "ce.h"
#include "test.h"

TEST(sanity_alloc_and_free)
{
     Buffer_t buffer = {};
     ce_alloc_lines(&buffer, 3);

     ASSERT(buffer.lines);
     EXPECT(buffer.line_count == 3);

     ce_free_buffer(&buffer);
}

TEST(sanity_load_string)
{
     const char* str = "TACOS";

     Buffer_t buffer = {};
     ce_load_string(&buffer, str);

     ASSERT(buffer.lines);
     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], str) == 0);

     ce_free_buffer(&buffer);
}

TEST(load_string_multiline)
{
     const char* str = "TACOS\nARE\nTHE\nBEST";

     Buffer_t buffer = {};
     ce_load_string(&buffer, str);

     ASSERT(buffer.lines);
     ASSERT(buffer.line_count == 4);
     EXPECT(strcmp(buffer.lines[0], "TACOS") == 0);
     EXPECT(strcmp(buffer.lines[1], "ARE") == 0);
     EXPECT(strcmp(buffer.lines[2], "THE") == 0);
     EXPECT(strcmp(buffer.lines[3], "BEST") == 0);

     ce_free_buffer(&buffer);
}

TEST(load_one_line_file)
{
     // NOTE: sorry, can't run this test if you're hd is full !
     char cmd[128];
     const char* tmp_file = "/tmp/ce_one_line_file.txt";
     sprintf(cmd, "echo 'TACOS' > %s", tmp_file);
     system(cmd);
     Buffer_t buffer = {};
     ce_load_file(&buffer, tmp_file);
     sprintf(cmd, "rm %s", tmp_file);
     system(cmd);

     ASSERT(buffer.lines);
     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TACOS") == 0);

     ce_free_buffer(&buffer);
}

TEST(load_multiline_file)
{
     // NOTE: sorry, can't run this test if you're hd is full !
     char cmd[128];
     const char* tmp_file = "/tmp/ce_multiline_file.txt";
     sprintf(cmd, "echo 'TACOS\nARE\nTHE\nBEST' > %s", tmp_file);
     system(cmd);
     Buffer_t buffer = {};
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

TEST(save_buffer_one_line)
{
     const char* tmp_file = "/tmp/ce_one_line_file.txt";

     Buffer_t buffer = {};
     buffer.filename = strdup(tmp_file);
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     ce_save_buffer(&buffer, tmp_file);

     // NOTE: not sure how else to validate this
     Buffer_t other_buffer = {};
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

TEST(save_buffer_multiline_line)
{
     const char* tmp_file = "/tmp/ce_multiline_file.txt";

     Buffer_t buffer = {};
     buffer.filename = strdup(tmp_file);
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE");
     buffer.lines[2] = strdup("AWESOME");

     ce_save_buffer(&buffer, tmp_file);

     // NOTE: not sure how else to validate this
     Buffer_t other_buffer = {};
     ce_load_file(&other_buffer, tmp_file);

     char cmd[128];
     sprintf(cmd, "rm %s", tmp_file);
     system(cmd);

     ASSERT(other_buffer.lines);
     ASSERT(other_buffer.line_count == 3);
     EXPECT(strcmp(other_buffer.lines[0], "TACOS") == 0);
     EXPECT(strcmp(other_buffer.lines[1], "ARE") == 0);
     EXPECT(strcmp(other_buffer.lines[2], "AWESOME") == 0);

     ce_free_buffer(&buffer);
     ce_free_buffer(&other_buffer);
}

TEST(point_on_buffer)
{
     Buffer_t buffer = {};

     buffer.line_count = 2;
     buffer.lines = malloc(2 * sizeof(char*));
     buffer.lines[0] = strdup("TA");
     buffer.lines[1] = strdup("TA");

     Point_t point = {1, 1};
     EXPECT(ce_point_on_buffer(&buffer, point));

     point = (Point_t){1, 2};
     EXPECT(!ce_point_on_buffer(&buffer, point));

     point = (Point_t){3, 1};
     EXPECT(!ce_point_on_buffer(&buffer, point));

     ce_free_buffer(&buffer);
}

TEST(sanity_insert_char)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point_t point = {2, 0};
     ce_insert_char(&buffer, point, 'R');

     EXPECT(strcmp(buffer.lines[0], "TARCOS") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_insert_char_readonly)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.readonly = true;

     Point_t point = {2, 0};
     ce_insert_char_readonly(&buffer, point, 'R');

     EXPECT(strcmp(buffer.lines[0], "TARCOS") == 0);

     ce_free_buffer(&buffer);
}

TEST(insert_char_newline_begin)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point_t point = {0, 0};
     ce_insert_char(&buffer, point, '\n');

     ASSERT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "") == 0);
     EXPECT(strcmp(buffer.lines[1], "TACOS") == 0);

     ce_free_buffer(&buffer);
}

TEST(insert_char_newline_end)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point_t point = {5, 0};
     ce_insert_char(&buffer, point, '\n');

     ASSERT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "TACOS") == 0);
     EXPECT(strcmp(buffer.lines[1], "") == 0);

     ce_free_buffer(&buffer);
}

TEST(insert_char_newline_middle)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point_t point = {2, 0};
     ce_insert_char(&buffer, point, '\n');

     ASSERT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "TA") == 0);
     EXPECT(strcmp(buffer.lines[1], "COS") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_remove_char)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point_t point = {2, 0};
     ce_remove_char(&buffer, point);

     EXPECT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TAOS") == 0);

     ce_free_buffer(&buffer);
}

TEST(remove_char_empty_line)
{
     Buffer_t buffer = {};
     buffer.line_count = 2;
     buffer.lines = malloc(2 * sizeof(char**));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("");

     Point_t point = {0, 1};
     ce_remove_char(&buffer, point);

     EXPECT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TACOS") == 0);

     ce_free_buffer(&buffer);
}

TEST(insert_string_newline_on_empty)
{
     Buffer_t buffer = {};

     Point_t point = {0, 0};
     ce_insert_string(&buffer, point, "\n");

     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "") == 0);

     ce_free_buffer(&buffer);
}

TEST(insert_string_begin)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point_t point = {0, 0};
     ce_insert_string(&buffer, point, "AHHH ");

     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "AHHH TACOS") == 0);
     ce_free_buffer(&buffer);
}

TEST(insert_string_readonly_begin)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.readonly = true;

     Point_t point = {0, 0};
     ce_insert_string_readonly(&buffer, point, "AHHH ");

     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "AHHH TACOS") == 0);
     ce_free_buffer(&buffer);
}

TEST(insert_string_mid)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point_t point = {2, 0};
     ce_insert_string(&buffer, point, " AHHH ");

     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TA AHHH COS") == 0);
     ce_free_buffer(&buffer);
}

TEST(insert_string_end)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point_t point = {5, 0};
     ce_insert_string(&buffer, point, " AHHH");

     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TACOS AHHH") == 0);
     ce_free_buffer(&buffer);
}

TEST(insert_string_multiline_begin)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point_t point = {0, 0};
     ce_insert_string(&buffer, point, "AH\nHH ");

     ASSERT(buffer.line_count == 2);
     // NOTE: I realize my examples are insane, but I'm intoxicated.
     EXPECT(strcmp(buffer.lines[0], "AH") == 0);
     EXPECT(strcmp(buffer.lines[1], "HH TACOS") == 0);

     ce_free_buffer(&buffer);
}

TEST(insert_string_multiline_mid)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point_t point = {2, 0};
     ce_insert_string(&buffer, point, " IN\nTHE\nMID\n ");

     ASSERT(buffer.line_count == 4);
     EXPECT(strcmp(buffer.lines[0], "TA IN") == 0);
     EXPECT(strcmp(buffer.lines[1], "THE") == 0);
     EXPECT(strcmp(buffer.lines[2], "MID") == 0);
     EXPECT(strcmp(buffer.lines[3], " COS") == 0);

     ce_free_buffer(&buffer);
}

TEST(insert_string_multiline_end)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point_t point = {5, 0};
     ce_insert_string(&buffer, point, " AH\nHH ");

     ASSERT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "TACOS AH") == 0);
     EXPECT(strcmp(buffer.lines[1], "HH ") == 0);

     ce_free_buffer(&buffer);
}

TEST(insert_string_multiline_blank_begin)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point_t point = {0, 0};
     ce_insert_string(&buffer, point, "\n\n");

     ASSERT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "") == 0);
     EXPECT(strcmp(buffer.lines[1], "") == 0);
     EXPECT(strcmp(buffer.lines[2], "TACOS") == 0);

     ce_free_buffer(&buffer);
}

TEST(insert_string_multiline_blank_mid)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point_t point = {2, 0};
     ce_insert_string(&buffer, point, "\n\n");

     ASSERT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "TA") == 0);
     EXPECT(strcmp(buffer.lines[1], "") == 0);
     EXPECT(strcmp(buffer.lines[2], "COS") == 0);

     ce_free_buffer(&buffer);
}

TEST(insert_string_multiline_blank_end)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point_t point = {5, 0};
     ce_insert_string(&buffer, point, "\n\n");

     ASSERT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "TACOS") == 0);
     EXPECT(strcmp(buffer.lines[1], "") == 0);
     EXPECT(strcmp(buffer.lines[2], "") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_append_string)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     ce_append_string(&buffer, 0, " ARE AWESOME");

     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TACOS ARE AWESOME") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_append_string_readonly)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.readonly = true;

     ce_append_string_readonly(&buffer, 0, " ARE AWESOME");

     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TACOS ARE AWESOME") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_append_string_multiline)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     ce_append_string(&buffer, 0, " ARE\nAWESOME");

     ASSERT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "TACOS ARE") == 0);
     EXPECT(strcmp(buffer.lines[1], "AWESOME") == 0);

     ce_free_buffer(&buffer);
}

TEST(remove_string_begin)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point_t point = {0, 0};
     ce_remove_string(&buffer, point, 2);

     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "COS") == 0);

     ce_free_buffer(&buffer);
}

TEST(remove_string_mid)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point_t point = {1, 0};
     ce_remove_string(&buffer, point, 2);

     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TOS") == 0);

     ce_free_buffer(&buffer);
}

TEST(remove_string_end)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point_t point = {3, 0};
     ce_remove_string(&buffer, point, 2);

     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TAC") == 0);

     ce_free_buffer(&buffer);
}

TEST(remove_string_multiline_begin)
{
     Buffer_t buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE");
     buffer.lines[2] = strdup("AWESOME");

     Point_t point = {0, 0};
     ce_remove_string(&buffer, point, 7);

     ASSERT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "RE") == 0);
     EXPECT(strcmp(buffer.lines[1], "AWESOME") == 0);

     ce_free_buffer(&buffer);
}

TEST(remove_string_multiline_mid)
{
     Buffer_t buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE");
     buffer.lines[2] = strdup("AWESOME");

     Point_t point = {1, 0};
     ce_remove_string(&buffer, point, 7);

     ASSERT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "TE") == 0);
     EXPECT(strcmp(buffer.lines[1], "AWESOME") == 0);

     ce_free_buffer(&buffer);
}

TEST(remove_string_multiline_end)
{
     Buffer_t buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE");
     buffer.lines[2] = strdup("AWESOMT");

     Point_t point = {2, 1};
     ce_remove_string(&buffer, point, 9);

     ASSERT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "TACOS") == 0);
     EXPECT(strcmp(buffer.lines[1], "AR") == 0);

     ce_free_buffer(&buffer);
}

TEST(remove_string_multiline_blank_begin)
{
     Buffer_t buffer = {};
     buffer.line_count = 5;
     buffer.lines = malloc(5 * sizeof(char*));
     buffer.lines[0] = strdup("");
     buffer.lines[1] = strdup("");
     buffer.lines[2] = strdup("TACOS");
     buffer.lines[3] = strdup("ARE");
     buffer.lines[4] = strdup("AWESOME");

     Point_t point = {0, 0};
     ce_remove_string(&buffer, point, 5);

     ASSERT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "OS") == 0);
     EXPECT(strcmp(buffer.lines[1], "ARE") == 0);
     EXPECT(strcmp(buffer.lines[2], "AWESOME") == 0);

     ce_free_buffer(&buffer);
}

TEST(remove_string_multiline_blank_mid)
{
     Buffer_t buffer = {};
     buffer.line_count = 5;
     buffer.lines = malloc(5 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("");
     buffer.lines[2] = strdup("");
     buffer.lines[3] = strdup("ARE");
     buffer.lines[4] = strdup("AWESOME");

     Point_t point = {0, 1};
     ce_remove_string(&buffer, point, 4);

     ASSERT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "TACOS") == 0);
     EXPECT(strcmp(buffer.lines[1], "E") == 0);
     EXPECT(strcmp(buffer.lines[2], "AWESOME") == 0);

     ce_free_buffer(&buffer);
}

TEST(remove_string_multiline_blank_end)
{
     Buffer_t buffer = {};
     buffer.line_count = 5;
     buffer.lines = malloc(5 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE");
     buffer.lines[2] = strdup("AWESOME");
     buffer.lines[3] = strdup("");
     buffer.lines[4] = strdup("");

     Point_t point = {3, 2};
     ce_remove_string(&buffer, point, 7);

     ASSERT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "TACOS") == 0);
     EXPECT(strcmp(buffer.lines[1], "ARE") == 0);
     EXPECT(strcmp(buffer.lines[2], "AWE") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_insert_line_readonly)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.readonly = true;

     ce_insert_line_readonly(&buffer, 0, "ARE AWESOME");

     ASSERT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "ARE AWESOME") == 0);
     EXPECT(strcmp(buffer.lines[1], "TACOS") == 0);

     ce_free_buffer(&buffer);
}

TEST(insert_line_multiline)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.readonly = true;

     ce_insert_line_readonly(&buffer, 0, "ARE\nAWESOME");

     ASSERT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "ARE") == 0);
     EXPECT(strcmp(buffer.lines[1], "AWESOME") == 0);
     EXPECT(strcmp(buffer.lines[2], "TACOS") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_append_line)
{
     Buffer_t buffer = {};
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

TEST(sanity_append_line_readonly)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.readonly = true;

     ce_append_line_readonly(&buffer, "ARE AWESOME");

     EXPECT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "TACOS") == 0);
     EXPECT(strcmp(buffer.lines[1], "ARE AWESOME") == 0);

     ce_free_buffer(&buffer);
}

#if 0
TEST(sanity_join_line)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE");
     buffer.lines[2] = strdup("AWESOME");

     ce_join_line(&buffer, 1);

     ASSERT(buffer.line_count == 2);

     EXPECT(strcmp(buffer.lines[0], "TACOS") == 0);
     EXPECT(strcmp(buffer.lines[1], "ARE AWESOME") == 0);

     ce_free_buffer(&buffer);
}
#endif

TEST(sanity_clear_lines)
{
     Buffer_t buffer = {};
     buffer.line_count = 2;
     buffer.lines = malloc(2 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE AWESOME");

     ce_clear_lines(&buffer);

     EXPECT(buffer.line_count == 0);
     EXPECT(buffer.lines == NULL);

     ce_free_buffer(&buffer);
}

TEST(sanity_clear_lines_readonly)
{
     Buffer_t buffer = {};
     buffer.line_count = 2;
     buffer.lines = malloc(2 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE AWESOME");
     buffer.readonly = true;

     ce_clear_lines_readonly(&buffer);

     EXPECT(buffer.line_count == 0);
     EXPECT(buffer.lines == NULL);

     ce_free_buffer(&buffer);
}

TEST(sanity_dupe_string)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point_t start = {1, 0};
     Point_t end = {3, 0};
     char* str = ce_dupe_string(&buffer, start, end);

     EXPECT(strcmp(str, "ACO") == 0);

     free(str);
     ce_free_buffer(&buffer);
}

TEST(dupe_string_multiline)
{
     Buffer_t buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE");
     buffer.lines[2] = strdup("AWESOME");

     Point_t start = {1, 0};
     Point_t end = {3, 2};
     char* str = ce_dupe_string(&buffer, start, end);

     EXPECT(strcmp(str, "ACOS\nARE\nAWES") == 0);

     free(str);
     ce_free_buffer(&buffer);
}

TEST(dupe_string_multiline_on_line_boundry)
{
     Buffer_t buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE");
     buffer.lines[2] = strdup("AWESOME");

     Point_t start = {1, 0};
     Point_t end = {3, 1};
     char* str = ce_dupe_string(&buffer, start, end);

     EXPECT(strcmp(str, "ACOS\nARE\n") == 0);

     free(str);
     ce_free_buffer(&buffer);
}

TEST(dupe_line)
{
     Buffer_t buffer = {};
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
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     char ch = 0;
     Point_t point = {2, 0};
     ce_get_char(&buffer, point, &ch);

     EXPECT(ch == 'C');

     ce_free_buffer(&buffer);
}

TEST(sanity_set_char)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point_t point = {2, 0};
     ce_set_char(&buffer, point, 'R');

     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TAROS") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_find_matching_pair_same_line)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("if(i == 3)");

     Point_t point = {2, 0};
     ce_move_cursor_to_matching_pair(&buffer, &point);

     EXPECT(point.x == 9);
     EXPECT(point.y == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_find_matching_pair_multiline)
{
     Buffer_t buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("if(x){");
     buffer.lines[1] = strdup("   x = 5;");
     buffer.lines[2] = strdup("}");

     Point_t point = {5, 0};
     ce_move_cursor_to_matching_pair(&buffer, &point);

     EXPECT(point.x == 0);
     EXPECT(point.y == 2);

     ce_free_buffer(&buffer);
}

TEST(sanity_find_matching_pair_multiline_nested)
{
     Buffer_t buffer = {};
     buffer.line_count = 5;
     buffer.lines = malloc(5 * sizeof(char*));
     buffer.lines[0] = strdup("if(x){");
     buffer.lines[1] = strdup("   if(y){");
     buffer.lines[2] = strdup("      x = 5;");
     buffer.lines[3] = strdup("   }");
     buffer.lines[4] = strdup("}");

     Point_t point = {5, 0};
     ce_move_cursor_to_matching_pair(&buffer, &point);

     EXPECT(point.x == 0);
     EXPECT(point.y == 4);

     ce_free_buffer(&buffer);
}

TEST(find_match_same_line)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS ARE AWESOME");

     Point_t point = {2, 0};
     Point_t match = {};
     ce_find_string(&buffer, point, "ARE", &match, CE_DOWN);

     EXPECT(match.x == 6);
     EXPECT(match.y == 0);

     ce_free_buffer(&buffer);
}

TEST(find_match_next_line)
{
     Buffer_t buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE SO");
     buffer.lines[2] = strdup("AWESOME");

     Point_t point = {2, 0};
     Point_t delta = {};
     ce_find_string(&buffer, point, "SO", &delta, CE_DOWN);

     EXPECT(delta.x == 4);
     EXPECT(delta.y == 1);

     ce_free_buffer(&buffer);
}

TEST(clamp_cursor_horizontal)
{
     Buffer_t buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE SO");
     buffer.lines[2] = strdup("AWESOME");

     Point_t cursor = {50, 0};
     ce_clamp_cursor(&buffer, &cursor);

     EXPECT(cursor.x == 4);
     EXPECT(cursor.y == 0);

     ce_free_buffer(&buffer);
}

TEST(clamp_cursor_vertical)
{
     Buffer_t buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE SO");
     buffer.lines[2] = strdup("AWESOME");

     Point_t cursor = {4, 50};
     ce_clamp_cursor(&buffer, &cursor);

     EXPECT(cursor.x == 4);
     EXPECT(cursor.y == 2);

     ce_free_buffer(&buffer);
}

TEST(sanity_move_cursor)
{
     Buffer_t buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE SO");
     buffer.lines[2] = strdup("AWESOME");

     Point_t cursor = {2, 0};
     ce_move_cursor(&buffer, &cursor, (Point_t){2, 1});

     EXPECT(cursor.x == 4);
     EXPECT(cursor.y == 1);

     ce_free_buffer(&buffer);
}

TEST(sanity_set_cursor)
{
     Buffer_t buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE SO");
     buffer.lines[2] = strdup("AWESOME");

     Point_t cursor = {2, 0};
     Point_t delta = {4, 2};
     ce_set_cursor(&buffer, &cursor, delta);

     EXPECT(cursor.x == 4);
     EXPECT(cursor.y == 2);

     ce_free_buffer(&buffer);
}

TEST(advance_cursor_same_line)
{
     Buffer_t buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE SO");
     buffer.lines[2] = strdup("AWESOME");

     Point_t cursor = {1, 0};
     ce_advance_cursor(&buffer, &cursor, 3);

     EXPECT(cursor.x == 4);
     EXPECT(cursor.y == 0);

     ce_free_buffer(&buffer);
}

TEST(advance_cursor_next_line)
{
     Buffer_t buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE SO");
     buffer.lines[2] = strdup("AWESOME");

     Point_t cursor = {1, 0};
     ce_advance_cursor(&buffer, &cursor, 6);

     EXPECT(cursor.x == 2);
     EXPECT(cursor.y == 1);

     ce_free_buffer(&buffer);
}

TEST(move_cursor_to_end_of_file)
{
     Buffer_t buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE SO");
     buffer.lines[2] = strdup("AWESOME");

     Point_t cursor = {1, 0};
     ce_move_cursor_to_end_of_file(&buffer, &cursor);

     EXPECT(cursor.x == 6);
     EXPECT(cursor.y == 2);

     ce_free_buffer(&buffer);
}

TEST(commit_insert_char_undo_redo)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     BufferCommitNode_t* tail = calloc(1, sizeof(*tail));
     ASSERT(tail != NULL);

     Point_t start = {2, 0};
     Point_t undo = {2, 0};
     Point_t redo = {3, 0};
     ce_commit_insert_char(&tail, start, undo, redo, 'C');

     Point_t cursor = {};
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

TEST(commit_insert_string_undo_redo)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS ARE AWESOME");

     BufferCommitNode_t* tail = calloc(1, sizeof(*tail));
     ASSERT(tail != NULL);

     Point_t start = {5, 0};
     Point_t undo = {5, 0};
     Point_t redo = {9, 0};
     ce_commit_insert_string(&tail, start, undo, redo, strdup(" ARE"));

     Point_t cursor = {};
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

TEST(commit_remove_char_undo_redo)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TAOS");

     BufferCommitNode_t* tail = calloc(1, sizeof(*tail));
     ASSERT(tail != NULL);

     Point_t start = {2, 0};
     Point_t undo = {2, 0};
     Point_t redo = {2, 0};
     ce_commit_remove_char(&tail, start, undo, redo, 'C');

     Point_t cursor = {};
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

TEST(commit_remove_string_undo_redo)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS AWESOME");

     BufferCommitNode_t* tail = calloc(1, sizeof(*tail));
     ASSERT(tail != NULL);

     Point_t start = {5, 0};
     Point_t undo = {9, 0};
     Point_t redo = {5, 0};
     ce_commit_remove_string(&tail, start, undo, redo, strdup(" ARE"));

     Point_t cursor = {};
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

TEST(commit_change_char_undo_redo)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TALOS");

     BufferCommitNode_t* tail = calloc(1, sizeof(*tail));
     ASSERT(tail != NULL);

     Point_t start = {2, 0};
     Point_t undo = {2, 0};
     Point_t redo = {2, 0};
     ce_commit_change_char(&tail, start, undo, redo, 'L', 'C');

     Point_t cursor = {};
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

TEST(commit_change_string_undo_redo)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS BE AWESOME");

     BufferCommitNode_t* tail = calloc(1, sizeof(*tail));
     ASSERT(tail != NULL);

     Point_t start = {5, 0};
     Point_t undo = {9, 0};
     Point_t redo = {5, 0};
     ce_commit_change_string(&tail, start, undo, redo, strdup(" BE"), strdup(" ARE"));

     Point_t cursor = {};
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

TEST(sanity_is_c_keyword)
{
     const char* keyword_line = "     while(i == 5)";
     const char* not_keyword_line = "TACOS ARE GREAT";
     EXPECT(ce_is_c_keyword(keyword_line, 5) == 5);
     EXPECT(ce_is_c_keyword(not_keyword_line, 6) == 0);
}

TEST(sanity_is_preprocessor)
{
     const char* preproc_line = "#include <iosux>";
     const char* not_preproc_line = "EVERYTHING IS AWESOME";

     EXPECT(ce_is_preprocessor(preproc_line, 0) == 8);
     EXPECT(ce_is_preprocessor(not_preproc_line, 0) == 0);
}

TEST(sanity_is_comment)
{
     const char* non_comment_line = "WHY DOES BACKSPACE NOT WORK FOR ME?";
     const char* comment_line = "     // tacos ";
     const char* begin_multiline_comment_line = "     /* ";
     const char* end_multiline_comment_line = "    */ ";

     EXPECT(ce_is_comment(non_comment_line, 0) == CT_NONE);
     EXPECT(ce_is_comment(comment_line, 5) == CT_SINGLE_LINE);
     EXPECT(ce_is_comment(begin_multiline_comment_line, 5) == CT_BEGIN_MULTILINE);
     EXPECT(ce_is_comment(end_multiline_comment_line, 5) == CT_END_MULTILINE);
}

TEST(sanity_is_string_literal)
{
     const char* non_string_line = "TACOS ARE AWESOME";
     const char* double_string_line = "printf(\"Hello World\")";
     const char* single_string_line = "printf('!')";

     bool non_inside_string = false;
     char non_last_quote_char = 0;
     ce_is_string_literal(non_string_line, 0, strlen(non_string_line), &non_inside_string, &non_last_quote_char);

     EXPECT(non_inside_string == false);
     EXPECT(non_last_quote_char == false);

     bool inside_string = false;
     char last_quote_char = 0;
     ce_is_string_literal(double_string_line, 7, strlen(double_string_line), &inside_string, &last_quote_char);

     EXPECT(inside_string == true);
     EXPECT(last_quote_char == '"');

     ce_is_string_literal(double_string_line, 19, strlen(double_string_line), &inside_string, &last_quote_char);

     EXPECT(inside_string == false);
     EXPECT(last_quote_char == '"');

     inside_string = false;
     last_quote_char = 0;
     ce_is_string_literal(single_string_line, 7, strlen(single_string_line), &inside_string, &last_quote_char);

     EXPECT(inside_string == true);
     EXPECT(last_quote_char == '\'');

     ce_is_string_literal(single_string_line, 9, strlen(single_string_line), &inside_string, &last_quote_char);

     EXPECT(inside_string == false);
     EXPECT(last_quote_char == '\'');
}

TEST(sanity_is_caps_var)
{
     const char* non_string_line = "i'm using ce to write these tests!";
     const char* string_line = "#define _GNU_SOURCE";

     EXPECT(ce_is_caps_var(non_string_line, 0) == 0);
     EXPECT(ce_is_caps_var(string_line, 8) == 11);
}

TEST(sanity_follow_cursor)
{
     int64_t left_column = 0;
     int64_t top_row = 0;
     int64_t view_width = 3;
     int64_t view_height = 4;

     Point_t cursor = {0, 0};

     ce_follow_cursor(cursor, &left_column, &top_row, view_width, view_height, false, false);
     EXPECT(left_column == 0);
     EXPECT(top_row == 0);

     cursor = (Point_t){3, 0};
     ce_follow_cursor(cursor, &left_column, &top_row, view_width, view_height, false, false);
     EXPECT(left_column == 1);
     EXPECT(top_row == 0);

     left_column = 0;
     cursor = (Point_t){0, 4};
     ce_follow_cursor(cursor, &left_column, &top_row, view_width, view_height, false, false);
     EXPECT(left_column == 0);
     EXPECT(top_row == 1);
}

TEST(sanity_buffer_list)
{
     BufferNode_t* head = calloc(1, sizeof(*head));
     ASSERT(head);

     Buffer_t one;
     Buffer_t two;
     Buffer_t three;

     head->buffer = &one;

     BufferNode_t* two_node = ce_append_buffer_to_list(head, &two);
     ASSERT(two_node != NULL);

     BufferNode_t* three_node = ce_append_buffer_to_list(head, &three);
     ASSERT(three_node != NULL);

     ASSERT(head->buffer == &one);
     ASSERT(head->next->buffer == &two);
     ASSERT(head->next->next->buffer == &three);

     EXPECT(ce_remove_buffer_from_list(head, &two_node) == true);
     EXPECT(ce_remove_buffer_from_list(head, &two_node) == false);

     ASSERT(head);
     ASSERT(head->next);
     ASSERT(head->next->buffer == &three);
     ASSERT(head->next->next == NULL);

     EXPECT(ce_remove_buffer_from_list(head, &three_node) == true);

     ASSERT(head);
     ASSERT(head->buffer == &one);
     ASSERT(head->next == NULL);

     free(head);
}

TEST(sanity_split_view)
{
     BufferView_t* head = calloc(1, sizeof(*head));
     ASSERT(head);

     Buffer_t buffers[4] = {};

     head->buffer = buffers + 0;

     // split views
     BufferView_t* horizontal_split_view = ce_split_view(head, buffers + 1, true);
     ASSERT(head->next_horizontal == horizontal_split_view);

     BufferView_t* vertical_split_view = ce_split_view(head, buffers + 2, false);
     ASSERT(head->next_vertical == vertical_split_view);

     BufferView_t* new_horizontal_split_view = ce_split_view(vertical_split_view, buffers + 3, true);
     ASSERT(vertical_split_view->next_horizontal == new_horizontal_split_view);

     // calc views
     Point_t top_left = {0, 0};
     Point_t bottom_right = {16, 9};
     ASSERT(ce_calc_views(head, top_left, bottom_right));

     EXPECT(head->top_left.x == 0);
     EXPECT(head->top_left.y == 0);
     EXPECT(head->bottom_right.x == 7);
     EXPECT(head->bottom_right.y == 4);

     EXPECT(horizontal_split_view->top_left.x == 8);
     EXPECT(horizontal_split_view->top_left.y == 0);
     EXPECT(horizontal_split_view->bottom_right.x == 16);
     EXPECT(horizontal_split_view->bottom_right.y == 9);

     EXPECT(vertical_split_view->top_left.x == 0);
     EXPECT(vertical_split_view->top_left.y == 5);
     EXPECT(vertical_split_view->bottom_right.x == 3);
     EXPECT(vertical_split_view->bottom_right.y == 9);

     EXPECT(new_horizontal_split_view->top_left.x == 4);
     EXPECT(new_horizontal_split_view->top_left.y == 5);
     EXPECT(new_horizontal_split_view->bottom_right.x == 7);
     EXPECT(new_horizontal_split_view->bottom_right.y == 9);

     // find view at point
     Point_t find_point = {7, 9};
     BufferView_t* found_view = ce_find_view_at_point(head, find_point);
     EXPECT(found_view == new_horizontal_split_view);

     // find view by buffer
     found_view = ce_buffer_in_view(head, buffers + 3);
     EXPECT(found_view == new_horizontal_split_view);

     // draw views
     // NOTE: we are not initializing curses or anything, so the calls should be nops? We make the call to 
     //       ensure no crashes, but can't really validate anything
     EXPECT(ce_draw_views(head, NULL, LNT_NONE));

     // remove views
     ASSERT(ce_remove_view(&head, vertical_split_view) == true);
     EXPECT(head->next_horizontal == horizontal_split_view);
     EXPECT(head->next_vertical == new_horizontal_split_view);
     EXPECT(head->next_horizontal->next_horizontal == NULL);

     ASSERT(ce_remove_view(&head, head) == true);
     EXPECT(head == new_horizontal_split_view);
     EXPECT(head->next_horizontal == horizontal_split_view);

     // free views
     ASSERT(ce_free_views(&head));
}

// TODO: this function should point us to the last character. not the newline.
TEST(sanity_move_cursor_to_end_of_line)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("Cats are delicious");

     Point_t cursor;

     cursor = (Point_t) {0, 0};
     ce_move_cursor_to_end_of_line(&buffer, &cursor);
     ASSERT(cursor.x == (int64_t) strlen(buffer.lines[0])-1);
     ASSERT(cursor.y == 0);

     cursor = (Point_t) {5, 0};
     ce_move_cursor_to_end_of_line(&buffer, &cursor);
     ASSERT(cursor.x == (int64_t) strlen(buffer.lines[0])-1);
     ASSERT(cursor.y == 0);

     ce_free_buffer(&buffer);
}

// TODO: vim's WORDS
TEST(move_cursor_to_beginning_of_word)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("Cats are delicious. Oh yeah!");

     Point_t cursor;

     cursor = (Point_t) {0, 0};
     ASSERT(ce_move_cursor_to_beginning_of_word(&buffer, &cursor, false));
     EXPECT(cursor.x == 0);
     EXPECT(cursor.y == 0);

     cursor = (Point_t) {2, 0};
     ASSERT(ce_move_cursor_to_beginning_of_word(&buffer, &cursor, false));
     EXPECT(cursor.x == 0);
     EXPECT(cursor.y == 0);

     // on whitespace
     cursor = (Point_t) {8, 0};
     ASSERT(ce_move_cursor_to_beginning_of_word(&buffer, &cursor, false));
     EXPECT(cursor.x == 5);
     EXPECT(cursor.y == 0);

     // on a beginning of word already
     cursor = (Point_t) {5, 0};
     ASSERT(ce_move_cursor_to_beginning_of_word(&buffer, &cursor, false));
     EXPECT(cursor.x == 0);
     EXPECT(cursor.y == 0);

     // move to punctuation
     cursor = (Point_t) {20, 0};
     ASSERT(ce_move_cursor_to_beginning_of_word(&buffer, &cursor, true));
     EXPECT(cursor.x == 18);
     EXPECT(cursor.y == 0);

     ce_free_buffer(&buffer);
}

TEST(move_cursor_to_end_of_word)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("Cats are delicious. Oh yeah!");

     Point_t cursor;

     cursor = (Point_t) {0, 0};
     ASSERT(ce_move_cursor_to_end_of_word(&buffer, &cursor, false));
     EXPECT(cursor.x == 3);
     EXPECT(cursor.y == 0);

     cursor = (Point_t) {2, 0};
     ASSERT(ce_move_cursor_to_end_of_word(&buffer, &cursor, false));
     EXPECT(cursor.x == 3);
     EXPECT(cursor.y == 0);

     // on whitespace
     cursor = (Point_t) {8, 0};
     ASSERT(ce_move_cursor_to_end_of_word(&buffer, &cursor, true));
     EXPECT(cursor.x == 17);
     EXPECT(cursor.y == 0);

     // on a end of word already
     cursor = (Point_t) {3, 0};
     ASSERT(ce_move_cursor_to_end_of_word(&buffer, &cursor, false));
     EXPECT(cursor.x == 7);
     EXPECT(cursor.y == 0);

     // move to punctuation
     cursor = (Point_t) {17, 0};
     ASSERT(ce_move_cursor_to_end_of_word(&buffer, &cursor, true));
     EXPECT(cursor.x == 18);
     EXPECT(cursor.y == 0);

     ce_free_buffer(&buffer);
}

TEST(move_cursor_to_next_word)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("Cats are delicious. Oh yeah!");

     Point_t cursor;

     cursor = (Point_t) {0, 0};
     ASSERT(ce_move_cursor_to_next_word(&buffer, &cursor, false));
     EXPECT(cursor.x == 5);
     EXPECT(cursor.y == 0);

     cursor = (Point_t) {2, 0};
     ASSERT(ce_move_cursor_to_next_word(&buffer, &cursor, false));
     EXPECT(cursor.x == 5);
     EXPECT(cursor.y == 0);

     // on whitespace
     cursor = (Point_t) {8, 0};
     ASSERT(ce_move_cursor_to_next_word(&buffer, &cursor, true));
     EXPECT(cursor.x == 9);
     EXPECT(cursor.y == 0);

     // on a end of word already
     cursor = (Point_t) {9, 0};
     ASSERT(ce_move_cursor_to_next_word(&buffer, &cursor, true));
     EXPECT(cursor.x == 18);
     EXPECT(cursor.y == 0);

     // move off punctuation
     cursor = (Point_t) {18, 0};
     ASSERT(ce_move_cursor_to_next_word(&buffer, &cursor, true));
     EXPECT(cursor.x == 20);
     EXPECT(cursor.y == 0);

     ce_free_buffer(&buffer);
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
     Buffer_t buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("Cats are delicious");
     buffer.lines[1] = strdup("This forever has been true");
     buffer.lines[2] = strdup("We should eat them all.");

     Point_t start;
     Point_t end;

     start = (Point_t) {0, 0};
     end = (Point_t) {1, 0};
     ASSERT(ce_compute_length(&buffer, start, end) == 2);

     start = (Point_t) {0, 0};
     end = (Point_t) {18, 0};
     ASSERT(ce_compute_length(&buffer, start, end) == 19);

     start = (Point_t) {0, 0};
     end = (Point_t) {0, 1};
     ASSERT(ce_compute_length(&buffer, start, end) == 20);

     start = (Point_t) {0, 0};
     end = (Point_t) {1, 1};
     ASSERT(ce_compute_length(&buffer, start, end) == 21);

     start = (Point_t) {0, 0};
     end = (Point_t) {strlen(buffer.lines[2]), 2};
     ASSERT(ce_compute_length(&buffer, start, end) == (int64_t) strlen(buffer.lines[0]) + 1 + // account for null
                                                        (int64_t) strlen(buffer.lines[1]) + 1 + // account for null
                                                        (int64_t) strlen(buffer.lines[2]) + 1);

     ce_free_buffer(&buffer);
}

#if 0
// This currently fails because the first character isn't checked by is_homogenous?
TEST(sanity_get_homogenous_adjacents)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("0123....    ");

     Point_t start, end;
     start = end = (Point_t) {1, 0};

     ASSERT(ce_get_homogenous_adjacents(&buffer, &start, &end, isblank));
     EXPECT(start.x == 1);
     EXPECT(start.y == 0);
     EXPECT(end.x == 1);
     EXPECT(end.y == 0);

     start = end = (Point_t) {1, 0};

     ASSERT(ce_get_homogenous_adjacents(&buffer, &start, &end, ce_ispunct));
     EXPECT(start.x == 1);
     EXPECT(start.y == 0);
     EXPECT(end.x == 1);
     EXPECT(end.y == 0);

     start = end = (Point_t) {1, 0};

     ASSERT(ce_get_homogenous_adjacents(&buffer, &start, &end, ce_iswordchar));
     EXPECT(start.x == 0);
     EXPECT(start.y == 0);
     EXPECT(end.x == 4);
     EXPECT(end.y == 0);

     start = end = (Point_t) {5, 0};

     ASSERT(ce_get_homogenous_adjacents(&buffer, &start, &end, ce_ispunct));
     EXPECT(start.x == 4);
     EXPECT(start.y == 0);
     EXPECT(end.x == 8);
     EXPECT(end.y == 0);

     start = end = (Point_t) {9, 0};

     ASSERT(ce_get_homogenous_adjacents(&buffer, &start, &end, ce_ispunct));
     EXPECT(start.x == 8);
     EXPECT(start.y == 0);
     EXPECT(end.x == 12);
     EXPECT(end.y == 0);

     ce_free_buffer(&buffer);
}
#endif

TEST(sanity_get_word_at_location)
{
     Buffer_t buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("0123 567 9ABC EFG");
     buffer.lines[1] = strdup("0123     9ABC EFG");
     buffer.lines[2] = strdup("0123.... 9ABC EFG");

     Point_t word_start, word_end;
     Point_t cursor = {6, 0};

     ASSERT(ce_get_word_at_location(&buffer, cursor, &word_start, &word_end));
     EXPECT(word_start.y == 0);
     EXPECT(word_start.x == 5);
     EXPECT(word_end.y == 0);
     EXPECT(word_end.x == 7);

     cursor = (Point_t) {6, 1};

     ASSERT(ce_get_word_at_location(&buffer, cursor, &word_start, &word_end));
     EXPECT(word_start.y == 1);
     EXPECT(word_start.x == 4);
     EXPECT(word_end.y == 1);
     EXPECT(word_end.x == 8);

     cursor = (Point_t) {6, 2};

     ASSERT(ce_get_word_at_location(&buffer, cursor, &word_start, &word_end));
     EXPECT(word_start.y == 2);
     EXPECT(word_start.x == 4);
     EXPECT(word_end.y == 2);
     EXPECT(word_end.x == 7);

     ce_free_buffer(&buffer);
}

TEST(get_indentation_for_next_line_open_bracket)
{
     const size_t tab_len = 5;

     Buffer_t buffer = {};
     buffer.line_count = 6;
     buffer.lines = malloc(6 * sizeof(char*));
     buffer.lines[0] = strdup("int is_delicious_cat(cat_t cat){");
     buffer.lines[1] = strdup("     if(is_kitten(cat)){");
     buffer.lines[2] = strdup("          return true;");
     buffer.lines[3] = strdup("       } // whoops! not aligned!?");
     buffer.lines[4] = strdup("     return true;");
     buffer.lines[5] = strdup("}");

     Point_t cursor;

     cursor = (Point_t) {5, 0};
     ASSERT(ce_get_indentation_for_next_line(&buffer, cursor, tab_len) == 5);

     cursor = (Point_t) {7, 1};
     ASSERT(ce_get_indentation_for_next_line(&buffer, cursor, tab_len) == 10);

     cursor = (Point_t) {4, 2};
     ASSERT(ce_get_indentation_for_next_line(&buffer, cursor, tab_len) == 10);

     cursor = (Point_t) {2, 3};
     ASSERT(ce_get_indentation_for_next_line(&buffer, cursor, tab_len) == 7); // un-aligned!

     cursor = (Point_t) {1, 4};
     ASSERT(ce_get_indentation_for_next_line(&buffer, cursor, tab_len) == 5);

     cursor = (Point_t) {0, 5};
     ASSERT(ce_get_indentation_for_next_line(&buffer, cursor, tab_len) == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_sort_points_swap)
{
     // swap case
     const Point_t start = {500, 500};
     const Point_t end = {250, 250};
     const Point_t* p_start = &start;
     const Point_t* p_end = &end;

     ce_sort_points ( &p_start, &p_end );
     ASSERT(memcmp(p_start, &end, sizeof(Point_t)) == 0);
     ASSERT(memcmp(p_end, &start, sizeof(Point_t)) == 0);
}

TEST(sanity_sort_points_no_swap)
{
     // no swap case
     const Point_t start = {250, 250};
     const Point_t end = {500, 500};
     const Point_t* p_start = &start;
     const Point_t* p_end = &end;

     ce_sort_points ( &p_start, &p_end );
     ASSERT(memcmp(p_start, &start, sizeof(Point_t)) == 0);
     ASSERT(memcmp(p_end, &end, sizeof(Point_t)) == 0);
}

TEST(sanity_move_cursor_to_soft_beginning_of_line)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("     Wow, cats are more delicious than I anticipated.");

     Point_t cursor;

     cursor = (Point_t) {0, 0};
     EXPECT(ce_move_cursor_to_soft_beginning_of_line(&buffer, &cursor) == true);
     ASSERT(cursor.x == 5);
     ASSERT(cursor.y == 0);

     cursor = (Point_t) {13, 0};
     EXPECT(ce_move_cursor_to_soft_beginning_of_line(&buffer, &cursor) == true);
     ASSERT(cursor.x == 5);
     ASSERT(cursor.y == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_move_cursor_to_soft_end_of_line)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("     Meow.     ");

     Point_t cursor;

     cursor = (Point_t) {0, 0};
     EXPECT(ce_move_cursor_to_soft_end_of_line(&buffer, &cursor) == true);
     ASSERT(cursor.x == 9);
     ASSERT(cursor.y == 0);

     cursor = (Point_t) {13, 0};
     EXPECT(ce_move_cursor_to_soft_end_of_line(&buffer, &cursor) == true);
     ASSERT(cursor.x == 9);
     ASSERT(cursor.y == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_append_char)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS ARE AWESOM");

     ASSERT(ce_append_char(&buffer, 'E'));
     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TACOS ARE AWESOME") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_append_char_readonly)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS ARE AWESOM");
     buffer.readonly = true;

     ASSERT(ce_append_char_readonly(&buffer, 'E'));
     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "TACOS ARE AWESOME") == 0);

     ce_free_buffer(&buffer);
}

TEST(append_newline)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS ARE AWESOME");

     ASSERT(ce_append_char(&buffer, '\n'));
     ASSERT(ce_append_char(&buffer, 'Y'));
     ASSERT(ce_append_char(&buffer, 'O'));
     ASSERT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "TACOS ARE AWESOME") == 0);
     EXPECT(strcmp(buffer.lines[1], "YO") == 0);

     ce_free_buffer(&buffer);
}

TEST(dupe_buffer_one_line)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS ARE AWESOME");

     char* duped = ce_dupe_buffer(&buffer);

     ASSERT(duped);
     EXPECT(strcmp(duped, "TACOS ARE AWESOME") == 0);

     ce_free_buffer(&buffer);
}

TEST(dupe_buffer_multiline)
{
     Buffer_t buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE");
     buffer.lines[2] = strdup("AWESOME");

     char* duped = ce_dupe_buffer(&buffer);

     ASSERT(duped);
     EXPECT(strcmp(duped, "TACOS\nARE\nAWESOME") == 0);

     ce_free_buffer(&buffer);
}

TEST(sanity_prepend_string)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     ce_prepend_string(&buffer, 0, "MY ");

     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "MY TACOS") == 0);

     ce_free_buffer(&buffer);
}

TEST(dupe_lines)
{
     Buffer_t buffer = {};
     buffer.line_count = 5;
     buffer.lines = malloc(5 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE");
     buffer.lines[2] = strdup("THE");
     buffer.lines[3] = strdup("BEST");
     buffer.lines[4] = strdup("YO");

     char* duped = ce_dupe_lines(&buffer, 1, 3);
     EXPECT(strcmp(duped, "ARE\nTHE\nBEST\n") == 0);

     free(duped);

     duped = ce_dupe_lines(&buffer, 2, 0);
     EXPECT(strcmp(duped, "TACOS\nARE\nTHE\n") == 0);

     free(duped);

     ce_free_buffer(&buffer);
}

TEST(sanity_point_in_line)
{
     Point_t start = {1, 1};
     Point_t end = {3, 3};
     Point_t a = {0, 0};
     Point_t b = {2, 2};

     EXPECT(ce_point_in_range(a, start, end) == false);
     EXPECT(ce_point_in_range(b, start, end) == true);
}

TEST(sanity_last_index)
{
     const char* short_str = "tacos";
     const char* long_str = "TACOS ARE THE BEST";
     const char* no_str = "";

     EXPECT(ce_last_index(short_str) == 4);
     EXPECT(ce_last_index(long_str) == 17);
     EXPECT(ce_last_index(no_str) == 0);
}

TEST(sanity_get_char_raw)
{
     Buffer_t buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE");
     buffer.lines[2] = strdup("AWESOME");

     Point_t point = {2, 0};
     EXPECT(ce_get_char_raw(&buffer, point) == 'C');

     point = (Point_t){1, 1};
     EXPECT(ce_get_char_raw(&buffer, point) == 'R');

     point = (Point_t){4, 2};
     EXPECT(ce_get_char_raw(&buffer, point) == 'O');

     ce_free_buffer(&buffer);
}

TEST(move_cursor_to_beginning_of_file)
{
     Buffer_t buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE");
     buffer.lines[2] = strdup("AWESOME");

     Point_t point = {4, 2};
     ce_move_cursor_to_beginning_of_file(&buffer, &point);
     EXPECT(point.x == 0);
     EXPECT(point.y == 0);

     ce_free_buffer(&buffer);
}

TEST(move_cursor_to_beginning_of_line)
{
     Buffer_t buffer = {};
     buffer.line_count = 3;
     buffer.lines = malloc(3 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");
     buffer.lines[1] = strdup("ARE");
     buffer.lines[2] = strdup("AWESOME");

     Point_t point = {4, 2};
     ce_move_cursor_to_beginning_of_line(&buffer, &point);
     EXPECT(point.x == 0);
     EXPECT(point.y == 2);

     ce_free_buffer(&buffer);
}

TEST(move_cursor_forward_to_char)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("tacos and cats are great");

     Point_t point = {0, 0};
     ce_move_cursor_forward_to_char(&buffer, &point, 'a');
     EXPECT(point.x == 1);
     EXPECT(point.y == 0);

     ce_move_cursor_forward_to_char(&buffer, &point, 'a');
     EXPECT(point.x == 6);
     EXPECT(point.y == 0);

     EXPECT(ce_move_cursor_forward_to_char(&buffer, &point, 'z') == false);
     EXPECT(point.x == 6);
     EXPECT(point.y == 0);

     ce_move_cursor_forward_to_char(&buffer, &point, '\0');
     EXPECT(point.x == (int64_t)(strlen(buffer.lines[0])));
     EXPECT(point.y == 0);

     ce_free_buffer(&buffer);
}

TEST(move_cursor_backward_to_char)
{
     Buffer_t buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("tacos and cats are great");

     Point_t point = {23, 0};
     ce_move_cursor_backward_to_char(&buffer, &point, 'a');
     EXPECT(point.x == 22);
     EXPECT(point.y == 0);

     ce_move_cursor_backward_to_char(&buffer, &point, 'a');
     EXPECT(point.x == 15);
     EXPECT(point.y == 0);

     EXPECT(ce_move_cursor_backward_to_char(&buffer, &point, 'z') == false);
     EXPECT(point.x == 15);
     EXPECT(point.y == 0);

     ce_free_buffer(&buffer);
}

void segv_handler(int signo)
{
     void *array[10];
     size_t size;
     char **strings;
     size_t i;

     size = backtrace(array, 10);
     strings = backtrace_symbols(array, size);

     printf("SIGSEV\n");
     printf("%zd frames.\n", size);
     for (i = 0; i < size; i++) printf ("%s\n", strings[i]);
     printf("\n");

     exit(signo);
}

int main()
{
     Point_t terminal_dimensions = {17, 10};
     g_terminal_dimensions = &terminal_dimensions;

     struct sigaction sa = {};
     sa.sa_handler = segv_handler;
     sigemptyset(&sa.sa_mask);
     if(sigaction(SIGSEGV, &sa, NULL) == -1){
          // TODO: handle error
     }

     RUN_TESTS();
}
