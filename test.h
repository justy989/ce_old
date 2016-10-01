#ifndef TEST_HPP
#define TEST_HPP

typedef struct{
     int passed;
     int failed;
} Results;

typedef void test_func(bool*);

// NOTE: we currently rely on __COUNTER__ being 0 to start! We can deal with this in the future if we want to
//       use this for other projects!

#define INDIR_TEST_FUNC(n) g_test_func_##n
#define GLOBAL_TEST_FUNC(n) INDIR_TEST_FUNC(n)

#define TEST(name)                                    \
     void name(bool* _test_failed);                   \
     test_func* GLOBAL_TEST_FUNC(__COUNTER__) = name; \
     void name(bool* _test_failed)

#define EXPECT(cond)                                                                             \
     if(!(cond)){                                                                                \
          printf("%s:%d %s() FAILED expecting (%s)\n", __FILE__, __LINE__, __FUNCTION__, #cond); \
          *_test_failed = true;                                                                  \
     }

#define ASSERT(cond)                                                                             \
     if(!(cond)){                                                                                \
          printf("%s:%d %s() FAILED asserting (%s)\n", __FILE__, __LINE__, __FUNCTION__, #cond); \
          *_test_failed = true;                                                                  \
          return;                                                                                \
     }

// NOTE: In registering tests, I'm so sick of the freakin pre-processor being lame, so I'm going to take
//       advantage of the knowledge that my globals are layed out sequentially
#define RUN_TESTS()                                        \
{                                                          \
     Results results = {0, 0};                             \
     int test_count = __COUNTER__;                         \
     printf("executing %d tests\n\n", test_count);         \
     for(int i = 0; i < test_count; ++i){                  \
          bool failed = false;                             \
          (*(&g_test_func_0 + i))(&failed);                \
          if(failed) results.failed++;                     \
          else results.passed++;                           \
     }                                                     \
     if(results.failed){                                   \
          printf("\n%d test(s) failed\n", results.failed); \
          return 1;                                        \
     }                                                     \
     printf("\nall test(s) passed\n");                     \
     return 0;                                             \
}

#endif
