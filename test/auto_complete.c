#include "auto_complete.h"
#include "test.h"

TEST(insert)
{
     AutoComplete_t auto_complete = {};

     EXPECT(auto_complete_insert(&auto_complete, "one", "the first option"));
     EXPECT(auto_complete_insert(&auto_complete, "two", "the second option"));
     EXPECT(auto_complete_insert(&auto_complete, "three", "the third option"));

     CompleteNode_t* itr = auto_complete.head;

     EXPECT(strcmp(itr->option, "one") == 0);
     EXPECT(strcmp(itr->description, "the first option") == 0);
     itr = itr->next;

     EXPECT(strcmp(itr->option, "two") == 0);
     EXPECT(strcmp(itr->description, "the second option") == 0);
     itr = itr->next;

     EXPECT(strcmp(itr->option, "three") == 0);
     EXPECT(strcmp(itr->description, "the third option") == 0);
     itr = itr->next;

     EXPECT(itr == NULL);
}

TEST(sanity_exact)
{
     AutoComplete_t auto_complete = {};

     EXPECT(auto_complete_insert(&auto_complete, "one", "the first option"));
     EXPECT(auto_complete_insert(&auto_complete, "two", "the second option"));
     EXPECT(auto_complete_insert(&auto_complete, "three", "the third option"));
     EXPECT(auto_complete_insert(&auto_complete, "four", "the fourth option"));

     auto_complete_start(&auto_complete, ACT_EXACT, (Point_t){5, 7});

     EXPECT(auto_completing(&auto_complete));

     EXPECT(auto_complete.start.x == 5);
     EXPECT(auto_complete.start.y == 7);
     EXPECT(auto_complete.type == ACT_EXACT);

     EXPECT(strcmp(auto_complete_get_completion(&auto_complete, 5), "one") == 0);
     EXPECT(strcmp(auto_complete_get_completion(&auto_complete, 6), "ne") == 0);
     EXPECT(auto_complete_get_completion(&auto_complete, 4) == NULL);
     EXPECT(auto_complete_get_completion(&auto_complete, 8) == NULL);

     EXPECT(auto_complete_next(&auto_complete, "t"));
     EXPECT(strcmp(auto_complete_get_completion(&auto_complete, 5), "two") == 0);
     EXPECT(strcmp(auto_complete_get_completion(&auto_complete, 6), "wo") == 0);
     EXPECT(auto_complete_get_completion(&auto_complete, 4) == NULL);
     EXPECT(auto_complete_get_completion(&auto_complete, 8) == NULL);

     EXPECT(auto_complete_next(&auto_complete, "t"));
     EXPECT(strcmp(auto_complete_get_completion(&auto_complete, 5), "three") == 0);
     EXPECT(strcmp(auto_complete_get_completion(&auto_complete, 6), "hree") == 0);
     EXPECT(auto_complete_get_completion(&auto_complete, 4) == NULL);
     EXPECT(auto_complete_get_completion(&auto_complete, 10) == NULL);

     auto_complete_end(&auto_complete);

     EXPECT(!auto_completing(&auto_complete));
}

TEST(sanity_occurance)
{
     AutoComplete_t auto_complete = {};

     EXPECT(auto_complete_insert(&auto_complete, "one", "the first option"));
     EXPECT(auto_complete_insert(&auto_complete, "two", "the second option"));
     EXPECT(auto_complete_insert(&auto_complete, "three", "the third option"));
     EXPECT(auto_complete_insert(&auto_complete, "four", "the fourth option"));

     auto_complete_start(&auto_complete, ACT_OCCURANCE, (Point_t){5, 7});

     EXPECT(auto_completing(&auto_complete));

     EXPECT(auto_complete.start.x == 5);
     EXPECT(auto_complete.start.y == 7);
     EXPECT(auto_complete.type == ACT_OCCURANCE);

     EXPECT(strcmp(auto_complete_get_completion(&auto_complete, 5), "one") == 0);
     EXPECT(strcmp(auto_complete_get_completion(&auto_complete, 6), "ne") == 0);
     EXPECT(auto_complete_get_completion(&auto_complete, 4) == NULL);
     EXPECT(auto_complete_get_completion(&auto_complete, 8) == NULL);

     EXPECT(auto_complete_next(&auto_complete, "o"));
     EXPECT(strcmp(auto_complete_get_completion(&auto_complete, 5), "two") == 0);
     EXPECT(strcmp(auto_complete_get_completion(&auto_complete, 6), "wo") == 0);
     EXPECT(auto_complete_get_completion(&auto_complete, 4) == NULL);
     EXPECT(auto_complete_get_completion(&auto_complete, 8) == NULL);

     EXPECT(auto_complete_next(&auto_complete, "o"));
     EXPECT(strcmp(auto_complete_get_completion(&auto_complete, 5), "four") == 0);
     EXPECT(strcmp(auto_complete_get_completion(&auto_complete, 6), "our") == 0);
     EXPECT(auto_complete_get_completion(&auto_complete, 4) == NULL);
     EXPECT(auto_complete_get_completion(&auto_complete, 9) == NULL);

     auto_complete_end(&auto_complete);

     EXPECT(!auto_completing(&auto_complete));
}

int main()
{
     RUN_TESTS();
}
