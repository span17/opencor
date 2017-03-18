//===--- CGBlocks.cpp - Emit LLVM Code for declarations ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit blocks.
//
//===----------------------------------------------------------------------===//

#include "CGBlocks.h"
#include "CGDebugInfo.h"
#include "CGObjCRuntime.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "ConstantBuilder.h"
#include "clang/AST/DeclObjC.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"
#include <algorithm>
#include <cstdio>

using namespace clang;
using namespace CodeGen;

CGBlockInfo::CGBlockInfo(const BlockDecl *block, StringRef name)
  : Name(name), CXXThisIndex(0), CanBeGlobal(false), NeedsCopyDispose(false),
    HasCXXObject(false), UsesStret(false), HasCapturedVariableLayout(false),
    LocalAddress(Address::invalid()), StructureType(nullptr), Block(block),
    DominatingIP(nullptr) {

  // Skip asm prefix, if any.  'name' is usually taken directly from
  // the mangled name of the enclosing function.
  if (!name.empty() && name[0] == '\01')
    name = name.substr(1);
}

// Anchor the vtable to this translation unit.
BlockByrefHelpers::~BlockByrefHelpers() {}

/// Build the given block as a global block.
static llvm::Constant *buildGlobalBlock(CodeGenModule &CGM,
                                        const CGBlockInfo &blockInfo,
                                        llvm::Constant *blockFn);

/// Build the helper function to copy a block.
static llvm::Constant *buildCopyHelper(CodeGenModule &CGM,
                                       const CGBlockInfo &blockInfo) {
  return CodeGenFunction(CGM).GenerateCopyHelperFunction(blockInfo);
}

/// Build the helper function to dispose of a block.
static llvm::Constant *buildDisposeHelper(CodeGenModule &CGM,
                                          const CGBlockInfo &blockInfo) {
  return CodeGenFunction(CGM).GenerateDestroyHelperFunction(blockInfo);
}

/// buildBlockDescriptor - Build the block descriptor meta-data for a block.
/// buildBlockDescriptor is accessed from 5th field of the Block_literal
/// meta-data and contains stationary information about the block literal.
/// Its definition will have 4 (or optinally 6) words.
/// \code
/// struct Block_descriptor {
///   unsigned long reserved;
///   unsigned long size;  // size of Block_literal metadata in bytes.
///   void *copy_func_helper_decl;  // optional copy helper.
///   void *destroy_func_decl; // optioanl destructor helper.
///   void *block_method_encoding_address; // @encode for block literal signature.
///   void *block_layout_info; // encoding of captured block variables.
/// };
/// \endcode
static llvm::Constant *buildBlockDescriptor(CodeGenModule &CGM,
                                            const CGBlockInfo &blockInfo) {
  ASTContext &C = CGM.getContext();

  llvm::IntegerType *ulong =
    cast<llvm::IntegerType>(CGM.getTypes().ConvertType(C.UnsignedLongTy));
  llvm::PointerType *i8p = nullptr;
  if (CGM.getLangOpts().OpenCL)
    i8p =
      llvm::Type::getInt8PtrTy(
           CGM.getLLVMContext(), C.getTargetAddressSpace(LangAS::opencl_constant));
  else
    i8p = CGM.VoidPtrTy;

  ConstantInitBuilder builder(CGM);
  auto elements = builder.beginStruct();

  // reserved
  elements.addInt(ulong, 0);

  // Size
  // FIXME: What is the right way to say this doesn't fit?  We should give
  // a user diagnostic in that case.  Better fix would be to change the
  // API to size_t.
  elements.addInt(ulong, blockInfo.BlockSize.getQuantity());

  // Optional copy/dispose helpers.
  if (blockInfo.NeedsCopyDispose) {
    // copy_func_helper_decl
    elements.add(buildCopyHelper(CGM, blockInfo));

    // destroy_func_decl
    elements.add(buildDisposeHelper(CGM, blockInfo));
  }

  // Signature.  Mandatory ObjC-style method descriptor @encode sequence.
  std::string typeAtEncoding =
    CGM.getContext().getObjCEncodingForBlock(blockInfo.getBlockExpr());
  elements.add(llvm::ConstantExpr::getBitCast(
    CGM.GetAddrOfConstantCString(typeAtEncoding).getPointer(), i8p));

  // GC layout.
  if (C.getLangOpts().ObjC1) {
    if (CGM.getLangOpts().getGC() != LangOptions::NonGC)
      elements.add(CGM.getObjCRuntime().BuildGCBlockLayout(CGM, blockInfo));
    else
      elements.add(CGM.getObjCRuntime().BuildRCBlockLayout(CGM, blockInfo));
  }
  else
    elements.addNullPointer(i8p);

  unsigned AddrSpace = 0;
  if (C.getLangOpts().OpenCL)
    AddrSpace = C.getTargetAddressSpace(LangAS::opencl_constant);

  llvm::GlobalVariable *global =
    elements.finishAndCreateGlobal("__block_descriptor_tmp",
                                   CGM.getPointerAlign(),
                                   /*constant*/ true,
                                   llvm::GlobalValue::InternalLinkage,
                                   AddrSpace);

  return llvm::ConstantExpr::getBitCast(global, CGM.getBlockDescriptorType());
}

/*
  Purely notional variadic template describing the layout of a block.

  template <class _ResultType, class... _ParamTypes, class... _CaptureTypes>
  struct Block_literal {
    /// Initialized to one of:
    ///   extern void *_NSConcreteStackBlock[];
    ///   extern void *_NSConcreteGlobalBlock[];
    ///
    /// In theory, we could start one off malloc'ed by setting
    /// BLOCK_NEEDS_FREE, giving it a refcount of 1, and using
    /// this isa:
    ///   extern void *_NSConcreteMallocBlock[];
    struct objc_class *isa;

    /// These are the flags (with corresponding bit number) that the
    /// compiler is actually supposed to know about.
    ///  25. BLOCK_HAS_COPY_DISPOSE - indicates that the block
    ///   descriptor provides copy and dispose helper functions
    ///  26. BLOCK_HAS_CXX_OBJ - indicates that there's a captured
    ///   object with a nontrivial destructor or copy constructor
    ///  28. BLOCK_IS_GLOBAL - indicates that the block is allocated
    ///   as global memory
    ///  29. BLOCK_USE_STRET - indicates that the block function
    ///   uses stret, which objc_msgSend needs to know about
    ///  30. BLOCK_HAS_SIGNATURE - indicates that the block has an
    ///   @encoded signature string
    /// And we're not supposed to manipulate these:
    ///  24. BLOCK_NEEDS_FREE - indicates that the block has been moved
    ///   to malloc'ed memory
    ///  27. BLOCK_IS_GC - indicates that the block has been moved to
    ///   to GC-allocated memory
    /// Additionally, the bottom 16 bits are a reference count which
    /// should be zero on the stack.
    int flags;

    /// Reserved;  should be zero-initialized.
    int reserved;

    /// Function pointer generated from block literal.
    _ResultType (*invoke)(Block_literal *, _ParamTypes...);

    /// Block description metadata generated from block literal.
    struct Block_descriptor *block_descriptor;

    /// Captured values follow.
    _CapturesTypes captures...;
  };
 */

namespace {
  /// A chunk of data that we actually have to capture in the block.
  struct BlockLayoutChunk {
    CharUnits Alignment;
    CharUnits Size;
    Qualifiers::ObjCLifetime Lifetime;
    const BlockDecl::Capture *Capture; // null for 'this'
    llvm::Type *Type;
    QualType FieldType;

    BlockLayoutChunk(CharUnits align, CharUnits size,
                     Qualifiers::ObjCLifetime lifetime,
                     const BlockDecl::Capture *capture,
                     llvm::Type *type, QualType fieldType)
      : Alignment(align), Size(size), Lifetime(lifetime),
        Capture(capture), Type(type), FieldType(fieldType) {}

    /// Tell the block info that this chunk has the given field index.
    void setIndex(CGBlockInfo &info, unsigned index, CharUnits offset) {
      if (!Capture) {
        info.CXXThisIndex = index;
        info.CXXThisOffset = offset;
      } else {
        auto C = CGBlockInfo::Capture::makeIndex(index, offset, FieldType);
        info.Captures.insert({Capture->getVariable(), C});
      }
    }
  };

  /// Order by 1) all __strong together 2) next, all byfref together 3) next,
  /// all __weak together. Preserve descending alignment in all situations.
  bool operator<(const BlockLayoutChunk &left, const BlockLayoutChunk &right) {
    if (left.Alignment != right.Alignment)
      return left.Alignment > right.Alignment;

    auto getPrefOrder = [](const BlockLayoutChunk &chunk) {
      if (chunk.Capture && chunk.Capture->isByRef())
        return 1;
      if (chunk.Lifetime == Qualifiers::OCL_Strong)
        return 0;
      if (chunk.Lifetime == Qualifiers::OCL_Weak)
        return 2;
      return 3;
    };

    return getPrefOrder(left) < getPrefOrder(right);
  }
} // end anonymous namespace

/// Determines if the given type is safe for constant capture in C++.
static bool isSafeForCXXConstantCapture(QualType type) {
  const RecordType *recordType =
    type->getBaseElementTypeUnsafe()->getAs<RecordType>();

  // Only records can be unsafe.
  if (!recordType) return true;

  const auto *record = cast<CXXRecordDecl>(recordType->getDecl());

  // Maintain semantics for classes with non-trivial dtors or copy ctors.
  if (!record->hasTrivialDestructor()) return false;
  if (record->hasNonTrivialCopyConstructor()) return false;

  // Otherwise, we just have to make sure there aren't any mutable
  // fields that might have changed since initialization.
  return !record->hasMutableFields();
}

/// It is illegal to modify a const object after initialization.
/// Therefore, if a const object has a constant initializer, we don't
/// actually need to keep storage for it in the block; we'll just
/// rematerialize it at the start of the block function.  This is
/// acceptable because we make no promises about address stability of
/// captured variables.
static llvm::Constant *tryCaptureAsConstant(CodeGenModule &CGM,
                                            CodeGenFunction *CGF,
                                            const VarDecl *var) {
  // Return if this is a function paramter. We shouldn't try to
  // rematerialize default arguments of function parameters.
  if (isa<ParmVarDecl>(var))
    return nullptr;

  QualType type = var->getType();

  // We can only do this if the variable is const.
  if (!type.isConstQualified()) return nullptr;

  // Furthermore, in C++ we have to worry about mutable fields:
  // C++ [dcl.type.cv]p4:
  //   Except that any class member declared mutable can be
  //   modified, any attempt to modify a const object during its
  //   lifetime results in undefined behavior.
  if (CGM.getLangOpts().CPlusPlus && !isSafeForCXXConstantCapture(type))
    return nullptr;

  // If the variable doesn't have any initializer (shouldn't this be
  // invalid?), it's not clear what we should do.  Maybe capture as
  // zero?
  const Expr *init = var->getInit();
  if (!init) return nullptr;

  return CGM.EmitConstantInit(*var, CGF);
}

/// Get the low bit of a nonzero character count.  This is the
/// alignment of the nth byte if the 0th byte is universally aligned.
static CharUnits getLowBit(CharUnits v) {
  return CharUnits::fromQuantity(v.getQuantity() & (~v.getQuantity() + 1));
}

static void initializeForBlockHeader(CodeGenModule &CGM, CGBlockInfo &info,
                             SmallVectorImpl<llvm::Type*> &elementTypes) {
  // The header is basically 'struct { void *; int; int; void *; void *; }'.
  // Assert that that struct is packed.
  assert(CGM.getIntSize() <= CGM.getPointerSize());
  assert(CGM.getIntAlign() <= CGM.getPointerAlign());
  assert((2 * CGM.getIntSize()).isMultipleOf(CGM.getPointerAlign()));

  info.BlockAlign = CGM.getPointerAlign();
  info.BlockSize = 3 * CGM.getPointerSize() + 2 * CGM.getIntSize();

  assert(elementTypes.empty());
  elementTypes.push_back(CGM.VoidPtrTy);
  elementTypes.push_back(CGM.IntTy);
  elementTypes.push_back(CGM.IntTy);
  elementTypes.push_back(CGM.VoidPtrTy);
  elementTypes.push_back(CGM.getBlockDescriptorType());
}

