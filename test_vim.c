#include <execinfo.h>
#include <signal.h>

#include "ce_vim.h"
#include "test.h"

TEST(marks_sanity)
{
     VimMarkNode_t* mark_head = NULL;

     Point_t first_mark = {3, 5};
     Point_t second_mark = {17, 31};
     Point_t third_mark = {10, 10};

     vim_mark_add(&mark_head, 'a', &first_mark);
     vim_mark_add(&mark_head, 'b', &second_mark);
     vim_mark_add(&mark_head, 'c', &third_mark);

     Point_t* find_first = vim_mark_find(mark_head, 'a');
     Point_t* find_second = vim_mark_find(mark_head, 'b');
     Point_t* find_third = vim_mark_find(mark_head, 'c');
     Point_t* find_fourth = vim_mark_find(mark_head, 'd');

     ASSERT(find_first);
     ASSERT(find_second);
     ASSERT(find_third);

     EXPECT(!find_fourth);

     EXPECT(find_first->x == first_mark.x && find_first->y == first_mark.y);
     EXPECT(find_second->x == second_mark.x && find_second->y == second_mark.y);
     EXPECT(find_third->x == third_mark.x && find_third->y == third_mark.y);

     vim_marks_free(&mark_head);
}

TEST(yanks_sanity)
{
     VimYankNode_t* yank_head = NULL;

     const char* first_yank = "TACOS ARE THE bestest";
     const char* second_yank = "Random set of words";
     const char* third_yank = "if(code == DESIGNED::WELL){readable = true;}";

     vim_yank_add(&yank_head, 'a', strdup(first_yank), YANK_NORMAL);
     vim_yank_add(&yank_head, 'b', strdup(second_yank), YANK_LINE);
     vim_yank_add(&yank_head, 'c', strdup(third_yank), YANK_NORMAL);

     VimYankNode_t* find_first = vim_yank_find(yank_head, 'a');
     VimYankNode_t* find_second = vim_yank_find(yank_head, 'b');
     VimYankNode_t* find_third = vim_yank_find(yank_head, 'c');
     VimYankNode_t* find_fourth = vim_yank_find(yank_head, 'd');

     ASSERT(find_first);
     ASSERT(find_second);
     ASSERT(find_third);

     EXPECT(!find_fourth);

     EXPECT(strcmp(first_yank, find_first->text) == 0);
     EXPECT(strcmp(second_yank, find_second->text) == 0);
     EXPECT(strcmp(third_yank, find_third->text) == 0);
     EXPECT(find_first->mode == YANK_NORMAL);
     EXPECT(find_second->mode == YANK_LINE);
     EXPECT(find_third->mode == YANK_NORMAL);

     vim_yanks_free(&yank_head);
}

TEST(macro_sanity)
{
     VimMacroNode_t* macro_head = NULL;

     const char* first_string = "ct_Tacos";
     const char* second_string = "viw\"ay";
     const char* third_string = "ggvGd";

     int64_t first_len = strlen(first_string);
     int64_t second_len = strlen(second_string);
     int64_t third_len = strlen(third_string);

     int* first_command = vim_char_string_to_command_string(first_string);
     int* second_command = vim_char_string_to_command_string(second_string);
     int* third_command = vim_char_string_to_command_string(third_string);

     vim_macro_add(&macro_head, 'a', first_command);
     vim_macro_add(&macro_head, 'b', second_command);
     vim_macro_add(&macro_head, 'c', third_command);

     VimMacroNode_t* find_first = vim_macro_find(macro_head, 'a');
     VimMacroNode_t* find_second = vim_macro_find(macro_head, 'b');
     VimMacroNode_t* find_third = vim_macro_find(macro_head, 'c');
     VimMacroNode_t* find_fourth = vim_macro_find(macro_head, 'd');

     ASSERT(find_first);
     ASSERT(find_second);
     ASSERT(find_third);

     EXPECT(!find_fourth);

     EXPECT(memcmp(find_first->command, first_command, first_len * sizeof(int)) == 0);
     EXPECT(memcmp(find_second->command, second_command, second_len * sizeof(int)) == 0);
     EXPECT(memcmp(find_third->command, third_command, third_len * sizeof(int)) == 0);

     vim_macros_free(&macro_head);
}

TEST(macro_commit_sanity)
{
     VimMacroCommitNode_t* macro_commit_head = NULL;

     vim_macro_commits_init(&macro_commit_head);

     KeyNode_t* first_command = NULL;
     KeyNode_t* second_command = NULL;
     KeyNode_t* third_command = NULL;

     ce_keys_push(&first_command, 1);
     ce_keys_push(&second_command, 2);
     ce_keys_push(&third_command, 3);

     vim_macro_commit_push(&macro_commit_head, first_command, false);
     vim_macro_commit_push(&macro_commit_head, second_command, true);
     vim_macro_commit_push(&macro_commit_head, third_command, false);

     VimMacroCommitNode_t* itr = macro_commit_head->prev;
     EXPECT(itr->command_begin->key == third_command->key);

     itr = itr->prev;
     EXPECT(itr->command_begin->key == second_command->key);

     itr = itr->prev;
     EXPECT(itr->command_begin->key == first_command->key);

     vim_macro_commits_free(&itr);
}

