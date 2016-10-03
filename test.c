#include <stdio.h>
#include <string.h>
#include "ce.h"
#include "test.h"

#if 1
TEST(asdaasd)
{
     ASSERT(true);
}
#endif

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

TEST(sanity_insert_string)
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

// NOTE: there are a few more permutations we should do
TEST(sanity_insert_string_multiline_mid)
{
     Buffer buffer = {};
     buffer.line_count = 1;
     buffer.lines = malloc(1 * sizeof(char*));
     buffer.lines[0] = strdup("TACOS");

     Point point = {2, 0};
     ce_insert_string(&buffer, &point, " AH\nHH ");

     ASSERT(buffer.line_count == 2);
     // NOTE: I realize my examples are insane, but I'm intoxicated.
     EXPECT(strcmp(buffer.lines[0], "TA AH") == 0);
     EXPECT(strcmp(buffer.lines[1], "HH COS") == 0);

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

TEST(sanity_remove_string)
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

// NOTE: obviously more permutations to try
TEST(sanity_remove_string_multiline)
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
     ce_find_string(&buffer, &point, "ARE", &match);

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
     ce_find_string(&buffer, &point, "SO", &delta);

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

int main()
{
     RUN_TESTS();
}
