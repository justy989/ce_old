#include "test.h"

#include "jump.h"

TEST(sanity)
{
     JumpArray_t array = {};

     jump_insert(&array, "1.c", (Point_t){1, 2});
     jump_insert(&array, "2.c", (Point_t){2, 3});
     jump_insert(&array, "3.c", (Point_t){3, 4});
     jump_insert(&array, "4.c", (Point_t){4, 5});

     const Jump_t* jump = jump_to_previous(&array);

     EXPECT(strcmp(jump->filepath, "4.c") == 0);
     EXPECT(jump->location.x == 4);
     EXPECT(jump->location.y == 5);

     jump = jump_to_previous(&array);

     EXPECT(strcmp(jump->filepath, "3.c") == 0);
     EXPECT(jump->location.x == 3);
     EXPECT(jump->location.y == 4);

     jump = jump_to_previous(&array);

     EXPECT(strcmp(jump->filepath, "2.c") == 0);
     EXPECT(jump->location.x == 2);
     EXPECT(jump->location.y == 3);

     jump = jump_to_previous(&array);

     EXPECT(strcmp(jump->filepath, "1.c") == 0);
     EXPECT(jump->location.x == 1);
     EXPECT(jump->location.y == 2);

     jump = jump_to_previous(&array);

     EXPECT(jump == NULL);

     jump = jump_to_next(&array);

     EXPECT(strcmp(jump->filepath, "2.c") == 0);
     EXPECT(jump->location.x == 2);
     EXPECT(jump->location.y == 3);

     jump = jump_to_next(&array);

     EXPECT(strcmp(jump->filepath, "3.c") == 0);
     EXPECT(jump->location.x == 3);
     EXPECT(jump->location.y == 4);

     jump = jump_to_next(&array);

     EXPECT(strcmp(jump->filepath, "4.c") == 0);
     EXPECT(jump->location.x == 4);
     EXPECT(jump->location.y == 5);

     jump = jump_to_next(&array);

     EXPECT(jump == NULL);
}

int main()
{
     RUN_TESTS();
}
