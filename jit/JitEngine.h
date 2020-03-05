#pragma once

#include <llvm/ADT/StringRef.h>
#include <llvm/ExecutionEngine/JITEventListener.h>
#include <llvm/ExecutionEngine/JITSymbol.h>
#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/ExecutionEngine/Orc/IRCompileLayer.h>
#include <llvm/ExecutionEngine/Orc/IRTransformLayer.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Error.h>
#include <llvm/Target/TargetMachine.h>

#include <functional>
#include <memory>
#include <string>

class JitEngine
{

public:
    static llvm::Expected<std::unique_ptr<JitEngine>> Create()
    {
        auto JTMB = llvm::orc::JITTargetMachineBuilder::detectHost();

        if (!JTMB)
        {
            return JTMB.takeError();
        }

        auto DL = JTMB->getDefaultDataLayoutForTarget();

        if (!DL)
        {
            return DL.takeError();
        }

        return std::make_unique<JitEngine>(std::move(*JTMB), std::move(*DL));
    }

    llvm::LLVMContext &getContext()
    {
        return *Context.getContext();
    }

    llvm::Error addModule(std::unique_ptr<llvm::Module> module);

    template <class Signature_t>
    llvm::Expected<std::function<Signature_t>> getFunction(llvm::StringRef Name)
    {
        if (auto A = getFunctionAddr(Name))
            return std::function<Signature_t>(
                llvm::jitTargetAddressToPointer<Signature_t *>(*A));
        else
            return A.takeError();
    }

    const llvm::DataLayout & getDataLayout() const { return DL; }
    const llvm::orc::MangleAndInterner & getMangle() const { return Mangle; }

    llvm::Error defineAbsolute(llvm::StringRef Name, llvm::JITEvaluatedSymbol Sym);

    /// Constructor
    JitEngine(llvm::orc::JITTargetMachineBuilder JTMB, llvm::DataLayout DL);

private:

    /// Execution Session
    /// This object controls the JIT program. It is thread safe.
    llvm::orc::ExecutionSession ES;

    /// GDB Listener
    /// This listener will be attached to the code generation to enable the
    /// debugging of JIT compiled code.
    llvm::JITEventListener *GDBListener;

    llvm::DataLayout DL;

    llvm::orc::RTDyldObjectLinkingLayer ObjectLayer;
    llvm::orc::IRCompileLayer CompileLayer;
    llvm::orc::IRTransformLayer OptimizeLayer;
    llvm::orc::ThreadSafeContext Context;

    llvm::orc::MangleAndInterner Mangle;

    llvm::orc::RTDyldObjectLinkingLayer::GetMemoryManagerFunction
    createMemoryManagerFtor();

    llvm::orc::JITDylib::GeneratorFunction createHostProcessResolver();

    llvm::orc::RTDyldObjectLinkingLayer::NotifyLoadedFunction
    createNotifyLoadedFtor();

    llvm::Error applyDataLayout(llvm::Module &module);

    llvm::Expected<llvm::JITTargetAddress> getFunctionAddr(llvm::StringRef Name);
};
