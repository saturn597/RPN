
/* RPN calculator and Forth imitator that compiles to LLVM.
 * Inspired by http://llvm.org/releases/3.4.2/docs/tutorial/index.html
 */

// TODO: Consistency about "static" functions

#include <algorithm>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include "llvm/Analysis/Verifier.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Support/TargetSelect.h"

//TODO: Take this stuff out of global scope?
using namespace llvm;

LLVMContext &context = getGlobalContext();
static Module *theModule = new Module("rpn", context); 
static IRBuilder<> builder(context);

static ExecutionEngine *TheExecutionEngine;

GlobalVariable *TheStack;

StructType *stackItemType; 
PointerType *stackItemPointerType;

Function *malloc_;
FunctionType *mallocType;
Function *free_;
FunctionType *freeType;
Function *printf_;
FunctionType *printfType;

Function *pop;
Function *push;

Function *add;
Function *sub;
Function *mul;
Function *divi;

Function *negate;

Function *lt;
Function *gt;
Function *eq;

Function *dup;
Function *swa;
Function *drop;
Function *over;
Function *nip;
Function *tuck;
Function *rot;

Function *dot;
Function *dotS;

Value *fstring;

std::map<std::string, Function *> words;
std::map<std::string, Value *> currentLocals;

std::istream *inputStream;

std::stack<BasicBlock *> beginBlocks;
std::stack<BasicBlock *> exitBlocks;

uint64_t stackItemSize;

struct StackItem {
  Value *val;
  Value *ptr;  // Maybe I should use more specific types where applicable?
};

class WordAST;

Function *buildFunction(std::string);  

// TODO: Move the stack management into its own class maybe

static Value *getInt64(int x) {
  return ConstantInt::get(Type::getInt64Ty(getGlobalContext()), x);
}

static Value *getInt32(int x) {
  return ConstantInt::get(Type::getInt32Ty(getGlobalContext()), x);
}

static Value *getInt8(int x) {
  return ConstantInt::get(Type::getInt8Ty(getGlobalContext()), x);
}

static Value *getDouble(double x) {
  return ConstantFP::get(getGlobalContext(), APFloat(x));
}

////////////////////
// Exceptions
////////////////////

class CompilerException: public std::runtime_error {
  public:
    CompilerException(std::string const& whatValue) : std::runtime_error(whatValue) {};
};

////////////////////
// Tokenizing
////////////////////

bool showPrompt = false;  // show prompt when getting next line?

static char getNextChar(bool advance) {
  // returns the next character from stdin (or returns the last char read if !advance)

  // Adding this function so that dropLine and gettok can have a char buffer in common (lastChar).
  // A singleton class with lastChar as an instance variable and drop and gettok as methods might be
  // another way to achieve that, and might be more idiomatic, so something to consider.
  // It's also possible this could work without even sharing a char buffer

  static std::string theLine = " ";
  static char lastChar = ' ';
  static std::string::iterator pos = theLine.begin();

  if (advance) {
    pos++;
    if (pos == theLine.end()) {
      if (showPrompt) std::cout << "Ready> ";
      if (!getline(*inputStream, theLine)) return EOF;
      theLine.append("\n");
      pos = theLine.begin();
    }
    lastChar = *pos;
  }
  return lastChar;
  //currently this is really nasty
}

static void dropLine() {
  // consumes input until the next newline or until EOF

  char currentChar = getNextChar(false); 
  while (currentChar != '\n' && currentChar != EOF) currentChar = getNextChar(true);

}

static std::string gettok() {

  std::string tokenString;
  
  char currentChar = getNextChar(false);

  if (currentChar == EOF) return "";

  // skip whitespace
  while (isspace(currentChar))
    currentChar = getNextChar(true);

  while (!isspace(currentChar) && currentChar != EOF) {
    tokenString += currentChar;
    currentChar = getNextChar(true);
  }

  // forth is case insensitive, so let's be case insensitive
  std::transform(tokenString.begin(), tokenString.end(), tokenString.begin(), ::tolower); 

  return tokenString;  
}


////////////////////
// AST Definitions
////////////////////

