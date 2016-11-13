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
