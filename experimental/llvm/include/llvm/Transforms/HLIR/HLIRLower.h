//===- HLIRLower.h - Passes to Lower HLIR into LLVM IR -------*- C++ -*----===//
//
//                     Project Ares
//
// This file is distributed under the expression permission of Los Alamos
// National laboratory. It is Licensed under the BSD-3 license. For more
// information, see LICENSE.md in the Ares root directory.
//
//===----------------------------------------------------------------------===//
//
/// HLIRLower represents a proof-of-concept family of llvm passes which will
/// lower "HLIR" into "LLVM IR". So called "HLIR" exists as a standard set
/// of metadata / conventions of LLVM IR which express parallel and high level
/// semantics.
///
/// The root class, HLIRLower, is an abstract class which is responsible for
/// identifying known constructs. Implementing classes can then take these
/// constructs and lower them into normal LLVM semantics however they see fit.
///
/// Lowering currently handles the following constructs...
/// - Launch Call
///   A call with with the "hlir.task.launch" metadata attached will be treated
///   like the launching of a task. Any uses of the return value of this
///   function will be treated like futures.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

namespace llvm {

class HLIRLower : public ModulePass {
private:
  /// Gives implementing class a chance to initialize anything it may need
  /// to.
  ///
  /// @return Was the module changed?
  virtual bool InitLower(Module &M) = 0;

  /// Given a launch call, must covert it into a task launch. This method
  /// is also responsible for futures of that task. If needed, this method
  /// will remove the original instruction.
  ///
  /// @return Was the module changed?
  virtual bool LowerLaunchCall(Module *M, CallInst *I) = 0;

public:
  // static char ID;
  HLIRLower(char ID) : ModulePass(ID){}; // : ModulePass(ID);

  /// Finds all HLIR constructs, and enqueues them. Then, for every HLIR
  /// instruction class, it calls the appropriate lowering method. This
  /// pass *will not* update the module in any way --- all of that is left
  /// to the implementing classes.
  ///
  /// @return Whether the module was changed.
  bool runOnModule(llvm::Module &M) override;
};
} // namespace