class WordAST {
// Base class for other word types
public: 
  virtual ~WordAST() {};  // Why does this destructor need to be declared?
  virtual void codeGen() = 0;
};

class BasicWordAST : public WordAST {
  std::string name;
public:
  BasicWordAST(std::string Name) : name(Name) {}
  virtual void codeGen();
};

class NumberAST : public WordAST {
  double val;
public:
  NumberAST(double Val) : val(Val) {}
  virtual void codeGen();
};

class IfAST : public WordAST {
  std::vector<WordAST *> thenContent;
  std::vector<WordAST *> elseContent;
public:
  IfAST(std::vector<WordAST *> ThenContent, std::vector<WordAST *> ElseContent) : thenContent(ThenContent), elseContent(ElseContent) {}
  virtual void codeGen();
};

class BeginAST : public WordAST {
public:
  BeginAST() {};
  virtual void codeGen();
};

class AgainAST : public WordAST { 
public:
  AgainAST() {};
  virtual void codeGen();
};

class WhileAST : public WordAST {
public:
  WhileAST() {};
  virtual void codeGen();
};

class DefinitionAST : public WordAST {
  std::vector<WordAST *> content;
  std::string name;
  std::vector<std::string> locals;
  bool recursive;
public:
  DefinitionAST(std::string Name, bool Recursive, std::vector<std::string> Locals, std::vector<WordAST *> Content) : 
    name(Name), recursive(Recursive), locals(Locals), content(Content) {}
  virtual void codeGen();
};

class LocalRefAST : public WordAST {
  std::string name;
public:
  LocalRefAST(std::string Name) : name(Name) {};
  virtual void codeGen();
};  // maybe merge this class with BasicWord

class RecurseAST : public WordAST {
public:
  virtual void codeGen();
};

class CommentAST : public WordAST {
public:
  virtual void codeGen();
};


////////////////////
// Parsing
////////////////////

WordAST *parseToken(std::string tokenString);

static std::string curTok;
std::string getNextToken() {
  return curTok = gettok();
}

BasicWordAST *parseBasicWord() {
  std::string name = curTok;
  return new BasicWordAST(name);
}

NumberAST *parseNumber() {
  double number = strtod(curTok.c_str(), 0);
 // getNextToken();  // eat the number
  return new NumberAST(number);
}

IfAST *parseIf() {
  
  std::vector<WordAST *> thenContent;
  std::vector<WordAST *> elseContent;
  
  getNextToken();  // Eat the if

  while (curTok != "else" && curTok != "then") {
    if (curTok == "") throw CompilerException("then or else expected");
    thenContent.push_back(parseToken(curTok));
    getNextToken();
  }
  
  if (curTok == "then") {
    return new IfAST(thenContent, elseContent);
  }

  getNextToken();  // Eat else

  while (curTok != "then") {  // If we haven't already hit a then, need to keep going until we do
    if (curTok == "") throw CompilerException("then expected");
    elseContent.push_back(parseToken(curTok));
    getNextToken();
  }

  return new IfAST(thenContent, elseContent);

}

BeginAST *parseBegin() {

  return new BeginAST();

}

AgainAST *parseAgain() {

  return new AgainAST();

}

WhileAST *parseWhile() {
  
  return new WhileAST();

}

DefinitionAST *parseDefinition() {  // Note: this will allow colon definitions inside : defs - not sure it works that way in forth
  getNextToken();  // eat :
  
  std::string name = curTok;  // the name we want to set for our word is the first token we get after the :

  getNextToken();  // eat name

  std::vector<std::string> locals;  // maybe use std::set for this because I'm mostly searching it and don't want dupes -- but I care about order
 
  bool recursive = false;
  if (curTok == "recursive") {  // the use of the "recursive" word is nonstandard forth per gforth manual (but seems nice)
    recursive = true; 
    words[name];  // add this to our word list (even though we don't actually have code for it yet) 
    // need a way to undo that definition if something fails
    getNextToken();  // eat recursive
  } 

  if (curTok == "{") {
    // word has locals
    getNextToken();  // eat {
    while (curTok != "}") {
      if (curTok == "") throw CompilerException("} expected");  // eof before end of locals definition
      locals.push_back(curTok);  // report on duplicates?
      currentLocals[curTok] = Constant::getNullValue(Type::getDoubleTy(getGlobalContext()));  // set to null for now - we'll do more when we codegen
      getNextToken(); 
    } 
    getNextToken();  // eat }
  }

  std::vector<WordAST *> content;

  while (curTok != ";") {
    if (curTok == "") throw CompilerException("; expected");  // eof before end of definition
    content.push_back(parseToken(curTok));
    getNextToken();
  }  

  return new DefinitionAST(name, recursive, locals, content);
}

