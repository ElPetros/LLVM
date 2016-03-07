//===- Contech.cpp - Based on Example code from "Writing an LLVM Pass" ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "Contech"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Pass.h"
#include "llvm/Support/GetElementPtrTypeIterator.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Instrumentation.h"
#include <map>
#include <vector>
#include "llvm/Type.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Module.h"
#include "llvm/Transforms/Utils/BuildLibCalls.h"
#include "llvm/LLVMContext.h"
#include "llvm/Metadata.h"
#include "llvm/ADT/Statistic.h"
#include <ct_event.h>
#include "Contech.h"
using namespace llvm;
//using namespace std;

// TODO:
// Transforms/IPO/zzProfile.cpp: insertBBMap -> id -> string

typedef struct _llvm_mem_op {
    bool isWrite;
    char size;
    Value* addr;
} llvm_mem_op, *pllvm_mem_op;


namespace llvm {
    //
    // Contech - First record every load or store in a program
    //
    class Contech : public ModulePass {
    public:
        static char ID; // Pass identification, replacement for typeid
        Constant* storeBasicBlockFunction;
        Constant* storeMemOpFunction;
        Constant* threadInitFunction;
        Constant* allocateBufferFunction;
        Constant* storeThreadCreateFunction;
        Constant* storeSyncFunction;
        Constant* storeMemoryEventFunction;
        Constant* queueBufferFunction;
        Constant* storeBarrierFunction;
        Constant* allocateCTidFunction;
        Constant* storeThreadJoinFunction;
        GlobalVariable* threadLocalNumber;
        Type* int8Ty;
        Type* int32Ty;
        Type* voidTy;
        Type* voidPtrTy;
        Type* int64Ty;
        Type* threadArgsTy;
        FunctionType* voidFuncTy;
        FunctionType* storeBBFuncTy;
        FunctionType* memOpFuncTy;
        FunctionType* storeTCreateTy;
        FunctionType* storeSyncTy;
        FunctionType* initThreadFuncTy;
        FunctionType* threadCreateTy;
        FunctionType* allocateBufferTy;
        FunctionType* storeMemoryEventTy;
        FunctionType* queueBufferTy;
        FunctionType* storeBarrierTy;

        Contech() : ModulePass(ID) {
    //        initializeContechPass(*PassRegistry::getPassRegistry());
        }
        
