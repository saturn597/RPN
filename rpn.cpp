#include <algorithm>
#include <cstdio>
#include <iostream>
#include "llvm/Analysis/Verifier.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include <string>

/* RPN calculator and Forth imitator that compiles to LLVM.
 * Inspired by http://llvm.org/releases/3.4.2/docs/tutorial/index.html
 */

//TODO: Take this stuff out of global?
using namespace llvm;

void buildPush(Value *);
Value* buildPop();

LLVMContext &context = getGlobalContext();
static Module *theModule = new Module("rpn", context); 
static IRBuilder<> builder(context);

StructType *stackItemType; 
PointerType *stackItemPointerType;
Function *malloc_;
FunctionType *mallocType;
Function *free_;
FunctionType *freeType;
Function *printf_;
FunctionType *printfType;
GlobalVariable *TheStack;

Function *pop;
Function *push;
Function *add;
Function *sub;
Function *mul;
Function *divi;

Function *lt;
Function *gt;
Function *eq;

Function *drop;
Function *dup;
Function *over;
Function *swa;

Function *dot;

Value *fstring;

std::map<std::string, Function *> words;

BasicBlock *mainEntry;

class WordAST;
WordAST *parseToken(std::string tokenString);  
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

static std::string gettok() {
  static char lastChar = ' ';

  std::string tokenString;

  if (lastChar == EOF) return "";

  // skip whitespace
  while (isspace(lastChar))
    lastChar = getchar();

  while (!isspace(lastChar) && lastChar != EOF) {
    tokenString += lastChar;
    lastChar = getchar();
  }

  // forth is case insensitive, so let's be case insensitive
  std::transform(tokenString.begin(), tokenString.end(), tokenString.begin(), ::tolower); 

  return tokenString;  
}

static std::string curTok;
std::string getNextToken() {   // Is this needed?
  return curTok = gettok();
}

class WordAST {
// Base class for other word types
public: 
  virtual ~WordAST() {};  // Why does this destructor need to be declared?
  virtual void codeGen() = 0;
};

class NumberAST : public WordAST {
  double val;
public:
  NumberAST(double Val) : val(Val) {}
  virtual void codeGen();
};

class BasicWordAST : public WordAST {
  std::string name;
public:
  BasicWordAST(std::string Name) : name(Name) {}
  virtual void codeGen();
};

class DefinitionAST : public WordAST {
  std::vector<WordAST *> content;
  std::string name;
public:
  DefinitionAST(std::string Name, std::vector<WordAST *> Content) : content(Content), name(Name) {}
  virtual void codeGen();
};

class IfAST : public WordAST {
  std::vector<WordAST *> thenContent;
  std::vector<WordAST *> elseContent;
public:
  IfAST(std::vector<WordAST *> ThenContent, std::vector<WordAST *> ElseContent) : thenContent(ThenContent), elseContent(ElseContent) {}
  virtual void codeGen();
};

WordAST *errorP(const char *msg) {
  fprintf(stderr, "Parser error: %s\n", msg);
  return 0;
}

NumberAST *parseNumber() {
  double number = strtod(curTok.c_str(), 0);
 // getNextToken();  // eat the number
  return new NumberAST(number);
}

BasicWordAST *parseBasicWord() {
  std::string name = curTok;
  return new BasicWordAST(name);
}

DefinitionAST *parseDefinition() {  // This will allow : definitions inside : defs which I think isn't possible in forth
  getNextToken();  // eat :
  
  std::string name = curTok;

  getNextToken();  // eat name

  std::vector<WordAST *> content;

  while (curTok != ";") {
    if (curTok == "") return (DefinitionAST *)errorP("; expected");  // eof before end of definition
    content.push_back(parseToken(curTok));
    getNextToken();
  }  

  if (curTok != ";") return (DefinitionAST *)errorP("Unexpected error");  // This shouldn't happen

  return new DefinitionAST(name, content);
}

IfAST *parseIf() {
  
  std::vector<WordAST *> thenContent;
  std::vector<WordAST *> elseContent;
  
  getNextToken();  // Eat the if

  while (curTok != "else" && curTok != "then") {
    if (curTok == "") return (IfAST *)errorP("then or else expected");
    thenContent.push_back(parseToken(curTok));
    getNextToken();
  }
  
  if (curTok == "then") {
    return new IfAST(thenContent, elseContent);
  }

  getNextToken();  // Eat else

  while (curTok != "then") {  // Only happens at all if we hit an else and thus still need to hit the then
    if (curTok == "") return (IfAST *)errorP("then expected");
    elseContent.push_back(parseToken(curTok));
    getNextToken();
  }

  return new IfAST(thenContent, elseContent);

}

void NumberAST::codeGen() {
  builder.CreateCall(push, getDouble(val));
}

void BasicWordAST::codeGen() {
  builder.CreateCall(words[name]);
}

void codeGenVector(std::vector<WordAST *> content) {

  for (unsigned idx = 0; idx < content.size(); ++idx) {
    content.at(idx) -> codeGen();
  }

}

void DefinitionAST::codeGen() {

  BasicBlock *originalBlock = builder.GetInsertBlock();
  Function *f = buildFunction(name);

  codeGenVector(content);  

  builder.CreateRetVoid();

  builder.SetInsertPoint(originalBlock);

  // Add function validation and optimization here, check for conflicting names
  verifyFunction(*f); 

  words[name] = f; 

}