typedef struct{
     VimState_t vim_state;
     Buffer_t buffer;
     BufferCommitNode_t* commit_tail;
     AutoComplete_t auto_complete;
     Point_t cursor;
     VimBufferState_t vim_buffer_state;
} KeyHandlerTest_t;

void key_handler_test_init(KeyHandlerTest_t* kht)
{
     memset(kht, 0, sizeof(*kht));
     kht->commit_tail = calloc(1, sizeof(BufferCommitNode_t));
     auto_complete_end(&kht->auto_complete);
}

void key_handler_test_run(KeyHandlerTest_t* kht, const char* string_command)
{
     int* int_command = vim_char_string_to_command_string(string_command);
     int* itr = int_command;
     while(*itr){
          vim_key_handler(*itr, &kht->vim_state, &kht->buffer, &kht->cursor, &kht->commit_tail, &kht->vim_buffer_state,
                          &kht->auto_complete, false);
          itr++;
     }

     free(int_command);
}

void key_handler_test_undo(KeyHandlerTest_t* kht)
{
     ce_commit_undo(&kht->buffer, &kht->commit_tail, &kht->cursor);
}

void key_handler_test_free(KeyHandlerTest_t* kht)
{
     free(kht->commit_tail);
     ce_free_buffer(&kht->buffer);
     vim_yanks_free(&kht->vim_state.yank_head);
     vim_marks_free(&kht->vim_buffer_state.mark_head);
     vim_macros_free(&kht->vim_state.macro_head);
}

TEST(motion_left)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "Tacos are the best");
     kht.cursor.x = 3;

     key_handler_test_run(&kht, "h");

     EXPECT(kht.cursor.x == 2);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_multi_left)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "Tacos are the best");
     kht.cursor.x = 3;

     key_handler_test_run(&kht, "2h");

     EXPECT(kht.cursor.x == 1);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_right)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "Tacos are the best");
     kht.cursor.x = 3;

     key_handler_test_run(&kht, "l");

     EXPECT(kht.cursor.x == 4);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_multi_right)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "Tacos are the best");
     kht.cursor.x = 3;

     key_handler_test_run(&kht, "4l");

     EXPECT(kht.cursor.x == 7);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_up)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "Line one");
     ce_append_line(&kht.buffer, "Line two");
     kht.cursor.y = 1;

     key_handler_test_run(&kht, "k");

     EXPECT(kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_multi_up)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "Line one");
     ce_append_line(&kht.buffer, "Line two");
     ce_append_line(&kht.buffer, "Line three");
     ce_append_line(&kht.buffer, "Line four");
     kht.cursor.y = 3;

     key_handler_test_run(&kht, "2k");

     EXPECT(kht.cursor.y == 1);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_down)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "Line one");
     ce_append_line(&kht.buffer, "Line two");
     kht.cursor.y = 0;

     key_handler_test_run(&kht, "j");

     EXPECT(kht.cursor.y == 1);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_multi_down)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "Line one");
     ce_append_line(&kht.buffer, "Line two");
     ce_append_line(&kht.buffer, "Line three");
     ce_append_line(&kht.buffer, "Line four");
     kht.cursor.y = 0;

     key_handler_test_run(&kht, "2j");

     EXPECT(kht.cursor.y == 2);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_word_little)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(best_food == F_TACOS)");

     key_handler_test_run(&kht, "w");

     EXPECT(kht.cursor.x == 2);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_multi_word_little)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(best_food == F_TACOS)");

     key_handler_test_run(&kht, "3w");

     EXPECT(kht.cursor.x == 13);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_word_big)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(best_food == F_TACOS)");

     key_handler_test_run(&kht, "W");

     EXPECT(kht.cursor.x == 13);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_multi_word_big)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(best_food == F_TACOS)");

     key_handler_test_run(&kht, "2W");

     EXPECT(kht.cursor.x == 16);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_word_beginning_little)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(best_food == F_TACOS)");
     kht.cursor.x = 8;

     key_handler_test_run(&kht, "b");

     EXPECT(kht.cursor.x == 3);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_multi_word_beginning_little)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(best_food == F_TACOS)");
     kht.cursor.x = 8;

     key_handler_test_run(&kht, "2b");

     EXPECT(kht.cursor.x == 2);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_word_beginning_big)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(best_food == F_TACOS)");
     kht.cursor.x = 8;

     key_handler_test_run(&kht, "B");

     EXPECT(kht.cursor.x == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_word_end_little)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(best_food == F_TACOS)");
     kht.cursor.x = 0;

     key_handler_test_run(&kht, "e");

     EXPECT(kht.cursor.x == 1);

     key_handler_test_free(&kht);
}

TEST(motion_multi_word_end_little)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(best_food == F_TACOS)");
     kht.cursor.x = 0;

     key_handler_test_run(&kht, "3e");

     EXPECT(kht.cursor.x == 11);

     key_handler_test_free(&kht);
}

