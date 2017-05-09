#include "test.h"

#include "input.h"
#include "buffer.h"

TEST(input_sanity)
{
     Input_t input = {};
     Buffer_t buffer = {};
     Buffer_t input_buffer = {};
     BufferView_t input_view = {};
     BufferView_t real_view = {};
     BufferView_t* view = &real_view;
     VimState_t vim_state = {};

     view->buffer = &buffer;
     input.view = &input_view;
     input_view.buffer = &input_buffer;
     input_start(&input, &view, &vim_state, "Input", ':');
     EXPECT(input.key == ':');
     EXPECT(strcmp(input.message, "Input") == 0);

     input_end(&input, &vim_state);
     EXPECT(input.key == 0);
}

TEST(get_history)
{
     Input_t input = {};
     Buffer_t buffer = {};
     Buffer_t input_buffer = {};
     BufferView_t input_view = {};
     BufferView_t real_view = {};
     BufferView_t* view = &real_view;
     VimState_t vim_state = {};

     view->buffer = &buffer;
     input.view = &input_view;
     input_view.buffer = &input_buffer;
     input_start(&input, &view, &vim_state, "Input", '/');
     EXPECT(input_get_history(&input) == &input.search_history);

     input_start(&input, &view, &vim_state, "Input", '?');
     EXPECT(input_get_history(&input) == &input.search_history);

     input_start(&input, &view, &vim_state, "Input", ':');
     EXPECT(input_get_history(&input) == &input.command_history);
}

TEST(cancel)
{
     Input_t input = {};
     Buffer_t buffer = {};
     Buffer_t input_buffer = {};
     BufferView_t input_view = {};
     BufferView_t real_view = {};
     BufferView_t* view = &real_view;
     VimState_t vim_state = {};

     view->buffer = &buffer;
     input.view = &input_view;
     input_view.buffer = &input_buffer;
     input_start(&input, &view, &vim_state, "Input", ':');
     EXPECT(input.key == ':');
     EXPECT(strcmp(input.message, "Input") == 0);

     input_cancel(&input, &view, &vim_state);
     EXPECT(input.key == 0);
}

TEST(commit_to_history)
{
     Input_t input = {};
     Buffer_t buffer = {};
     Buffer_t input_buffer = {};
     BufferView_t input_view = {};
     BufferView_t real_view = {};
     BufferView_t* view = &real_view;
     VimState_t vim_state = {};

     text_history_init(&input.search_history);
     text_history_init(&input.command_history);

     view->buffer = &buffer;
     input.view = &input_view;
     input_view.buffer = &input_buffer;
     input_start(&input, &view, &vim_state, "Input", '/');
     EXPECT(input.key == '/');
     EXPECT(strcmp(input.message, "Input") == 0);

     ce_append_string(&input_buffer, 0, "tacos");

     input_commit_to_history(&input_buffer, &input.search_history);
     EXPECT(strcmp(input_buffer.lines[0], input.search_history.head->entry) == 0);
}

TEST(history_iterate)
{
     Input_t input = {};
     Buffer_t buffer = {};
     Buffer_t input_buffer = {};
     BufferView_t input_view = {};
     BufferView_t real_view = {};
     BufferView_t* view = &real_view;
     VimState_t vim_state = {};

     text_history_init(&input.search_history);
     text_history_init(&input.command_history);

     buffer_initialize(&input_buffer);

     view->buffer = &buffer;
     input.view = &input_view;
     input_view.buffer = &input_buffer;
     input_start(&input, &view, &vim_state, "Input", '/');
     EXPECT(input.key == '/');
     EXPECT(strcmp(input.message, "Input") == 0);

     ce_append_string(&input_buffer, 0, "tacos");
     input_commit_to_history(&input_buffer, &input.search_history);
     ce_clear_lines(&input_buffer);

     ce_append_string(&input_buffer, 0, "are");
     input_commit_to_history(&input_buffer, &input.search_history);
     ce_clear_lines(&input_buffer);

     ce_append_string(&input_buffer, 0, "awesome");
     input_commit_to_history(&input_buffer, &input.search_history);
     ce_clear_lines(&input_buffer);

     input_history_iterate(&input, true);
     TextHistory_t* history = input_get_history(&input);
     ASSERT(history);
     EXPECT(strcmp(history->cur->entry, "awesome") == 0);

     input_history_iterate(&input, true);
     EXPECT(strcmp(history->cur->entry, "are") == 0);

     input_history_iterate(&input, true);
     EXPECT(strcmp(history->cur->entry, "tacos") == 0);

     input_history_iterate(&input, true);
     EXPECT(strcmp(history->cur->entry, "tacos") == 0);

     input_history_iterate(&input, false);
     EXPECT(strcmp(history->cur->entry, "are") == 0);

     input_history_iterate(&input, false);
     EXPECT(strcmp(history->cur->entry, "awesome") == 0);

     input_history_iterate(&input, false);
     EXPECT(history->cur->entry == NULL);
}

int main()
{
     RUN_TESTS();
}
