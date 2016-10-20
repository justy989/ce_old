# ce: a c language terminal editor

[![Build Status](https://travis-ci.org/justy989/ce.svg?branch=master)](https://travis-ci.org/justy989/ce)

###Build
1. Dependencies: c11 enabled compiler, libncurses
2. run `make`

###Default keybindings (when not in insert mode)
Key Sequence|Action
------------|------
Ctrl+f|load file
Ctrl+q|kill current window, if there is only 1 window, then quit
Ctrl+w|save buffer
Ctrl+a|save buffer as new filename
Ctrl+e|open new unnamed buffer
Ctrl+t|create new tab
Ctrl+x|shell command
Ctrl+i|send shell command input
Ctrl+u|page up
Ctrl+d|page down
Ctrl+b|view buffer list (hit return on the buffer to open it in the buffer list view)
Ctrl+r|redo
Ctrl+v|vertical split
Ctrl+g|horizontal split
Ctrl+h|move cursor to the view to the left
Ctrl+j|move cursor to the view to the below
Ctrl+k|move cursor to the view to the above
Ctrl+l|move cursor to the view to the right
Ctrl+n|goto the next file definition in the shell command buffer
Ctrl+p|goto the previous file definition in the shell command buffer
Ctrl+y|confirm input, or if cursor on file definition in shell buffer then goto it, or if the cursor is on a buffer in the buffer list then goto it
i|enter insert mode
esc|enter normal mode
h|move cursor left
j|move cursor down
k|move cursor up
l|move cursor right|
w|move by word
e|m|ove to end of word
b|move to beginning of word
c|change
d|delete
r|replace character
x|remove character
s|remove character and enter insert
f|goto character on line (next character typed)
t|goto before character on line (next character typed)
y|yank
p|paste after cursor
P|paste before cursor
u|undo
/|incremental search forward
?|incremental search backward
n|goto next search match
N|goto previous search match
v|visual mode
V|visual line mode
zt|scroll view so cursor is at the top
zz|scroll view so cursor is in the middle
zb|scroll view so cursor is at the bottom
gf|goto file under cursor
gt|goto next tab
gT|goto previous tab
gc|comment line
gu|uncomment line
gr|reload buffer from associated file
<<|unindent line
>>|indent line
%|find matching quotes, parents, brackets, square brackets, angled brackets
\*|search forward for the word under the cursor
#|search forward for the word under the cursor
~|flip case

###Cool commands to run
`fgrep -n -H <pattern> <files>`  
`fgrep -n -H Buffer ce_config.c`  
`fgrep -n default *`  
ctrl+n and ctrl+p will move you between matches  
  
`make`  
ctrl+n and ctrl+p will move you between build failures  
  
`cscope -L1<symbol>`  
`cscope -L1BufferView`  
ctrl+n and ctrl+p will move you between definitions of that symbol (if tags have been generated)  
  
`cscope -L3ce_insert_char`  
ctrl+n and ctrl+p will move you between functions calling this function (if tags have been generated)  

`man pthread_create`  
c syntax highlighted man pages
