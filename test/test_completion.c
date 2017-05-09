#include "test.h"

#include "completion.h"
#include "buffer.h"

#include <stdlib.h>

TEST(update_buffer)
{
     Buffer_t buffer = {};
     buffer_initialize(&buffer);
     buffer.status = BS_READONLY;

     AutoComplete_t auto_complete = {};
     auto_complete_insert(&auto_complete, "first", NULL);
     auto_complete_insert(&auto_complete, "second", NULL);
     auto_complete_insert(&auto_complete, "third", NULL);
     auto_complete_insert(&auto_complete, "fourth", NULL);

     completion_update_buffer(&buffer, &auto_complete, NULL);

     ASSERT(buffer.line_count == 4);
     EXPECT(strcmp(buffer.lines[0], "first") == 0);
     EXPECT(strcmp(buffer.lines[1], "second") == 0);
     EXPECT(strcmp(buffer.lines[2], "third") == 0);
     EXPECT(strcmp(buffer.lines[3], "fourth") == 0);

     completion_update_buffer(&buffer, &auto_complete, "f");

     ASSERT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "first") == 0);
     EXPECT(strcmp(buffer.lines[1], "fourth") == 0);
}

bool line_in_buffer(Buffer_t* buffer, const char* line)
{
     for(int64_t i = 0; i < buffer->line_count; ++i){
          if(strcmp(buffer->lines[i], line) == 0){
               return true;
          }
     }

     return false;
}

TEST(auto_complete_files_in_dir)
{
     EXPECT(system("mkdir completion_test_dir") == 0);
     EXPECT(system("touch completion_test_dir/first") == 0);
     EXPECT(system("touch completion_test_dir/second") == 0);
     EXPECT(system("touch completion_test_dir/third") == 0);

     Buffer_t buffer = {};
     buffer_initialize(&buffer);
     buffer.status = BS_READONLY;

     AutoComplete_t auto_complete = {};
     EXPECT(completion_calc_start_and_path(&auto_complete, "completion_test_dir/", (Point_t){20, 0}, &buffer, "."));

     EXPECT(buffer.line_count == 5);
     EXPECT(line_in_buffer(&buffer, "./"));
     EXPECT(line_in_buffer(&buffer, "../"));
     EXPECT(line_in_buffer(&buffer, "first"));
     EXPECT(line_in_buffer(&buffer, "second"));
     EXPECT(line_in_buffer(&buffer, "third"));

     EXPECT(system("rm -fr completion_test_dir") == 0);
}

int main()
{
     RUN_TESTS();
}
