#include "test.h"
#include "buffer.h"

TEST(initialize)
{
     Buffer_t buffer = {};

     EXPECT(buffer_initialize(&buffer));
     EXPECT(buffer.user_data);
     BufferState_t* buffer_state = buffer.user_data;
     EXPECT(buffer_state->commit_tail)
     EXPECT(buffer.mark.x == -1);
     EXPECT(buffer.mark.y == -1);
     EXPECT(buffer.type == BFT_PLAIN);
}

TEST(create_empty)
{
     BufferNode_t* head = NULL;
     BufferNode_t* buffer_node = buffer_create_empty(&head, "name_of_file");

     EXPECT(buffer_node);
     EXPECT(head);
     EXPECT(buffer_node == head);

     EXPECT(strcmp(buffer_node->buffer->name, "name_of_file") == 0);
     EXPECT(buffer_node->buffer->line_count == 0);
}

TEST(create_from_file)
{
     BufferNode_t* head = NULL;
     BufferNode_t* buffer_node = buffer_create_from_file(&head, "Makefile");

     EXPECT(buffer_node);
     EXPECT(head);
     EXPECT(buffer_node == head);

     EXPECT(strcmp(buffer_node->buffer->name, "Makefile") == 0);
     EXPECT(buffer_node->buffer->line_count > 0);
}

TEST(delete_at_index)
{
     BufferNode_t* head = NULL;
     TerminalNode_t* terminal_head = NULL;
     TerminalNode_t* terminal_current = NULL;
     TabView_t tab_head = {};
     BufferView_t view = {};
     tab_head.view_head = &view;
     tab_head.view_current = &view;

     Buffer_t one = {};
     Buffer_t two = {};
     Buffer_t three = {};

     EXPECT(buffer_initialize(&one));
     EXPECT(buffer_initialize(&two));
     EXPECT(buffer_initialize(&three));

     EXPECT(ce_append_buffer_to_list(&head, &one) != NULL);
     EXPECT(ce_append_buffer_to_list(&head, &two) != NULL);
     EXPECT(ce_append_buffer_to_list(&head, &three) != NULL);

     EXPECT(buffer_delete_at_index(&head, &tab_head, 1, &terminal_head, &terminal_current));
     BufferNode_t* itr = head;
     EXPECT(itr->buffer == &one);
     itr = itr->next;
     EXPECT(itr->buffer == &three);
     itr = itr->next;
     EXPECT(itr == NULL);

     EXPECT(buffer_delete_at_index(&head, &tab_head, 1, &terminal_head, &terminal_current));
     itr = head;
     EXPECT(itr->buffer == &one);
     itr = itr->next;
     EXPECT(itr == NULL);

     EXPECT(!buffer_delete_at_index(&head, &tab_head, 1, &terminal_head, &terminal_current));
     EXPECT(buffer_delete_at_index(&head, &tab_head, 0, &terminal_head, &terminal_current));
     itr = head;
     EXPECT(itr == NULL);
}

int main()
{
     RUN_TESTS();
}
