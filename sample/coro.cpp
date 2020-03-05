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
using namespace llvm::orc;


void print(int32_t i) {
    std::cout << i << std::endl;
}

Error addCoroRoutines(Module &module)
{

    LLVMContext &ctx = module.getContext();
    IRBuilder<> B(ctx);

    std::string buffer;
    raw_string_ostream es(buffer);

    auto coro_resume_signature = FunctionType::get(
        Type::getVoidTy(ctx), Type::getInt8PtrTy(ctx), false);

    auto coro_resume = Function::Create(coro_resume_signature, 
        Function::ExternalLinkage, "coro_resume", module);

    Value *hdl = coro_resume->arg_begin();
    hdl->setName("hdl");

    B.SetInsertPoint(BasicBlock::Create(ctx, "entry", coro_resume));

    // call void @llvm.coro.resume(i8* hdl)
    B.CreateCall(Intrinsic::getDeclaration(
        &module, Intrinsic::coro_resume), hdl);

    B.CreateRetVoid();

    if (verifyFunction(*coro_resume, &es))
        return createStringError(inconvertibleErrorCode(),
                                 "Function verification failed: %s",
                                 es.str().c_str());

    auto coro_destroy_signature = FunctionType::get(
        Type::getVoidTy(ctx), Type::getInt8PtrTy(ctx), false);

    auto coro_destroy = Function::Create(coro_destroy_signature, 
        Function::ExternalLinkage, "coro_destroy", module);

    hdl = coro_destroy->arg_begin();
    hdl->setName("hdl");

    B.SetInsertPoint(BasicBlock::Create(ctx, "entry", coro_destroy));

    // call void @llvm.coro.coro_destroy(i8* hdl)
    B.CreateCall(Intrinsic::getDeclaration(
        &module, Intrinsic::coro_destroy), hdl);

    B.CreateRetVoid();

    if (verifyFunction(*coro_destroy, &es))
        return createStringError(inconvertibleErrorCode(),
                                 "Function verification failed: %s",
                                 es.str().c_str());

    auto coro_done_signature = FunctionType::get(
        Type::getInt1Ty(ctx), Type::getInt8PtrTy(ctx), false);

    auto coro_done = Function::Create(coro_done_signature, 
        Function::ExternalLinkage, "coro_done", module);

    hdl = coro_done->arg_begin();
    hdl->setName("hdl");

    B.SetInsertPoint(BasicBlock::Create(ctx, "entry", coro_done));

    // %done = call i1 @llvm.coro.done(i8* hdl)
    Value * done = B.CreateCall(Intrinsic::getDeclaration(
        &module, Intrinsic::coro_done), hdl, "done");

    B.CreateRet(done);

    if (verifyFunction(*coro_destroy, &es))
        return createStringError(inconvertibleErrorCode(),
                                 "Function verification failed: %s",
                                 es.str().c_str());

    if (verifyModule(module, &es))
        return createStringError(inconvertibleErrorCode(),
                                 "Module verification failed: %s",
                                 es.str().c_str());

    return Error::success();
}

