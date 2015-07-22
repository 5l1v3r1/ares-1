//===- HLIRLowerPThread.cpp - Lower HLIR into pthreads  -------------------===//
//
//                     Project Ares
//
// This file is distributed under the expression permission of Los Alamos
// National laboratory. It is Licensed under the BSD-3 license. For more
// information, see LICENSE.md in the Ares root directory.
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//
#include <list>
#include <map>

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

#include "llvm/Transforms/HLIR/HLIRLower.h"

using namespace llvm;

namespace {

class HLIRLowerPThread : public HLIRLower {
private:
  static const unsigned int ANSWER_OFFSET = 0;
  static const unsigned int ARG_OFFSET = 0;

  /// Common Functions
  Function *pthread_create;
  Function *pthread_exit;
  Function *pthread_join;

  Function *sem_init;
  Function *sem_wait;
  Function *sem_post;
  Function *sem_destroy;

  /// Common Type
  PointerType *PthreadAttrPtrTy;
  Type *PThreadTy;
  Type *SemTy;

  /// Maps a function to a wrapped version of that function which is
  /// able to be called by llvm.
  std::map<Function *, Function *> FuncToWrapFunc;
  std::map<Function *, StructType *> FuncToStruct;

  bool InitLower(Module &M) { return false; }

  /// Make a declaration of `pthread_create` if necessary. Returns whether or
  /// not the module was changed.
  ///
  /// TODO: Currently always makes the declaration.
  bool initPthreadCreate(Module *M) {
    /*
      typedef long pthread_t;
     */
    this->PThreadTy = Type::getInt64Ty(M->getContext());

    /*
      struct pthread_attr_t {
        long;
        char[48];
      }
    */
    Type *PThreadAttrs[2] = {
        Type::getInt64Ty(M->getContext()),
        ArrayType::get(Type::getInt8Ty(M->getContext()), 48)};
    this->PthreadAttrPtrTy = PointerType::get(
        StructType::create(PThreadAttrs, "union.pthread_attr_t"), 0);

    /*
       void (*start)(void *args)
    */
    Type *VoidPtrArgTy[1] = {Type::getInt8PtrTy(M->getContext())};
    Type *StartFTy =
        PointerType::get(FunctionType::get(Type::getInt8PtrTy(M->getContext()),
                                           VoidPtrArgTy, false),
                         0);

    /*
      int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
      void *(*start_routine) (void *), void *arg);
    */
    Type *PthreadArgsTy[4] = {PointerType::get(PThreadTy, 0), PthreadAttrPtrTy,
                              StartFTy, Type::getInt8PtrTy(M->getContext())};
    FunctionType *FTy = FunctionType::get(Type::getInt32Ty(M->getContext()),
                                          PthreadArgsTy, false);
    this->pthread_create =
        Function::Create(FTy, Function::ExternalLinkage, "pthread_create", M);

    /*
     void pthread_exit(void *retval);
    */
    FunctionType *ExitTy = FunctionType::get(Type::getVoidTy(M->getContext()),
                                             VoidPtrArgTy, false);
    this->pthread_exit =
        Function::Create(FTy, Function::ExternalLinkage, "pthread_exit", M);

    /*
      int pthread_join(pthread_t thread, void **retval);
    */
    Type *JoinArgsTy[2] = {
        PThreadTy, PointerType::get(Type::getInt8PtrTy(M->getContext()), 0)};
    FTy =
        FunctionType::get(Type::getInt32Ty(M->getContext()), JoinArgsTy, false);
    this->pthread_join =
        Function::Create(FTy, Function::ExternalLinkage, "pthread_join", M);

    /*
      struct sem_t {
        long;
        char[24];
      };
    */
    Type *SemComps[2] = {Type::getInt64Ty(M->getContext()),
                         ArrayType::get(Type::getInt8Ty(M->getContext()), 24)};
    this->SemTy = StructType::create(SemComps, "union.sem_t");

    /*
      int sem_init(sem_t*, int, unsigned int);
    */
    Type *SemInitArgsTy[3] = {PointerType::get(SemTy, 0),
                              Type::getInt32Ty(M->getContext()),
                              Type::getInt32Ty(M->getContext())};
    FTy = FunctionType::get(Type::getInt32Ty(M->getContext()), SemInitArgsTy,
                            false);
    this->sem_init =
        Function::Create(FTy, Function::ExternalLinkage, "sem_init", M);

    /*
      int sem_wait(sem_t*);
    */
    Type *SemWaitArgsTy[1] = {PointerType::get(SemTy, 0)};
    FTy = FunctionType::get(Type::getInt32Ty(M->getContext()), SemWaitArgsTy,
                            false);
    this->sem_wait =
        Function::Create(FTy, Function::ExternalLinkage, "sem_wait", M);

    /*
      int sem_post(sem_t*);
    */
    Type *SemPostArgsTy[1] = {PointerType::get(SemTy, 0)};
    FTy = FunctionType::get(Type::getInt32Ty(M->getContext()), SemPostArgsTy,
                            false);
    this->sem_post =
        Function::Create(FTy, Function::ExternalLinkage, "sem_post", M);

    /*
      int sem_destroy(sem_t*);
     */
    Type *SemDestroyArgTy[1] = {PointerType::get(SemTy, 0)};
    FTy = FunctionType::get(Type::getInt32Ty(M->getContext()), SemDestroyArgTy,
                            false);
    this->sem_destroy =
        Function::Create(FTy, Function::ExternalLinkage, "sem_destroy", M);

    return true;
  }

