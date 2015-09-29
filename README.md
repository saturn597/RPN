About
=====================
RPN (short for "reverse polish notation") is a compiler for a toy Forth-like language. It compiles to LLVM IR and was created to teach myself about LLVM and compilers. Along the way, I also learned some C++ and Forth.

RPN has Forth-inspired commands and syntax, including several built-in stack-manipulating words, the ability to define new words, and loops. It features a JIT-compiling REPL and can also be used to generate stand-alone executables (by piping to clang).

I referenced the "Kaleidoscope" compiler, provided in the [LLVM documentation](http://llvm.org/docs/tutorial/LangImpl1.html), as basis for this program.

I also used the GNU project's Gforth and the [Gforth manual](http://www.complang.tuwien.ac.at/forth/gforth/Docs-html/index.html) as a reference for various aspects of Forth (though RPN is not perfectly faithful to Forth). 

Compiling the compiler
=====================
Compiling the compiler requires LLVM 3.4. Given the rapid pace of LLVM's development, other versions of LLVM probably will not work without some tweaking.

Using Clang, the following should produce an executable compiler called "rpn", provided you have installed the appropriate version of LLVM:

``clang++ rpn.cpp `llvm-config --cppflags --ldflags --libs core jit native` -o rpn``

A similar command should work for g++ as well. Depending on the version of your C++ compiler, you may need to specify `--std=c++11`.

Usage
=====================
You can start RPN's JIT-compiling REPL by running the `rpn` executable. You will be greeted by a command prompt.

`./rpn`

`Welcome to rpn!`

`Ready>`

You can immediately begin typing commands and you will see the results after pressing return. While in the interpreter, each RPN "word" you type is compiled into native machine code and executed. 

Alternatively, you can use RPN to generate LLVM intermediate representation code (IR). Typing:

`./rpn program.rpn` 

will compile the RPN code contained in `program.rpn` and display the resulting LLVM IR in your terminal.

To actually run the program, you can pipe the output to LLVM's IR interpreter, lli:

`./rpn program.rpn | lli`

Or you can produce a standalone executable by piping the output to clang:

`./rpn program.rpn | clang -x ir -o program -`

This will produce an executable file called "program."

Example programs
=====================
You will find several example programs in the "examples" directory of this repository. There is a sample "fizzbuzz" program, another program that can identify and list prime numbers, and a program that defines a word capable of reversing RPN's stack down to an arbitrary depth.

You can run these examples using the methods discussed under "Usage". For example, the following line will run the example "primes.rpn" in LLVM's interpreter:

`./rpn examples/primes.rpn | lli`

Whereas this line will produce an executable program called "primes":

`./rpn examples/primes.rpn | clang -x ir -o primes -`

The remainder of this document will describe the RPN language as it has been built so far.

Language basics
=====================
Like Forth, RPN operates on "words." Words are sequences of characters separated by whitespace. They can contain any characters except white space. 

Note that RPN words, like Forth words, are case insensitive - `DUP` or `DuP` or `duP` all mean the same thing.

Also, like Forth, RPN operates on a stack. RPN's stack is (mostly) last-in-first-out.

The simplest thing you can do is to push a number to the top of the stack. To do this, just enter the number. In the REPL:

`Ready> 2.739`

`Ready>`

After we type it and press return, the number 2.739 is now on RPN's stack. To pop the top of the stack and print it, use the word `.`:

`Ready> .`

`2.739000`

Note that if you enter a `.` again, the program will crash, since there is nothing left that can be popped off the stack.

Simple Arithmetic
=====================
If you have two numbers on the stack, you can add them using the `+` word. `+` pops the top two numbers off the stack, adds them, then puts the result back on the top of the stack. (More precisely, it pops the top number, then replaces the new top value with the result.) You can then see the result of the addition with `.`:

`Ready> 2.739`

`Ready> 5.678`

`Ready> +`

`Ready> .`

`8.417000`

You can achieve the same thing with just one line of code:

`Ready> 2.739 5.678 + .`

`8.417000`

RPN also recognizes the arithmetic operators `*`, `-`, and `/`, as well as a `negate` word that negates the number at the top of the stack. Note that binary operators take the top of the stack as the second/rightmost number in the operation - so `6 3 /` results in 2, since the stack had 3 on top and 6 below that.

For example:

`Ready> 4 2 15 3 / * - negate .`

`6.000000`

In the above line, the first four words the JIT encounters are numbers, so each is pushed to the stack in order from left to right. Then, the first operation, division, pops 15 and 3 off the stack. It then places the result of dividing those two numbers on top.

So the top of the stack is now 5, and 2 is the next number down. The next operation, multiplication, pops those off and pushes the result of multiplying them, 10, to the top of the stack.

Now the stack contains 10 at the top and 4 as the second item down. Subtracting 10 from 4 then yields -6.

The `negate` turns this into a positive 6. Then, finally, `.` pops the 6 from the top of the stack and prints it.

Comments
=====================
The word `(` indicates the beginning of a comment. Comments are then ended with a `)`. 

`Ready> 4 3 ( this will be 7 ) + .`

`7.000000`

There has to be a space on either side of the `(` in order for RPN to recognize it as a separate word. Thus, the following does not work: 

`Ready> 4 3 (this will be 7) + .`

`Unknown word "(this"`

However, the closing parenthesis `)` does *not* need to have a space on either side in order to close the comment.

Stack manipulation
=====================
If you want to see the entire contents of the stack, without popping anything, use the word `.s`:

`Ready> 1 2 3 .s`

`3.000000`

`2.000000`

`1.000000`

Note that `.s` in RPN lists items from the top of the stack down. In contrast, Gforth's `.s` goes from the bottom up.

RPN has a number of built-in words for manipulating the stack, all of which are based on words in Gforth. One of these is `dup`, which simply duplicates the top item on the stack:

`Ready> 1 2 3 dup .s`

`3.000000`

`3.000000`

`2.000000`

`1.000000`

Another built-in stack manipulation word is "swap":

`Ready> 1 2 3 swap .s`

`2.000000`

`3.000000`

`1.000000`

Here's an abbreviated way of describing the effect of `dup` and `swap`, based on a Forth convention for comments:

word: (stack before) -- (stack after)

`dup`: x1 -- x1 x1

`swap`: x1 x2 -- x2 x1

This notation provides an easy way to describe several other stack manipulation words built into RPN:

`drop`: x1 --

`over`: x1 x2 -- x1 x2 x1

`nip`: x1 x2 x3 -- x1 x3

`tuck`: x1 x2 -- x2 x1 x2

Comparisons and conditionals
=====================
RPN represents false with 0 and true with -1, similarly to Gforth. In fact, RPN will take any non-zero number to be true.

Further, RPN recognizes the comparison operators <, >, and =. These pop the top two items from the stack, make the comparison, and push the result to the top of the stack. The result will be either -1 or 0 depending on whether the comparison is true or false.

`Ready> 1 2 < .`

`-1.000000`

`Ready> 1 2 = .`

`-0.000000`

Knowing RPN's treatment of truth and falsehood, you can now make things happen conditionally using the words `if` and `then`. 

`If` pops the top of the stack. If the top of the stack is true (non-zero), then the words immediately following `if` are executed. Otherwise, the words immediately following `if` are skipped and execution jumps to the word immediately following `then`. For example:

`Ready> 1 2 3 < if 5 2 + then .`

`7.000000`

2 is less than 3. Thus, the `<` leaves -1 (or true) on top of the stack. The `if` then pops off -1. Because `if` popped a true value, the words that immediately follow (`5 2 +`) are executed. This results in 7 being placed on the top of the stack, which is output the `.` is executed. 

Now try it with `>` instead of `<`:

`Ready> 1 2 3 > if 5 2 + then .`

`1.000000`

`>` leaves 0 (or false) on top of the stack, since 2 is not greater than 3. The `if` pops off the "false", and execution skips to the point after `then`. As a result, `5 2 +` is never executed, 7 is never placed on top of the stack, and we are left with only 1 on the stack.

RPN also has an `else`. Observe:

`Ready> -1 if 2 else 3 then .`

`2.000000`

`Ready> 0 if 2 else 3 then .`

`3.000000`

If `if` pops something true (non-zero) from the top of the stack, then the words immediately following `if` are executed up until the `else`, but the words between `else` and `then` are skipped. If `if` pops something false, it skips to the word after the `else`.

Defining new words
=====================

RPN lets you define your own words. The beginning of a word definition is denoted with `:`. The `:` is then followed by a name for the newly defined word. Any series of RPN words and/or numbers then follows. This series of words and/or numbers describes what will happen whenever the word is encountered. The definition is terminated by a `;`. 

For example, to define a word called `times10`, try 

`Ready> : times10 10 * ;`

Now you can use `times10` anywhere you would otherwise write `10 *`.

`Ready> : times10 10 * ;`

`Ready> 5 times10 .`

`50.000000`

You can also give a word local variables. To do this, insert a list of variable names between curly braces at the beginning of the word definition, but before the new word name. For each variable name listed, a value will be popped off the stack and assigned to that variable. This happens from right to left, so the rightmost variable gets whatever is on top of the stack. 

Now, any time the local variable appears within the word definition, the value assigned to it will be pushed to the top of the stack. For example: 

`Ready> : local-test { a b c } a . b . c . b . a . c . ;`

`Ready> 1 2 3 local-test`

`1.000000`

`2.000000`

`3.000000`

`2.000000`

`1.000000`

`3.000000`

Recursion and loops
=====================
You cannot normally refer to a word being defined within the word definition itself, because the word has not been defined yet:

`Ready> : inf 1 + dup . inf ;`

`Unknown word "inf"`

However, RPN will allow you to do this if you explicitly declare that a definition is recursive. You do this by typing `recursive` after the word name but before the definition of locals:

`Ready> : inf recursive 1 + dup . inf ;`

`Ready> 1 inf`

`2.000000`

`3.000000`

`4.000000`

`5.000000`

`6.000000`

Etc.

You can also use the word `recurse` to refer to the word currently being defined, even if you have not explicitly declared the word to be recursive. The following is equivalent to the `inf` word above:

`Ready> : inf 1 + dup . recurse ;`

RPN also allows looping via `begin` and `again`. `Begin` signals the beginning of a block. `Again` jumps back to the point immediately after the last begin.

`Ready> : inf begin 1 + dup . again ;`

This, again, has the same results as the `inf` word defined recursively above.

Note that nested "begin" and "again" blocks may not function the way you would expect when `if`s are involved. (For its part, Gforth does not appear to allow `again` as the result of an `if`).

Finally, RPN has a `while` word that goes inside `begin`/`again` blocks. `While` pops the top value off the stack. If that value is true (non-zero), execution continues until it reaches the `again`, at which point execution jumps back to `begin`. If the value is false, execution skips to the point immediately after the `again`, exiting the loop.

For example:

`Ready> : while-test begin dup 0 > while dup . 1 - again ;`

`Ready> 3 while-test`

`3.000000`

`2.000000`

`1.000000`

`Ready>`

Quirks
=====================
A quirk: at the beginning of an RPN program, the stack is actually initialized with a single item, the value of which is "null" and that has nothing above or below it. This can lead to some strange behavior. For example, on first starting up the RPN REPL:

`Welcome to rpn!`

`Ready> .s` - Nothing appears to be on the stack. But...

`Ready> dup`

`Ready> .s`

`0.000000`

Avoiding this sort of thing may require inserting more pervasive bounds-checking into the compiled code.