void IfAST::codeGen() {

  Value *cond = builder.CreateFCmpONE(buildPop(), getDouble(0.0), "ifCond");  // should buildPop be a call to pop instead?
  Function *currentFunction = builder.GetInsertBlock() -> getParent();

  BasicBlock *thenBB = BasicBlock::Create(getGlobalContext(), "then", currentFunction);
  BasicBlock *elseBB = BasicBlock::Create(getGlobalContext(), "else");
  BasicBlock *mergeBB = BasicBlock::Create(getGlobalContext(), "merge");
  
  if (elseContent.size() == 0) elseBB = mergeBB;  // If we don't have an else branch

  builder.CreateCondBr(cond, thenBB, elseBB);

  builder.SetInsertPoint(thenBB);
  codeGenVector(thenContent); 
  builder.CreateBr(mergeBB);

  if (elseContent.size() > 0) {  // If we do have an else branch
    currentFunction -> getBasicBlockList().push_back(elseBB);
    builder.SetInsertPoint(elseBB);
    codeGenVector(elseContent);
    builder.CreateBr(mergeBB);
  }

  currentFunction -> getBasicBlockList().push_back(mergeBB);  // why can't this happen above?
  builder.SetInsertPoint(mergeBB);

}

WordAST *parseToken(std::string tokenString) { 

  if (tokenString == "") {  // eof
    return 0;
  } else if (isdigit(tokenString.front())) {  // Do more validating to ensure it's a number
    return parseNumber(); 
  } else if (tokenString == ":") {  // definition
    return parseDefinition();
  } else if (tokenString == "if") {  // if
    return parseIf();
  } else {  // Just a basic word
    return parseBasicWord();
  }

}

void mainLoop() {

  WordAST *nextWord;
  
  while (true) {
    getNextToken();
    
    nextWord = parseToken(curTok);
    
    if (nextWord == 0) {
      return;
    } else {
      nextWord -> codeGen();
    }
  }

}

struct StackItem {
  Value *val;
  Value *ptr;  // Maybe I should use more specific types where applicable?
};
uint64_t stackItemSize;

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

void buildSetStackItem(Value *stackItemPtr, Value *newValue, Value *newPtr) { // maybe stackItemPtr should have the right type
  // Generate code to set the fields of a stack item structure
  
  buildSetStackValue(stackItemPtr, newValue);

  Value *idx[] = { getInt32(0), getInt32(1) }; 
  Value *ptrPtr = builder.CreateInBoundsGEP(stackItemPtr, idx, "settingPtr");
  builder.CreateStore(newPtr, ptrPtr);

}

struct StackItem buildGetStackItem(Value *stackItemPtr) {
  // Generate code to retrieve the fields of a stack item structure

  struct StackItem item;

  item.val = buildGetStackValue(stackItemPtr);

  Value *idx[] = { getInt32(0), getInt32(1) };
  Value *ptrPtr = builder.CreateInBoundsGEP(stackItemPtr, idx, "gettingPtr");
  item.ptr = builder.CreateLoad(ptrPtr, "ptr");

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

int main() {

  // Set up useful types
  // TODO: Maybe declare other types here to shorten the function declarations
  stackItemType = StructType::create(getGlobalContext());  // create opaque structure to forward declare it
  stackItemPointerType = PointerType::get(stackItemType, 0);  // so this and the next line can refer to each other
  stackItemType -> setBody(Type::getDoubleTy(getGlobalContext()), stackItemPointerType, NULL);

  Type *voidTy = Type::getVoidTy(getGlobalContext());
  Type *doubleTy = Type::getDoubleTy(getGlobalContext());
  Type *int8Ty = Type::getInt8Ty(getGlobalContext());
  Type *int32Ty = Type::getInt32Ty(getGlobalContext());
  Type *int64Ty = Type::getInt64Ty(getGlobalContext());
  PointerType *int8PointerTy = PointerType::get(int8Ty, 0);

  // Our global stack
  TheStack = (GlobalVariable*)(theModule -> getOrInsertGlobal("thestack", stackItemType));
  TheStack -> setInitializer(Constant::getNullValue(stackItemType));

  // Declare useful external functions
  mallocType = FunctionType::get(int8PointerTy, int64Ty, false);
  malloc_ = Function::Create(mallocType, Function::ExternalLinkage, "malloc", theModule);
  freeType = FunctionType::get(voidTy, int8PointerTy, false);
  free_ = Function::Create(freeType, Function::ExternalLinkage, "free", theModule); 
  printfType = FunctionType::get(int32Ty, int8PointerTy, true);
  printf_ = Function::Create(printfType, Function::ExternalLinkage, "printf", theModule);

  // Create some useful functions
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
 
  dot = buildFunction("dot");
  a = builder.CreateCall(pop);
  Value *opts[] = { fstring, a };
  builder.CreateCall(printf_, opts, "printfCall");
  builder.CreateRetVoid();

  dotS = buildFunction("dotS");
  // fill this in
  builder.CreateRetVoid();

  words["+"] = add;  // Do forth words even need to correspond to functions?
  words["-"] = sub;
  words["*"] = mul;
  words["/"] = divi;
  words["<"] = lt;
  words[">"] = gt;
  words["="] = eq;

  words["drop"] = drop;
  words["dup"] = dup;
  words["over"] = over;
  words["swap"] = swa;
  words["."] = dot;

  // Insert a main function and an entry block 
  FunctionType *mainType = FunctionType::get(int32Ty, false);
  Function *mainFunction = Function::Create(mainType, Function::ExternalLinkage, "main", theModule);
  mainEntry = BasicBlock::Create(getGlobalContext(), "entry", mainFunction);
  builder.SetInsertPoint(mainEntry);

  DataLayout dl = DataLayout("");
  stackItemSize = dl.getTypeAllocSize(stackItemType);

  mainLoop(); 

  // Create a return for main()
  builder.CreateRet(getInt32(0));


  /*static FunctionPassManager *ThePM;
  ThePM -> add(createCFGSimplificationPass());*/

  theModule -> print(*(new raw_os_ostream(std::cout)), 0);  // Figure out the correct way to do this
  
  return 0;
}