/// Compute the layout of the given block.  Attempts to lay the block
/// out with minimal space requirements.
static void computeBlockInfo(CodeGenModule &CGM, CodeGenFunction *CGF,
                             CGBlockInfo &info) {
  ASTContext &C = CGM.getContext();
  const BlockDecl *block = info.getBlockDecl();

  SmallVector<llvm::Type*, 8> elementTypes;
  initializeForBlockHeader(CGM, info, elementTypes);

  if (!block->hasCaptures()) {
    info.StructureType =
      llvm::StructType::get(CGM.getLLVMContext(), elementTypes, true);
    info.CanBeGlobal = true;
    return;
  }
  else if (C.getLangOpts().ObjC1 &&
           CGM.getLangOpts().getGC() == LangOptions::NonGC)
    info.HasCapturedVariableLayout = true;

  // Collect the layout chunks.
  SmallVector<BlockLayoutChunk, 16> layout;
  layout.reserve(block->capturesCXXThis() +
                 (block->capture_end() - block->capture_begin()));

  CharUnits maxFieldAlign;

  // First, 'this'.
  if (block->capturesCXXThis()) {
    assert(CGF && CGF->CurFuncDecl && isa<CXXMethodDecl>(CGF->CurFuncDecl) &&
           "Can't capture 'this' outside a method");
    QualType thisType = cast<CXXMethodDecl>(CGF->CurFuncDecl)->getThisType(C);

    // Theoretically, this could be in a different address space, so
    // don't assume standard pointer size/align.
    llvm::Type *llvmType = CGM.getTypes().ConvertType(thisType);
    std::pair<CharUnits,CharUnits> tinfo
      = CGM.getContext().getTypeInfoInChars(thisType);
    maxFieldAlign = std::max(maxFieldAlign, tinfo.second);

    layout.push_back(BlockLayoutChunk(tinfo.second, tinfo.first,
                                      Qualifiers::OCL_None,
                                      nullptr, llvmType, thisType));
  }

  // Next, all the block captures.
  for (const auto &CI : block->captures()) {
    const VarDecl *variable = CI.getVariable();

    if (CI.isByRef()) {
      // We have to copy/dispose of the __block reference.
      info.NeedsCopyDispose = true;

      // Just use void* instead of a pointer to the byref type.
      CharUnits align = CGM.getPointerAlign();
      maxFieldAlign = std::max(maxFieldAlign, align);

      layout.push_back(BlockLayoutChunk(align, CGM.getPointerSize(),
                                        Qualifiers::OCL_None, &CI,
                                        CGM.VoidPtrTy, variable->getType()));
      continue;
    }

    // Otherwise, build a layout chunk with the size and alignment of
    // the declaration.
    if (llvm::Constant *constant = tryCaptureAsConstant(CGM, CGF, variable)) {
      info.Captures[variable] = CGBlockInfo::Capture::makeConstant(constant);
      continue;
    }

    // If we have a lifetime qualifier, honor it for capture purposes.
    // That includes *not* copying it if it's __unsafe_unretained.
    Qualifiers::ObjCLifetime lifetime =
      variable->getType().getObjCLifetime();
    if (lifetime) {
      switch (lifetime) {
      case Qualifiers::OCL_None: llvm_unreachable("impossible");
      case Qualifiers::OCL_ExplicitNone:
      case Qualifiers::OCL_Autoreleasing:
        break;

      case Qualifiers::OCL_Strong:
      case Qualifiers::OCL_Weak:
        info.NeedsCopyDispose = true;
      }

    // Block pointers require copy/dispose.  So do Objective-C pointers.
    } else if (variable->getType()->isObjCRetainableType()) {
      // But honor the inert __unsafe_unretained qualifier, which doesn't
      // actually make it into the type system.
       if (variable->getType()->isObjCInertUnsafeUnretainedType()) {
        lifetime = Qualifiers::OCL_ExplicitNone;
      } else {
        info.NeedsCopyDispose = true;
        // used for mrr below.
        lifetime = Qualifiers::OCL_Strong;
      }

    // So do types that require non-trivial copy construction.
    } else if (CI.hasCopyExpr()) {
      info.NeedsCopyDispose = true;
      info.HasCXXObject = true;

    // And so do types with destructors.
    } else if (CGM.getLangOpts().CPlusPlus) {
      if (const CXXRecordDecl *record =
            variable->getType()->getAsCXXRecordDecl()) {
        if (!record->hasTrivialDestructor()) {
          info.HasCXXObject = true;
          info.NeedsCopyDispose = true;
        }
      }
    }

    QualType VT = variable->getType();

    // If the variable is captured by an enclosing block or lambda expression,
    // use the type of the capture field.
    if (CGF->BlockInfo && CI.isNested())
      VT = CGF->BlockInfo->getCapture(variable).fieldType();
    else if (auto *FD = CGF->LambdaCaptureFields.lookup(variable))
      VT = FD->getType();

    CharUnits size = C.getTypeSizeInChars(VT);
    CharUnits align = C.getDeclAlign(variable);

    maxFieldAlign = std::max(maxFieldAlign, align);

    llvm::Type *llvmType =
      CGM.getTypes().ConvertTypeForMem(VT);

    layout.push_back(
        BlockLayoutChunk(align, size, lifetime, &CI, llvmType, VT));
  }

  // If that was everything, we're done here.
  if (layout.empty()) {
    info.StructureType =
      llvm::StructType::get(CGM.getLLVMContext(), elementTypes, true);
    info.CanBeGlobal = true;
    return;
  }

  // Sort the layout by alignment.  We have to use a stable sort here
  // to get reproducible results.  There should probably be an
  // llvm::array_pod_stable_sort.
  std::stable_sort(layout.begin(), layout.end());

  // Needed for blocks layout info.
  info.BlockHeaderForcedGapOffset = info.BlockSize;
  info.BlockHeaderForcedGapSize = CharUnits::Zero();

  CharUnits &blockSize = info.BlockSize;
  info.BlockAlign = std::max(maxFieldAlign, info.BlockAlign);

  // Assuming that the first byte in the header is maximally aligned,
  // get the alignment of the first byte following the header.
  CharUnits endAlign = getLowBit(blockSize);

  // If the end of the header isn't satisfactorily aligned for the
  // maximum thing, look for things that are okay with the header-end
  // alignment, and keep appending them until we get something that's
  // aligned right.  This algorithm is only guaranteed optimal if
  // that condition is satisfied at some point; otherwise we can get
  // things like:
  //   header                 // next byte has alignment 4
  //   something_with_size_5; // next byte has alignment 1
  //   something_with_alignment_8;
  // which has 7 bytes of padding, as opposed to the naive solution
  // which might have less (?).
  if (endAlign < maxFieldAlign) {
    SmallVectorImpl<BlockLayoutChunk>::iterator
      li = layout.begin() + 1, le = layout.end();

    // Look for something that the header end is already
    // satisfactorily aligned for.
    for (; li != le && endAlign < li->Alignment; ++li)
      ;

    // If we found something that's naturally aligned for the end of
    // the header, keep adding things...
    if (li != le) {
      SmallVectorImpl<BlockLayoutChunk>::iterator first = li;
      for (; li != le; ++li) {
        assert(endAlign >= li->Alignment);

        li->setIndex(info, elementTypes.size(), blockSize);
        elementTypes.push_back(li->Type);
        blockSize += li->Size;
        endAlign = getLowBit(blockSize);

        // ...until we get to the alignment of the maximum field.
        if (endAlign >= maxFieldAlign) {
          break;
        }
      }
      // Don't re-append everything we just appended.
      layout.erase(first, li);
    }
  }

  assert(endAlign == getLowBit(blockSize));

  // At this point, we just have to add padding if the end align still
  // isn't aligned right.
  if (endAlign < maxFieldAlign) {
    CharUnits newBlockSize = blockSize.alignTo(maxFieldAlign);
    CharUnits padding = newBlockSize - blockSize;

    // If we haven't yet added any fields, remember that there was an
    // initial gap; this need to go into the block layout bit map.
    if (blockSize == info.BlockHeaderForcedGapOffset) {
      info.BlockHeaderForcedGapSize = padding;
    }

    elementTypes.push_back(llvm::ArrayType::get(CGM.Int8Ty,
                                                padding.getQuantity()));
    blockSize = newBlockSize;
    endAlign = getLowBit(blockSize); // might be > maxFieldAlign
  }

  assert(endAlign >= maxFieldAlign);
  assert(endAlign == getLowBit(blockSize));
  // Slam everything else on now.  This works because they have
  // strictly decreasing alignment and we expect that size is always a
  // multiple of alignment.
  for (SmallVectorImpl<BlockLayoutChunk>::iterator
         li = layout.begin(), le = layout.end(); li != le; ++li) {
    if (endAlign < li->Alignment) {
      // size may not be multiple of alignment. This can only happen with
      // an over-aligned variable. We will be adding a padding field to
      // make the size be multiple of alignment.
      CharUnits padding = li->Alignment - endAlign;
      elementTypes.push_back(llvm::ArrayType::get(CGM.Int8Ty,
                                                  padding.getQuantity()));
      blockSize += padding;
      endAlign = getLowBit(blockSize);
    }
    assert(endAlign >= li->Alignment);
    li->setIndex(info, elementTypes.size(), blockSize);
    elementTypes.push_back(li->Type);
    blockSize += li->Size;
    endAlign = getLowBit(blockSize);
  }

  info.StructureType =
    llvm::StructType::get(CGM.getLLVMContext(), elementTypes, true);
}

/// Enter the scope of a block.  This should be run at the entrance to
/// a full-expression so that the block's cleanups are pushed at the
/// right place in the stack.
static void enterBlockScope(CodeGenFunction &CGF, BlockDecl *block) {
  assert(CGF.HaveInsertPoint());

  // Allocate the block info and place it at the head of the list.
  CGBlockInfo &blockInfo =
    *new CGBlockInfo(block, CGF.CurFn->getName());
  blockInfo.NextBlockInfo = CGF.FirstBlockInfo;
  CGF.FirstBlockInfo = &blockInfo;

  // Compute information about the layout, etc., of this block,
  // pushing cleanups as necessary.
  computeBlockInfo(CGF.CGM, &CGF, blockInfo);

  // Nothing else to do if it can be global.
  if (blockInfo.CanBeGlobal) return;

  // Make the allocation for the block.
  blockInfo.LocalAddress = CGF.CreateTempAlloca(blockInfo.StructureType,
                                                blockInfo.BlockAlign, "block");

  // If there are cleanups to emit, enter them (but inactive).
  if (!blockInfo.NeedsCopyDispose) return;

  // Walk through the captures (in order) and find the ones not
  // captured by constant.
  for (const auto &CI : block->captures()) {
    // Ignore __block captures; there's nothing special in the
    // on-stack block that we need to do for them.
    if (CI.isByRef()) continue;

    // Ignore variables that are constant-captured.
    const VarDecl *variable = CI.getVariable();
    CGBlockInfo::Capture &capture = blockInfo.getCapture(variable);
    if (capture.isConstant()) continue;

    // Ignore objects that aren't destructed.
    QualType::DestructionKind dtorKind =
      variable->getType().isDestructedType();
    if (dtorKind == QualType::DK_none) continue;

    CodeGenFunction::Destroyer *destroyer;

    // Block captures count as local values and have imprecise semantics.
    // They also can't be arrays, so need to worry about that.
    if (dtorKind == QualType::DK_objc_strong_lifetime) {
      destroyer = CodeGenFunction::destroyARCStrongImprecise;
    } else {
      destroyer = CGF.getDestroyer(dtorKind);
    }

    // GEP down to the address.
    Address addr = CGF.Builder.CreateStructGEP(blockInfo.LocalAddress,
                                               capture.getIndex(),
                                               capture.getOffset());

    // We can use that GEP as the dominating IP.
    if (!blockInfo.DominatingIP)
      blockInfo.DominatingIP = cast<llvm::Instruction>(addr.getPointer());

    CleanupKind cleanupKind = InactiveNormalCleanup;
    bool useArrayEHCleanup = CGF.needsEHCleanup(dtorKind);
    if (useArrayEHCleanup)
      cleanupKind = InactiveNormalAndEHCleanup;

    CGF.pushDestroy(cleanupKind, addr, variable->getType(),
                    destroyer, useArrayEHCleanup);

    // Remember where that cleanup was.
    capture.setCleanup(CGF.EHStack.stable_begin());
  }
}

