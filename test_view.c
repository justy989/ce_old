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

TEST(center)
{
     BufferView_t view = {};
     view.top_left = (Point_t){2, 4};
     view.bottom_right = (Point_t){10, 20};
     view.cursor = (Point_t){8, 16};

     view_center(&view);

     EXPECT(view.left_column == 0);
     EXPECT(view.top_row == 8);
}

TEST(center_when_cursor_outside_portion)
{
     BufferView_t view = {};
     view.top_left = (Point_t){0, 0};
     view.bottom_right = (Point_t){10, 20};
     view.cursor = (Point_t){8, 18};

     view_center_when_cursor_outside_portion(&view, 0.05f, 0.95f);

     EXPECT(view.left_column == 0);
     EXPECT(view.top_row == 0);

     view_center_when_cursor_outside_portion(&view, 0.2f, 0.8f);

     EXPECT(view.left_column == 0);
     EXPECT(view.top_row == 8);
}

TEST(follow_cursor)
{
     Buffer_t buffer = {};
     buffer.line_count = 50;
     BufferView_t view = {};
     view.top_left = (Point_t){0, 0};
     view.bottom_right = (Point_t){10, 20};
     view.cursor = (Point_t){20, 40};
     view.buffer = &buffer;

     view_follow_cursor(&view, LNT_NONE);

     EXPECT(view.left_column == 11);
     EXPECT(view.top_row == 21);
}

TEST(follow_highlight)
{
     Buffer_t buffer = {};
     buffer.line_count = 50;
     buffer.highlight_start = (Point_t){20, 40};
     BufferView_t view = {};
     view.top_left = (Point_t){0, 0};
     view.bottom_right = (Point_t){10, 20};
     view.buffer = &buffer;

     view_follow_highlight(&view);

     EXPECT(view.left_column == 11);
     EXPECT(view.top_row == 21);
}

TEST(split)
{
     Buffer_t buffer = {};
     BufferView_t head_view = {};
     head_view.buffer = &buffer;

     view_split(&head_view, &head_view, true, LNT_NONE);
     view_split(&head_view, &head_view, false, LNT_NONE);

     EXPECT(head_view.next_horizontal);
     EXPECT(head_view.next_vertical);
}

//TEST(switch_to_point)
//{

//}

int main()
{
     Point_t terminal_dimensions = {0, 0};
     g_terminal_dimensions = &terminal_dimensions;

     RUN_TESTS();
}
