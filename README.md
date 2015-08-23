About
=====================
RPN (short for "reverse polish notation") is a compiler for a toy Forth-like language. It compiles to LLVM IR and was created to teach myself about LLVM and compilers. Along the way, I also learned some C++ and Forth.

RPN has Forth-inspired commands and syntax, including several stack-manipulating words, the ability to define new words, including words with recursive definitions, and loops. It features a JIT-compiling REPL and can also be used to generate stand-alone executables (by piping to clang).

I used the "Kaleidoscope" compiler, provided in the [LLVM documentation](http://llvm.org/docs/tutorial/LangImpl1.html), as a base for this program.

I used the GNU project's Gforth and the [Gforth manual](http://www.complang.tuwien.ac.at/forth/gforth/Docs-html/index.html) as a reference for various aspects of Forth (though RPN does not attempt to completely faithful to Forth). 

Compiling the compiler
=====================
Compiling the compiler requires LLVM 3.4. Given the rapid pace of LLVM's development, other versions of LLVM probably will not work.

Using Clang, provided the appropriate version of LLVM is installed, the following should produce an executable compiler called "rpn":

``clang++ rpn.cpp `llvm-config --cppflags --ldflags --libs core jit native` -o rpn``

Usage
=====================
You can start RPN's JIT-compiling REPL by running the `rpn` executable. You will be greeted by a command prompt.

`./rpn`

`Welcome to rpn!`

`Ready>`

You can immediately begin typing commands and you will see the results after pressing return. Each RPN "word" you type is compiled into native machine code and executed. 

Alternatively, you can use RPN to generate LLVM intermediate representation code (IR). Typing:

`./rpn program.rpn` 

will compile the RPN code contained in `program.rpn` and display the resulting LLVM IR in your terminal.

To actually run the program, you can pipe the output to LLVM's interpreter, lli:

`./rpn program.rpn | lli`

Or you can produce a standalone executable by piping the output to clang:

`./rpn program.rpn | clang -x ir -o program -`

This will produce an executable file called "program."

Example programs
=====================
You will find several example programs in the "examples" directory of this repository. There is a sample "fizzbuzz" program, another program that can identify and list prime numbers, and a program demonstrating an algorithm that reverses the stack down to a given depth.

You can view the source or try running or compiling them using the methods discussed above. For example:

`./rpn examples/primes.rpn | lli`

`./rpn examples/primes.rpn | clang -x ir -o primes -`

The remainder of this document will describe the RPN language as it has been built so far.

Pushing to the stack
=====================
Like Forth, RPN operates on "words." Words are sequences of characters separated by whitespace. They can contain any characters except white space. 

Note that RPN words, like Forth words, are case insensitive - `DUP` or `DuP` or `duP` all mean the same thing.

Also, like Forth, RPN operates on a mostly last-in-first-out stack. The simplest thing you can do is to push a number to the top of the stack. To do this, simply enter the number. In the REPL:

`Ready> 2.739`

`Ready>`

After we type it and press return, the number 2.739 is now on RPN's stack. To pop the top of the stack and print it, use the word `.`:

`Ready> .`

`2.739000`

If you enter the `.` again, the program will crash, since there is nothing left that can be popped off the stack.

Simple Arithmetic
=====================
If you have two numbers on the stack, you can add them. The `+` word pops the top two numbers off the stack, adds them, then puts the result back on the top of the stack. (More precisely: it pops the top number, then replaces the new top value with the result, but the effect is mostly the same). You can then see the result with `.`:

`Ready> 2.739`

`Ready> 5.678`

`Ready> +`

`Ready> .`

`8.417000`

You can achieve the same thing with just one line of code:

`Ready> 2.739 5.678 + .`

`8.417000`

RPN also recognizes the arithmetic operators `*`, `-`, and `/`, as well as a `negate` word that switches the number at the top of the stack to its negative. For example:

`Ready> 4 2 15 3 / * - negate .`

`6.000000`

In the above line, the first four words the JIT encounters are numbers, so each is pushed to the stack in order from left to right. Then, the first operation, division, pops 15 and 3 off the stack. It then places the result of dividing those two numbers on top.

So the top of the stack is now 5, and 2 is the next number down. Thus, the next operation, multiplication, pops those off and pushes the result of multiplying them, 10, to the top of the stack.

Now the stack contains 10 at the top and 4 as the second item down. Subtracting 10 from 4 then yields -6.

`Negate` turns this into a positive 6. Then, finally, `.` pops the 6 from the top of the stack and prints it.

Comments
=====================
You can add comments to your RPN code. You begin a comment with the word `(`. The comment is then ended with a `)`. 

`Ready> 4 3 ( this will be 7 ) + .`

`7.000000`

Because there has to be a space on either side of the starting parenthesis (so that the tokenizer recognizes it as a separate word), the following does not work as you might expect: 

`Ready> 4 3 (this will be 7) + .`

`Unknown word "(this"`

Note that, once you begin a comment in the REPL, all input will be ignored until you close it, including new lines, since it is assumed you are still entering your comment. 

Stack manipulation
=====================
If you want to see the entire contents of the stack, without popping anything off of it, use the word `.s`:

`Ready> 1 2 3 .s`

`3.000000`

`2.000000`

`1.000000`

Note that `.s` in RPN lists items from the top of the stack down (Gforth's `.s` goes from the bottom up).

One built-in word for manipulating the stack is `dup`, which simply duplicates the top item on the stack:

`Ready> 1 2 3 dup .s`

`3.000000`

`3.000000`

`2.000000`

`1.000000`

Another built-in stack-manipulation word is "swap":

`Ready> 1 2 3 swap .s`

`2.000000`

`3.000000`

`1.000000`

An easy way to describe the results of these kinds of words follows this format:

(stack before) -- (stack after)

Using this format, swap and dup can be described like this:

`dup`: x1 -- x1 x1

`swap`: x1 x2 -- x2 x1

In addition to `dup` and `swap`, RPN has several other stack manipulation words built in. They can be described as follows:

`drop`: x1 --

`over`: x1 x2 -- x1 x2 x1

`nip`: x1 x2 x3 -- x1 x3

`tuck`: x1 x2 -- x2 x1 x2

All of these words are based on similar words in Gforth.

Comparisons and conditionals
=====================
RPN represents false with 0 and true with -1, similarly to Gforth. (In fact, any non-zero number in RPN is true).

Further, RPN recognizes comparison operators <, >, and =. These pop the top two items from the stack, apply the comparison, and push the result (-1 or 0) to the top of the stack.

`Ready> 1 2 < .`

`-1.000000`

`Ready> 1 2 = .`

`-0.000000`

Now you can make things happen conditionally using the words `if` and `then`. 

`If` pops the top of the stack. If the top is true (non-zero), then the words immediately following `if` are executed. Otherwise, the words immediately following `if` are skipped and execution jumps to the word immediately following `then`.

`Ready> 1 2 3 < if 5 2 + then .`

`7.000000`

2 is less than 3. Thus, in the above line, the top word on the stack is true (i.e., -1) after the `<`. Thus, the `if` pops off a true, and the words immediately following if (`5 2 +`) are executed. This results in 7 being placed on the top of the stack. 

Now try it with `>` instead of `<`:

`Ready> 1 2 3 > if 5 2 + then .`

`1.000000`

If we switch the "less than" to a "greater than", execution skips to the point after `then`. 7 isn't placed on top of the stack, and the stack contains only the 1 by the time we reach the ending `.`.

RPN also has an `else`. Observe:

`Ready> -1 if 2 else 3 then .`

`2.000000`

`Ready> 0 if 2 else 3 then .`

`3.000000`

When an "else" is present, if `if` pops something true (non-zero) from the top of the stack, then the words immediately following `if` are executed up until the `else`, but the words between `else` and `then` are skipped. If `if` pops something false, it skips to the point after the `else`.

Defining new words
=====================

You can also define your own words. The beginning of a word definition is denoted with `:`. The next word after that is the name of the new word being defined. Any series of RPN words or numbers then follows. That series of words/numbers describes what will happen every time your new word appears. The definition is terminated by a `;`. 

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

Alternatively, you can use the word `recurse` to refer to the word currently being defined, even without explicitly declaring the word to be recursive:

`Ready> : inf 1 + dup . recurse ;`

RPN also allows looping via `begin` and `again`. `Begin` signals the beginning of a block. `Again` jumps back to the point immediately after the last begin.

`Ready> : inf begin 1 + dup . again ;`

This has the same results as the `inf` word defined recursively above.

Note that nested "begin" and "again" blocks may not function the way you would expect when `if`s are involved. (For its part, Gforth does not appear to allow `again` as the result of an `if`).

Finally, RPN has a `while` word that goes inisde `begin`/`again` blocks. `While` pops the top value from the stack. If that value is true, execution continues until reaching `again`, at which point we jump back to `begin`. If the value is false, execution skips to the point immediately after the `again` and thus exits the loop.

For example:

`Ready> : while-test begin dup 0 > while dup . 1 - again ;`

`Ready> 3 while-test`

`3.000000`

`2.000000`

`1.000000`

`Ready>`

Quirks
=====================
Finally, one quirk: at the beginning of an RPN program, the stack is actually initialized with a single item, the value of which is "null" and that has nothing above or below it. This can lead to some strange behavior. For example, on first starting up the RPN REPL:

`Welcome to rpn!`

`Ready> .s` - Nothing appears to be on the stack. But...

`Ready> dup`

`Ready> .s`

`0.000000`

Avoiding this sort of thing may require inserting more pervasive bounds-checking into the compiled code.
