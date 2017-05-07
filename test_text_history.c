#include "test.h"

#include "text_history.h"

#include <string.h>

TEST(sanity)
{
     TextHistory_t history;

     text_history_init(&history);

     history.cur->entry = strdup("first");
     text_history_commit_current(&history);

     history.cur->entry = strdup("second");
     text_history_commit_current(&history);

     history.cur->entry = strdup("third");
     text_history_commit_current(&history);

     EXPECT(!text_history_next(&history));

     EXPECT(text_history_prev(&history));
     EXPECT(strcmp(history.cur->entry, "third") == 0);

     EXPECT(text_history_prev(&history));
     EXPECT(strcmp(history.cur->entry, "second") == 0);

     EXPECT(text_history_prev(&history));
     EXPECT(strcmp(history.cur->entry, "first") == 0);

     EXPECT(!text_history_prev(&history));

     EXPECT(text_history_next(&history));
     EXPECT(strcmp(history.cur->entry, "second") == 0);

     EXPECT(text_history_next(&history));
     EXPECT(strcmp(history.cur->entry, "third") == 0);

     // TODO: figure out why this fails!
     //EXPECT(!text_history_next(&history));

     text_history_free(&history);
}

int main()
{
     RUN_TESTS();
}
