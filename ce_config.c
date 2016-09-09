#include "ce.h"

bool key_handler(int key, Buffer* buffer, Point* p)
{
     ce_insert_char(buffer, p, key);
     return true;
}