        virtual bool doInitialization(Module &M);
        virtual bool runOnModule(Module &M);
        virtual bool internalRunOnBasicBlock(BasicBlock &B,  int bbid);
        void internalAddAllocate(BasicBlock& B);
        unsigned int getSizeofType(Type*);
        unsigned int getSimpleLog(unsigned int);
        void insertBBMap(Module*, const std::vector<BasicBlock*> &bbs);
    };
    ModulePass* createContechPass() { return new Contech(); }
}    
    //
    // Create any globals requied for this module
    //
    bool Contech::doInitialization(Module &M)
    {
        LLVMContext &ctx = M.getContext();
        int8Ty = Type::getInt8Ty(ctx);
        int32Ty = Type::getInt32Ty(ctx);
        int64Ty = Type::getInt64Ty(ctx);
        voidTy = Type::getVoidTy(ctx);
        voidPtrTy = int8Ty->getPointerTo();
        voidFuncTy = FunctionType::get(voidTy, false);   

        Type* threadCreateTypes[] = {voidPtrTy};
        threadCreateTy = FunctionType::get(voidPtrTy, ArrayRef<Type*>(threadCreateTypes, 1), false);

        Type* threadStructTypes[] = {static_cast<Type *>(threadCreateTy)->getPointerTo(), voidPtrTy, int32Ty, int32Ty};
        threadArgsTy = StructType::create(ArrayRef<Type*>(threadStructTypes, 4), "contech_thread_create", false);
        
        Type* argsBB[] = {int32Ty, int32Ty};
        storeBBFuncTy = FunctionType::get(voidTy, ArrayRef<Type*>(argsBB, 2), false);
        storeBasicBlockFunction = M.getOrInsertFunction("__ctStoreBasicBlock", storeBBFuncTy);
        Type* argsMO[] = {int8Ty, int8Ty, voidPtrTy};
        memOpFuncTy = FunctionType::get(voidTy, ArrayRef<Type*>(argsMO, 3), false);
        storeMemOpFunction = M.getOrInsertFunction("__ctStoreMemOp", memOpFuncTy);
        Type* argsInit[] = {voidPtrTy};//threadArgsTy->getPointerTo()};
        initThreadFuncTy = FunctionType::get(voidPtrTy, ArrayRef<Type*>(argsInit, 1), false);
        threadInitFunction = M.getOrInsertFunction("__ctInitThread", initThreadFuncTy);

        // void (void) functions:
        allocateBufferTy = FunctionType::get(voidTy, false);
        allocateBufferFunction = M.getOrInsertFunction("__ctAllocateLocalBuffer", allocateBufferTy);
        storeThreadJoinFunction = M.getOrInsertFunction("__ctStoreThreadJoin", allocateBufferTy);
        
        allocateCTidFunction = M.getOrInsertFunction("__ctAllocateCTid", FunctionType::get(int32Ty, false));
        
        Type* argsSSync[] = {voidPtrTy};
        storeSyncTy = FunctionType::get(voidPtrTy, ArrayRef<Type*>(argsSSync, 1), false);
        storeSyncFunction = M.getOrInsertFunction("__ctStoreSync", storeSyncTy);
        
        Type* argsTC[] = {int32Ty};
        storeTCreateTy = FunctionType::get(voidPtrTy, ArrayRef<Type*>(argsTC, 1), false);
        storeThreadCreateFunction = M.getOrInsertFunction("__ctStoreThreadCreate", storeTCreateTy);
        
        threadLocalNumber = new GlobalVariable(M, int32Ty, false, GlobalValue::ExternalLinkage, 0, "__ctThreadLocalNumber", 
                                               0, true, 0);
        Type* argsME[] = {int8Ty, int64Ty, voidPtrTy};
        storeMemoryEventTy = FunctionType::get(voidPtrTy, ArrayRef<Type*>(argsME, 3), false);
        storeMemoryEventFunction = M.getOrInsertFunction("__ctStoreMemoryEvent", storeMemoryEventTy);
        
        Type* argsQB[] = {int8Ty};
        queueBufferTy = FunctionType::get(voidTy, ArrayRef<Type*>(argsQB, 1), false);
        queueBufferFunction = M.getOrInsertFunction("__ctQueueBuffer", queueBufferTy);
        
        Type* argsSB[] = {int8Ty, voidPtrTy};
        storeBarrierTy = FunctionType::get(voidPtrTy, ArrayRef<Type*>(argsSB, 2), false);
        storeBarrierFunction = M.getOrInsertFunction("__ctStoreBarrier", storeBarrierTy);
        
        return true;
    }
    
    void Contech::internalAddAllocate(BasicBlock& B)
    {
        CallInst::Create(allocateBufferFunction, "", B.begin());
    }
    
    //
    // Go through the module to get the basic blocks
    //
    bool Contech::runOnModule(Module &M)
    {
        int bb_count = 0;
        doInitialization(M);
        for (Module::iterator F = M.begin(), FE = M.end(); F != FE; ++F) {
            if (F->getName() == "main")
            {
                F->setName(Twine("ct_orig_main"));
            }
        
            for (Function::iterator B = F->begin(), BE = F->end(); B != BE; ++B) {
                BasicBlock &pB = *B;
                
                internalRunOnBasicBlock(pB, bb_count);
                bb_count++;
            }
        }
        
        return true;
    }
    
    // returns size in bytes
    unsigned int Contech::getSizeofType(Type* t)
    {
        unsigned int r = t->getPrimitiveSizeInBits();
        if (r > 0) return r / 8;
        else if (t->isPointerTy()) { return 8;}
        errs() << "Failed get size - " << *t << "\n";
        return 0;
    }    
    
    unsigned int Contech::getSimpleLog(unsigned int i)
    {
        if (i > 128) {return 8;}
        if (i > 64) { return 7;}
        if (i > 32) { return 6;}
        if (i > 16) { return 5;}
        if (i > 8) { return 4;}
        if (i > 4) { return 3;}
        if (i > 2) { return 2;}
        if (i > 1) { return 1;}
        return 0;
    }
    
    //
    // For each basic block
    //
    bool Contech::internalRunOnBasicBlock(BasicBlock &B,  int bbid)
    {
        Instruction* iPt = B.getTerminator();
        std::vector<pllvm_mem_op> opsInBlock;
        errs() << "Basic Block - " << bbid << "\n";
        for (BasicBlock::iterator I = B.begin(), E = B.end(); I != E; ++I){
            // <result> = load [volatile] <ty>* <pointer>[, align <alignment>][, !nontemporal !<index>][, !invariant.load !<index>]
            if (LoadInst *li = dyn_cast<LoadInst>(&*I)){
                pllvm_mem_op t = (pllvm_mem_op)malloc(sizeof(llvm_mem_op));
                t->isWrite = false;
                t->size = getSizeofType(li->getPointerOperand()->getType()->getPointerElementType());
                t->addr = li->getPointerOperand();
                opsInBlock.push_back(t);
            }
            //  store [volatile] <ty> <value>, <ty>* <pointer>[, align <alignment>][, !nontemporal !<index>]
            else if (StoreInst *si = dyn_cast<StoreInst>(&*I)) {
                pllvm_mem_op t = (pllvm_mem_op)malloc(sizeof(llvm_mem_op));
                t->isWrite = true;
                t->size = getSizeofType(si->getPointerOperand()->getType()->getPointerElementType());
                t->addr = si->getPointerOperand();
                opsInBlock.push_back(t);
            }
            else if (CallInst *ci = dyn_cast<CallInst>(&*I)) {
                Function *f = ci->getCalledFunction();
                if (ci->doesNotReturn())
                {
                    iPt = ci;
                }
                if (0 == strcmp(f->getName().data(), "malloc"))
                {
                    Value* cArg[] = {ConstantInt::get(int8Ty, 1), ci->getArgOperand(0), ci};
                    CallInst* nStoreME = CallInst::Create(storeMemoryEventFunction, ArrayRef<Value*>(cArg, 3),
                                                        "ctStoreMemoryEvent", ++I);
                    I = nStoreME;
                }
                else if (0 == strcmp(f->getName().data(), "free"))
                {
                errs() << "Free\n";
                    Value* cz = ConstantInt::get(int8Ty, 0);
                    Value* cz32 = ConstantInt::get(int64Ty, 0);
                    Value* cArg[] = {cz, cz32, ci->getArgOperand(0)};
                    CallInst* nStoreME = CallInst::Create(storeMemoryEventFunction, ArrayRef<Value*>(cArg, 3),
                                                        "ctStoreMemoryEvent", ++I);
                    I = nStoreME;
                }
                else if (0 == strcmp(f->getName().data(), "pthread_mutex_lock"))
                {
                    Value* cArg[] = {new BitCastInst(ci->getArgOperand(0), voidPtrTy, "locktovoid", ci)};
                    CallInst* nStoreSync = CallInst::Create(storeSyncFunction, ArrayRef<Value*>(cArg,1),
                                                        "ctStoreMutexLock", ++I);
                    I = nStoreSync;
                    cArg[0] = ConstantInt::get(int8Ty, 1);
                    CallInst* nQueueBuf = CallInst::Create(queueBufferFunction, ArrayRef<Value*>(cArg, 1),
                                                        "", ++I);
                    I = nQueueBuf;
                }
                else if (0 == strcmp(f->getName().data(), "pthread_mutex_unlock"))
                {
                    BitCastInst* bci = new BitCastInst(ci->getArgOperand(0), voidPtrTy, "locktovoid", ++I);
                    Value* cArg[] = {bci};
                    CallInst* nStoreSync = CallInst::Create(storeSyncFunction, ArrayRef<Value*>(cArg,1),
                                                        "ctStoreMutexUnlock", I);
                    I = nStoreSync;
                    cArg[0] = ConstantInt::get(int8Ty, 1);
                    CallInst* nQueueBuf = CallInst::Create(queueBufferFunction, ArrayRef<Value*>(cArg, 1),
                                                        "", ++I);
                    I = nQueueBuf;
                }
                else if (0 == strcmp(f->getName().data(), "pthread_barrier_wait"))
                {
                    BitCastInst* bci = new BitCastInst(ci->getArgOperand(0), voidPtrTy, "locktovoid", I);
                    Value* c1 = ConstantInt::get(int8Ty, 1);
                    Value* cArgs[] = {c1, bci};
                    CallInst* nStoreBar = CallInst::Create(storeBarrierFunction, ArrayRef<Value*>(cArgs,2),
                                                        "ctStoreBarrierEnter", I);
                    I++;
                    cArgs[0] = ConstantInt::get(int8Ty, 0);
                    nStoreBar = CallInst::Create(storeBarrierFunction, ArrayRef<Value*>(cArgs,2),
                                                        "ctStoreBarrierExit", I);
                    I = nStoreBar;
                    Value* cArg[] = {c1};
                    CallInst* nQueueBuf = CallInst::Create(queueBufferFunction, ArrayRef<Value*>(cArg, 1),
                                                        "", ++I);
                    I = nQueueBuf;
                }
                else if (0 == strcmp(f->getName().data(), "pthread_join"))
                {
                    CallInst* nStoreJ = CallInst::Create(storeThreadJoinFunction, Twine(""), I);
                }
                //I++;
                //int pthread_create(pthread_t * thread, const pthread_attr_t * attr,
                //                   void * (*start_routine)(void *), void * arg);
                //
                else if (0 == strcmp(f->getName().data(), "pthread_create"))
                {
                    Instruction* wrapperAllocate = CallInst::CreateMalloc(ci, int64Ty, threadArgsTy, 
                                                        ConstantInt::get(int64Ty, 24), ConstantInt::get(int64Ty, 1));
                    
                    errs() << "Pthread_create\n";
                    Constant* zeroC = ConstantInt::get(int32Ty, 0);
                    Value* gepIdx[] = {zeroC, zeroC};
                    Value* gepA1 = (Value*)GetElementPtrInst::Create(wrapperAllocate, ArrayRef<Value*>(gepIdx, 2), "", ci);
//errs() << *threadArgsTy << "\n";
//errs() << *wrapperAllocate << "\n" << *gepA1 << "\n";
                    Value* carg2 = ci->getArgOperand(2);//new BitCastInst(ci->getArgOperand(2), voidPtrTy, "", ci);
                    
                    gepIdx[1] = ConstantInt::get(int32Ty, 1);
                    new StoreInst(ci->getArgOperand(3),
                        (Value*)GetElementPtrInst::Create(wrapperAllocate, ArrayRef<Value*>(gepIdx, 2), "", ci), ci);
                    gepIdx[1] = ConstantInt::get(int32Ty, 2);
                    LoadInst* ldThreadId = new LoadInst(threadLocalNumber, "get_thread_local_id", ci);
                    new StoreInst(ldThreadId,
                        (Value*)GetElementPtrInst::Create(wrapperAllocate, ArrayRef<Value*>(gepIdx, 2), "", ci), ci);
                    
                    new StoreInst(carg2, gepA1, ci);
                    gepIdx[1] = ConstantInt::get(int32Ty, 3);
                    Value* chThreadId = CallInst::Create(allocateCTidFunction, Twine("allocate_child_id"), ci);
                    new StoreInst(chThreadId,
                        (Value*)GetElementPtrInst::Create(wrapperAllocate, ArrayRef<Value*>(gepIdx, 2), "", ci), ci);
                    Value* cArg = new BitCastInst(wrapperAllocate, voidPtrTy, Twine("Cast as void"), ci);
                    Value* cArgs[] = {ci->getArgOperand(0), ci->getArgOperand(1), threadInitFunction, cArg};
                    Value* cTcArg[] = {chThreadId};
                    CallInst* stThreadCreate = CallInst::Create(storeThreadCreateFunction, ArrayRef<Value*>(cTcArg, 1),
                                                Twine("ct_store_thread_create"), ci);
                    cTcArg[0] = ConstantInt::get(int8Ty, 1);
                    CallInst::Create(queueBufferFunction, ArrayRef<Value*>(cTcArg, 1), "", ci);
                    CallInst* nThreadCreate = CallInst::Create(ci->getCalledValue(), ArrayRef<Value*>(cArgs,4),
                                                Twine("ct_thread_create_wrapper"), ci);
                    I = nThreadCreate;
                    ci->replaceAllUsesWith(nThreadCreate);
                    ci->eraseFromParent();
                    
                    
                    //I = stThreadCreate;
                }
            }
        }
        
        // In inserting these calls, we trust the compiler / linker will inline them
        // Insert storeBasicBlock(cid, bbid, num_ops)
        // foreach op in opsInBlock
        //   Insert storeMemOp(op)
        
        //errs() << "terminator - " << bbid << "\t" << opsInBlock.size() << "\n";
        Constant* llvm_bbid = ConstantInt::get(int32Ty, bbid);
        Constant* llvm_nops = ConstantInt::get(int32Ty, (unsigned long) opsInBlock.size());
        Value* argsBB[] = {llvm_bbid, llvm_nops};
        CallInst::Create(storeBasicBlockFunction, ArrayRef<Value*>(argsBB, 2), "", iPt);
        //errs() << "per op\n";
        for (std::vector<pllvm_mem_op>::iterator b = opsInBlock.begin(), e = opsInBlock.end(); b != e; b++)
        {
            pllvm_mem_op t = (pllvm_mem_op)(*b);
            Constant* cIsWrite = ConstantInt::get(int8Ty, t->isWrite);
            Constant* cSize = ConstantInt::get(int8Ty, getSimpleLog(t->size));
            Value* addrI = new BitCastInst(t->addr, voidPtrTy, Twine("Cast as void"), iPt);
            Value* argsMO[] = {cIsWrite, cSize, addrI};
            //errs() << *storeMemOpFunction << "\n";
            CallInst::Create(storeMemOpFunction, ArrayRef<Value*>(argsMO, 3), "", iPt);
            free(t);
        }
        opsInBlock.clear();
        
        return true;
    }