WordAST *parseComment() {

  while (true) {
    char nextChar = getNextChar(true);
    if (nextChar == EOF) throw CompilerException(") expected");
    if (nextChar == ')') break;
  }
  getNextChar(true);

  return new CommentAST();

}

WordAST *parseToken(std::string tokenString) { 
  // General function for parsing any top level token

  if (currentLocals.count(tokenString) == 1) {
    return new LocalRefAST(tokenString);
  } else if (words.count(tokenString) == 1) {  // test if our list of defined words contains the tokenString
    // If so, this is just a basic word
    // Currently I search words for tokenString twice - once here and once during codegen - fix?
    // Does this allow EOF shenannigans? Should I check EOF first?
    return parseBasicWord();
  } else if (isdigit(tokenString.front())) {  // TODO: do more validating to ensure it's a number
    return parseNumber(); 
  } else if (tokenString.front() == '-' && isdigit(tokenString[1])) {
    // handle negative numbers
    return parseNumber(); 
  } else if (tokenString == "if") {
    return parseIf();
  } else if (tokenString == "begin") {
    return parseBegin();
  } else if (tokenString == "again") {
    return parseAgain();
  } else if (tokenString == "while") {
    return parseWhile();
  } else if (tokenString == ":") {  // colon definition
    return parseDefinition();
  } else if (tokenString == "recurse") {  // recurse
    return new RecurseAST();
  } else if (tokenString == "(") {  // beginning of a comment
    return parseComment();
  } else if (tokenString == "") {  // eof
    return 0;
  }
  
  dropLine();
  throw CompilerException("Unknown word \"" + tokenString + "\"");
}


////////////////////
// Code Generation
////////////////////

void codeGenMultiple(std::vector<WordAST *> content) {
  // Generate code for multiple words in sequence

  for (unsigned idx = 0; idx < content.size(); ++idx) {
    content.at(idx) -> codeGen();
  }
}

void BasicWordAST::codeGen() {
  builder.CreateCall(words[name]);
}

void NumberAST::codeGen() {
  builder.CreateCall(push, getDouble(val));
}

void IfAST::codeGen() {

  Value *cond = builder.CreateFCmpONE(builder.CreateCall(pop), getDouble(0.0), "ifCond");  
  Function *currentFunction = builder.GetInsertBlock() -> getParent();

  BasicBlock *thenBB = BasicBlock::Create(getGlobalContext(), "then", currentFunction);
  BasicBlock *mergeBB = BasicBlock::Create(getGlobalContext(), "merge", currentFunction);
  BasicBlock *elseBB;
  
  if (elseContent.size() == 0) {
    // If we don't have an else, we just want to branch straight to the mergeBB when the cond is false
    builder.CreateCondBr(cond, thenBB, mergeBB);
  } else {
    // Otherwise, we need to branch to else's BB
    elseBB = BasicBlock::Create(getGlobalContext(), "else", currentFunction);
    builder.CreateCondBr(cond, thenBB, elseBB);
  }

  builder.SetInsertPoint(thenBB);
  codeGenMultiple(thenContent); 
  builder.CreateBr(mergeBB);

  if (elseContent.size() > 0) {  // If we have an else branch
    builder.SetInsertPoint(elseBB);
    codeGenMultiple(elseContent);
    builder.CreateBr(mergeBB);
  }

  builder.SetInsertPoint(mergeBB);

}

void BeginAST::codeGen() {

  Function *currentFunction = builder.GetInsertBlock() -> getParent();

  BasicBlock *beginBlock = BasicBlock::Create(getGlobalContext(), "begin", currentFunction);
  BasicBlock *exitBlock = BasicBlock::Create(getGlobalContext(), "exitBlock", currentFunction);
  beginBlocks.push(beginBlock);
  exitBlocks.push(exitBlock);

  builder.CreateBr(beginBlock);
  builder.SetInsertPoint(beginBlock);

}