  /// Get or create a wrapper struct for a given function. Saves structs in the
  /// `FuncToStruct` map.
  ///
  /// Struct will have the following shape:
  ///     struct {
  ///       <return type | int>;
  ///       Semaphore;
  ///       arg1;
  ///       arg2;
  ///       ...
  ///     }
  StructType *GetFuncStruct(Function *F) {
    StructType *WrapTy = NULL;

    if (this->FuncToStruct.find(F) == this->FuncToStruct.end()) {
      std::vector<Type *> Members;

      if (StructType::isValidElementType(F->getReturnType())) {
        Members.push_back(F->getReturnType());
      } else {
        Members.push_back(Type::getInt32Ty(F->getContext()));
      }

      for (auto param : F->getFunctionType()->params()) {
        Members.push_back(param);
      }

      WrapTy = StructType::create(ArrayRef<Type *>(Members));
      this->FuncToStruct.emplace(F, WrapTy);
    } else {
      WrapTy = FuncToStruct[F];
    }

    return WrapTy;
  }

  /// Simply declares a wrapper function for F with an entry block, and
  /// returns it.
  ///
  /// Used almost exclusively by `GetWrapperFunction`.
  Function *DeclareWrapFunc(Function *F, Module *M) {
    Type *Arg[1] = {Type::getInt8PtrTy(F->getContext())};
    Function *WF = Function::Create(
        FunctionType::get(Type::getInt8PtrTy(F->getContext()), Arg, false),
        Function::ExternalLinkage, "hlir.pthread.wrapped." + F->getName(), M);
    BasicBlock::Create(M->getContext(), "entry", WF, 0);

    return WF;
  }

  /// From the first (and only) argument from a wrapper function, unloads
  /// it into stack memory, and bitcasts it to the known struct type.
  ///
  /// TODO: Copy semantics, and copy strait into the correct type.
  ///
  /// Used almost exclusively by `GetWrapperFunction`.
  Value *LoadPackedArgs(Function *WF, StructType *Ty) {
    IRBuilder<> B(&WF->getEntryBlock());

    AllocaInst *PtrArg = B.CreateAlloca(Type::getInt8PtrTy(WF->getContext()));
    B.CreateStore(WF->arg_begin(), PtrArg);
    Value *PackedArgs = B.CreateLoad(PtrArg);
    PackedArgs = B.CreateBitCast(PackedArgs, PointerType::get(Ty, 0));

    return PackedArgs;
  }

  /// Inside a wrapper function, this function will unpack all arguments.
  /// It will then return an ArrayRef of the unpacked args which can be
  /// used for a function call.
  ///
  /// Used almost exclusively by `GetWrapperFunction`.
  std::vector<Value *> UnpackArgs(Function *WF, StructType *WrapTy,
                                  Value *PackedArgs) {
    std::vector<Value *> UnpackedArgs;
    IRBuilder<> B(&WF->getEntryBlock());

    for (unsigned int ElemIndex = 0; ElemIndex < WrapTy->elements().size() - 1;
         ElemIndex++) {
      Value *GEPIndex[2] = {
          ConstantInt::get(Type::getInt64Ty(WF->getContext()), 0),
          ConstantInt::get(Type::getInt32Ty(WF->getContext()), ElemIndex + 1)};
      Value *ElemPtr = B.CreateGEP(PackedArgs, GEPIndex);
      // Value *Elem = B.CreateLoad(ElemPtr);
      UnpackedArgs.push_back(B.CreateLoad(ElemPtr));
    }

    return UnpackedArgs;
  }

  /// Given a wrapper function and a function to wrap, will actually call the
  /// to-be-wrapped function. If needed, the result will be stored in the first
  /// element to the structure pointed to by RetPtr.
  ///
  /// Used almost exclusively by `GetWrapperFunction`.
  void WrapFuncCall(Function *WF, Function *F, ArrayRef<Value *> UnpackedArgs,
                    Value *RetPtr) {
    IRBuilder<> B(&WF->getEntryBlock());
    Value *RetVal = B.CreateCall(F, UnpackedArgs);

    // If need be, store the return.
    if (StructType::isValidElementType(F->getReturnType())) {
      Value *GEPIndex[2] = {
          ConstantInt::get(Type::getInt64Ty(F->getContext()), 0),
          ConstantInt::get(Type::getInt32Ty(F->getContext()), 0)};
      B.CreateStore(RetVal, B.CreateGEP(RetPtr, GEPIndex));
    }
  }