TEST(motion_word_end_big)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(best_food == F_TACOS)");
     kht.cursor.x = 0;

     key_handler_test_run(&kht, "E");

     EXPECT(kht.cursor.x == 11);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_find_next_matching_char)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(ce_insert_line(buffer, 0, new_line)){");
     kht.cursor.x = 0;

     key_handler_test_run(&kht, "fe");
     EXPECT(kht.cursor.x == 4);

     key_handler_test_run(&kht, ";");
     EXPECT(kht.cursor.x == 9);

     key_handler_test_run(&kht, ",");
     EXPECT(kht.cursor.x == 4);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_multi_find_next_matching_char)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(ce_insert_line(buffer, 0, new_line)){");
     kht.cursor.x = 0;

     key_handler_test_run(&kht, "2fe");
     EXPECT(kht.cursor.x == 9);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_find_prev_matching_char)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(ce_insert_line(buffer, 0, new_line)){");
     kht.cursor.x = 15;

     key_handler_test_run(&kht, "Fe");
     EXPECT(kht.cursor.x == 9);

     key_handler_test_run(&kht, ";");
     EXPECT(kht.cursor.x == 4);

     key_handler_test_run(&kht, ",");
     EXPECT(kht.cursor.x == 9);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_multi_find_prev_matching_char)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(ce_insert_line(buffer, 0, new_line)){");
     kht.cursor.x = 15;

     key_handler_test_run(&kht, "2Fe");
     EXPECT(kht.cursor.x == 4);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_to_next_matching_char)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(ce_insert_line(buffer, 0, new_line)){");
     kht.cursor.x = 0;

     key_handler_test_run(&kht, "te");
     EXPECT(kht.cursor.x == 3);

     key_handler_test_run(&kht, ";");
     EXPECT(kht.cursor.x == 8);

     key_handler_test_run(&kht, ",");
     EXPECT(kht.cursor.x == 5);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_multi_to_next_matching_char)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(ce_insert_line(buffer, 0, new_line)){");
     kht.cursor.x = 0;

     key_handler_test_run(&kht, "2te");
     EXPECT(kht.cursor.x == 8);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_to_prev_matching_char)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(ce_insert_line(buffer, 0, new_line)){");
     kht.cursor.x = 15;

     key_handler_test_run(&kht, "Te");
     EXPECT(kht.cursor.x == 10);

     key_handler_test_run(&kht, ";");
     EXPECT(kht.cursor.x == 5);

     key_handler_test_run(&kht, ",");
     EXPECT(kht.cursor.x == 8);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_multi_to_prev_matching_char)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(ce_insert_line(buffer, 0, new_line)){");
     kht.cursor.x = 15;

     key_handler_test_run(&kht, "2Te");
     EXPECT(kht.cursor.x == 5);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_beginning_of_file)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "Line one");
     ce_append_line(&kht.buffer, "Line two");
     ce_append_line(&kht.buffer, "Line three");
     kht.cursor = (Point_t){3, 3};

     key_handler_test_run(&kht, "gg");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_beginning_of_line_hard)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "     if(best.editor == ce)");
     kht.cursor.x = strlen(kht.buffer.lines[0]) - 1;

     key_handler_test_run(&kht, "0");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_beginning_of_line_soft)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "     if(best.editor == ce)");
     kht.cursor.x = strlen(kht.buffer.lines[0]) - 1;

     key_handler_test_run(&kht, "^");
     EXPECT(kht.cursor.x == 5 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_end_of_line_hard)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "     if(best.editor == ce){");

     key_handler_test_run(&kht, "$");
     EXPECT(kht.cursor.x == 26 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_end_of_file)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(best.editor == ce){");
     ce_append_line(&kht.buffer, "     printf(\"hooray!\");");
     ce_append_line(&kht.buffer, "}");

     key_handler_test_run(&kht, "G");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 2);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_search_word_under_cursor_forward)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(best.editor == ce){");
     ce_append_line(&kht.buffer, "     printf(\"we're the best!\");");
     ce_append_line(&kht.buffer, "}");
     kht.cursor.x = 5;

     key_handler_test_run(&kht, "*");
     EXPECT(kht.cursor.x == 23 && kht.cursor.y == 1);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_run(&kht, "N");
     EXPECT(kht.cursor.x == 3 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_run(&kht, "n");
     EXPECT(kht.cursor.x == 23 && kht.cursor.y == 1);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_search_word_under_cursor_backward)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(best.editor == ce){");
     ce_append_line(&kht.buffer, "     printf(\"we're the best!\");");
     ce_append_line(&kht.buffer, "}");
     kht.cursor = (Point_t){23, 1};

     key_handler_test_run(&kht, "#");
     EXPECT(kht.cursor.x == 3 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_matching_paren)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(best.editor == ce){");
     ce_append_line(&kht.buffer, "     printf(\"we're the best!\");");
     ce_append_line(&kht.buffer, "}");
     kht.cursor.x = 2;

     key_handler_test_run(&kht, "%");
     EXPECT(kht.cursor.x == 20 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_next_and_prev_blank_line)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "printf(\"we're the best!\");");
     ce_append_line(&kht.buffer, "");
     ce_append_line(&kht.buffer, "if(best.editor == ce){");
     ce_append_line(&kht.buffer, "     printf(\"we're the best!\");");
     ce_append_line(&kht.buffer, "}");
     ce_append_line(&kht.buffer, "");
     ce_append_line(&kht.buffer, "printf(\"we're the best!\");");
     kht.cursor.y = 3;

     key_handler_test_run(&kht, "{");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 1);

     key_handler_test_run(&kht, "}");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 5);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(motion_insert_next)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "Taco are the best");
     kht.cursor.x = 3;

     key_handler_test_run(&kht, "as\\e");
     EXPECT(kht.cursor.x == 5 && kht.cursor.y == 0);

     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(strcmp(kht.buffer.lines[0], "Tacos are the best") == 0);

     key_handler_test_free(&kht);
}