/// Enter a full-expression with a non-trivial number of objects to
/// clean up.  This is in this file because, at the moment, the only
/// kind of cleanup object is a BlockDecl*.
void CodeGenFunction::enterNonTrivialFullExpression(const ExprWithCleanups *E) {
  assert(E->getNumObjects() != 0);
  ArrayRef<ExprWithCleanups::CleanupObject> cleanups = E->getObjects();
  for (ArrayRef<ExprWithCleanups::CleanupObject>::iterator
         i = cleanups.begin(), e = cleanups.end(); i != e; ++i) {
    enterBlockScope(*this, *i);
  }
}

/// Find the layout for the given block in a linked list and remove it.
static CGBlockInfo *findAndRemoveBlockInfo(CGBlockInfo **head,
                                           const BlockDecl *block) {
  while (true) {
    assert(head && *head);
    CGBlockInfo *cur = *head;

    // If this is the block we're looking for, splice it out of the list.
    if (cur->getBlockDecl() == block) {
      *head = cur->NextBlockInfo;
      return cur;
    }

    head = &cur->NextBlockInfo;
  }
}

/// Destroy a chain of block layouts.
void CodeGenFunction::destroyBlockInfos(CGBlockInfo *head) {
  assert(head && "destroying an empty chain");
  do {
    CGBlockInfo *cur = head;
    head = cur->NextBlockInfo;
    delete cur;
  } while (head != nullptr);
}

/// Emit a block literal expression in the current function.
llvm::Value *CodeGenFunction::EmitBlockLiteral(const BlockExpr *blockExpr) {
  // If the block has no captures, we won't have a pre-computed
  // layout for it.
  if (!blockExpr->getBlockDecl()->hasCaptures()) {
    if (llvm::Constant *Block = CGM.getAddrOfGlobalBlockIfEmitted(blockExpr))
      return Block;
    CGBlockInfo blockInfo(blockExpr->getBlockDecl(), CurFn->getName());
    computeBlockInfo(CGM, this, blockInfo);
    blockInfo.BlockExpression = blockExpr;
    return EmitBlockLiteral(blockInfo);
  }

  // Find the block info for this block and take ownership of it.
  std::unique_ptr<CGBlockInfo> blockInfo;
  blockInfo.reset(findAndRemoveBlockInfo(&FirstBlockInfo,
                                         blockExpr->getBlockDecl()));

  blockInfo->BlockExpression = blockExpr;
  return EmitBlockLiteral(*blockInfo);
}

llvm::Value *CodeGenFunction::EmitBlockLiteral(const CGBlockInfo &blockInfo) {
  // Using the computed layout, generate the actual block function.
  bool isLambdaConv = blockInfo.getBlockDecl()->isConversionFromLambda();
  llvm::Constant *blockFn
    = CodeGenFunction(CGM, true).GenerateBlockFunction(CurGD, blockInfo,
                                                       LocalDeclMap,
                                                       isLambdaConv);
  blockFn = llvm::ConstantExpr::getBitCast(blockFn, VoidPtrTy);

  // If there is nothing to capture, we can emit this as a global block.
  if (blockInfo.CanBeGlobal)
    return buildGlobalBlock(CGM, blockInfo, blockFn);

  // Otherwise, we have to emit this as a local block.

  llvm::Constant *isa = CGM.getNSConcreteStackBlock();
  isa = llvm::ConstantExpr::getBitCast(isa, VoidPtrTy);

  // Build the block descriptor.
  llvm::Constant *descriptor = buildBlockDescriptor(CGM, blockInfo);

  Address blockAddr = blockInfo.LocalAddress;
  assert(blockAddr.isValid() && "block has no address!");

  // Compute the initial on-stack block flags.
  BlockFlags flags = BLOCK_HAS_SIGNATURE;
  if (blockInfo.HasCapturedVariableLayout) flags |= BLOCK_HAS_EXTENDED_LAYOUT;
  if (blockInfo.NeedsCopyDispose) flags |= BLOCK_HAS_COPY_DISPOSE;
  if (blockInfo.HasCXXObject) flags |= BLOCK_HAS_CXX_OBJ;
  if (blockInfo.UsesStret) flags |= BLOCK_USE_STRET;

  auto projectField =
    [&](unsigned index, CharUnits offset, const Twine &name) -> Address {
      return Builder.CreateStructGEP(blockAddr, index, offset, name);
    };
  auto storeField =
    [&](llvm::Value *value, unsigned index, CharUnits offset,
        const Twine &name) {
      Builder.CreateStore(value, projectField(index, offset, name));
    };

  // Initialize the block header.
  {
    // We assume all the header fields are densely packed.
    unsigned index = 0;
    CharUnits offset;
    auto addHeaderField =
      [&](llvm::Value *value, CharUnits size, const Twine &name) {
        storeField(value, index, offset, name);
        offset += size;
        index++;
      };

    addHeaderField(isa, getPointerSize(), "block.isa");
    addHeaderField(llvm::ConstantInt::get(IntTy, flags.getBitMask()),
                   getIntSize(), "block.flags");
    addHeaderField(llvm::ConstantInt::get(IntTy, 0),
                   getIntSize(), "block.reserved");
    addHeaderField(blockFn, getPointerSize(), "block.invoke");
    addHeaderField(descriptor, getPointerSize(), "block.descriptor");
  }

  // Finally, capture all the values into the block.
  const BlockDecl *blockDecl = blockInfo.getBlockDecl();

  // First, 'this'.
  if (blockDecl->capturesCXXThis()) {
    Address addr = projectField(blockInfo.CXXThisIndex, blockInfo.CXXThisOffset,
                                "block.captured-this.addr");
    Builder.CreateStore(LoadCXXThis(), addr);
  }

  // Next, captured variables.
  for (const auto &CI : blockDecl->captures()) {
    const VarDecl *variable = CI.getVariable();
    const CGBlockInfo::Capture &capture = blockInfo.getCapture(variable);

    // Ignore constant captures.
    if (capture.isConstant()) continue;

    QualType type = capture.fieldType();

    // This will be a [[type]]*, except that a byref entry will just be
    // an i8**.
    Address blockField =
      projectField(capture.getIndex(), capture.getOffset(), "block.captured");

    // Compute the address of the thing we're going to move into the
    // block literal.
    Address src = Address::invalid();

    if (blockDecl->isConversionFromLambda()) {
      // The lambda capture in a lambda's conversion-to-block-pointer is
      // special; we'll simply emit it directly.
      src = Address::invalid();
    } else if (CI.isByRef()) {
      if (BlockInfo && CI.isNested()) {
        // We need to use the capture from the enclosing block.
        const CGBlockInfo::Capture &enclosingCapture =
            BlockInfo->getCapture(variable);

        // This is a [[type]]*, except that a byref entry wil just be an i8**.
        src = Builder.CreateStructGEP(LoadBlockStruct(),
                                      enclosingCapture.getIndex(),
                                      enclosingCapture.getOffset(),
                                      "block.capture.addr");
      } else {
        auto I = LocalDeclMap.find(variable);
        assert(I != LocalDeclMap.end());
        src = I->second;
      }
    } else {
      DeclRefExpr declRef(const_cast<VarDecl *>(variable),
                          /*RefersToEnclosingVariableOrCapture*/ CI.isNested(),
                          type.getNonReferenceType(), VK_LValue,
                          SourceLocation());
      src = EmitDeclRefLValue(&declRef).getAddress();
    };

    // For byrefs, we just write the pointer to the byref struct into
    // the block field.  There's no need to chase the forwarding
    // pointer at this point, since we're building something that will
    // live a shorter life than the stack byref anyway.
    if (CI.isByRef()) {
      // Get a void* that points to the byref struct.
      llvm::Value *byrefPointer;
      if (CI.isNested())
        byrefPointer = Builder.CreateLoad(src, "byref.capture");
      else
        byrefPointer = Builder.CreateBitCast(src.getPointer(), VoidPtrTy);

      // Write that void* into the capture field.
      Builder.CreateStore(byrefPointer, blockField);

    // If we have a copy constructor, evaluate that into the block field.
    } else if (const Expr *copyExpr = CI.getCopyExpr()) {
      if (blockDecl->isConversionFromLambda()) {
        // If we have a lambda conversion, emit the expression
        // directly into the block instead.
        AggValueSlot Slot =
            AggValueSlot::forAddr(blockField, Qualifiers(),
                                  AggValueSlot::IsDestructed,
                                  AggValueSlot::DoesNotNeedGCBarriers,
                                  AggValueSlot::IsNotAliased);
        EmitAggExpr(copyExpr, Slot);
      } else {
        EmitSynthesizedCXXCopyCtor(blockField, src, copyExpr);
      }

    // If it's a reference variable, copy the reference into the block field.
    } else if (type->isReferenceType()) {
      Builder.CreateStore(src.getPointer(), blockField);

    // If this is an ARC __strong block-pointer variable, don't do a
    // block copy.
    //
    // TODO: this can be generalized into the normal initialization logic:
    // we should never need to do a block-copy when initializing a local
    // variable, because the local variable's lifetime should be strictly
    // contained within the stack block's.
    } else if (type.getObjCLifetime() == Qualifiers::OCL_Strong &&
               type->isBlockPointerType()) {
      // Load the block and do a simple retain.
      llvm::Value *value = Builder.CreateLoad(src, "block.captured_block");
      value = EmitARCRetainNonBlock(value);

      // Do a primitive store to the block field.
      Builder.CreateStore(value, blockField);

    // Otherwise, fake up a POD copy into the block field.
    } else {
      // Fake up a new variable so that EmitScalarInit doesn't think
      // we're referring to the variable in its own initializer.
      ImplicitParamDecl blockFieldPseudoVar(getContext(), /*DC*/ nullptr,
                                            SourceLocation(), /*name*/ nullptr,
                                            type);

      // We use one of these or the other depending on whether the
      // reference is nested.
      DeclRefExpr declRef(const_cast<VarDecl *>(variable),
                          /*RefersToEnclosingVariableOrCapture*/ CI.isNested(),
                          type, VK_LValue, SourceLocation());

      ImplicitCastExpr l2r(ImplicitCastExpr::OnStack, type, CK_LValueToRValue,
                           &declRef, VK_RValue);
      // FIXME: Pass a specific location for the expr init so that the store is
      // attributed to a reasonable location - otherwise it may be attributed to
      // locations of subexpressions in the initialization.
      EmitExprAsInit(&l2r, &blockFieldPseudoVar,
                     MakeAddrLValue(blockField, type, AlignmentSource::Decl),
                     /*captured by init*/ false);
    }

    // Activate the cleanup if layout pushed one.
    if (!CI.isByRef()) {
      EHScopeStack::stable_iterator cleanup = capture.getCleanup();
      if (cleanup.isValid())
        ActivateCleanupBlock(cleanup, blockInfo.DominatingIP);
    }
  }

  // Cast to the converted block-pointer type, which happens (somewhat
  // unfortunately) to be a pointer to function type.
  llvm::Value *result =
    Builder.CreateBitCast(blockAddr.getPointer(),
                          ConvertType(blockInfo.getBlockExpr()->getType()));

  return result;
}