  /// Declare, construct, and return a Function wrapped to be launched by a
  /// pthread. Saves functions in the `FuncToWrapFunc` map.
  ///
  /// Functions will have the following structure.
  ///  void *f(<ret> (*func)(<args>), void* wrap_struct) {
  ///   *wrap_struct = func(wrap_struct.x, wrap_struct.y, ...);
  ///   return 0;
  /// }
  Function *GetWrapperFunction(Module *M, Function *F, StructType *WrapTy) {
    Function *WF;

    if (this->FuncToWrapFunc.find(F) == this->FuncToWrapFunc.end()) {
      // clang-format off
      WF                                = DeclareWrapFunc(F, M);
      Value *PackedArgs                 = LoadPackedArgs(WF, WrapTy);
      std::vector<Value *> UnpackedArgs = UnpackArgs(WF, WrapTy, PackedArgs);
      // clang-format on

      WrapFuncCall(WF, F, ArrayRef<Value *>(UnpackedArgs), PackedArgs);
      IRBuilder<> B(&WF->getEntryBlock());
      B.CreateRet(
          ConstantPointerNull::get(Type::getInt8PtrTy(M->getContext())));

      this->FuncToWrapFunc.emplace(F, WF);
    } else {
      WF = this->FuncToWrapFunc[F];
    }

    return WF;
  }

  /// Given a call instruction, will wrap up the call into a pthread launch.
  ///
  /// TODO: Reduce the number of arguments this takes.
  void LaunchWrapper(CallInst *I, Function *WF, StructType *Ty, Value *ArgPtr,
                     Value *ThreadPtr, IRBuilder<> &B) {
    int ArgId = 0;
    for (auto &Arg : I->arg_operands()) {
      Value *GEPIndex[2] = {
          ConstantInt::get(Type::getInt64Ty(I->getContext()), 0),
          ConstantInt::get(Type::getInt32Ty(I->getContext()), ArgId + 1)};
      B.CreateStore(Arg, B.CreateGEP(ArgPtr, GEPIndex));
      ArgId++;
    }

    Value *PThreadArgs[4] = {
        ThreadPtr, ConstantPointerNull::get(PthreadAttrPtrTy), WF,
        B.CreateBitCast(ArgPtr, Type::getInt8PtrTy(I->getContext()))};
    B.CreateCall(this->pthread_create, PThreadArgs);
  }

  /// Given a call that has just been wrapped into a launch, will find
  /// THE FIRST USE, and put a force before it, and replace all further uses
  /// with this forced value.
  ///
  /// This is not even a little correct. It essentially is assuming the first
  /// use will be in the defining block. This is an assumption that was made
  /// for prototype reasons. TODO: Generalize this technique.
  void ForceFutures(CallInst *I, Value *ArgPtr, Value *ThreadPtr) {
    for (User *U : I->users()) {
      if (Instruction *Inst = dyn_cast<Instruction>(U)) {
        // This builder inserts before the first use.
        IRBuilder<> ForceRet(Inst);

        // FIRST, wait for the thread
        Value *JoinArgs[2] = {ForceRet.CreateLoad(ThreadPtr),
                              ConstantPointerNull::get(PointerType::get(
                                  Type::getInt8PtrTy(I->getContext()), 0))};
        ForceRet.CreateCall(this->pthread_join, JoinArgs);

        // NEXT: get the ret out of the struct
        // (I know the return is there because there was a use.)
        Value *GEPIndex[2] = {
            ConstantInt::get(Type::getInt64Ty(I->getContext()), 0),
            ConstantInt::get(Type::getInt32Ty(I->getContext()), 0)};
        Value *RetVal =
            ForceRet.CreateLoad(ForceRet.CreateGEP(ArgPtr, GEPIndex));

        // LAST: Replace all uses with the true return value
        I->replaceAllUsesWith(RetVal);
        break;
      }
    }
  }

  /// Given a function call to lower, will build/lookup a wrapper function, and
  /// launch a pthread instead. Then this function will find all uses of the old
  /// return value, and replace them with futures.
  bool LowerLaunchCall(Module *M, CallInst *I) {
    Function *F = I->getCalledFunction();
    StructType *Ty = GetFuncStruct(F);
    Function *WF = GetWrapperFunction(M, F, Ty);

    if (!this->pthread_create) {
      initPthreadCreate(M);
    }

    IRBuilder<> B(I);
    Value *ThreadPtr = B.CreateAlloca(Type::getInt64Ty(I->getContext()));
    Value *ArgPtr = B.CreateAlloca(Ty);

    LaunchWrapper(I, WF, Ty, ArgPtr, ThreadPtr, B);
    ForceFutures(I, ArgPtr, ThreadPtr);

    I->eraseFromParent();
    return true;
  }

public:
  static char ID;
  HLIRLowerPThread()
      : HLIRLower(ID), pthread_create(nullptr), pthread_exit(nullptr),
        pthread_join(nullptr){};

}; // class HLIRLower
} // namespace

char HLIRLowerPThread::ID = 0;
static RegisterPass<HLIRLowerPThread> X("hlir.pthread",
                                        "Lower HLIR to LLIR with pthreads");