TEST(motion_insert_after_end_of_line)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "Tacos are the best");

     key_handler_test_run(&kht, "A, obviously\\e");
     EXPECT(kht.cursor.x == 28 && kht.cursor.y == 0);

     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(strcmp(kht.buffer.lines[0], "Tacos are the best, obviously") == 0);

     key_handler_test_free(&kht);
}

TEST(insert_string)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "begin end";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 5;

     key_handler_test_run(&kht, "i and\\e");
     EXPECT(kht.cursor.x == 9 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(strcmp(kht.buffer.lines[0], "begin and end") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(insert_multiline_string)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "begin end";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 5;

     key_handler_test_run(&kht, "i\\rand\\r\\e");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 2);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     ASSERT(kht.buffer.line_count == 3);
     EXPECT(strcmp(kht.buffer.lines[0], "begin") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "and") == 0);
     EXPECT(strcmp(kht.buffer.lines[2], " end") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(insert_backspace)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "begin end";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 5;

     key_handler_test_run(&kht, "i\\b\\b\\b\\e");
     EXPECT(kht.cursor.x == 2 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     ASSERT(kht.buffer.line_count == 1);
     EXPECT(strcmp(kht.buffer.lines[0], "be end") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(insert_backspace_to_prev_line)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "line one");
     ce_append_line(&kht.buffer, "line two");
     ce_append_line(&kht.buffer, "line three");
     kht.cursor = (Point_t){1, 1};

     key_handler_test_run(&kht, "i\\b\\b\\b\\e");
     EXPECT(kht.cursor.x == 7 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     ASSERT(kht.buffer.line_count == 2);
     EXPECT(strcmp(kht.buffer.lines[0], "line onine two") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "line three") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], "line one") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "line two") == 0);
     EXPECT(strcmp(kht.buffer.lines[2], "line three") == 0);

     key_handler_test_free(&kht);
}

TEST(insert_tab)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "begin end";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 5;

     key_handler_test_run(&kht, "i\\t\\e");
     EXPECT(kht.cursor.x == 10 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     ASSERT(kht.buffer.line_count == 1);
     EXPECT(strcmp(kht.buffer.lines[0], "begin      end") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(insert_indent)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "if(tacos){";
     ce_append_line(&kht.buffer, original_line);

     key_handler_test_run(&kht, "A\\reat(tacos);\\e");
     EXPECT(kht.cursor.x == 15 && kht.cursor.y == 1);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     ASSERT(kht.buffer.line_count == 2);
     EXPECT(strcmp(kht.buffer.lines[0], "if(tacos){") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "     eat(tacos);") == 0);

     key_handler_test_undo(&kht);
     EXPECT(kht.buffer.line_count == 1);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(insert_indented_brace)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "if(tacos){";
     ce_append_line(&kht.buffer, original_line);

     key_handler_test_run(&kht, "A\\r}\\e");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 1);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     ASSERT(kht.buffer.line_count == 2);
     EXPECT(strcmp(kht.buffer.lines[0], "if(tacos){") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "}") == 0);

     key_handler_test_undo(&kht);
     EXPECT(kht.buffer.line_count == 1);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(insert_soft_beginning_of_line)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "     begin end";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 9;

     key_handler_test_run(&kht, "Imiddle \\e");
     EXPECT(kht.cursor.x == 12 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     ASSERT(kht.buffer.line_count == 1);
     EXPECT(strcmp(kht.buffer.lines[0], "     middle begin end") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(visual_delete_range)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "begin end";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 5;

     key_handler_test_run(&kht, "vlld");
     EXPECT(kht.cursor.x == 5 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     ASSERT(kht.buffer.line_count == 1);
     EXPECT(strcmp(kht.buffer.lines[0], "begind") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(visual_delete_in_word)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "line one");
     kht.cursor.x = 2;

     key_handler_test_run(&kht, "viwd");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(strcmp(kht.buffer.lines[0], " one") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], "line one") == 0);

     key_handler_test_free(&kht);
}

TEST(visual_delete_line)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "line one");
     ce_append_line(&kht.buffer, "line two");
     ce_append_line(&kht.buffer, "line three");
     kht.cursor.y = 1;

     key_handler_test_run(&kht, "Vd");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 1);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     ASSERT(kht.buffer.line_count == 2);
     EXPECT(strcmp(kht.buffer.lines[0], "line one") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "line three") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], "line one") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "line two") == 0);
     EXPECT(strcmp(kht.buffer.lines[2], "line three") == 0);

     key_handler_test_free(&kht);
}

