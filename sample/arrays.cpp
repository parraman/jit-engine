#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>

#include <functional>
#include <memory>
#include <fstream>
#include <iostream>

#include "JitEngine.h"

using namespace llvm;

struct Point {
    int32_t x;
    int32_t y;
};

/**
 * 
 * The following function generates the code of a function equivalent to this one:
 * 
 * int sum_array(struct Point array[4]) {
 *
 *   int inc;
 *   int result = 0;
 *
 *   for (inc = 0; inc < 4; inc++) {
 *
 *     result = result + array[inc].x;
 *
 *   }
 *
 *   return res;
 *
 * }
 */ 

Expected<std::string> codegenIR(Module &module)
{

    LLVMContext &ctx = module.getContext();
    IRBuilder<> B(ctx);

    auto name = "sum_array";
    auto returnTy = Type::getInt32Ty(ctx);

    auto pointStr = StructType::create(ctx, 
        { Type::getInt32Ty(ctx), Type::getInt32Ty(ctx) }, "pointStr", true );

    auto argTy = PointerType::get(pointStr, 0);
    auto signature = FunctionType::get(returnTy, argTy, false);
    auto linkage = Function::ExternalLinkage;

    auto fn = Function::Create(signature, linkage, name, module);

    Function::arg_iterator args = fn->arg_begin();
    Value *pArray = args;
    pArray->setName("array");

    BasicBlock * entry = BasicBlock::Create(ctx, "entry", fn);

    BasicBlock * loop = BasicBlock::Create(ctx, "loop", fn);
    BasicBlock * end = BasicBlock::Create(ctx, "end", fn);

    B.SetInsertPoint(entry);

    // Allocate inc on the stack. The function returns the address of the
    // local variable
    Value * inc_addr = B.CreateAlloca(Type::getInt32Ty(ctx));

    // *inc_addr = 0
    B.CreateStore(ConstantInt::get(Type::getInt32Ty(ctx), 0), inc_addr);
    
    // Allocate result on the stack. The function returns the address of the
    // local variable
    Value * result_addr = B.CreateAlloca(Type::getInt32Ty(ctx));
    B.CreateStore(ConstantInt::get(Type::getInt32Ty(ctx), 0), result_addr);

    // Inconditional branch to loop
    B.CreateBr(loop);

    // Starting loop definition
    B.SetInsertPoint(loop);

    // inc = *inc_addr
    Value * inc = B.CreateLoad(inc_addr);
    
    // ptr = &(ptr[inc].x)
    Value * ptr = B.CreateInBoundsGEP(
        pointStr, pArray, { inc, ConstantInt::get(Type::getInt32Ty(ctx), 0) });
    
    // x = *ptr
    Value * x = B.CreateLoad(ptr);

    // result = *result_addr;
    Value * result = B.CreateLoad(result_addr);
    
    // inc = inc + 1
    inc = B.CreateAdd(inc, ConstantInt::get(Type::getInt32Ty(ctx), 1));

    // *inc_addr = inc
    B.CreateStore(inc, inc_addr);
    
    // result = result + x
    result = B.CreateAdd(result, x);

    // *result_addr = result
    B.CreateStore(result, result_addr);
    
    // cmp = (inc == 4)
    Value * cmp = B.CreateICmpEQ(inc, ConstantInt::get(Type::getInt32Ty(ctx), 4));

    // if cmp then end else loop
    B.CreateCondBr(cmp, end, loop);

    // Starting end definition
    B.SetInsertPoint(end);

    // result = * result_addr
    result = B.CreateLoad(result_addr);

    // return result
    B.CreateRet(result);

    std::string buffer;
    raw_string_ostream es(buffer);

    if (verifyFunction(*fn, &es))
        return createStringError(inconvertibleErrorCode(),
                                 "Function verification failed: %s",
                                 es.str().c_str());

    if (verifyModule(module, &es))
        return createStringError(inconvertibleErrorCode(),
                                 "Module verification failed: %s",
                                 es.str().c_str());

    return name;
}

std::unique_ptr<JitEngine> TheJIT;
static ExitOnError ExitOnErr;

int main(int argc, char **argv)
{

    InitLLVM X(argc, argv);

    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();

    PassRegistry &Registry = *PassRegistry::getPassRegistry();
    initializeCoroutines(Registry);

    TheJIT = ExitOnErr(JitEngine::Create());

    auto module = std::make_unique<Module>("MyFirstJIT", TheJIT->getContext());
    module->setDataLayout(TheJIT->getDataLayout());

    std::string JitedFnName = ExitOnErr(codegenIR(*module));

    ExitOnErr(TheJIT->addModule(std::move(module)));

    // Request function; this compiles to machine code and links.
    auto sum_array =
        ExitOnErr(TheJIT->getFunction<int32_t(struct Point [4])>(JitedFnName));

    struct Point test[] = {{2,0}, {3,1}, {8,0}, {173,1}};

    int32_t ret = sum_array(test);

    std::cout << "result = " << ret << std::endl;

    return 0;
}