#include "JitOptimizer.h"

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/IRPrintingPasses.h>
#include <llvm/Transforms/Coroutines.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/PassRegistry.h>

#include <fstream>
#include <iostream>

using namespace llvm;
using namespace llvm::orc;

Expected<ThreadSafeModule>
JitOptimizer::operator()(ThreadSafeModule TSM,
                            const MaterializationResponsibility &)
{
    Module &M = *TSM.getModule();

    legacy::FunctionPassManager FPM(&M);

    B.Inliner = createFunctionInliningPass(B.OptLevel, B.SizeLevel, false);

    addCoroutinePassesToExtensionPoints(B);

    B.populateFunctionPassManager(FPM);

    FPM.doInitialization();
    for (Function &F : M)
        FPM.run(F);
    FPM.doFinalization();

    legacy::PassManager MPM;
    B.populateModulePassManager(MPM);
    MPM.run(M);

    return std::move(TSM);
}