TEST(visual_delete_multi_line)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "line one");
     ce_append_line(&kht.buffer, "line two");
     ce_append_line(&kht.buffer, "line three");
     ce_append_line(&kht.buffer, "line four");
     kht.cursor.y = 1;

     key_handler_test_run(&kht, "Vjd");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 1);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     ASSERT(kht.buffer.line_count == 2);
     EXPECT(strcmp(kht.buffer.lines[0], "line one") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "line four") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], "line one") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "line two") == 0);
     EXPECT(strcmp(kht.buffer.lines[2], "line three") == 0);

     key_handler_test_free(&kht);
}

TEST(change_delete_little_word)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "if(best.editor == ce){";
     ce_append_line(&kht.buffer, original_line);

     key_handler_test_run(&kht, "dw");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(strcmp(kht.buffer.lines[0], "(best.editor == ce){") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(change_delete_multi_little_words)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "if(best.editor == ce){";
     ce_append_line(&kht.buffer, original_line);

     key_handler_test_run(&kht, "d3w");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(strcmp(kht.buffer.lines[0], ".editor == ce){") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(change_change_little_word)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "if(best.editor == ce){";
     ce_append_line(&kht.buffer, original_line);

     key_handler_test_run(&kht, "cw");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_INSERT);
     EXPECT(strcmp(kht.buffer.lines[0], "(best.editor == ce){") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(change_change_to_end_of_line)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "if(best.editor == ce){";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 5;

     key_handler_test_run(&kht, "Ces suck){\\e");
     EXPECT(kht.cursor.x == 13 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(strcmp(kht.buffer.lines[0], "if(bees suck){") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(change_change_from_soft_beginning_of_line)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "     if(best.editor == ce){";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 10;

     key_handler_test_run(&kht, "SI'm on a boat\\e");
     EXPECT(kht.cursor.x == 17 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(strcmp(kht.buffer.lines[0], "     I'm on a boat") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(change_change_from_soft_beginning_of_line_dupe)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "     if(best.editor == ce){";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 10;

     key_handler_test_run(&kht, "ccI'm on a boat\\e");
     EXPECT(kht.cursor.x == 17 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(strcmp(kht.buffer.lines[0], "     I'm on a boat") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(change_delete_little_beginning_of_word)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "if(best.editor == ce){";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 5;

     key_handler_test_run(&kht, "db");
     EXPECT(kht.cursor.x == 3 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(strcmp(kht.buffer.lines[0], "if(st.editor == ce){") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(change_delete_big_beginning_of_word)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "if(best.editor == ce){";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 5;

     key_handler_test_run(&kht, "dB");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(strcmp(kht.buffer.lines[0], "st.editor == ce){") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(change_delete_line)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "Line one");
     ce_append_line(&kht.buffer, "Line two");
     ce_append_line(&kht.buffer, "Line three");
     kht.cursor.y = 1;

     key_handler_test_run(&kht, "dd");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 1);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(kht.buffer.line_count == 2);
     EXPECT(strcmp(kht.buffer.lines[0], "Line one") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "Line three") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], "Line one") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "Line two") == 0);
     EXPECT(strcmp(kht.buffer.lines[2], "Line three") == 0);

     key_handler_test_free(&kht);
}

TEST(change_delete_line_down)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "Line one");
     ce_append_line(&kht.buffer, "Line two");
     ce_append_line(&kht.buffer, "Line three");
     ce_append_line(&kht.buffer, "Line four");
     kht.cursor.y = 1;

     key_handler_test_run(&kht, "dj");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 1);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(kht.buffer.line_count == 2);
     EXPECT(strcmp(kht.buffer.lines[0], "Line one") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "Line four") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], "Line one") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "Line two") == 0);
     EXPECT(strcmp(kht.buffer.lines[2], "Line three") == 0);
     EXPECT(strcmp(kht.buffer.lines[3], "Line four") == 0);

     key_handler_test_free(&kht);
}

TEST(change_delete_line_up)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "Line one");
     ce_append_line(&kht.buffer, "Line two");
     ce_append_line(&kht.buffer, "Line three");
     ce_append_line(&kht.buffer, "Line four");
     kht.cursor.y = 1;

     key_handler_test_run(&kht, "dk");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(kht.buffer.line_count == 2);
     EXPECT(strcmp(kht.buffer.lines[0], "Line three") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "Line four") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], "Line one") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "Line two") == 0);
     EXPECT(strcmp(kht.buffer.lines[2], "Line three") == 0);
     EXPECT(strcmp(kht.buffer.lines[3], "Line four") == 0);

     key_handler_test_free(&kht);
}

TEST(change_delete_to_end_of_line)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "Line one");
     kht.cursor.x = 3;

     key_handler_test_run(&kht, "D");
     EXPECT(kht.cursor.x == 2 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(strcmp(kht.buffer.lines[0], "Lin") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], "Line one") == 0);

     key_handler_test_free(&kht);
}

