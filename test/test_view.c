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

// TODO: view_switch_to_point()
// TODO: view_switch_to_buffer_list()

TEST(override_with_buffer)
{
     Buffer_t old_buffer = {};
     Buffer_t new_buffer = {};
     Buffer_t* p_old_buffer = &old_buffer;
     BufferView_t view = {};
     view.cursor = (Point_t){5, 7};
     view.buffer = &old_buffer;

     view_override_with_buffer(&view, &new_buffer, &p_old_buffer);

     EXPECT(view.buffer == &new_buffer);
     EXPECT(view.cursor.x == 0);
     EXPECT(view.cursor.y == 0);
     EXPECT(old_buffer.cursor.x == 5);
     EXPECT(old_buffer.cursor.y == 7);
}

TEST(page_up_and_down)
{
     Buffer_t buffer = {};
     buffer.line_count = 100;

     BufferView_t view = {};
     view.buffer = &buffer;
     view.top_left.y = 10;
     view.bottom_right.y = 20;
     view.cursor.y = 10;

     view_move_cursor_half_page_down(&view);
     EXPECT(view.cursor.y == 15);

     view_move_cursor_half_page_up(&view);
     EXPECT(view.cursor.y == 10);
}

int main()
{
     Point_t terminal_dimensions = {0, 0};
     g_terminal_dimensions = &terminal_dimensions;

     RUN_TESTS();
}