llvm::Type *CodeGenModule::getBlockDescriptorType() {
  if (BlockDescriptorType)
    return BlockDescriptorType;

  llvm::Type *UnsignedLongTy =
    getTypes().ConvertType(getContext().UnsignedLongTy);

  // struct __block_descriptor {
  //   unsigned long reserved;
  //   unsigned long block_size;
  //
  //   // later, the following will be added
  //
  //   struct {
  //     void (*copyHelper)();
  //     void (*copyHelper)();
  //   } helpers;                // !!! optional
  //
  //   const char *signature;   // the block signature
  //   const char *layout;      // reserved
  // };
  BlockDescriptorType =
    llvm::StructType::create("struct.__block_descriptor",
                             UnsignedLongTy, UnsignedLongTy, nullptr);

  // Now form a pointer to that.
  unsigned AddrSpace = 0;
  if (getLangOpts().OpenCL)
    AddrSpace = getContext().getTargetAddressSpace(LangAS::opencl_constant);
  BlockDescriptorType = llvm::PointerType::get(BlockDescriptorType, AddrSpace);
  return BlockDescriptorType;
}

llvm::Type *CodeGenModule::getGenericBlockLiteralType() {
  if (GenericBlockLiteralType)
    return GenericBlockLiteralType;

  llvm::Type *BlockDescPtrTy = getBlockDescriptorType();

  // struct __block_literal_generic {
  //   void *__isa;
  //   int __flags;
  //   int __reserved;
  //   void (*__invoke)(void *);
  //   struct __block_descriptor *__descriptor;
  // };
  GenericBlockLiteralType =
    llvm::StructType::create("struct.__block_literal_generic",
                             VoidPtrTy, IntTy, IntTy, VoidPtrTy,
                             BlockDescPtrTy, nullptr);

  return GenericBlockLiteralType;
}

RValue CodeGenFunction::EmitBlockCallExpr(const CallExpr *E,
                                          ReturnValueSlot ReturnValue) {
  const BlockPointerType *BPT =
    E->getCallee()->getType()->getAs<BlockPointerType>();

  llvm::Value *BlockPtr = EmitScalarExpr(E->getCallee());

  // Get a pointer to the generic block literal.
  llvm::Type *BlockLiteralTy =
    llvm::PointerType::getUnqual(CGM.getGenericBlockLiteralType());

  // Bitcast the callee to a block literal.
  BlockPtr = Builder.CreateBitCast(BlockPtr, BlockLiteralTy, "block.literal");

  // Get the function pointer from the literal.
  llvm::Value *FuncPtr =
    Builder.CreateStructGEP(CGM.getGenericBlockLiteralType(), BlockPtr, 3);

  BlockPtr = Builder.CreateBitCast(BlockPtr, VoidPtrTy);

  // Add the block literal.
  CallArgList Args;
  Args.add(RValue::get(BlockPtr), getContext().VoidPtrTy);

  QualType FnType = BPT->getPointeeType();

  // And the rest of the arguments.
  EmitCallArgs(Args, FnType->getAs<FunctionProtoType>(), E->arguments());

  // Load the function.
  llvm::Value *Func = Builder.CreateAlignedLoad(FuncPtr, getPointerAlign());

  const FunctionType *FuncTy = FnType->castAs<FunctionType>();
  const CGFunctionInfo &FnInfo =
    CGM.getTypes().arrangeBlockFunctionCall(Args, FuncTy);

  // Cast the function pointer to the right type.
  llvm::Type *BlockFTy = CGM.getTypes().GetFunctionType(FnInfo);

  llvm::Type *BlockFTyPtr = llvm::PointerType::getUnqual(BlockFTy);
  Func = Builder.CreateBitCast(Func, BlockFTyPtr);

  // Prepare the callee.
  CGCallee Callee(CGCalleeInfo(), Func);

  // And call the block.
  return EmitCall(FnInfo, Callee, ReturnValue, Args);
}

Address CodeGenFunction::GetAddrOfBlockDecl(const VarDecl *variable,
                                            bool isByRef) {
  assert(BlockInfo && "evaluating block ref without block information?");
  const CGBlockInfo::Capture &capture = BlockInfo->getCapture(variable);

  // Handle constant captures.
  if (capture.isConstant()) return LocalDeclMap.find(variable)->second;

  Address addr =
    Builder.CreateStructGEP(LoadBlockStruct(), capture.getIndex(),
                            capture.getOffset(), "block.capture.addr");

  if (isByRef) {
    // addr should be a void** right now.  Load, then cast the result
    // to byref*.

    auto &byrefInfo = getBlockByrefInfo(variable);
    addr = Address(Builder.CreateLoad(addr), byrefInfo.ByrefAlignment);

    auto byrefPointerType = llvm::PointerType::get(byrefInfo.Type, 0);
    addr = Builder.CreateBitCast(addr, byrefPointerType, "byref.addr");

    addr = emitBlockByrefAddress(addr, byrefInfo, /*follow*/ true,
                                 variable->getName());
  }

  if (auto refType = capture.fieldType()->getAs<ReferenceType>())
    addr = EmitLoadOfReference(addr, refType);

  return addr;
}

void CodeGenModule::setAddrOfGlobalBlock(const BlockExpr *BE,
                                         llvm::Constant *Addr) {
  bool Ok = EmittedGlobalBlocks.insert(std::make_pair(BE, Addr)).second;
  (void)Ok;
  assert(Ok && "Trying to replace an already-existing global block!");
}

llvm::Constant *
CodeGenModule::GetAddrOfGlobalBlock(const BlockExpr *BE,
                                    StringRef Name) {
  if (llvm::Constant *Block = getAddrOfGlobalBlockIfEmitted(BE))
    return Block;

  CGBlockInfo blockInfo(BE->getBlockDecl(), Name);
  blockInfo.BlockExpression = BE;

  // Compute information about the layout, etc., of this block.
  computeBlockInfo(*this, nullptr, blockInfo);

  // Using that metadata, generate the actual block function.
  llvm::Constant *blockFn;
  {
    CodeGenFunction::DeclMapTy LocalDeclMap;
    blockFn = CodeGenFunction(*this).GenerateBlockFunction(GlobalDecl(),
                                                           blockInfo,
                                                           LocalDeclMap,
                                                           false);
  }
  blockFn = llvm::ConstantExpr::getBitCast(blockFn, VoidPtrTy);

  return buildGlobalBlock(*this, blockInfo, blockFn);
}

static llvm::Constant *buildGlobalBlock(CodeGenModule &CGM,
                                        const CGBlockInfo &blockInfo,
                                        llvm::Constant *blockFn) {
  assert(blockInfo.CanBeGlobal);
  // Callers should detect this case on their own: calling this function
  // generally requires computing layout information, which is a waste of time
  // if we've already emitted this block.
  assert(!CGM.getAddrOfGlobalBlockIfEmitted(blockInfo.BlockExpression) &&
         "Refusing to re-emit a global block.");

  // Generate the constants for the block literal initializer.
  ConstantInitBuilder builder(CGM);
  auto fields = builder.beginStruct();

  // isa
  fields.add(CGM.getNSConcreteGlobalBlock());

  // __flags
  BlockFlags flags = BLOCK_IS_GLOBAL | BLOCK_HAS_SIGNATURE;
  if (blockInfo.UsesStret) flags |= BLOCK_USE_STRET;

  fields.addInt(CGM.IntTy, flags.getBitMask());

  // Reserved
  fields.addInt(CGM.IntTy, 0);

  // Function
  fields.add(blockFn);

  // Descriptor
  fields.add(buildBlockDescriptor(CGM, blockInfo));

  llvm::Constant *literal =
    fields.finishAndCreateGlobal("__block_literal_global",
                                 blockInfo.BlockAlign,
                                 /*constant*/ true);

  // Return a constant of the appropriately-casted type.
  llvm::Type *RequiredType =
    CGM.getTypes().ConvertType(blockInfo.getBlockExpr()->getType());
  llvm::Constant *Result =
      llvm::ConstantExpr::getBitCast(literal, RequiredType);
  CGM.setAddrOfGlobalBlock(blockInfo.BlockExpression, Result);
  return Result;
}

void CodeGenFunction::setBlockContextParameter(const ImplicitParamDecl *D,
                                               unsigned argNum,
                                               llvm::Value *arg) {
  assert(BlockInfo && "not emitting prologue of block invocation function?!");

  llvm::Value *localAddr = nullptr;
  if (CGM.getCodeGenOpts().OptimizationLevel == 0) {
    // Allocate a stack slot to let the debug info survive the RA.
    Address alloc = CreateMemTemp(D->getType(), D->getName() + ".addr");
    Builder.CreateStore(arg, alloc);
    localAddr = Builder.CreateLoad(alloc);
  }

  if (CGDebugInfo *DI = getDebugInfo()) {
    if (CGM.getCodeGenOpts().getDebugInfo() >=
        codegenoptions::LimitedDebugInfo) {
      DI->setLocation(D->getLocation());
      DI->EmitDeclareOfBlockLiteralArgVariable(*BlockInfo, arg, argNum,
                                               localAddr, Builder);
    }
  }

  SourceLocation StartLoc = BlockInfo->getBlockExpr()->getBody()->getLocStart();
  ApplyDebugLocation Scope(*this, StartLoc);

  // Instead of messing around with LocalDeclMap, just set the value
  // directly as BlockPointer.
  BlockPointer = Builder.CreateBitCast(arg,
                                       BlockInfo->StructureType->getPointerTo(),
                                       "block");
}

Address CodeGenFunction::LoadBlockStruct() {
  assert(BlockInfo && "not in a block invocation function!");
  assert(BlockPointer && "no block pointer set!");
  return Address(BlockPointer, BlockInfo->BlockAlign);
}

