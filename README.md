About
=====================
RPN (for "reverse polish notation") is a compiler for a toy Forth-like language. It compiles to LLVM IR and was created to teach myself about LLVM and compilers. Along the way, I also learned some C++ and Forth.

RPN has Forth-inspired commands and syntax, including several stack-manipulating words, the ability to define new words, including words with recursive definitions, and loops. It features a JIT-compiling REPL and can also be used to generate stand-alone executables (by piping to clang).

I used the "Kaleidoscope" compiler, provided in the [LLVM documentation](http://llvm.org/docs/tutorial/LangImpl1.html), as a base for this program.

I used the [Gforth manual](http://www.complang.tuwien.ac.at/forth/gforth/Docs-html/index.html) as a reference for various aspects of Forth (though RPN does not attempt to completely faithful to Forth).

Compiling the compiler
=====================
Compiling the compiler requires LLVM 3.4. Given the rapid pace of LLVM's development, other versions of LLVM probably will not work.

Using clang, the following should produce an executable compiler called "rpn":

``clang++ rpn.cpp `llvm-config --cppflags --ldflags --libs core jit native` -o rpn``

Usage
=====================
Once compiled, you can start the REPL by running the executable. You will be greeted by a command prompt.

`./rpn`

`Welcome to rpn!`

`Ready>`

You can immediately begin typing commands and you will see the result. IEach RPN "word" you type is compiled into native machine code which is wrapped into an anonymous function. That function is then called. 

Alternatively, you can use RPN to generate LLVM intermediate representation code (IR). Typing:

`./rpn file` 

will compile the RPN code and display the resulting LLVM IR in your terminal.

To actually run the program represented by the IR, you can pipe the output to LLVM's interpreter, lli:

`./rpn program.rpn | lli`

Or you can produce a standalone executable by piping the output to clang:

`./rpn program.rpn | clang -x ir -o program -`

This will produce an executable file called "program."

Pushing to the stack
=====================
Like Forth, RPN operates on "words." Words are sequences of characters separated by whitespace. They can contain any character (except white space).

Also, like Forth, RPN operates on a stack. The simplest thing you can do is to push a number to the top of the stack. To do this, simply enter the number. In the REPL:

`Ready> 2.739`

`Ready>`

Though doing this printed nothing special, the number 2.739 is now on RPN's stack. To pop the top of the stack and print it, use the word `.`:

`Ready> .`

`2.739000`

If you entered the `.` again, the program would crash, since there is nothing left to pop off of the stack.

Simple Arithmetic
=====================
If you have two numbers on the stack, you can add them. The `+` word pops the top two numbers off the stack, adds them, then puts the result back on the top of the stack. (More precisely: it pops the top number, then replaces the new top value with the result, but the effect is mostly the same). You can then see the result with `.`:

`Ready> 2.739`

`Ready> 5.678`

`Ready> +`

`Ready> .`

`8.417000`

Or you can achieve the same thing with just one line of code:

`Ready> 2.739 5.678 + .`

`8.417000`

RPN also recognizes the arithmetic operators `*`, `-`, and `/`. 

`Ready> 4 2 15 3 / * - .`

`-6.000000`

Reading left to right, the first operation encountered, division, pops 15 and 3 off the stack and places the result of dividing them, 5, on top. 

Now, 5 is at the top of the stack and 2 is the next number down. So the next operation, multiplication, pops those off and pushes the result, 10. 

Now the stack contains 10 at the top and 4 as the second item down. Subtracting 10 from 4 then yields -6.

If you want a positive 6, you can use `negate`:

`Ready> 4 2 15 3 / * - negate .`

`6.000000`

Comments
=====================
Note that you can add comments to your RPN code. You begin a comment with `(`. The word to end one is `)`. Note that there must be white space on either side of `(` and `)` for this to work (so that they are identified as separate words).

`Ready> 4 3 ( this will be 7 ) + .`

`7.000000`

Because there has to be a space on either side of the parentheses, the following does not work as you might expect: 

`Ready> 4 3 (this will be 7) + .`

`Unknown word "(this"`

Also, once you begin a comment in the REPL, all input will be ignored until you close it, including new lines, since it is assumed you are still entering your comment. 

`Ready> 1 2 ( this is a comment) 3 4`

If you hit return after this, RPN will seem to be stuck, because the comment was not properly closed, since the `)` was not surrounded by spaces. Entering a correct closing `)` will close the comment and you will then be able to continue.

If you want to see the entire contents of the stack, without popping anything off of it, use `.s`:

`Ready> 1 2 3 .s`

`3.000000`

`2.000000`

`1.000000`

Stack manipulation
=====================
You can duplicate the top item on the the stack using `dup`:

`Ready> 1 2 3 dup .s`

`3.000000`

`3.000000`

`2.000000`

`1.000000`

Note that RPN, like Forth, is case insensitive - `DUP` or `DuP` or `duP` would do the same thing.

Or you can swap the top two: 

`Ready> 1 2 3 swap .s`

`2.000000`

`3.000000`

`1.000000`

The effects of RPN's built-in stack manipulation words are similar to those of Forth, and can be described as follows:

(word): (stack before) -- (stack after)

drop: x1 --

dup: x1 -- x1 x1

swap: x1 x2 -- x2 x1

over: x1 x2 -- x1 x2 x1

nip: x1 x2 x3 -- x1 x3

tuck: x1 x2 -- x2 x1 x2

Comparisons and conditionals
=====================
RPN recognizes comparison operators <, >, and =. Note that RPN represents false with 0 and true with -1. (In fact, any non-zero number in RPN is true).

`Ready> 1 2 < .`

`-1.000000`

`Ready> 1 2 = .`

`-0.000000`

You can make things happen conditionally using the words `if` and `then`. 

These words operate slightly differently in RPN (and Forth) compared with most programming languages. `If` pops the top of the stack. If the top is true (non-zero), then the words immediately following `if` are executed. Otherwise, execution jumps to the word immediately following `then`.

`Ready> 1 2 3 < if 5 2 + then .`

`7.000000`

2 is less than 3. Thus, after the `<`, the top word on the stack is true (i.e., -1). Thus, the `if` pops off a true, and the words immediately following if (`5 2 +`) are executed. This results in 7 being placed on the top of the stack. 

If we switch the "less than" to a "greater than", execution skips to the point after the then. 7 isn't placed on top of the stack. By the time we get to the `.`, only the 1 remains on the stack:

`Ready> 1 2 3 > if 5 2 + then .`

`1.000000`

RPN also has an `else`. Observe:

`Ready> -1 if 2 else 3 then .`

`2.000000`

`Ready> 0 if 2 else 3 then .`

`3.000000`

If `if` pops something true (non-zero) from the top of the stack, then the words immediately following `if` are executed, but the words between `else` and `then` are skipped. If `if` pops something false, it skips to the point after the `else`.

Defining new words
=====================

You can also define your own words. The beginning of a word definition is denoted with `:`. The next word after that is the name of the new word being defined. Any legal series of RPN words or numbers then follows. That series of words/numbers describes what will happen every time your new word appears. The definition is terminated by a `;`. 

To define a word called `times10`, try 

`Ready> : times10 10 * ;`

Now you can use `times10` anywhere you would otherwise write `10 *`.

`Ready> : times10 10 * ;`

`Ready> 5 times10 .`

`50.000000`

You can also give a word local variables. At the beginning of the word definition, insert a list of variable names between curly braces. For each variable name listed, a value will be popped off the stack and stored in that variable, which may then be used repeatedly without further effect on the stack.

`Ready> : add-triples { a b } a a a + + b b b + + + ;`

`Ready> 1 2 add-triples .`

`9.000000`

Recursion and loops
=====================
You cannot normally refer to the word being defined within the word definition itself, because the word has not been defined yet:

`Ready> : inf 1 + dup . inf ;`

`Unknown word "inf"`

However, you can if you explicitly declare that you are making a recursive definition:

`Ready> : inf recursive 1 + dup . inf ;`

`Ready> 1 inf`

`2.000000`

`3.000000`

`4.000000`

`5.000000`

`6.000000`

Etc.

The `recursive` goes before the definition of locals.

Alternatively, you can use the word `recurse`:

`Ready> : inf 1 + dup . recurse ;`

RPN also allows looping via `begin` and `again`. `Begin` signals the beginning of a block. `Again` jumps back to the point immediately after the last begin.

`Ready> : inf begin 1 + dup . again ;`

This has the same results as the `inf` word defined recursively above.

****ADD WHILE****

Quirks
=====================
Finally, one quirk: at the beginning of an RPN program, the stack is actually initialized with a single item, the value of which is zero but that has nothing above or below it. This can lead to some strange behavior. For example, on first starting up the RPN REPL:

`Welcome to rpn!`

`Ready> .s` - Nothing appears to be on the stack. But...

`Ready> dup`

`Ready> .s`

`0.000000`

Avoiding this sort of thing may require inserting more pervasive bounds-checking into the compiled code.
