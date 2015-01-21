#include <cstdio>
#include <iostream>
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_os_ostream.h"
#include <string>


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
Function *dot;
Function *dup;
Function *swa;

Value *fstring;

std::map<std::string, Function *> builtIns;

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
  char lastChar = ' ';

  std::string tokenString;

  // skip whitespace
  while (isspace(lastChar))
    lastChar = getchar();

  if (lastChar == EOF) return ""; 

  while (!isspace(lastChar)) {
    tokenString += lastChar;
    lastChar = getchar();
  }

  // skip comments -- maybe implement this in forth at some point?
  if (tokenString == "((") {
    while (tokenString != "))")
      tokenString = gettok();
    return gettok();
  }

  return tokenString;
}

void processToken(std::string tokenString) {

  if (isdigit(tokenString.front())) {
    builder.CreateCall(push, getDouble(strtod(tokenString.c_str(), 0)));
  } else {
    builder.CreateCall(builtIns[tokenString]);  // should check if tokenString is in builtIns first
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

Function *makeBuiltIn(std::string name) {
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

  // Generate code for functions corresponding to various forth words.
  add = makeBuiltIn("add");
  Value *a = builder.CreateCall(pop, "poppedForAdd");
  Value *b = buildGetStackValue(TheStack);
  Value *result = builder.CreateFAdd(a, b, "addtmp");
  buildSetStackValue(TheStack, result);
  builder.CreateRetVoid();

  sub = makeBuiltIn("sub"); 
  a = builder.CreateCall(pop, "poppedForSub");
  b = buildGetStackValue(TheStack);
  result = builder.CreateFSub(b, a, "subtmp");
  buildSetStackValue(TheStack, result);
  builder.CreateRetVoid();
  
  mul = makeBuiltIn("mul");
  a = builder.CreateCall(pop, "poppedForMul");
  b = buildGetStackValue(TheStack);
  result = builder.CreateFMul(b, a, "multmp");
  buildSetStackValue(TheStack, result);
  builder.CreateRetVoid();
  
  divi = makeBuiltIn("div"); 
  a = builder.CreateCall(pop, "poppedForMul");
  b = buildGetStackValue(TheStack);
  result = builder.CreateFDiv(b, a, "divtmp");
  buildSetStackValue(TheStack, result);
  builder.CreateRetVoid();
  
  dot = makeBuiltIn("dot");
  a = builder.CreateCall(pop);
  Value *opts[] = { fstring, a };
  builder.CreateCall(printf_, opts, "printfCall");
  builder.CreateRetVoid();

  dup = makeBuiltIn("dup");
  a = buildGetStackValue(TheStack);
  builder.CreateCall(push, a);
  builder.CreateRetVoid();

  swa = makeBuiltIn("swap");
  StackItem top = buildGetStackItem(TheStack);
  a = buildGetStackValue(top.ptr);
  buildSetStackValue(TheStack, a);
  buildSetStackValue(top.ptr, top.val);
  builder.CreateRetVoid();

  builtIns["+"] = add;  // Do forth words even need to correspond to functions?
  builtIns["-"] = sub;
  builtIns["*"] = mul;
  builtIns["/"] = divi;

  builtIns["dup"] = dup;  
  builtIns["."] = dot;
  builtIns["swap"] = swa;

  // Insert a main function and an entry block 
  FunctionType *mainType = FunctionType::get(int32Ty, false);
  Function *mainFunction = Function::Create(mainType, Function::ExternalLinkage, "main", theModule);
  BasicBlock *entryBlock = BasicBlock::Create(getGlobalContext(), "entry", mainFunction);
  builder.SetInsertPoint(entryBlock);

  DataLayout dl = DataLayout("");
  stackItemSize = dl.getTypeAllocSize(stackItemType);

  std::string tokenString;
  
  while ((tokenString = gettok()) != "") {
    processToken(tokenString);
  }

  // Create a return for main()
  builder.SetInsertPoint(entryBlock);
  builder.CreateRet(getInt32(0));
  theModule -> print(*(new raw_os_ostream(std::cout)), 0);  // Figure out the correct way to do this
  
  return 0;
}