llvm::Function *
CodeGenFunction::GenerateBlockFunction(GlobalDecl GD,
                                       const CGBlockInfo &blockInfo,
                                       const DeclMapTy &ldm,
                                       bool IsLambdaConversionToBlock) {
  const BlockDecl *blockDecl = blockInfo.getBlockDecl();

  CurGD = GD;

  CurEHLocation = blockInfo.getBlockExpr()->getLocEnd();

  BlockInfo = &blockInfo;

  // Arrange for local static and local extern declarations to appear
  // to be local to this function as well, in case they're directly
  // referenced in a block.
  for (DeclMapTy::const_iterator i = ldm.begin(), e = ldm.end(); i != e; ++i) {
    const auto *var = dyn_cast<VarDecl>(i->first);
    if (var && !var->hasLocalStorage())
      setAddrOfLocalVar(var, i->second);
  }

  // Begin building the function declaration.

  // Build the argument list.
  FunctionArgList args;

  // The first argument is the block pointer.  Just take it as a void*
  // and cast it later.
  QualType selfTy = getContext().VoidPtrTy;
  IdentifierInfo *II = &CGM.getContext().Idents.get(".block_descriptor");

  ImplicitParamDecl selfDecl(getContext(), const_cast<BlockDecl*>(blockDecl),
                             SourceLocation(), II, selfTy);
  args.push_back(&selfDecl);

  // Now add the rest of the parameters.
  args.append(blockDecl->param_begin(), blockDecl->param_end());

  // Create the function declaration.
  const FunctionProtoType *fnType = blockInfo.getBlockExpr()->getFunctionType();
  const CGFunctionInfo &fnInfo =
    CGM.getTypes().arrangeBlockFunctionDeclaration(fnType, args);
  if (CGM.ReturnSlotInterferesWithArgs(fnInfo))
    blockInfo.UsesStret = true;

  llvm::FunctionType *fnLLVMType = CGM.getTypes().GetFunctionType(fnInfo);

  StringRef name = CGM.getBlockMangledName(GD, blockDecl);
  llvm::Function *fn = llvm::Function::Create(
      fnLLVMType, llvm::GlobalValue::InternalLinkage, name, &CGM.getModule());
  CGM.SetInternalFunctionAttributes(blockDecl, fn, fnInfo);

  // Begin generating the function.
  StartFunction(blockDecl, fnType->getReturnType(), fn, fnInfo, args,
                blockDecl->getLocation(),
                blockInfo.getBlockExpr()->getBody()->getLocStart());

  // Okay.  Undo some of what StartFunction did.

  // At -O0 we generate an explicit alloca for the BlockPointer, so the RA
  // won't delete the dbg.declare intrinsics for captured variables.
  llvm::Value *BlockPointerDbgLoc = BlockPointer;
  if (CGM.getCodeGenOpts().OptimizationLevel == 0) {
    // Allocate a stack slot for it, so we can point the debugger to it
    Address Alloca = CreateTempAlloca(BlockPointer->getType(),
                                      getPointerAlign(),
                                      "block.addr");
    // Set the DebugLocation to empty, so the store is recognized as a
    // frame setup instruction by llvm::DwarfDebug::beginFunction().
    auto NL = ApplyDebugLocation::CreateEmpty(*this);
    Builder.CreateStore(BlockPointer, Alloca);
    BlockPointerDbgLoc = Alloca.getPointer();
  }

  // If we have a C++ 'this' reference, go ahead and force it into
  // existence now.
  if (blockDecl->capturesCXXThis()) {
    Address addr =
      Builder.CreateStructGEP(LoadBlockStruct(), blockInfo.CXXThisIndex,
                              blockInfo.CXXThisOffset, "block.captured-this");
    CXXThisValue = Builder.CreateLoad(addr, "this");
  }

  // Also force all the constant captures.
  for (const auto &CI : blockDecl->captures()) {
    const VarDecl *variable = CI.getVariable();
    const CGBlockInfo::Capture &capture = blockInfo.getCapture(variable);
    if (!capture.isConstant()) continue;

    CharUnits align = getContext().getDeclAlign(variable);
    Address alloca =
      CreateMemTemp(variable->getType(), align, "block.captured-const");

    Builder.CreateStore(capture.getConstant(), alloca);

    setAddrOfLocalVar(variable, alloca);
  }

  // Save a spot to insert the debug information for all the DeclRefExprs.
  llvm::BasicBlock *entry = Builder.GetInsertBlock();
  llvm::BasicBlock::iterator entry_ptr = Builder.GetInsertPoint();
  --entry_ptr;

  if (IsLambdaConversionToBlock)
    EmitLambdaBlockInvokeBody();
  else {
    PGO.assignRegionCounters(GlobalDecl(blockDecl), fn);
    incrementProfileCounter(blockDecl->getBody());
    EmitStmt(blockDecl->getBody());
  }

  // Remember where we were...
  llvm::BasicBlock *resume = Builder.GetInsertBlock();

  // Go back to the entry.
  ++entry_ptr;
  Builder.SetInsertPoint(entry, entry_ptr);

  // Emit debug information for all the DeclRefExprs.
  // FIXME: also for 'this'
  if (CGDebugInfo *DI = getDebugInfo()) {
    for (const auto &CI : blockDecl->captures()) {
      const VarDecl *variable = CI.getVariable();
      DI->EmitLocation(Builder, variable->getLocation());

      if (CGM.getCodeGenOpts().getDebugInfo() >=
          codegenoptions::LimitedDebugInfo) {
        const CGBlockInfo::Capture &capture = blockInfo.getCapture(variable);
        if (capture.isConstant()) {
          auto addr = LocalDeclMap.find(variable)->second;
          DI->EmitDeclareOfAutoVariable(variable, addr.getPointer(),
                                        Builder);
          continue;
        }

        DI->EmitDeclareOfBlockDeclRefVariable(
            variable, BlockPointerDbgLoc, Builder, blockInfo,
            entry_ptr == entry->end() ? nullptr : &*entry_ptr);
      }
    }
    // Recover location if it was changed in the above loop.
    DI->EmitLocation(Builder,
                     cast<CompoundStmt>(blockDecl->getBody())->getRBracLoc());
  }

  // And resume where we left off.
  if (resume == nullptr)
    Builder.ClearInsertionPoint();
  else
    Builder.SetInsertPoint(resume);

  FinishFunction(cast<CompoundStmt>(blockDecl->getBody())->getRBracLoc());

  return fn;
}

/*
    notes.push_back(HelperInfo());
    HelperInfo &note = notes.back();
    note.index = capture.getIndex();
    note.RequiresCopying = (ci->hasCopyExpr() || BlockRequiresCopying(type));
    note.cxxbar_import = ci->getCopyExpr();

    if (ci->isByRef()) {
      note.flag = BLOCK_FIELD_IS_BYREF;
      if (type.isObjCGCWeak())
        note.flag |= BLOCK_FIELD_IS_WEAK;
    } else if (type->isBlockPointerType()) {
      note.flag = BLOCK_FIELD_IS_BLOCK;
    } else {
      note.flag = BLOCK_FIELD_IS_OBJECT;
    }
 */

/// Generate the copy-helper function for a block closure object:
///   static void block_copy_helper(block_t *dst, block_t *src);
/// The runtime will have previously initialized 'dst' by doing a
/// bit-copy of 'src'.
///
/// Note that this copies an entire block closure object to the heap;
/// it should not be confused with a 'byref copy helper', which moves
/// the contents of an individual __block variable to the heap.
llvm::Constant *
CodeGenFunction::GenerateCopyHelperFunction(const CGBlockInfo &blockInfo) {
  ASTContext &C = getContext();

  FunctionArgList args;
  ImplicitParamDecl dstDecl(getContext(), nullptr, SourceLocation(), nullptr,
                            C.VoidPtrTy);
  args.push_back(&dstDecl);
  ImplicitParamDecl srcDecl(getContext(), nullptr, SourceLocation(), nullptr,
                            C.VoidPtrTy);
  args.push_back(&srcDecl);

  const CGFunctionInfo &FI =
    CGM.getTypes().arrangeBuiltinFunctionDeclaration(C.VoidTy, args);

  // FIXME: it would be nice if these were mergeable with things with
  // identical semantics.
  llvm::FunctionType *LTy = CGM.getTypes().GetFunctionType(FI);

  llvm::Function *Fn =
    llvm::Function::Create(LTy, llvm::GlobalValue::InternalLinkage,
                           "__copy_helper_block_", &CGM.getModule());

  IdentifierInfo *II
    = &CGM.getContext().Idents.get("__copy_helper_block_");

  FunctionDecl *FD = FunctionDecl::Create(C,
                                          C.getTranslationUnitDecl(),
                                          SourceLocation(),
                                          SourceLocation(), II, C.VoidTy,
                                          nullptr, SC_Static,
                                          false,
                                          false);

  CGM.SetInternalFunctionAttributes(nullptr, Fn, FI);

  auto NL = ApplyDebugLocation::CreateEmpty(*this);
  StartFunction(FD, C.VoidTy, Fn, FI, args);
  // Create a scope with an artificial location for the body of this function.
  auto AL = ApplyDebugLocation::CreateArtificial(*this);
  llvm::Type *structPtrTy = blockInfo.StructureType->getPointerTo();

  Address src = GetAddrOfLocalVar(&srcDecl);
  src = Address(Builder.CreateLoad(src), blockInfo.BlockAlign);
  src = Builder.CreateBitCast(src, structPtrTy, "block.source");

  Address dst = GetAddrOfLocalVar(&dstDecl);
  dst = Address(Builder.CreateLoad(dst), blockInfo.BlockAlign);
  dst = Builder.CreateBitCast(dst, structPtrTy, "block.dest");

  const BlockDecl *blockDecl = blockInfo.getBlockDecl();

  for (const auto &CI : blockDecl->captures()) {
    const VarDecl *variable = CI.getVariable();
    QualType type = variable->getType();

    const CGBlockInfo::Capture &capture = blockInfo.getCapture(variable);
    if (capture.isConstant()) continue;

    const Expr *copyExpr = CI.getCopyExpr();
    BlockFieldFlags flags;

    bool useARCWeakCopy = false;
    bool useARCStrongCopy = false;

    if (copyExpr) {
      assert(!CI.isByRef());
      // don't bother computing flags

    } else if (CI.isByRef()) {
      flags = BLOCK_FIELD_IS_BYREF;
      if (type.isObjCGCWeak())
        flags |= BLOCK_FIELD_IS_WEAK;

    } else if (type->isObjCRetainableType()) {
      flags = BLOCK_FIELD_IS_OBJECT;
      bool isBlockPointer = type->isBlockPointerType();
      if (isBlockPointer)
        flags = BLOCK_FIELD_IS_BLOCK;

      // Special rules for ARC captures:
      Qualifiers qs = type.getQualifiers();

      // We need to register __weak direct captures with the runtime.
      if (qs.getObjCLifetime() == Qualifiers::OCL_Weak) {
        useARCWeakCopy = true;

      // We need to retain the copied value for __strong direct captures.
      } else if (qs.getObjCLifetime() == Qualifiers::OCL_Strong) {
        // If it's a block pointer, we have to copy the block and
        // assign that to the destination pointer, so we might as
        // well use _Block_object_assign.  Otherwise we can avoid that.
        if (!isBlockPointer)
          useARCStrongCopy = true;

      // Non-ARC captures of retainable pointers are strong and
      // therefore require a call to _Block_object_assign.
      } else if (!qs.getObjCLifetime() && !getLangOpts().ObjCAutoRefCount) {
        // fall through

      // Otherwise the memcpy is fine.
      } else {
        continue;
      }

    // For all other types, the memcpy is fine.
    } else {
      continue;
    }

    unsigned index = capture.getIndex();
    Address srcField = Builder.CreateStructGEP(src, index, capture.getOffset());
    Address dstField = Builder.CreateStructGEP(dst, index, capture.getOffset());

    // If there's an explicit copy expression, we do that.
    if (copyExpr) {
      EmitSynthesizedCXXCopyCtor(dstField, srcField, copyExpr);
    } else if (useARCWeakCopy) {
      EmitARCCopyWeak(dstField, srcField);
    } else {
      llvm::Value *srcValue = Builder.CreateLoad(srcField, "blockcopy.src");
      if (useARCStrongCopy) {
        // At -O0, store null into the destination field (so that the
        // storeStrong doesn't over-release) and then call storeStrong.
        // This is a workaround to not having an initStrong call.
        if (CGM.getCodeGenOpts().OptimizationLevel == 0) {
          auto *ty = cast<llvm::PointerType>(srcValue->getType());
          llvm::Value *null = llvm::ConstantPointerNull::get(ty);
          Builder.CreateStore(null, dstField);
          EmitARCStoreStrongCall(dstField, srcValue, true);

        // With optimization enabled, take advantage of the fact that
        // the blocks runtime guarantees a memcpy of the block data, and
        // just emit a retain of the src field.
        } else {
          EmitARCRetainNonBlock(srcValue);

          // We don't need this anymore, so kill it.  It's not quite
          // worth the annoyance to avoid creating it in the first place.
          cast<llvm::Instruction>(dstField.getPointer())->eraseFromParent();
        }
      } else {
        srcValue = Builder.CreateBitCast(srcValue, VoidPtrTy);
        llvm::Value *dstAddr =
          Builder.CreateBitCast(dstField.getPointer(), VoidPtrTy);
        llvm::Value *args[] = {
          dstAddr, srcValue, llvm::ConstantInt::get(Int32Ty, flags.getBitMask())
        };

        bool copyCanThrow = false;
        if (CI.isByRef() && variable->getType()->getAsCXXRecordDecl()) {
          const Expr *copyExpr =
            CGM.getContext().getBlockVarCopyInits(variable);
          if (copyExpr) {
            copyCanThrow = true; // FIXME: reuse the noexcept logic
          }
        }

        if (copyCanThrow) {
          EmitRuntimeCallOrInvoke(CGM.getBlockObjectAssign(), args);
        } else {
          EmitNounwindRuntimeCall(CGM.getBlockObjectAssign(), args);
        }
      }
    }
  }

  FinishFunction();

  return llvm::ConstantExpr::getBitCast(Fn, VoidPtrTy);
}