void llvm::Contech::insertBBMap(Module* pM, const std::vector<BasicBlock*> &bbs) {
  // inserting this equivalent C code:
  // char *PROF_bbmap[] = { "alpha", "beta" };
  //
  // which is this in LLVM:
  // @.str = private unnamed_addr constant [6 x i8] c"alpha\00", align 1
  // @.str1 = private unnamed_addr constant [5 x i8] c"beta\00", align 1
  // @PROF_bbmap = global [2 x i8*] [i8* getelementptr inbounds ([6 x i8]* @.str, i32 0, i32 0), i8* getelementptr inbounds ([5 x i8]* @.str1, i32 0, i32 0)], align 16
  //
  // which is the following in C++:
  std::vector<Constant*> gepIndices;
  Constant* zero = ConstantInt::get(int32Ty, 0);
  gepIndices.push_back(zero);
  gepIndices.push_back(zero);

  std::vector<Constant*> bbNames;

  char bb_n_str[64];
  for (int i = 0, sz = bbs.size(); i < sz; ++i) {
    snprintf(bb_n_str, 63, "%d", i);
    Twine tName = bbs[i]->getParent()->getName() + ":" + bbs[i]->getName();
    std::string srName(tName.str());
    ArrayType *ty = ArrayType::get(int8Ty, srName.size() + 1);
    GlobalVariable *v = new GlobalVariable(/*Module=*/*pM,
      /*Type=*/ty,
      /*isConstant=*/true,
      /*Linkage*/GlobalValue::PrivateLinkage,
      /*Initializer=*/NULL,//ConstantArray::get(pM->getContext(), srName, true), // c string shorthand
      /*Name=*/std::string("PROF_bb_id_") + bb_n_str);
    v->setAlignment(1);
    Constant *const_ptr_v = ConstantExpr::getGetElementPtr(v, gepIndices);
    bbNames.push_back(const_ptr_v);
  }
  // Null-terminate this list
  PointerType *ptrInt8Ty = PointerType::get(int8Ty, 0);
  bbNames.push_back(llvm::ConstantPointerNull::get(ptrInt8Ty));
  ArrayType *BBMapTy = ArrayType::get(ptrInt8Ty, bbNames.size());
  Constant *constBBMap = ConstantArray::get(BBMapTy, bbNames);
  GlobalVariable *gvBBMap = new GlobalVariable(*pM,
      BBMapTy,
      true,
      GlobalValue::ExternalLinkage,
      constBBMap,
      "ctBasicBlockNames");

  // Also add a counter
  GlobalVariable *gvNbbs = new GlobalVariable(*pM,
      int32Ty,
      true,
      GlobalValue::ExternalLinkage,
      ConstantInt::get(int32Ty, bbNames.size()),
      "ctBasicBlockCount");
}
    
char Contech::ID = 0;
static RegisterPass<Contech> X("Contech", "Contech Pass", false, false);
//INITIALIZE_PASS(Contech, "contech", "Contech front-end", false, false)