void AgainAST::codeGen() {
  // currently doesn't work in JIT (outside of : defs)

  // this stack approach to tracking begin and exit blocks is a bit quirky -
  // for example, "again" will return to the last begin encountered at compile time, but 
  // not necessarily whatever was most recently encountered at run time (but the latter 
  // might be a more reasonable way to do it).

  // Maybe look into how this is handled by forth.
  // : weird 10 begin 1 - dup . dup 0 > if again then ; fails in gforth, but counts down from 10 in RPN

  BasicBlock *beginBlock = beginBlocks.top();
  BasicBlock *exitBlock = exitBlocks.top();
  beginBlocks.pop();
  exitBlocks.pop();
  builder.CreateBr(beginBlock);

  builder.SetInsertPoint(exitBlock); 

}

void WhileAST::codeGen() {
  // currently doesn't work in JIT (outside of : defs)

  Function *currentFunction = builder.GetInsertBlock() -> getParent();

  BasicBlock *exitBlock = exitBlocks.top();
  BasicBlock *afterWhile = BasicBlock::Create(getGlobalContext(), "afterWhile", currentFunction);

  Value *cond = builder.CreateFCmpONE(builder.CreateCall(pop), getDouble(0.0), "ifCond");  
  builder.CreateCondBr(cond, afterWhile, exitBlock); 
  builder.SetInsertPoint(afterWhile);

}

void DefinitionAST::codeGen() {

  BasicBlock *originalBlock = builder.GetInsertBlock();

  Function *f = buildFunction(name);
 
  words[name] = f; // maybe provide for the ability to undo this if something fails

  unsigned idx;
  for (std::vector<std::string>::reverse_iterator i = locals.rbegin(); i != locals.rend(); ++i) {
    currentLocals[*i] = builder.CreateAlloca(Type::getDoubleTy(getGlobalContext()));
    builder.CreateStore(builder.CreateCall(pop), currentLocals[*i]); 
  }

  codeGenMultiple(content);  

  builder.CreateRetVoid();

  // Add function validation and optimization here, check for conflicting names
  verifyFunction(*f); 

  currentLocals.clear();

  builder.SetInsertPoint(originalBlock);

}

void RecurseAST::codeGen() {
  // This might be a little weird - calling recurse on the top level in real forth results in "Interpreting a compile-only word" error
  // In ours will it call the anonymous function we're JITing to?
  Function *currentFunction = builder.GetInsertBlock() -> getParent();
  builder.CreateCall(currentFunction); 
}

void LocalRefAST::codeGen() {
  builder.CreateCall(push, builder.CreateLoad(currentLocals[name]));
}

void CommentAST::codeGen() {}  // don't do anything for comments


/////////////////////////////////////
// Generating code for built-in words
/////////////////////////////////////

Type *voidTy = Type::getVoidTy(getGlobalContext());
Type *doubleTy = Type::getDoubleTy(getGlobalContext());
Type *int8Ty = Type::getInt8Ty(getGlobalContext());
Type *int32Ty = Type::getInt32Ty(getGlobalContext());
Type *int64Ty = Type::getInt64Ty(getGlobalContext());
PointerType *int8PointerTy = PointerType::get(int8Ty, 0);

Value *buildGetStackValue(Value *stackItemPtr) {
  // Generate code to get the value from the stack element pointed to by stackItemPtr
  
  Value *idx[] = { getInt32(0), getInt32(0) };
  Value *valPtr = builder.CreateInBoundsGEP(stackItemPtr, idx, "gettingVal");
  return builder.CreateLoad(valPtr, "val");

}

void buildSetStackValue(Value *stackItemPtr, Value *newValue) {
  // Generate code to set the value from the stack element pointed to by stackItemPtr
  
  Value *idx[] = { getInt32(0), getInt32(0) };
  Value *valPtr = builder.CreateInBoundsGEP(stackItemPtr, idx, "gettingVal");
  builder.CreateStore(newValue, valPtr);

}

