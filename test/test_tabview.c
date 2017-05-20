#include "test.h"

#include "tab_view.h"

TEST(sanity)
{
     TabView_t* view_head = calloc(1, sizeof(*view_head));
     TabView_t* first_new = tab_view_insert(view_head);
     TabView_t* second_new = tab_view_insert(view_head);

     EXPECT(view_head->next == first_new);
     EXPECT(view_head->next->next == second_new);
     EXPECT(view_head->next->next->next == NULL);

     tab_view_remove(&view_head, first_new);
     EXPECT(view_head->next == second_new);
     EXPECT(view_head->next->next == NULL);

     tab_view_remove(&view_head, view_head);
     EXPECT(view_head == second_new);

     tab_view_remove(&view_head, view_head);
     EXPECT(!view_head);
}

int main()
{
     RUN_TESTS();
}
