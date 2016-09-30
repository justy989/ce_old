#include <stdio.h>
#include <string.h>
#include "ce.h"

typedef struct{
     int passed;
     int failed;
} Results;

typedef void test_func(bool*);

typedef struct TestFuncNode {
     test_func* func;
     struct TestFuncNode* next;
} TestFuncNode;

typedef struct{
     TestFuncNode* head;
     TestFuncNode* tail;
} TestList;

void insert_test_func(TestList* list, test_func* func)
{
     TestFuncNode* node = malloc(sizeof(*node));
     node->func = func;

     if(list->tail){
          list->tail->next = node;
     }else{
          list->head = node;
     }

     list->tail = node;
}

#define TEST(name) void name(bool* _test_failed)

#define EXPECT(cond)                                                \
     if(!(cond)){                                                   \
          printf("%s() failed expect (%s)\n", __FUNCTION__, #cond); \
          *_test_failed = true;                                     \
     }

#define ASSERT(cond)                                                \
     if(!(cond)){                                                   \
          printf("%s() failed assert (%s)\n", __FUNCTION__, #cond); \
          *_test_failed = true;                                     \
          return;                                                   \
     }

TEST(alloc_lines)
{
     Buffer buffer;
     buffer.filename = strdup("test.txt");
     ce_alloc_lines(&buffer, 3);

     ASSERT(buffer.lines);
     EXPECT(buffer.line_count == 3);

     ce_free_buffer(&buffer);
}

TEST(load_string)
{
     const char* str = "TACOS";

     Buffer buffer;
     buffer.filename = strdup("test.txt");
     ce_alloc_lines(&buffer, 1);
     ce_load_string(&buffer, str);

     ASSERT(buffer.lines);
     ASSERT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], str) == 0);

     ce_free_buffer(&buffer);
}

int main()
{
     TestList test_list = {NULL, NULL};
     Results results = {0, 0};

     insert_test_func(&test_list, alloc_lines);
     insert_test_func(&test_list, load_string);

     TestFuncNode* itr = test_list.head;
     while(itr){
          bool failed = false;
          itr->func(&failed);
          if(failed) results.failed++;
          else results.passed++;
          itr = itr->next;
     }

     printf("%d tests run, %d passed, %d failed\n", results.passed + results.failed, results.passed, results.failed);

     // TODO: free test list?

     if(results.failed){
          return 1;
     }

     return 0;
}
