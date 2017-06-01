#include "test.h"
#include "command.h"

#include <string.h>

TEST(parse_sanity)
{
     Command_t command = {};

     EXPECT(!command_parse(&command, ""));
     EXPECT(command_parse(&command, "command"));
     EXPECT(strcmp(command.name, "command") == 0);
     EXPECT(command.arg_count == 0);
     command_free(&command);

     EXPECT(command_parse(&command, "command arg1"));
     EXPECT(strcmp(command.name, "command") == 0);
     EXPECT(command.arg_count == 1);
     EXPECT(command.args[0].type == CAT_STRING);
     EXPECT(strcmp(command.args[0].string, "arg1") == 0);
     command_free(&command);

     EXPECT(command_parse(&command, "command 10"));
     EXPECT(strcmp(command.name, "command") == 0);
     EXPECT(command.arg_count == 1);
     EXPECT(command.args[0].type == CAT_INTEGER);
     EXPECT(command.args[0].integer == 10);
     command_free(&command);

     EXPECT(command_parse(&command, "command 8.5"));
     EXPECT(strcmp(command.name, "command") == 0);
     EXPECT(command.arg_count == 1);
     EXPECT(command.args[0].type == CAT_DECIMAL);
     EXPECT(command.args[0].decimal == 8.5);
     command_free(&command);

     EXPECT(!command_parse(&command, "command 8.5.3"));

     EXPECT(command_parse(&command, "command arg1 5 3.2"));
     EXPECT(strcmp(command.name, "command") == 0);
     EXPECT(command.arg_count == 3);
     EXPECT(command.args[0].type == CAT_STRING);
     EXPECT(strcmp(command.args[0].string, "arg1") == 0);
     EXPECT(command.args[1].type == CAT_INTEGER);
     EXPECT(command.args[1].integer == 5);
     EXPECT(command.args[2].type == CAT_DECIMAL);
     EXPECT(command.args[2].decimal == 3.2);
     command_free(&command);

     EXPECT(command_parse(&command, "command \"make clean\""));
     command_log(&command);
     EXPECT(strcmp(command.name, "command") == 0);
     EXPECT(command.arg_count == 1);
     EXPECT(command.args[0].type == CAT_STRING);
     EXPECT(strcmp(command.args[0].string, "make clean") == 0);
     command_free(&command);
}

int main()
{
     RUN_TESTS();
}
