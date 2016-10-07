# ce

[![Build Status](https://travis-ci.org/justy989/ce.svg?branch=master)](https://travis-ci.org/justy989/ce)

Cool commands to run:
fgrep -n <pattern> <files>
ex: fgrep -n buffer *
ctrl+n and ctrl+p will move you between matches

make
ctrl+n and ctrl+p will move you between build failures

cscope -L1<symbol>
ctrl+n and ctrl+p will move you between definitions of that symbol

cscope -L3<function>
cscope -L3ce_insert_char
ctrl+n and ctrl+p will move you between functions calling this function