Value *buildGetStackPointer(Value *stackItemPtr) {
  // Given a pointer to a stack item, return the pointer to the next item down the stack
  
 Value *idx[] = { getInt32(0), getInt32(1) }; 
 Value *ptrPtr = builder.CreateInBoundsGEP(stackItemPtr, idx, "gettingPtr");
 return builder.CreateLoad(ptrPtr, "ptr");

}

Value *buildSetStackPointer(Value *stackItemPtr, Value *newPtr) {
  // Generate code to set the pointer of the stack element pointed to by stackItemPtr
  
 Value *idx[] = { getInt32(0), getInt32(1) }; 
 Value *ptrPtr = builder.CreateInBoundsGEP(stackItemPtr, idx, "gettingPtr");
 return builder.CreateStore(newPtr, ptrPtr);

}

void buildSetStackItem(Value *stackItemPtr, Value *newValue, Value *newPtr) { // maybe stackItemPtr should have the right type
  // Generate code to set the fields of a stack item structure
  
  buildSetStackValue(stackItemPtr, newValue);
  buildSetStackPointer(stackItemPtr, newPtr);

}

struct StackItem buildGetStackItem(Value *stackItemPtr) {
  // Generate code to retrieve the fields of a stack item structure

  struct StackItem item;

  item.val = buildGetStackValue(stackItemPtr);

  item.ptr = buildGetStackPointer(stackItemPtr);

  return item;

}

void buildPush(Value *x) {
  // Generate code to push a new number to the stack

  StackItem stackTop = buildGetStackItem(TheStack); 

  Value *voidPtr = builder.CreateCall(malloc_, getInt64(stackItemSize), "mallocResult");
  Value *newItemPtr = builder.CreateBitCast(voidPtr, stackItemPointerType, "newStackItem");

  buildSetStackItem(newItemPtr, stackTop.val, stackTop.ptr);  // Copy the current top of the stack to the new item
  buildSetStackItem(TheStack, x, newItemPtr);  // Then put the new value in the top of the stack, pointing it to our new item

}

Value *buildPop() {
  // Generate code to pop an item off the stack. Returns the Value* indicating what was popped
  
  StackItem oldStackTop = buildGetStackItem(TheStack);
  StackItem newStackTop = buildGetStackItem(oldStackTop.ptr);
  
  buildSetStackItem(TheStack, newStackTop.val, newStackTop.ptr);
  
  Value *castPtr = builder.CreateBitCast(oldStackTop.ptr, PointerType::get(Type::getInt8Ty(getGlobalContext()), 0));
  builder.CreateCall(free_, castPtr);

  return oldStackTop.val;

}

std::vector<Value *> buildPopX(unsigned short x) {
  // Insert x number of pop calls, returning a vector of the results.

  std::vector<Value *> result;
  while (x > 0) {
    result.push_back(builder.CreateCall(pop, "popped"));
    x--;
  }
  return result;

}

Function *buildFunction(std::string name) {
  // Return a function with void return type and no parameters, and with builder set to insert into the entry block.
  // Useful for generating our built-in functions.

  FunctionType *t = FunctionType::get(Type::getVoidTy(getGlobalContext()), false);  
  Function *f = Function::Create(t, Function::ExternalLinkage, name, theModule);
  BasicBlock *entry = BasicBlock::Create(getGlobalContext(), "entry", f);
  builder.SetInsertPoint(entry);
  return f;

}

void buildPrintDouble(Value *doub) {
  // Prints out the supplied Value * doub 
  Value *opts[] = { fstring, doub };
  builder.CreateCall(printf_, opts, "printfCall");
}