TEST(change_delete_inside_pair)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "if(best.food == F_TACOS){";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 5;

     key_handler_test_run(&kht, "di(");
     EXPECT(kht.cursor.x == 3 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(strcmp(kht.buffer.lines[0], "if(){") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(change_delete_inside_word)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "if(best.food == F_TACOS){";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 5;

     key_handler_test_run(&kht, "diw");
     EXPECT(kht.cursor.x == 3 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(strcmp(kht.buffer.lines[0], "if(.food == F_TACOS){") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(change_delete_inside_big_word)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "if(best.food == F_TACOS){";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 5;

     key_handler_test_run(&kht, "diW");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(strcmp(kht.buffer.lines[0], " == F_TACOS){") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(change_delete_around_little_word)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "if(best.food == F_TACOS){";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 5;

     key_handler_test_run(&kht, "daw");
     EXPECT(kht.cursor.x == 3 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(strcmp(kht.buffer.lines[0], "if(.food == F_TACOS){") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(change_delete_around_big_word)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "if(best.food == F_TACOS){";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 5;

     key_handler_test_run(&kht, "daW");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(strcmp(kht.buffer.lines[0], "== F_TACOS){") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(change_delete_around_pair)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "if(best.food == F_TACOS){";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 5;

     key_handler_test_run(&kht, "da(");
     EXPECT(kht.cursor.x == 2 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(strcmp(kht.buffer.lines[0], "if{") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(change_character)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "if(test.food == F_TACOS){";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 3;

     key_handler_test_run(&kht, "rb");
     EXPECT(kht.cursor.x == 3 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(strcmp(kht.buffer.lines[0], "if(best.food == F_TACOS){") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(change_character_then_insert)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "if(test.food == F_TACOS){";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 3;

     key_handler_test_run(&kht, "sbest\\e");
     EXPECT(kht.cursor.x == 7 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(strcmp(kht.buffer.lines[0], "if(bestest.food == F_TACOS){") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(delete_character)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "if(sbest.food == F_TACOS){";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 3;

     key_handler_test_run(&kht, "x");
     EXPECT(kht.cursor.x == 3 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     EXPECT(strcmp(kht.buffer.lines[0], "if(best.food == F_TACOS){") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(join_line)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "line one");
     ce_append_line(&kht.buffer, "line two");

     key_handler_test_run(&kht, "J");
     EXPECT(kht.cursor.x == 8 && kht.cursor.y == 0);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     ASSERT(kht.buffer.line_count == 1);
     EXPECT(strcmp(kht.buffer.lines[0], "line one line two") == 0);

     key_handler_test_undo(&kht);
     ASSERT(kht.buffer.line_count == 2);
     EXPECT(strcmp(kht.buffer.lines[0], "line one") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "line two") == 0);

     key_handler_test_free(&kht);
}

TEST(open_line_below)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "line one");
     ce_append_line(&kht.buffer, "line two");

     key_handler_test_run(&kht, "onew line\\e");
     EXPECT(kht.cursor.x == 7 && kht.cursor.y == 1);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     ASSERT(kht.buffer.line_count == 3);
     EXPECT(strcmp(kht.buffer.lines[0], "line one") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "new line") == 0);
     EXPECT(strcmp(kht.buffer.lines[2], "line two") == 0);

     key_handler_test_undo(&kht);
     ASSERT(kht.buffer.line_count == 2);
     EXPECT(strcmp(kht.buffer.lines[0], "line one") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "line two") == 0);

     key_handler_test_free(&kht);
}

TEST(open_line_above)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "line one");
     ce_append_line(&kht.buffer, "line two");
     kht.cursor.y = 1;

     key_handler_test_run(&kht, "Onew line\\e");
     EXPECT(kht.cursor.x == 7 && kht.cursor.y == 1);
     EXPECT(kht.vim_state.mode == VM_NORMAL);
     ASSERT(kht.buffer.line_count == 3);
     EXPECT(strcmp(kht.buffer.lines[0], "line one") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "new line") == 0);
     EXPECT(strcmp(kht.buffer.lines[2], "line two") == 0);

     key_handler_test_undo(&kht);
     ASSERT(kht.buffer.line_count == 2);
     EXPECT(strcmp(kht.buffer.lines[0], "line one") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "line two") == 0);

     key_handler_test_free(&kht);
}

TEST(mark_set_goto)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(true){");
     ce_append_line(&kht.buffer, "     printf(\"\\n\");");
     ce_append_line(&kht.buffer, "}");
     kht.cursor.y = 1;

     key_handler_test_run(&kht, "maj'a");
     EXPECT(kht.cursor.x == 5 && kht.cursor.y == 1);

     Point_t* mark_loc = vim_mark_find(kht.vim_buffer_state.mark_head, 'a');
     EXPECT(mark_loc && mark_loc->y == 1);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(yank_word)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(true){");
     kht.cursor.x = 3;

     key_handler_test_run(&kht, "yw");
     EXPECT(kht.cursor.x == 3 && kht.cursor.y == 0);

     VimYankNode_t* yank = vim_yank_find(kht.vim_state.yank_head, '0');
     EXPECT(yank && strcmp(yank->text, "true") == 0);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(yank_line)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(true){");
     ce_append_line(&kht.buffer, "     printf(\"\\n\");");
     ce_append_line(&kht.buffer, "}");
     kht.cursor.y = 1;

     key_handler_test_run(&kht, "yy");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 1);

     VimYankNode_t* yank = vim_yank_find(kht.vim_state.yank_head, '0');
     EXPECT(yank && strcmp(yank->text, "     printf(\"\\n\");") == 0);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(yank_to_end_of_line)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(true){");
     kht.cursor.x = 3;

     key_handler_test_run(&kht, "Y");
     EXPECT(kht.cursor.x == 3 && kht.cursor.y == 0);

     VimYankNode_t* yank = vim_yank_find(kht.vim_state.yank_head, '0');
     EXPECT(yank && strcmp(yank->text, "true){") == 0);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(paste_after)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "if(){";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 2;
     vim_yank_add(&kht.vim_state.yank_head, '"', strdup("true"), YANK_NORMAL);

     key_handler_test_run(&kht, "p");
     EXPECT(kht.cursor.x == 6 && kht.cursor.y == 0);
     EXPECT(strcmp(kht.buffer.lines[0], "if(true){") == 0);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(paste_before)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "if(){";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 3;
     vim_yank_add(&kht.vim_state.yank_head, '"', strdup("true"), YANK_NORMAL);

     key_handler_test_run(&kht, "P");
     EXPECT(kht.cursor.x == 3 && kht.cursor.y == 0);
     EXPECT(strcmp(kht.buffer.lines[0], "if(true){") == 0);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(paste_line_after)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(true){");
     ce_append_line(&kht.buffer, "}");
     vim_yank_add(&kht.vim_state.yank_head, '"', strdup("     goto label;"), YANK_LINE);

     key_handler_test_run(&kht, "p");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 1);
     EXPECT(kht.buffer.line_count == 3);
     EXPECT(strcmp(kht.buffer.lines[0], "if(true){") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "     goto label;") == 0);
     EXPECT(strcmp(kht.buffer.lines[2], "}") == 0);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_undo(&kht);
     EXPECT(kht.buffer.line_count == 2);
     EXPECT(strcmp(kht.buffer.lines[0], "if(true){") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "}") == 0);

     key_handler_test_free(&kht);
}

TEST(paste_line_before)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(true){");
     ce_append_line(&kht.buffer, "}");
     vim_yank_add(&kht.vim_state.yank_head, '"', strdup("     goto label;"), YANK_LINE);
     kht.cursor.y = 1;

     key_handler_test_run(&kht, "P");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 1);
     EXPECT(kht.buffer.line_count == 3);
     EXPECT(strcmp(kht.buffer.lines[0], "if(true){") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "     goto label;") == 0);
     EXPECT(strcmp(kht.buffer.lines[2], "}") == 0);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_undo(&kht);
     EXPECT(kht.buffer.line_count == 2);
     EXPECT(strcmp(kht.buffer.lines[0], "if(true){") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "}") == 0);

     key_handler_test_free(&kht);
}

TEST(paste_from_register)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "if(){";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 2;
     vim_yank_add(&kht.vim_state.yank_head, 'a', strdup("true"), YANK_NORMAL);

     key_handler_test_run(&kht, "\"ap");
     EXPECT(kht.cursor.x == 6 && kht.cursor.y == 0);
     EXPECT(strcmp(kht.buffer.lines[0], "if(true){") == 0);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(comment_line)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "if(true){";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 2;

     key_handler_test_run(&kht, "gc");
     EXPECT(kht.cursor.x == 2 && kht.cursor.y == 0);
     EXPECT(strcmp(kht.buffer.lines[0], "//if(true){") == 0);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(comment_multi_line)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(tacos.are.the.best){");
     ce_append_line(&kht.buffer, "     eat(tacos);");
     ce_append_line(&kht.buffer, "}");
     kht.cursor.x = 2;

     key_handler_test_run(&kht, "Vjjgc");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 2);
     EXPECT(strcmp(kht.buffer.lines[0], "//if(tacos.are.the.best){") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "     //eat(tacos);") == 0);
     EXPECT(strcmp(kht.buffer.lines[2], "//}") == 0);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], "if(tacos.are.the.best){") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "     eat(tacos);") == 0);
     EXPECT(strcmp(kht.buffer.lines[2], "}") == 0);

     key_handler_test_free(&kht);
}