/// Generate the destroy-helper function for a block closure object:
///   static void block_destroy_helper(block_t *theBlock);
///
/// Note that this destroys a heap-allocated block closure object;
/// it should not be confused with a 'byref destroy helper', which
/// destroys the heap-allocated contents of an individual __block
/// variable.
llvm::Constant *
CodeGenFunction::GenerateDestroyHelperFunction(const CGBlockInfo &blockInfo) {
  ASTContext &C = getContext();

  FunctionArgList args;
  ImplicitParamDecl srcDecl(getContext(), nullptr, SourceLocation(), nullptr,
                            C.VoidPtrTy);
  args.push_back(&srcDecl);

  const CGFunctionInfo &FI =
    CGM.getTypes().arrangeBuiltinFunctionDeclaration(C.VoidTy, args);

  // FIXME: We'd like to put these into a mergable by content, with
  // internal linkage.
  llvm::FunctionType *LTy = CGM.getTypes().GetFunctionType(FI);

  llvm::Function *Fn =
    llvm::Function::Create(LTy, llvm::GlobalValue::InternalLinkage,
                           "__destroy_helper_block_", &CGM.getModule());

  IdentifierInfo *II
    = &CGM.getContext().Idents.get("__destroy_helper_block_");

  FunctionDecl *FD = FunctionDecl::Create(C, C.getTranslationUnitDecl(),
                                          SourceLocation(),
                                          SourceLocation(), II, C.VoidTy,
                                          nullptr, SC_Static,
                                          false, false);

  CGM.SetInternalFunctionAttributes(nullptr, Fn, FI);

  // Create a scope with an artificial location for the body of this function.
  auto NL = ApplyDebugLocation::CreateEmpty(*this);
  StartFunction(FD, C.VoidTy, Fn, FI, args);
  auto AL = ApplyDebugLocation::CreateArtificial(*this);

  llvm::Type *structPtrTy = blockInfo.StructureType->getPointerTo();

  Address src = GetAddrOfLocalVar(&srcDecl);
  src = Address(Builder.CreateLoad(src), blockInfo.BlockAlign);
  src = Builder.CreateBitCast(src, structPtrTy, "block");

  const BlockDecl *blockDecl = blockInfo.getBlockDecl();

  CodeGenFunction::RunCleanupsScope cleanups(*this);

  for (const auto &CI : blockDecl->captures()) {
    const VarDecl *variable = CI.getVariable();
    QualType type = variable->getType();

    const CGBlockInfo::Capture &capture = blockInfo.getCapture(variable);
    if (capture.isConstant()) continue;

    BlockFieldFlags flags;
    const CXXDestructorDecl *dtor = nullptr;

    bool useARCWeakDestroy = false;
    bool useARCStrongDestroy = false;

    if (CI.isByRef()) {
      flags = BLOCK_FIELD_IS_BYREF;
      if (type.isObjCGCWeak())
        flags |= BLOCK_FIELD_IS_WEAK;
    } else if (const CXXRecordDecl *record = type->getAsCXXRecordDecl()) {
      if (record->hasTrivialDestructor())
        continue;
      dtor = record->getDestructor();
    } else if (type->isObjCRetainableType()) {
      flags = BLOCK_FIELD_IS_OBJECT;
      if (type->isBlockPointerType())
        flags = BLOCK_FIELD_IS_BLOCK;

      // Special rules for ARC captures.
      Qualifiers qs = type.getQualifiers();

      // Use objc_storeStrong for __strong direct captures; the
      // dynamic tools really like it when we do this.
      if (qs.getObjCLifetime() == Qualifiers::OCL_Strong) {
        useARCStrongDestroy = true;

      // Support __weak direct captures.
      } else if (qs.getObjCLifetime() == Qualifiers::OCL_Weak) {
        useARCWeakDestroy = true;

      // Non-ARC captures are strong, and we need to use _Block_object_dispose.
      } else if (!qs.hasObjCLifetime() && !getLangOpts().ObjCAutoRefCount) {
        // fall through

      // Otherwise, we have nothing to do.
      } else {
        continue;
      }
    } else {
      continue;
    }

    Address srcField =
      Builder.CreateStructGEP(src, capture.getIndex(), capture.getOffset());

    // If there's an explicit copy expression, we do that.
    if (dtor) {
      PushDestructorCleanup(dtor, srcField);

    // If this is a __weak capture, emit the release directly.
    } else if (useARCWeakDestroy) {
      EmitARCDestroyWeak(srcField);

    // Destroy strong objects with a call if requested.
    } else if (useARCStrongDestroy) {
      EmitARCDestroyStrong(srcField, ARCImpreciseLifetime);

    // Otherwise we call _Block_object_dispose.  It wouldn't be too
    // hard to just emit this as a cleanup if we wanted to make sure
    // that things were done in reverse.
    } else {
      llvm::Value *value = Builder.CreateLoad(srcField);
      value = Builder.CreateBitCast(value, VoidPtrTy);
      BuildBlockRelease(value, flags);
    }
  }

  cleanups.ForceCleanup();

  FinishFunction();

  return llvm::ConstantExpr::getBitCast(Fn, VoidPtrTy);
}

namespace {

/// Emits the copy/dispose helper functions for a __block object of id type.
class ObjectByrefHelpers final : public BlockByrefHelpers {
  BlockFieldFlags Flags;

public:
  ObjectByrefHelpers(CharUnits alignment, BlockFieldFlags flags)
    : BlockByrefHelpers(alignment), Flags(flags) {}

  void emitCopy(CodeGenFunction &CGF, Address destField,
                Address srcField) override {
    destField = CGF.Builder.CreateBitCast(destField, CGF.VoidPtrTy);

    srcField = CGF.Builder.CreateBitCast(srcField, CGF.VoidPtrPtrTy);
    llvm::Value *srcValue = CGF.Builder.CreateLoad(srcField);

    unsigned flags = (Flags | BLOCK_BYREF_CALLER).getBitMask();

    llvm::Value *flagsVal = llvm::ConstantInt::get(CGF.Int32Ty, flags);
    llvm::Value *fn = CGF.CGM.getBlockObjectAssign();

    llvm::Value *args[] = { destField.getPointer(), srcValue, flagsVal };
    CGF.EmitNounwindRuntimeCall(fn, args);
  }

  void emitDispose(CodeGenFunction &CGF, Address field) override {
    field = CGF.Builder.CreateBitCast(field, CGF.Int8PtrTy->getPointerTo(0));
    llvm::Value *value = CGF.Builder.CreateLoad(field);

    CGF.BuildBlockRelease(value, Flags | BLOCK_BYREF_CALLER);
  }

  void profileImpl(llvm::FoldingSetNodeID &id) const override {
    id.AddInteger(Flags.getBitMask());
  }
};

/// Emits the copy/dispose helpers for an ARC __block __weak variable.
class ARCWeakByrefHelpers final : public BlockByrefHelpers {
public:
  ARCWeakByrefHelpers(CharUnits alignment) : BlockByrefHelpers(alignment) {}

  void emitCopy(CodeGenFunction &CGF, Address destField,
                Address srcField) override {
    CGF.EmitARCMoveWeak(destField, srcField);
  }

  void emitDispose(CodeGenFunction &CGF, Address field) override {
    CGF.EmitARCDestroyWeak(field);
  }

  void profileImpl(llvm::FoldingSetNodeID &id) const override {
    // 0 is distinguishable from all pointers and byref flags
    id.AddInteger(0);
  }
};

/// Emits the copy/dispose helpers for an ARC __block __strong variable
/// that's not of block-pointer type.
class ARCStrongByrefHelpers final : public BlockByrefHelpers {
public:
  ARCStrongByrefHelpers(CharUnits alignment) : BlockByrefHelpers(alignment) {}

  void emitCopy(CodeGenFunction &CGF, Address destField,
                Address srcField) override {
    // Do a "move" by copying the value and then zeroing out the old
    // variable.

    llvm::Value *value = CGF.Builder.CreateLoad(srcField);

    llvm::Value *null =
      llvm::ConstantPointerNull::get(cast<llvm::PointerType>(value->getType()));

    if (CGF.CGM.getCodeGenOpts().OptimizationLevel == 0) {
      CGF.Builder.CreateStore(null, destField);
      CGF.EmitARCStoreStrongCall(destField, value, /*ignored*/ true);
      CGF.EmitARCStoreStrongCall(srcField, null, /*ignored*/ true);
      return;
    }
    CGF.Builder.CreateStore(value, destField);
    CGF.Builder.CreateStore(null, srcField);
  }

  void emitDispose(CodeGenFunction &CGF, Address field) override {
    CGF.EmitARCDestroyStrong(field, ARCImpreciseLifetime);
  }

  void profileImpl(llvm::FoldingSetNodeID &id) const override {
    // 1 is distinguishable from all pointers and byref flags
    id.AddInteger(1);
  }
};

/// Emits the copy/dispose helpers for an ARC __block __strong
/// variable that's of block-pointer type.
class ARCStrongBlockByrefHelpers final : public BlockByrefHelpers {
public:
  ARCStrongBlockByrefHelpers(CharUnits alignment)
    : BlockByrefHelpers(alignment) {}

  void emitCopy(CodeGenFunction &CGF, Address destField,
                Address srcField) override {
    // Do the copy with objc_retainBlock; that's all that
    // _Block_object_assign would do anyway, and we'd have to pass the
    // right arguments to make sure it doesn't get no-op'ed.
    llvm::Value *oldValue = CGF.Builder.CreateLoad(srcField);
    llvm::Value *copy = CGF.EmitARCRetainBlock(oldValue, /*mandatory*/ true);
    CGF.Builder.CreateStore(copy, destField);
  }

  void emitDispose(CodeGenFunction &CGF, Address field) override {
    CGF.EmitARCDestroyStrong(field, ARCImpreciseLifetime);
  }

  void profileImpl(llvm::FoldingSetNodeID &id) const override {
    // 2 is distinguishable from all pointers and byref flags
    id.AddInteger(2);
  }
};

/// Emits the copy/dispose helpers for a __block variable with a
/// nontrivial copy constructor or destructor.
class CXXByrefHelpers final : public BlockByrefHelpers {
  QualType VarType;
  const Expr *CopyExpr;

public:
  CXXByrefHelpers(CharUnits alignment, QualType type,
                  const Expr *copyExpr)
    : BlockByrefHelpers(alignment), VarType(type), CopyExpr(copyExpr) {}

  bool needsCopy() const override { return CopyExpr != nullptr; }
  void emitCopy(CodeGenFunction &CGF, Address destField,
                Address srcField) override {
    if (!CopyExpr) return;
    CGF.EmitSynthesizedCXXCopyCtor(destField, srcField, CopyExpr);
  }

  void emitDispose(CodeGenFunction &CGF, Address field) override {
    EHScopeStack::stable_iterator cleanupDepth = CGF.EHStack.stable_begin();
    CGF.PushDestructorCleanup(VarType, field);
    CGF.PopCleanupBlocks(cleanupDepth);
  }

  void profileImpl(llvm::FoldingSetNodeID &id) const override {
    id.AddPointer(VarType.getCanonicalType().getAsOpaquePtr());
  }
};
} // end anonymous namespace