void codeGenBuiltIns() {
  // Generates the code for some words that we want built into our language (and some code that's useful for defining those words)

  // Declare useful external functions
  mallocType = FunctionType::get(int8PointerTy, int64Ty, false);
  malloc_ = Function::Create(mallocType, Function::ExternalLinkage, "malloc", theModule);
  freeType = FunctionType::get(voidTy, int8PointerTy, false);
  free_ = Function::Create(freeType, Function::ExternalLinkage, "free", theModule); 
  printfType = FunctionType::get(int32Ty, int8PointerTy, true);
  printf_ = Function::Create(printfType, Function::ExternalLinkage, "printf", theModule);

  // Create some general useful functions
  FunctionType *pushType = FunctionType::get(voidTy, doubleTy, false);
  push = Function::Create(pushType, Function::ExternalLinkage, "push", theModule);
  BasicBlock *pushEntry = BasicBlock::Create(getGlobalContext(), "entry", push);
  builder.SetInsertPoint(pushEntry);
  Value *pushedItem = push -> arg_begin();
  pushedItem -> setName("pushedItem");
  buildPush(pushedItem);
  builder.CreateRetVoid();

  fstring = builder.CreateGlobalStringPtr("%f\n", "fstring");  // Causes segfault if attempted before a SetInsertPoint?

  FunctionType *popType = FunctionType::get(doubleTy, false);  
  pop = Function::Create(popType, Function::ExternalLinkage, "pop", theModule);
  BasicBlock *popEntry = BasicBlock::Create(getGlobalContext(), "entry", pop);
  builder.SetInsertPoint(popEntry);
  builder.CreateRet(buildPop());
   
  std::vector<Value *> stack;

  // Generate code for functions corresponding to various built in words.
  add = buildFunction("add");
  Value *a = builder.CreateCall(pop, "poppedForAdd");
  Value *b = buildGetStackValue(TheStack);
  Value *result = builder.CreateFAdd(a, b, "addtmp");
  buildSetStackValue(TheStack, result);
  builder.CreateRetVoid();

  sub = buildFunction("sub"); 
  a = builder.CreateCall(pop, "poppedForSub");
  b = buildGetStackValue(TheStack);
  result = builder.CreateFSub(b, a, "subtmp");
  buildSetStackValue(TheStack, result);
  builder.CreateRetVoid();
  
  mul = buildFunction("mul");
  a = builder.CreateCall(pop, "poppedForMul");
  b = buildGetStackValue(TheStack);
  result = builder.CreateFMul(b, a, "multmp");
  buildSetStackValue(TheStack, result);
  builder.CreateRetVoid();
  
  divi = buildFunction("div"); 
  a = builder.CreateCall(pop, "poppedForMul");
  b = buildGetStackValue(TheStack);
  result = builder.CreateFDiv(b, a, "divtmp");
  buildSetStackValue(TheStack, result);
  builder.CreateRetVoid();

  negate = buildFunction("negate");
  a = buildGetStackValue(TheStack);
  result = builder.CreateFNeg(a, "negtmp"); 
  buildSetStackValue(TheStack, result);
  builder.CreateRetVoid();

  lt = buildFunction("lt");
  a = builder.CreateCall(pop, "poppedForLt");
  b = buildGetStackValue(TheStack);
  result = builder.CreateUIToFP(builder.CreateFCmpULT(b, a, "lttmp"), Type::getDoubleTy(getGlobalContext()), "lttmpdbl");
  result = builder.CreateFMul(result, getDouble(-1.0));  // Forth "true" results are -1
  buildSetStackValue(TheStack, result);
  builder.CreateRetVoid();

  gt = buildFunction("gt");
  a = builder.CreateCall(pop, "poppedForGt");
  b = buildGetStackValue(TheStack); 
  result = builder.CreateUIToFP(builder.CreateFCmpUGT(b, a, "gttmp"), Type::getDoubleTy(getGlobalContext()), "gttmpdbl");
  result = builder.CreateFMul(result, getDouble(-1.0));  // Forth "true" results are -1
  buildSetStackValue(TheStack, result);
  builder.CreateRetVoid();

  eq = buildFunction("eq");
  a = builder.CreateCall(pop, "poppedForEq");
  b = buildGetStackValue(TheStack);
  result = builder.CreateUIToFP(builder.CreateFCmpOEQ(b, a, "eqtmp"), Type::getDoubleTy(getGlobalContext()), "eqtmpdbl"); 
  result = builder.CreateFMul(result, getDouble(-1.0));  // Forth "true" results are -1
  buildSetStackValue(TheStack, result);
  builder.CreateRetVoid();

  dup = buildFunction("dup");
  a = buildGetStackValue(TheStack);
  builder.CreateCall(push, a);
  builder.CreateRetVoid();

  swa = buildFunction("swap");
  StackItem top = buildGetStackItem(TheStack);
  a = buildGetStackValue(top.ptr);
  buildSetStackValue(TheStack, a);
  buildSetStackValue(top.ptr, top.val);
  builder.CreateRetVoid();

  drop = buildFunction("drop");
  builder.CreateCall(pop);
  builder.CreateRetVoid();

  over = buildFunction("over");
  a = buildGetStackValue(buildGetStackPointer(TheStack));
  builder.CreateCall(push, a);
  builder.CreateRetVoid();

  nip = buildFunction("nip"); 
  Value *nipped = buildGetStackPointer(TheStack); 
  Value *castNipped = builder.CreateBitCast(nipped, PointerType::get(Type::getInt8Ty(getGlobalContext()), 0));
  buildSetStackPointer(TheStack, buildGetStackPointer(nipped));
  builder.CreateCall(free_, castNipped);
  builder.CreateRetVoid();

  tuck = buildFunction("tuck");
  StackItem tucksTop = buildGetStackItem(TheStack);  // Can I generalize this and use it in multiple functions?
  Value *nextDownsPtr = buildGetStackPointer(tucksTop.ptr);
  Value *voidPtr = builder.CreateCall(malloc_, getInt64(stackItemSize), "mallocResult");
  Value *newItemPtr = builder.CreateBitCast(voidPtr, stackItemPointerType, "newStackItem");
  buildSetStackItem(newItemPtr, tucksTop.val, nextDownsPtr); 
  buildSetStackPointer(tucksTop.ptr, newItemPtr); 
  builder.CreateRetVoid();

  rot = buildFunction("rot");
  StackItem rotsTop = buildGetStackItem(TheStack);  // 3 -> 1 
  StackItem rotsNext = buildGetStackItem(rotsTop.ptr);  // 2 -> 3
  Value *rotsBottomVal = buildGetStackValue(rotsNext.ptr); // 1 -> 2
  buildSetStackValue(TheStack, rotsBottomVal);
  buildSetStackValue(rotsTop.ptr, rotsTop.val); 
  buildSetStackValue(rotsNext.ptr, rotsNext.val);
  builder.CreateRetVoid(); 

  dot = buildFunction("dot");
  a = builder.CreateCall(pop);
  buildPrintDouble(a);
  builder.CreateRetVoid();

  // dotS definition is long - it begins here
  dotS = buildFunction("dotS");  // Forth .s prints out the stack in LILO order, this is LIFO - fix?
  BasicBlock *entry = builder.GetInsertBlock();  // could the entry block replace one of the below?
  BasicBlock *checkBlock = BasicBlock::Create(getGlobalContext(), "checkBlock", dotS);
  BasicBlock *finishedBlock = BasicBlock::Create(getGlobalContext(), "loopFinished", dotS);
  BasicBlock *unfinishedBlock = BasicBlock::Create(getGlobalContext(), "loopUnfinished", dotS);
  builder.CreateBr(checkBlock);

  builder.SetInsertPoint(checkBlock);
  PHINode *p = builder.CreatePHI(stackItemPointerType, 2, "phi");
  p -> addIncoming(TheStack, entry);
  StackItem currentItem = buildGetStackItem(p);
  p -> addIncoming(currentItem.ptr, unfinishedBlock);
  Value *cond = builder.CreateIsNull(currentItem.ptr, "isItemNull");
  builder.CreateCondBr(cond, finishedBlock, unfinishedBlock);
  
  builder.SetInsertPoint(unfinishedBlock);
  buildPrintDouble(currentItem.val);
  builder.CreateBr(checkBlock);

  builder.SetInsertPoint(finishedBlock); 
  builder.CreateRetVoid();
  // dotS definition ends here

  words["+"] = add;
  words["-"] = sub;
  words["*"] = mul;
  words["/"] = divi;

  words["negate"] = negate;

  words["<"] = lt;
  words[">"] = gt;
  words["="] = eq;
  
  words["dup"] = dup;
  words["swap"] = swa;
  words["drop"] = drop;
  words["over"] = over;
  words["nip"] = nip;
  words["tuck"] = tuck;
  words["rot"] = rot;

  words["."] = dot;
  words[".s"] = dotS;

}


