#include "test.h"

#include "view.h"

TEST(scroll_to_location)
{
     BufferView_t view = {};

     Point_t location = (Point_t){3, 5};
     view_scroll_to_location(&view, &location);

     EXPECT(view.left_column == 3);
     EXPECT(view.top_row == 5);
}

int main()
{
     RUN_TESTS();
}