static llvm::Constant *
generateByrefCopyHelper(CodeGenFunction &CGF, const BlockByrefInfo &byrefInfo,
                        BlockByrefHelpers &generator) {
  ASTContext &Context = CGF.getContext();

  QualType R = Context.VoidTy;

  FunctionArgList args;
  ImplicitParamDecl dst(CGF.getContext(), nullptr, SourceLocation(), nullptr,
                        Context.VoidPtrTy);
  args.push_back(&dst);

  ImplicitParamDecl src(CGF.getContext(), nullptr, SourceLocation(), nullptr,
                        Context.VoidPtrTy);
  args.push_back(&src);

  const CGFunctionInfo &FI =
    CGF.CGM.getTypes().arrangeBuiltinFunctionDeclaration(R, args);

  llvm::FunctionType *LTy = CGF.CGM.getTypes().GetFunctionType(FI);

  // FIXME: We'd like to put these into a mergable by content, with
  // internal linkage.
  llvm::Function *Fn =
    llvm::Function::Create(LTy, llvm::GlobalValue::InternalLinkage,
                           "__Block_byref_object_copy_", &CGF.CGM.getModule());

  IdentifierInfo *II
    = &Context.Idents.get("__Block_byref_object_copy_");

  FunctionDecl *FD = FunctionDecl::Create(Context,
                                          Context.getTranslationUnitDecl(),
                                          SourceLocation(),
                                          SourceLocation(), II, R, nullptr,
                                          SC_Static,
                                          false, false);

  CGF.CGM.SetInternalFunctionAttributes(nullptr, Fn, FI);

  CGF.StartFunction(FD, R, Fn, FI, args);

  if (generator.needsCopy()) {
    llvm::Type *byrefPtrType = byrefInfo.Type->getPointerTo(0);

    // dst->x
    Address destField = CGF.GetAddrOfLocalVar(&dst);
    destField = Address(CGF.Builder.CreateLoad(destField),
                        byrefInfo.ByrefAlignment);
    destField = CGF.Builder.CreateBitCast(destField, byrefPtrType);
    destField = CGF.emitBlockByrefAddress(destField, byrefInfo, false,
                                          "dest-object");

    // src->x
    Address srcField = CGF.GetAddrOfLocalVar(&src);
    srcField = Address(CGF.Builder.CreateLoad(srcField),
                       byrefInfo.ByrefAlignment);
    srcField = CGF.Builder.CreateBitCast(srcField, byrefPtrType);
    srcField = CGF.emitBlockByrefAddress(srcField, byrefInfo, false,
                                         "src-object");

    generator.emitCopy(CGF, destField, srcField);
  }

  CGF.FinishFunction();

  return llvm::ConstantExpr::getBitCast(Fn, CGF.Int8PtrTy);
}

/// Build the copy helper for a __block variable.
static llvm::Constant *buildByrefCopyHelper(CodeGenModule &CGM,
                                            const BlockByrefInfo &byrefInfo,
                                            BlockByrefHelpers &generator) {
  CodeGenFunction CGF(CGM);
  return generateByrefCopyHelper(CGF, byrefInfo, generator);
}

/// Generate code for a __block variable's dispose helper.
static llvm::Constant *
generateByrefDisposeHelper(CodeGenFunction &CGF,
                           const BlockByrefInfo &byrefInfo,
                           BlockByrefHelpers &generator) {
  ASTContext &Context = CGF.getContext();
  QualType R = Context.VoidTy;

  FunctionArgList args;
  ImplicitParamDecl src(CGF.getContext(), nullptr, SourceLocation(), nullptr,
                        Context.VoidPtrTy);
  args.push_back(&src);

  const CGFunctionInfo &FI =
    CGF.CGM.getTypes().arrangeBuiltinFunctionDeclaration(R, args);

  llvm::FunctionType *LTy = CGF.CGM.getTypes().GetFunctionType(FI);

  // FIXME: We'd like to put these into a mergable by content, with
  // internal linkage.
  llvm::Function *Fn =
    llvm::Function::Create(LTy, llvm::GlobalValue::InternalLinkage,
                           "__Block_byref_object_dispose_",
                           &CGF.CGM.getModule());

  IdentifierInfo *II
    = &Context.Idents.get("__Block_byref_object_dispose_");

  FunctionDecl *FD = FunctionDecl::Create(Context,
                                          Context.getTranslationUnitDecl(),
                                          SourceLocation(),
                                          SourceLocation(), II, R, nullptr,
                                          SC_Static,
                                          false, false);

  CGF.CGM.SetInternalFunctionAttributes(nullptr, Fn, FI);

  CGF.StartFunction(FD, R, Fn, FI, args);

  if (generator.needsDispose()) {
    Address addr = CGF.GetAddrOfLocalVar(&src);
    addr = Address(CGF.Builder.CreateLoad(addr), byrefInfo.ByrefAlignment);
    auto byrefPtrType = byrefInfo.Type->getPointerTo(0);
    addr = CGF.Builder.CreateBitCast(addr, byrefPtrType);
    addr = CGF.emitBlockByrefAddress(addr, byrefInfo, false, "object");

    generator.emitDispose(CGF, addr);
  }

  CGF.FinishFunction();

  return llvm::ConstantExpr::getBitCast(Fn, CGF.Int8PtrTy);
}

/// Build the dispose helper for a __block variable.
static llvm::Constant *buildByrefDisposeHelper(CodeGenModule &CGM,
                                               const BlockByrefInfo &byrefInfo,
                                               BlockByrefHelpers &generator) {
  CodeGenFunction CGF(CGM);
  return generateByrefDisposeHelper(CGF, byrefInfo, generator);
}

/// Lazily build the copy and dispose helpers for a __block variable
/// with the given information.
template <class T>
static T *buildByrefHelpers(CodeGenModule &CGM, const BlockByrefInfo &byrefInfo,
                            T &&generator) {
  llvm::FoldingSetNodeID id;
  generator.Profile(id);

  void *insertPos;
  BlockByrefHelpers *node
    = CGM.ByrefHelpersCache.FindNodeOrInsertPos(id, insertPos);
  if (node) return static_cast<T*>(node);

  generator.CopyHelper = buildByrefCopyHelper(CGM, byrefInfo, generator);
  generator.DisposeHelper = buildByrefDisposeHelper(CGM, byrefInfo, generator);

  T *copy = new (CGM.getContext()) T(std::forward<T>(generator));
  CGM.ByrefHelpersCache.InsertNode(copy, insertPos);
  return copy;
}

/// Build the copy and dispose helpers for the given __block variable
/// emission.  Places the helpers in the global cache.  Returns null
/// if no helpers are required.
BlockByrefHelpers *
CodeGenFunction::buildByrefHelpers(llvm::StructType &byrefType,
                                   const AutoVarEmission &emission) {
  const VarDecl &var = *emission.Variable;
  QualType type = var.getType();

  auto &byrefInfo = getBlockByrefInfo(&var);

  // The alignment we care about for the purposes of uniquing byref
  // helpers is the alignment of the actual byref value field.
  CharUnits valueAlignment =
    byrefInfo.ByrefAlignment.alignmentAtOffset(byrefInfo.FieldOffset);

  if (const CXXRecordDecl *record = type->getAsCXXRecordDecl()) {
    const Expr *copyExpr = CGM.getContext().getBlockVarCopyInits(&var);
    if (!copyExpr && record->hasTrivialDestructor()) return nullptr;

    return ::buildByrefHelpers(
        CGM, byrefInfo, CXXByrefHelpers(valueAlignment, type, copyExpr));
  }

  // Otherwise, if we don't have a retainable type, there's nothing to do.
  // that the runtime does extra copies.
  if (!type->isObjCRetainableType()) return nullptr;

  Qualifiers qs = type.getQualifiers();

  // If we have lifetime, that dominates.
  if (Qualifiers::ObjCLifetime lifetime = qs.getObjCLifetime()) {
    switch (lifetime) {
    case Qualifiers::OCL_None: llvm_unreachable("impossible");

    // These are just bits as far as the runtime is concerned.
    case Qualifiers::OCL_ExplicitNone:
    case Qualifiers::OCL_Autoreleasing:
      return nullptr;

    // Tell the runtime that this is ARC __weak, called by the
    // byref routines.
    case Qualifiers::OCL_Weak:
      return ::buildByrefHelpers(CGM, byrefInfo,
                                 ARCWeakByrefHelpers(valueAlignment));

    // ARC __strong __block variables need to be retained.
    case Qualifiers::OCL_Strong:
      // Block pointers need to be copied, and there's no direct
      // transfer possible.
      if (type->isBlockPointerType()) {
        return ::buildByrefHelpers(CGM, byrefInfo,
                                   ARCStrongBlockByrefHelpers(valueAlignment));

      // Otherwise, we transfer ownership of the retain from the stack
      // to the heap.
      } else {
        return ::buildByrefHelpers(CGM, byrefInfo,
                                   ARCStrongByrefHelpers(valueAlignment));
      }
    }
    llvm_unreachable("fell out of lifetime switch!");
  }

  BlockFieldFlags flags;
  if (type->isBlockPointerType()) {
    flags |= BLOCK_FIELD_IS_BLOCK;
  } else if (CGM.getContext().isObjCNSObjectType(type) ||
             type->isObjCObjectPointerType()) {
    flags |= BLOCK_FIELD_IS_OBJECT;
  } else {
    return nullptr;
  }

  if (type.isObjCGCWeak())
    flags |= BLOCK_FIELD_IS_WEAK;

  return ::buildByrefHelpers(CGM, byrefInfo,
                             ObjectByrefHelpers(valueAlignment, flags));
}

Address CodeGenFunction::emitBlockByrefAddress(Address baseAddr,
                                               const VarDecl *var,
                                               bool followForward) {
  auto &info = getBlockByrefInfo(var);
  return emitBlockByrefAddress(baseAddr, info, followForward, var->getName());
}

Address CodeGenFunction::emitBlockByrefAddress(Address baseAddr,
                                               const BlockByrefInfo &info,
                                               bool followForward,
                                               const llvm::Twine &name) {
  // Chase the forwarding address if requested.
  if (followForward) {
    Address forwardingAddr =
      Builder.CreateStructGEP(baseAddr, 1, getPointerSize(), "forwarding");
    baseAddr = Address(Builder.CreateLoad(forwardingAddr), info.ByrefAlignment);
  }

  return Builder.CreateStructGEP(baseAddr, info.FieldIndex,
                                 info.FieldOffset, name);
}

/// BuildByrefInfo - This routine changes a __block variable declared as T x
///   into:
///
///      struct {
///        void *__isa;
///        void *__forwarding;
///        int32_t __flags;
///        int32_t __size;
///        void *__copy_helper;       // only if needed
///        void *__destroy_helper;    // only if needed
///        void *__byref_variable_layout;// only if needed
///        char padding[X];           // only if needed
///        T x;
///      } x
///
const BlockByrefInfo &CodeGenFunction::getBlockByrefInfo(const VarDecl *D) {
  auto it = BlockByrefInfos.find(D);
  if (it != BlockByrefInfos.end())
    return it->second;

  llvm::StructType *byrefType =
    llvm::StructType::create(getLLVMContext(),
                             "struct.__block_byref_" + D->getNameAsString());

  QualType Ty = D->getType();

  CharUnits size;
  SmallVector<llvm::Type *, 8> types;

  // void *__isa;
  types.push_back(Int8PtrTy);
  size += getPointerSize();

  // void *__forwarding;
  types.push_back(llvm::PointerType::getUnqual(byrefType));
  size += getPointerSize();

  // int32_t __flags;
  types.push_back(Int32Ty);
  size += CharUnits::fromQuantity(4);

  // int32_t __size;
  types.push_back(Int32Ty);
  size += CharUnits::fromQuantity(4);

  // Note that this must match *exactly* the logic in buildByrefHelpers.
  bool hasCopyAndDispose = getContext().BlockRequiresCopying(Ty, D);
  if (hasCopyAndDispose) {
    /// void *__copy_helper;
    types.push_back(Int8PtrTy);
    size += getPointerSize();

    /// void *__destroy_helper;
    types.push_back(Int8PtrTy);
    size += getPointerSize();
  }

  bool HasByrefExtendedLayout = false;
  Qualifiers::ObjCLifetime Lifetime;
  if (getContext().getByrefLifetime(Ty, Lifetime, HasByrefExtendedLayout) &&
      HasByrefExtendedLayout) {
    /// void *__byref_variable_layout;
    types.push_back(Int8PtrTy);
    size += CharUnits::fromQuantity(PointerSizeInBytes);
  }

  // T x;
  llvm::Type *varTy = ConvertTypeForMem(Ty);

  bool packed = false;
  CharUnits varAlign = getContext().getDeclAlign(D);
  CharUnits varOffset = size.alignTo(varAlign);

  // We may have to insert padding.
  if (varOffset != size) {
    llvm::Type *paddingTy =
      llvm::ArrayType::get(Int8Ty, (varOffset - size).getQuantity());

    types.push_back(paddingTy);
    size = varOffset;

  // Conversely, we might have to prevent LLVM from inserting padding.
  } else if (CGM.getDataLayout().getABITypeAlignment(varTy)
               > varAlign.getQuantity()) {
    packed = true;
  }
  types.push_back(varTy);

  byrefType->setBody(types, packed);

  BlockByrefInfo info;
  info.Type = byrefType;
  info.FieldIndex = types.size() - 1;
  info.FieldOffset = varOffset;
  info.ByrefAlignment = std::max(varAlign, getPointerAlign());

  auto pair = BlockByrefInfos.insert({D, info});
  assert(pair.second && "info was inserted recursively?");
  return pair.first->second;
}