////////////////////
// Top level loops
////////////////////

void JITASTNode(WordAST *node) {  // very simple way to JIT execute a single word
  Function *F = buildFunction("");  // create anonymous function to run the current word
  node -> codeGen();
  builder.CreateRetVoid();
  void *FPtr = TheExecutionEngine->getPointerToFunction(F);
  void (*FP)() = (void (*)())FPtr;  // from Kaleidoscope - look into how this works
  FP();
  TheExecutionEngine -> freeMachineCodeForFunction(F);
}

int mainLoop(bool JITMode) {

  WordAST *nextASTNode;
  
  while (true) {
    if (JITMode) showPrompt = true;  // ick
    getNextToken();
    showPrompt = false;  // ick
    
    try {
      nextASTNode = parseToken(curTok);
      if (nextASTNode == 0) return 0;  // EOF
      if (JITMode) {
        JITASTNode(nextASTNode);
      } else {
        nextASTNode -> codeGen();
      }
    } catch (CompilerException e) {  // TODO: Use more meaningful types than std::string or char const
      std::cout << e.what() << "\n";
      if (!JITMode) {
        // If we're compiling a file, we want to stop here because we ran into an error
        return 1;  
      }
    }
  }

}

int main(int argc, char *argv[]) {
  
  bool JITMode;

  std::ifstream fs;  // the filestream to open if we need one

  if (argc == 1) {  // if no file, then we just read stdin in JITMode
    JITMode = true;
    inputStream = &std::cin;
  } else if (argc == 2) {  // otherwise open the file we got on the command line
    JITMode = false;
    fs.open(argv[1]);
    if (!fs.is_open()) {
      std::cout << "Couldn't open file \"" << argv[1] << "\"\n";
      return 1;
    }
    inputStream = &fs;  // point inputStream to our file so other functions can use it
  } else {
    std::cout << "usage: rpn [filename]\n";
    return 1;
  }

  InitializeNativeTarget(); 

  // Set up useful types
  // TODO: Maybe declare other types here to shorten the function declarations
  stackItemType = StructType::create(getGlobalContext());  // create opaque structure to forward declare it
  stackItemPointerType = PointerType::get(stackItemType, 0);  // so this and the next line can refer to each other
  stackItemType -> setBody(Type::getDoubleTy(getGlobalContext()), stackItemPointerType, NULL);

  // Our global stack
  TheStack = (GlobalVariable*)(theModule -> getOrInsertGlobal("thestack", stackItemType));
  TheStack -> setInitializer(Constant::getNullValue(stackItemType));

  codeGenBuiltIns();
  
  DataLayout dl = DataLayout("");
  stackItemSize = dl.getTypeAllocSize(stackItemType);

  if (JITMode) {
    // if we're JITing, we need to set up an execution engine
    
    std::string ErrStr;
    TheExecutionEngine = EngineBuilder(theModule).setErrorStr(&ErrStr).create();
    
    if (!TheExecutionEngine) {
      fprintf(stderr, "Could not create ExecutionEngine: %s\n", ErrStr.c_str());
      exit(1);
    }

    std::cout << "Welcome to rpn!\n";

    mainLoop(JITMode);

  } else {
    // if we're not JITing, then we need to wrap our generated code in a main function
    
    FunctionType *mainType = FunctionType::get(int32Ty, false);
    Function *mainFunction = Function::Create(mainType, Function::ExternalLinkage, "main", theModule);
    BasicBlock *mainEntry = BasicBlock::Create(getGlobalContext(), "entry", mainFunction);
    builder.SetInsertPoint(mainEntry);

    if (mainLoop(JITMode) == 1) return 1;
    
    if (fs.is_open()) fs.close();

    // Create a return for main function
    builder.CreateRet(getInt32(0));
  }

  /*static FunctionPassManager *ThePM;
  ThePM -> add(createCFGSimplificationPass());*/

  theModule -> print(*(new raw_os_ostream(std::cout)), 0);  // Figure out the correct way to do this

  return 0;
}
