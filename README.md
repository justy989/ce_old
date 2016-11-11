# ce: a c language terminal editor

[![Build Status](https://travis-ci.org/justy989/ce.svg?branch=master)](https://travis-ci.org/justy989/ce)

###But why?
-Emacs and Vim are awesome, but they have some problems we'd like to address without having to learn vimscript
 or emacs lisp
-The config is written in 'c' and compiled into a shared object that you can reload while running. No need to
 learn a new language!
-The authors need to work remotely through a terminal, so the editor needs to be able to run in a terminal.
-Editting in Vim is awesome, Emacs's idea of everything being pain text in a buffer is awesome, so we combine
 those ideas
 -Taking emacs's idea where everything is a buffer, we can do things like, run shell commands that output to a
  buffer just like any other. So you can copy+paste, etc.
 -Taking vim's editting, we were able to transition to using the editor fulltime pretty quickly
-Input boxes in both vim and emacs are special 'insert mode only' constructs. In ce, the input box works just
 like any other buffer. So it's easy to do things like paste. It also means the input box can be multiple lines.
-Registers in vim are also awesome, but it's not easy to see what you have in registers or even know what
 registers are occupied. In ce, for example, you can type 'q?' to see which macros you have defined. This
 also works for paste registers and marks.
-Macros in vim are awesome, but:
 -If you mess up while creating a macro, you have to start over. In ce, when you are viewing your recorded macros,
  you can select one to edit it
 -If you accidently 'undo' while creating a macro, it won't replay correctly. In ce, undo clears the part of the macro associated with
 change you want to undo. You can also redo that macro.
-It is fun learning how to make a text editor. Also, Using something you created on a daily basis is pretty great.

###Build
1. Dependencies: c11 enabled compiler, libncurses
2. run `make`

###Default keybindings (when not in insert mode)
Key Sequence|Action
------------|------
Ctrl+f|load file
Ctrl+q|kill current window, if there is only 1 window, then quit, or stop recording macro
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
Ctrl+n|goto the next file definition in the shell command buffer (works with compilation errors, fgrep, etc)
Ctrl+p|goto the previous file definition in the shell command buffer (works with compilation errors, fgrep, etc)
Ctrl+y|confirm input, or if cursor on file definition in shell buffer then goto it, or if the cursor is on a buffer in the buffer list then goto it
i|enter insert mode
esc|enter normal mode
h|move cursor left
j|move cursor down
k|move cursor up
l|move cursor right|
w|move by word
e|move to end of word
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
o|insert a new line and move the cursor
O|insert a new line before the cursor and move the cursor
m|set mark in register (next character typed)
m?|view mark registers
q|record macro to a register (next character typed)
@|replay macro from a register (next character typed)
@?|view macro registers
q?|view macro registers
"|specify yank or paste from a specific register (next character typesd)
"?|view yank registers
y?|view yank registers
zt|scroll view so cursor is at the top
zz|scroll view so cursor is in the middle
zb|scroll view so cursor is at the bottom
gf|goto file under cursor
gt|goto next tab
gT|goto previous tab
gc|comment line
gu|uncomment line
gr|reload buffer from associated file
gv|mark the view as overrideable
gl|cycle through line number modes
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