/// Initialize the structural components of a __block variable, i.e.
/// everything but the actual object.
void CodeGenFunction::emitByrefStructureInit(const AutoVarEmission &emission) {
  // Find the address of the local.
  Address addr = emission.Addr;

  // That's an alloca of the byref structure type.
  llvm::StructType *byrefType = cast<llvm::StructType>(
    cast<llvm::PointerType>(addr.getPointer()->getType())->getElementType());

  unsigned nextHeaderIndex = 0;
  CharUnits nextHeaderOffset;
  auto storeHeaderField = [&](llvm::Value *value, CharUnits fieldSize,
                              const Twine &name) {
    auto fieldAddr = Builder.CreateStructGEP(addr, nextHeaderIndex,
                                             nextHeaderOffset, name);
    Builder.CreateStore(value, fieldAddr);

    nextHeaderIndex++;
    nextHeaderOffset += fieldSize;
  };

  // Build the byref helpers if necessary.  This is null if we don't need any.
  BlockByrefHelpers *helpers = buildByrefHelpers(*byrefType, emission);

  const VarDecl &D = *emission.Variable;
  QualType type = D.getType();

  bool HasByrefExtendedLayout;
  Qualifiers::ObjCLifetime ByrefLifetime;
  bool ByRefHasLifetime =
    getContext().getByrefLifetime(type, ByrefLifetime, HasByrefExtendedLayout);

  llvm::Value *V;

  // Initialize the 'isa', which is just 0 or 1.
  int isa = 0;
  if (type.isObjCGCWeak())
    isa = 1;
  V = Builder.CreateIntToPtr(Builder.getInt32(isa), Int8PtrTy, "isa");
  storeHeaderField(V, getPointerSize(), "byref.isa");

  // Store the address of the variable into its own forwarding pointer.
  storeHeaderField(addr.getPointer(), getPointerSize(), "byref.forwarding");

  // Blocks ABI:
  //   c) the flags field is set to either 0 if no helper functions are
  //      needed or BLOCK_BYREF_HAS_COPY_DISPOSE if they are,
  BlockFlags flags;
  if (helpers) flags |= BLOCK_BYREF_HAS_COPY_DISPOSE;
  if (ByRefHasLifetime) {
    if (HasByrefExtendedLayout) flags |= BLOCK_BYREF_LAYOUT_EXTENDED;
      else switch (ByrefLifetime) {
        case Qualifiers::OCL_Strong:
          flags |= BLOCK_BYREF_LAYOUT_STRONG;
          break;
        case Qualifiers::OCL_Weak:
          flags |= BLOCK_BYREF_LAYOUT_WEAK;
          break;
        case Qualifiers::OCL_ExplicitNone:
          flags |= BLOCK_BYREF_LAYOUT_UNRETAINED;
          break;
        case Qualifiers::OCL_None:
          if (!type->isObjCObjectPointerType() && !type->isBlockPointerType())
            flags |= BLOCK_BYREF_LAYOUT_NON_OBJECT;
          break;
        default:
          break;
      }
    if (CGM.getLangOpts().ObjCGCBitmapPrint) {
      printf("\n Inline flag for BYREF variable layout (%d):", flags.getBitMask());
      if (flags & BLOCK_BYREF_HAS_COPY_DISPOSE)
        printf(" BLOCK_BYREF_HAS_COPY_DISPOSE");
      if (flags & BLOCK_BYREF_LAYOUT_MASK) {
        BlockFlags ThisFlag(flags.getBitMask() & BLOCK_BYREF_LAYOUT_MASK);
        if (ThisFlag ==  BLOCK_BYREF_LAYOUT_EXTENDED)
          printf(" BLOCK_BYREF_LAYOUT_EXTENDED");
        if (ThisFlag ==  BLOCK_BYREF_LAYOUT_STRONG)
          printf(" BLOCK_BYREF_LAYOUT_STRONG");
        if (ThisFlag == BLOCK_BYREF_LAYOUT_WEAK)
          printf(" BLOCK_BYREF_LAYOUT_WEAK");
        if (ThisFlag == BLOCK_BYREF_LAYOUT_UNRETAINED)
          printf(" BLOCK_BYREF_LAYOUT_UNRETAINED");
        if (ThisFlag == BLOCK_BYREF_LAYOUT_NON_OBJECT)
          printf(" BLOCK_BYREF_LAYOUT_NON_OBJECT");
      }
      printf("\n");
    }
  }
  storeHeaderField(llvm::ConstantInt::get(IntTy, flags.getBitMask()),
                   getIntSize(), "byref.flags");

  CharUnits byrefSize = CGM.GetTargetTypeStoreSize(byrefType);
  V = llvm::ConstantInt::get(IntTy, byrefSize.getQuantity());
  storeHeaderField(V, getIntSize(), "byref.size");

  if (helpers) {
    storeHeaderField(helpers->CopyHelper, getPointerSize(),
                     "byref.copyHelper");
    storeHeaderField(helpers->DisposeHelper, getPointerSize(),
                     "byref.disposeHelper");
  }

  if (ByRefHasLifetime && HasByrefExtendedLayout) {
    auto layoutInfo = CGM.getObjCRuntime().BuildByrefLayout(CGM, type);
    storeHeaderField(layoutInfo, getPointerSize(), "byref.layout");
  }
}

void CodeGenFunction::BuildBlockRelease(llvm::Value *V, BlockFieldFlags flags) {
  llvm::Value *F = CGM.getBlockObjectDispose();
  llvm::Value *args[] = {
    Builder.CreateBitCast(V, Int8PtrTy),
    llvm::ConstantInt::get(Int32Ty, flags.getBitMask())
  };
  EmitNounwindRuntimeCall(F, args); // FIXME: throwing destructors?
}

namespace {
  /// Release a __block variable.
  struct CallBlockRelease final : EHScopeStack::Cleanup {
    llvm::Value *Addr;
    CallBlockRelease(llvm::Value *Addr) : Addr(Addr) {}

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      // Should we be passing FIELD_IS_WEAK here?
      CGF.BuildBlockRelease(Addr, BLOCK_FIELD_IS_BYREF);
    }
  };
} // end anonymous namespace

/// Enter a cleanup to destroy a __block variable.  Note that this
/// cleanup should be a no-op if the variable hasn't left the stack
/// yet; if a cleanup is required for the variable itself, that needs
/// to be done externally.
void CodeGenFunction::enterByrefCleanup(const AutoVarEmission &emission) {
  // We don't enter this cleanup if we're in pure-GC mode.
  if (CGM.getLangOpts().getGC() == LangOptions::GCOnly)
    return;

  EHStack.pushCleanup<CallBlockRelease>(NormalAndEHCleanup,
                                        emission.Addr.getPointer());
}

/// Adjust the declaration of something from the blocks API.
static void configureBlocksRuntimeObject(CodeGenModule &CGM,
                                         llvm::Constant *C) {
  auto *GV = cast<llvm::GlobalValue>(C->stripPointerCasts());

  if (CGM.getTarget().getTriple().isOSBinFormatCOFF()) {
    IdentifierInfo &II = CGM.getContext().Idents.get(C->getName());
    TranslationUnitDecl *TUDecl = CGM.getContext().getTranslationUnitDecl();
    DeclContext *DC = TranslationUnitDecl::castToDeclContext(TUDecl);

    assert((isa<llvm::Function>(C->stripPointerCasts()) ||
            isa<llvm::GlobalVariable>(C->stripPointerCasts())) &&
           "expected Function or GlobalVariable");

    const NamedDecl *ND = nullptr;
    for (const auto &Result : DC->lookup(&II))
      if ((ND = dyn_cast<FunctionDecl>(Result)) ||
          (ND = dyn_cast<VarDecl>(Result)))
        break;

    // TODO: support static blocks runtime
    if (GV->isDeclaration() && (!ND || !ND->hasAttr<DLLExportAttr>())) {
      GV->setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);
      GV->setLinkage(llvm::GlobalValue::ExternalLinkage);
    } else {
      GV->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);
      GV->setLinkage(llvm::GlobalValue::ExternalLinkage);
    }
  }

  if (!CGM.getLangOpts().BlocksRuntimeOptional)
    return;

  if (GV->isDeclaration() && GV->hasExternalLinkage())
    GV->setLinkage(llvm::GlobalValue::ExternalWeakLinkage);
}

llvm::Constant *CodeGenModule::getBlockObjectDispose() {
  if (BlockObjectDispose)
    return BlockObjectDispose;

  llvm::Type *args[] = { Int8PtrTy, Int32Ty };
  llvm::FunctionType *fty
    = llvm::FunctionType::get(VoidTy, args, false);
  BlockObjectDispose = CreateRuntimeFunction(fty, "_Block_object_dispose");
  configureBlocksRuntimeObject(*this, BlockObjectDispose);
  return BlockObjectDispose;
}

llvm::Constant *CodeGenModule::getBlockObjectAssign() {
  if (BlockObjectAssign)
    return BlockObjectAssign;

  llvm::Type *args[] = { Int8PtrTy, Int8PtrTy, Int32Ty };
  llvm::FunctionType *fty
    = llvm::FunctionType::get(VoidTy, args, false);
  BlockObjectAssign = CreateRuntimeFunction(fty, "_Block_object_assign");
  configureBlocksRuntimeObject(*this, BlockObjectAssign);
  return BlockObjectAssign;
}

llvm::Constant *CodeGenModule::getNSConcreteGlobalBlock() {
  if (NSConcreteGlobalBlock)
    return NSConcreteGlobalBlock;

  NSConcreteGlobalBlock = GetOrCreateLLVMGlobal("_NSConcreteGlobalBlock",
                                                Int8PtrTy->getPointerTo(),
                                                nullptr);
  configureBlocksRuntimeObject(*this, NSConcreteGlobalBlock);
  return NSConcreteGlobalBlock;
}

llvm::Constant *CodeGenModule::getNSConcreteStackBlock() {
  if (NSConcreteStackBlock)
    return NSConcreteStackBlock;

  NSConcreteStackBlock = GetOrCreateLLVMGlobal("_NSConcreteStackBlock",
                                               Int8PtrTy->getPointerTo(),
                                               nullptr);
  configureBlocksRuntimeObject(*this, NSConcreteStackBlock);
  return NSConcreteStackBlock;
}
