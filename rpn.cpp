#include <cstdio>
#include <iostream>
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include <string>

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
    std::cout << "You entered a digit\n";
  }
}

int main() {
  
  // Set up llvm basics
  llvm::LLVMContext &context = llvm::getGlobalContext();
  static llvm::Module *theModule = new llvm::Module("rpn", context); 
  static llvm::IRBuilder<> builder(context);

  // Insert a main function and an entry block 
  llvm::FunctionType *ft = llvm::FunctionType::get(llvm::Type::getInt32Ty(llvm::getGlobalContext()), false);
  llvm::Function *mainFunction = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "main", theModule);
  llvm::BasicBlock *entryBlock = llvm::BasicBlock::Create(llvm::getGlobalContext(), "entry", mainFunction);
  builder.SetInsertPoint(entryBlock);

  std::string tokenString;
  
  while ((tokenString = gettok()) != "") {
    processToken(tokenString);
    std::cout << tokenString << "\n";
  }
  
  theModule -> dump();

  return 0;
}
