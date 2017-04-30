#include "test.h"

#include "info.h"
#include "ce_config.h"

#include <string.h>

// TODO: for now don't really validate the format of these buffers as they are expected to change
TEST(buffer_list)
{
     BufferNode_t* head = NULL;

     Buffer_t one = {};
     Buffer_t two = {};
     Buffer_t three = {};

     one.name = strdup("one");
     two.name = strdup("two");
     three.name = strdup("three");

     ce_append_buffer_to_list(&head, &one);
     ce_append_buffer_to_list(&head, &two);
     ce_append_buffer_to_list(&head, &three);

     Buffer_t buffer = {};

     info_update_buffer_list_buffer(&buffer, head);

     EXPECT(buffer.line_count > 0);
}

TEST(mark_list)
{
     Buffer_t marked = {};

     BufferState_t* buffer_state = calloc(1, sizeof(*buffer_state));
     ASSERT(buffer_state);

     Point_t first = {3, 4};
     Point_t second = {8, 5};
     Point_t third = {7, 2};
     vim_mark_add(&buffer_state->vim_buffer_state.mark_head, 'a', &first);
     vim_mark_add(&buffer_state->vim_buffer_state.mark_head, 'z', &second);
     vim_mark_add(&buffer_state->vim_buffer_state.mark_head, 'm', &third);

     marked.user_data = buffer_state;

     Buffer_t buffer = {};

     info_update_mark_list_buffer(&buffer, &marked);

     EXPECT(buffer.line_count > 0);
}

TEST(yank_list)
{
     VimYankNode_t* head = NULL;

     Buffer_t buffer = {};

     vim_yank_add(&head, 'a', "first text", YANK_NORMAL);
     vim_yank_add(&head, 'z', "next text", YANK_NORMAL);
     vim_yank_add(&head, 'm', "last text", YANK_NORMAL);

     info_update_yank_list_buffer(&buffer, head);

     EXPECT(buffer.line_count > 0);
}

TEST(macro_list)
{
     VimState_t vim_state = {};

     int first[] = {'d', 'e', 0};
     int second[] = {'y', '$', 0};
     int third[] = {'f', '2', '_', 0};

     vim_macro_add(&vim_state.macro_head, 'a', first);
     vim_macro_add(&vim_state.macro_head, 'z', second);
     vim_macro_add(&vim_state.macro_head, 'm', third);

     Buffer_t buffer = {};

     info_update_macro_list_buffer(&buffer, &vim_state);

     EXPECT(buffer.line_count > 0);
}

int main()
{
     RUN_TESTS();
}