TEST(uncomment_line)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "//if(true){";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 2;

     key_handler_test_run(&kht, "gu");
     EXPECT(kht.cursor.x == 2 && kht.cursor.y == 0);
     EXPECT(strcmp(kht.buffer.lines[0], "if(true){") == 0);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(uncomment_multi_line)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "//if(tacos.are.the.best){");
     ce_append_line(&kht.buffer, "     //eat(tacos);");
     ce_append_line(&kht.buffer, "//}");
     kht.cursor.x = 2;

     key_handler_test_run(&kht, "Vjjgu");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 2);
     EXPECT(strcmp(kht.buffer.lines[0], "if(tacos.are.the.best){") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "     eat(tacos);") == 0);
     EXPECT(strcmp(kht.buffer.lines[2], "}") == 0);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], "//if(tacos.are.the.best){") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "     //eat(tacos);") == 0);
     EXPECT(strcmp(kht.buffer.lines[2], "//}") == 0);

     key_handler_test_free(&kht);
}

TEST(indent_line)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "if(true){";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 2;

     key_handler_test_run(&kht, ">>");
     EXPECT(kht.cursor.x == 2 && kht.cursor.y == 0);
     EXPECT(strcmp(kht.buffer.lines[0], "     if(true){") == 0);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(indent_multi_line)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "if(tacos.are.the.best){");
     ce_append_line(&kht.buffer, "     eat(tacos);");
     ce_append_line(&kht.buffer, "}");
     kht.cursor.x = 2;

     key_handler_test_run(&kht, "Vjj>>");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 2);
     EXPECT(strcmp(kht.buffer.lines[0], "     if(tacos.are.the.best){") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "          eat(tacos);") == 0);
     EXPECT(strcmp(kht.buffer.lines[2], "     }") == 0);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], "if(tacos.are.the.best){") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "     eat(tacos);") == 0);
     EXPECT(strcmp(kht.buffer.lines[2], "}") == 0);

     key_handler_test_free(&kht);
}

