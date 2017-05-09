#include "test.h"

#include "misc.h"
#include "buffer.h"

TEST(buffer_flag_string)
{
     Buffer_t buffer = {};

     EXPECT(strlen(misc_buffer_flag_string(&buffer)) == 0);
     buffer.status = BS_READONLY;
     EXPECT(strlen(misc_buffer_flag_string(&buffer)));

     buffer.status = BS_NEW_FILE;
     EXPECT(strlen(misc_buffer_flag_string(&buffer)));

     buffer.status = BS_MODIFIED;
     EXPECT(strlen(misc_buffer_flag_string(&buffer)));
}

TEST(count_digits)
{
     EXPECT(misc_count_digits(3) == 1);
     EXPECT(misc_count_digits(92) == 2);
     EXPECT(misc_count_digits(652) == 3);
     EXPECT(misc_count_digits(8924) == 4);
     EXPECT(misc_count_digits(12345) == 5);
}

TEST(get_cursor_on_user_terminal)
{
     Point_t cursor = {3, 5};
     Buffer_t buffer = {};
     BufferView_t buffer_view = {};
     buffer_view.buffer = &buffer;

     buffer_view.left_column = 2;
     buffer_view.top_row = 4;
     buffer_view.top_left.x = 4;
     buffer_view.top_left.y = 6;

     Point_t result = misc_get_cursor_on_user_terminal(&cursor, &buffer_view, LNT_NONE);
     EXPECT(result.x == 5);
     EXPECT(result.y == 7);

     // TODO: more cases with different line number types and buffer line counts
}

// TODO: move_jump_location_to_end_of_output

TEST(quit_and_prompt)
{
     TabView_t tab_view = {};
     BufferView_t current_view = {};
     BufferView_t input_view = {};
     ConfigState_t config_state = {};
     config_state.tab_current = &tab_view;
     config_state.input.view = &input_view;
     config_state.input.view->buffer = &config_state.input.buffer;
     tab_view.view_current = &current_view;
     BufferNode_t* head = NULL;

     buffer_create_empty(&head, "empty");

     misc_quit_and_prompt_if_unsaved(&config_state, head);

     EXPECT(config_state.quit);
     config_state.quit = false;

     BufferNode_t* new_node = buffer_create_empty(&head, "modified");
     new_node->buffer->status = BS_MODIFIED;

     misc_quit_and_prompt_if_unsaved(&config_state, head);

     EXPECT(!config_state.quit);
}

int main()
{
     RUN_TESTS();
}
