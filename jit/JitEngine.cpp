#include "JitEngine.h"
#include "JitOptimizer.h"

#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>

using namespace llvm;
using namespace llvm::orc;

JitEngine::JitEngine(JITTargetMachineBuilder JTMB, DataLayout DL) : 
    GDBListener(JITEventListener::createGDBRegistrationListener()),
    ObjectLayer(ES, createMemoryManagerFtor()),
    CompileLayer(ES, ObjectLayer, ConcurrentIRCompiler(std::move(JTMB))),
    OptimizeLayer(ES, CompileLayer),
    DL(std::move(DL)),
    Mangle(ES, this->DL),
    Context(std::make_unique<LLVMContext>())
{

    ObjectLayer.setNotifyLoaded(createNotifyLoadedFtor());
    auto R = createHostProcessResolver();
    ES.getMainJITDylib().setGenerator(std::move(R));
}

JITDylib::GeneratorFunction JitEngine::createHostProcessResolver()
{
    char Prefix = DL.getGlobalPrefix();
    return cantFail(DynamicLibrarySearchGenerator::GetForCurrentProcess(Prefix));
}

RTDyldObjectLinkingLayer::NotifyLoadedFunction JitEngine::createNotifyLoadedFtor()
{
    using namespace std::placeholders;
    return std::bind(&JITEventListener::notifyObjectLoaded,
                     GDBListener, _1, _2, _3);
}

using GetMemoryManagerFunction =
    RTDyldObjectLinkingLayer::GetMemoryManagerFunction;

GetMemoryManagerFunction JitEngine::createMemoryManagerFtor() {
  return []() -> GetMemoryManagerFunction::result_type {
    return std::make_unique<SectionMemoryManager>();
  };
}

Error JitEngine::applyDataLayout(Module &module)
{
    if (module.getDataLayout().isDefault())
        module.setDataLayout(DL);

    if (module.getDataLayout() != DL)
        return make_error<StringError>(
            "Added modules have incompatible data layouts",
            inconvertibleErrorCode());

    return Error::success();
}

Error JitEngine::addModule(std::unique_ptr<llvm::Module> module)
{

    if (auto Err = applyDataLayout(*module))
        return Err;

    OptimizeLayer.setTransform(JitOptimizer(2));

    return OptimizeLayer.add(ES.getMainJITDylib(),
                             ThreadSafeModule(std::move(module), Context),
                             ES.allocateVModule());
}

Expected<JITTargetAddress> JitEngine::getFunctionAddr(StringRef Name)
{
    SymbolStringPtr NamePtr = Mangle(Name);
    JITDylibSearchList JDs{{&ES.getMainJITDylib(), true}};

    Expected<JITEvaluatedSymbol> S = ES.lookup(JDs, NamePtr);
    
    if (!S)
        return S.takeError();

    JITTargetAddress A = S->getAddress();
    if (!A)
        return createStringError(inconvertibleErrorCode(),
                                 "'%s' evaluated to nullptr", Name.data());

    return A;
}