TEST(unindent_line)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     const char* original_line = "     if(true){";
     ce_append_line(&kht.buffer, original_line);
     kht.cursor.x = 2;

     key_handler_test_run(&kht, "<<");
     EXPECT(kht.cursor.x == 2 && kht.cursor.y == 0);
     EXPECT(strcmp(kht.buffer.lines[0], "if(true){") == 0);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], original_line) == 0);

     key_handler_test_free(&kht);
}

TEST(unindent_multi_line)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "     if(tacos.are.the.best){");
     ce_append_line(&kht.buffer, "          eat(tacos);");
     ce_append_line(&kht.buffer, "     }");
     kht.cursor.x = 2;

     key_handler_test_run(&kht, "Vjj<<");
     EXPECT(kht.cursor.x == 0 && kht.cursor.y == 2);
     EXPECT(strcmp(kht.buffer.lines[0], "if(tacos.are.the.best){") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "     eat(tacos);") == 0);
     EXPECT(strcmp(kht.buffer.lines[2], "}") == 0);

     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], "     if(tacos.are.the.best){") == 0);
     EXPECT(strcmp(kht.buffer.lines[1], "          eat(tacos);") == 0);
     EXPECT(strcmp(kht.buffer.lines[2], "     }") == 0);

     key_handler_test_free(&kht);
}

TEST(changing_modes)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "unused");

     key_handler_test_run(&kht, "i");
     EXPECT(kht.vim_state.mode == VM_INSERT);

     key_handler_test_run(&kht, "\\e");
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_run(&kht, "v");
     EXPECT(kht.vim_state.mode == VM_VISUAL_RANGE);

     key_handler_test_run(&kht, "V");
     EXPECT(kht.vim_state.mode == VM_VISUAL_LINE);

     key_handler_test_run(&kht, "v");
     EXPECT(kht.vim_state.mode == VM_VISUAL_RANGE);

     key_handler_test_run(&kht, "\\e");
     EXPECT(kht.vim_state.mode == VM_NORMAL);

     key_handler_test_free(&kht);
}

TEST(record_macro)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "Buffer_t buffer = {};");

     key_handler_test_run(&kht, "qact_Taco\\ewcetaco\\ef{a0, 3\\eq");

     EXPECT(strcmp(kht.buffer.lines[0], "Taco_t taco = {0, 3};") == 0);

     VimMacroNode_t* macro = vim_macro_find(kht.vim_state.macro_head, 'a');
     ASSERT(macro);

     char* string_cmd = vim_command_string_to_char_string(macro->command);
     EXPECT(strcmp(string_cmd, "ct_Taco\\ewcetaco\\ef{a0, 3\\e") == 0);
     free(string_cmd);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], "Buffer_t buffer = {};") == 0);

     key_handler_test_free(&kht);
}

TEST(play_macro)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "Buffer_t buffer = {};");
     const char* string_command = "ct_Taco\\ewcetaco\\ef{a0, 3\\e";
     int* int_command = vim_char_string_to_command_string(string_command);
     vim_macro_add(&kht.vim_state.macro_head, 'a', int_command);

     key_handler_test_run(&kht, "@a");

     EXPECT(strcmp(kht.buffer.lines[0], "Taco_t taco = {0, 3};") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], "Buffer_t buffer = {};") == 0);

     key_handler_test_free(&kht);
}

TEST(flip_word_case)
{
     KeyHandlerTest_t kht;
     key_handler_test_init(&kht);

     ce_append_line(&kht.buffer, "tacos");

     key_handler_test_run(&kht, "~w");

     EXPECT(strcmp(kht.buffer.lines[0], "TACOS") == 0);

     key_handler_test_undo(&kht);
     EXPECT(strcmp(kht.buffer.lines[0], "tacos") == 0);

     key_handler_test_free(&kht);
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
     struct sigaction sa = {};
     sa.sa_handler = segv_handler;
     sigemptyset(&sa.sa_mask);
     if(sigaction(SIGSEGV, &sa, NULL) == -1){
          // TODO: handle error
     }

     RUN_TESTS();
}
