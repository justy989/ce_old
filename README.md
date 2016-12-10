# ce: a c language terminal editor

[![Build Status](https://travis-ci.org/justy989/ce.svg?branch=master)](https://travis-ci.org/justy989/ce)

###But why?
- Emacs and Vim are awesome, but they have some problems we'd like to address without having to learn/write
  vimscript or emacs lisp. However, we want to take some awesome ideas from each:
  - Emacs's idea where everything is a just a plain text buffer. We can do things like, run shell
    commands that output to a buffer just like any other. So you can copy+paste, etc.
  - Vim's modal editting. We really like it! It also makes the transition to using ce easier.
    (Note: the default configuration implements vim-like editting, but your configuration can implement
    any arbitrary editting style)
- The config is written in the C programming language and compiled into a shared object that you can reload
  while running. No need to learn a new language!
- Input boxes in both vim and emacs are special 'insert mode only' constructs. In ce, the input box works just
  like any other buffer. So it's easy to do things like paste. It also means the input box can be multiple lines.
- Registers in vim are awesome, but it's not easy to see what you have in registers or even know what
  registers are occupied. In ce, for example, you can type 'q?' to see which macros you have defined. This
  also works for yank registers and mark registers.
- Search in vim/emacs with regular expressions is awesome, but each have a special implementation of regexes
  with special syntax in certain cases. We just want to use the c standard library's regex implementation, so we
  don't have to remember special rules.
- Macros in vim are awesome, but:
  - If you mess up while creating a macro, you have to start over. In ce, when you are viewing your recorded macros,
    you can select any macro to edit it
  - If you accidently 'undo' while creating a macro, it won't replay correctly. In ce, undo clears the part of the
    macro associated with change you want to undo. You can also redo while creating a macro.
- The authors need to work remotely, so the editor needs to be able to run in a terminal.
- It is fun learning how to make a text editor.
- Using something on a daily basis that we created gives us the warm and fuzzies.

###How To Build
- Requirements
  - c11 compiler
  - ncurses library
- Step(s)
  - `$ make`

###How To Run
`$ ce path/to/file.c`

###Default Keybindings (in normal or visual mode)
Key Sequence|Action
------------|------
Ctrl+f|load file
Ctrl+q|kill current window, if there is only 1 window, then quit, or stop recording macro
Ctrl+w|save buffer
Ctrl+a|save buffer as new filename
Ctrl+e|open new unnamed buffer
Ctrl+t|create new tab
Ctrl+x|start/resume dumb terminal
Ctrl+u|page up
Ctrl+d|page down
Ctrl+y|confirm action (used to perform actions based on current buffer)
Ctrl+b|view buffer list (confirm on the cursor selected buffer to open it)
Ctrl+r|redo
Ctrl+v|vertical split
Ctrl+g|horizontal split
Ctrl+h|move cursor to the view to the left
Ctrl+j|move cursor to the view to the below
Ctrl+k|move cursor to the view to the above
Ctrl+l|move cursor to the view to the right
Ctrl+n|goto the next file definition in the shell command buffer (works with compilation errors, fgrep, git diff, etc)
Ctrl+p|goto the previous file definition in the shell command buffer (works with compilation errors, fgrep, git diff, etc)
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
m?|view mark registers (confirm on register you want to goto)
q|record macro to a register (next character typed)
@|replay macro from a register (next character typed)
@?|view macro registers (confirm on register you want to edit)
q?|view macro registers (confirm on register you want to edit)
"|specify yank or paste from a specific register (next character typesd)
"?|view yank registers (confirm on register you want to edit)
y?|view yank registers (confirm on register you want to edit)
zt|scroll view so cursor is at the top
zz|scroll view so cursor is in the middle
zb|scroll view so cursor is at the bottom
gf|goto file under cursor
gt|goto next tab
gT|goto previous tab
gc|comment current line or all lines in visual selection
gu|uncomment current line or all lines in visual selection
gr|reload buffer from associated file
gv|mark the view as overrideable
gl|cycle through line number modes
<<|unindent current line or all lines in visual selection
>>|indent current line or all lines in visual selection
%|find matching quotes, parents, brackets, square brackets, angled brackets
\*|search forward for the word under the cursor
#|search forward for the word under the cursor
~|flip case
F5|reload ce's config

###Cool Shell Commands To Run In ce
`$ fgrep -n -H <pattern> <files>`  
`$ fgrep -n -H Buffer ce_config.c`  
`$ fgrep -n default *`  
ctrl+n and ctrl+p will move you between matches  
  
`$ make`  
ctrl+n and ctrl+p will move you between build failures  
  
`$ cscope -L1<symbol>`  
`$ cscope -L1BufferView`  
ctrl+n and ctrl+p will move you between definitions of that symbol (if tags have been generated)  
  
`$ cscope -L3ce_insert_char`  
ctrl+n and ctrl+p will move you between functions calling this function (if tags have been generated)  

`$ man pthread_create`  
c-syntax highlighted man pages
