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

Expected<std::string> codegenIR(Module &module)
{

    LLVMContext &ctx = module.getContext();
    IRBuilder<> B(ctx);

    auto name = "mul_add";
    auto returnTy = Type::getInt32Ty(ctx);
    auto argTy = Type::getInt32Ty(ctx);
    auto signature = FunctionType::get(returnTy, {argTy, argTy, argTy}, false);
    auto linkage = Function::ExternalLinkage;

    auto fn = Function::Create(signature, linkage, name, module);

    Function::arg_iterator args = fn->arg_begin();
    Value *x = args++;
    x->setName("x");
    Value *y = args++;
    y->setName("y");
    Value *z = args++;
    z->setName("z");

    B.SetInsertPoint(BasicBlock::Create(ctx, "entry", fn));

    Value* tmp = B.CreateBinOp(Instruction::Mul,
                                   x, y, "tmp");
    Value* tmp2 = B.CreateBinOp(Instruction::Add,
                                    tmp, z, "tmp2");

    B.CreateRet(tmp2);

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
    auto mul_add =
        ExitOnErr(TheJIT->getFunction<int32_t(int32_t, int32_t, int32_t)>(JitedFnName));

    int32_t ret = mul_add(23, 80, 90);

    std::cout << "23 * 80 + 90 = " << ret << std::endl;

    return 0;
}