Expected<std::string> codegenIR(Module &module)
{

    LLVMContext &ctx = module.getContext();
    IRBuilder<> B(ctx);

    std::string buffer;
    raw_string_ostream es(buffer);

    auto name = "coro_inc";
    auto returnTy = Type::getInt8PtrTy(ctx);
    auto argTy = Type::getInt32Ty(ctx);
    auto signature = FunctionType::get(returnTy, argTy, false);
    auto linkage = Function::ExternalLinkage;

    auto fn = Function::Create(signature, linkage, name, module);

    Function::arg_iterator args = fn->arg_begin();
    Value *n = args;
    n->setName("n");

    std::vector<Value*> coro_id_args;
    coro_id_args.push_back(ConstantInt::get(Type::getInt32Ty(ctx), 0, false));
    coro_id_args.push_back(ConstantPointerNull::get(Type::getInt8PtrTy(ctx)));
    coro_id_args.push_back(ConstantPointerNull::get(Type::getInt8PtrTy(ctx)));
    coro_id_args.push_back(ConstantPointerNull::get(Type::getInt8PtrTy(ctx)));

    B.SetInsertPoint(BasicBlock::Create(ctx, "entry", fn));

    BasicBlock * resume = BasicBlock::Create(ctx, "resume", fn);
    BasicBlock * cleanup = BasicBlock::Create(ctx, "cleanup", fn);
    BasicBlock * suspend = BasicBlock::Create(ctx, "suspend", fn);
    BasicBlock * trap = BasicBlock::Create(ctx, "trap", fn);

    // %id = call token @llvm.coro.id(i32 0, i8* null, i8* null, i8* null)
    Value * id = B.CreateCall(Intrinsic::getDeclaration(
        &module, Intrinsic::coro_id), coro_id_args, "id");
    
    // %size = call i32 @llvm.coro.size.i32()
    Value * size = B.CreateCall(Intrinsic::getDeclaration(
        &module, Intrinsic::coro_size, Type::getInt32Ty(ctx)), {}, "size");

    // %alloc = call i8* @malloc(i32 %size)
    Value * alloc = B.CreateCall(
    module.getOrInsertFunction("malloc",
      FunctionType::get(
          Type::getInt8PtrTy(ctx), Type::getInt32Ty(ctx), false)), size, "alloc");

    // %hdl = call noalias i8* @llvm.coro.begin(token %id, i8* %alloc)
    Value * hdl =
    B.CreateCall(Intrinsic::getDeclaration(&module, Intrinsic::coro_begin),
        { id, alloc }, "hdl");

    // %inc = add i32 %n, 1
    Value* inc = B.CreateBinOp(Instruction::Add,
        n, ConstantInt::get(Type::getInt32Ty(ctx), 1), "inc");

    // call void @print(i32 %inc)
    B.CreateCall(module.getOrInsertFunction("print",
        FunctionType::get(
            Type::getVoidTy(ctx), Type::getInt32Ty(ctx), false)), inc);

    // %0 = call i8 @llvm.coro.suspend(token none, i1 false)
    Value * zero = B.CreateCall(Intrinsic::getDeclaration(
        &module, Intrinsic::coro_suspend), 
        { ConstantTokenNone::get(ctx), ConstantInt::getFalse(ctx) });
    
    // switch i8 %0, label %suspend [i8 0, label %resume
    //                               i8 1, label %cleanup]
    SwitchInst * swch = B.CreateSwitch(zero, suspend, 2);
    swch->addCase(ConstantInt::get(Type::getInt8Ty(ctx), 0), resume);
    swch->addCase(ConstantInt::get(Type::getInt8Ty(ctx), 1), cleanup);

    // Start introducing instructions into the "resume" basic block
    B.SetInsertPoint(resume);

    // %inc2 = add i32 %inc, 1
    Value * inc2 = B.CreateBinOp(Instruction::Add,
                                 inc, ConstantInt::get(Type::getInt32Ty(ctx), 1), "inc2");

    // call void @print(i32 %inc2)
    B.CreateCall(
    module.getOrInsertFunction("print",
      FunctionType::get(
          Type::getVoidTy(ctx), Type::getInt32Ty(ctx), false)), inc2);
    
    // %0 = call i8 @llvm.coro.suspend(token none, i1 true) # Final Suspend!
    Value * final = B.CreateCall(Intrinsic::getDeclaration(
        &module, Intrinsic::coro_suspend), 
        { ConstantTokenNone::get(ctx), ConstantInt::getTrue(ctx) }, "final");

    // switch i8 %0, label %suspend [i8 0, label %trap
    //                               i8 1, label %cleanup]
    SwitchInst * final_swch = B.CreateSwitch(final, suspend, 2);
    final_swch->addCase(ConstantInt::get(Type::getInt8Ty(ctx), 0), trap);
    final_swch->addCase(ConstantInt::get(Type::getInt8Ty(ctx), 1), cleanup);

    // Start introducing instructions into the "cleanup" basic block
    B.SetInsertPoint(cleanup);

    // %mem = call i8 * @llvm.coro.free()
    Value * mem = B.CreateCall(Intrinsic::getDeclaration(
        &module, Intrinsic::coro_free), {id, hdl}, "mem");

    // call void @free(i8* %mem)
    B.CreateCall(
    module.getOrInsertFunction("free",
      FunctionType::get(
          Type::getVoidTy(ctx), Type::getInt8PtrTy(ctx), false)), mem);

    // br label %suspend
    B.CreateBr(suspend);

    // Start introducing instructions into the "trap" basic block
    B.SetInsertPoint(trap);

    // call void @llvm.trap()
    B.CreateCall(Intrinsic::getDeclaration(
        &module, Intrinsic::trap), {});

    B.CreateUnreachable();

    // Start introducing instructions into the "suspend" basic block
    B.SetInsertPoint(suspend);

    // %unused = call i1 @llvm.coro.end(i8* %hdl, i1 false)
    B.CreateCall(Intrinsic::getDeclaration(
        &module, Intrinsic::coro_end), { hdl, ConstantInt::getFalse(ctx) }, "mem");
    B.CreateRet(hdl);

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

    // Add local absolute symbol
    TheJIT->defineAbsolute("print",
        JITEvaluatedSymbol((JITTargetAddress)print, JITSymbolFlags::Exported));

    auto module = std::make_unique<Module>("MyFirstJIT", TheJIT->getContext());
    module->setDataLayout(TheJIT->getDataLayout());

    ExitOnErr(addCoroRoutines(*module));

    std::string JitedFnName = ExitOnErr(codegenIR(*module));

    ExitOnErr(TheJIT->addModule(std::move(module)));

    // Request function; this compiles to machine code and links.
    auto coro_inc =
        ExitOnErr(TheJIT->getFunction<int8_t * (int32_t)>(JitedFnName));
    auto coro_resume =
        ExitOnErr(TheJIT->getFunction<void (int8_t *)>("coro_resume"));
    auto coro_destroy =
        ExitOnErr(TheJIT->getFunction<void (int8_t *)>("coro_destroy"));
    auto coro_done =
        ExitOnErr(TheJIT->getFunction<bool (int8_t *)>("coro_done"));

    int8_t * hdl = coro_inc(8192);

    while (coro_done(hdl) == false) {
        coro_resume(hdl);
    }
    coro_destroy(hdl);  
    
    return 0;
}
