

# Checkout LLVM
# TODO- get current path
# cd where-you-want-llvm-live
# svn co http://llvm.org/svn/llvm-project/llvm/trunk llvm

#Checkout Clang
# cd llvm/tools
# svn co http://llvm.org/svn/llvm-project/cfe/trunk clang

#Checkout Compiler-RT
# cd llvm/projects
# svn co http://llvm.org/svn/llvm-project/compiler-rt/trunk compiler-rt


# Go to build directory

###### Configure LLVM with CMake
###### We will need support for link time optimizations (LLVM LTO gold)
###### TODO
cmake -G "Unix Makefiles" -DLLVM_TARGETS_TO_BUILD="X86" -DLLM_ENABLE_LTO="On" -DLLVM_BINUTILS_INCDIR=/path/to/binutils/include


###### Parallel build
make -j10
