#ifndef CONTECH_H
#define CONTECH_H

#include "llvm/Pass.h"
#include "llvm/Module.h"

namespace llvm {
    class Contech;
    ModulePass* createContechPass();
}

#endif