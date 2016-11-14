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
