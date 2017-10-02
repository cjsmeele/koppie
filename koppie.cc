/**
 * \file
 * \brief     Koppie - A Brainfuck compiler built on top of LLVM.
 * \author    Chris Smeele
 * \copyright Copyright (c) 2017, Chris Smeele.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <iostream>
#include <stack>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetSelect.h>

constexpr uint32_t tape_size      = 60000;
constexpr uint32_t initial_offset = 30000; // Allow some 'negative space', but do not wrap around.

int main(int argc, char **argv) {

    // Set up LLVM stuff.
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    llvm::LLVMContext context;
    llvm::IRBuilder builder(context);

    auto module = std::make_unique<llvm::Module>(argc>1 ? argv[1] : "module", context);

    // Create main function.
    llvm::Function *f = llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getInt32Ty(context), {}, false),
                                               llvm::Function::ExternalLinkage, "main", module.get());
    llvm::BasicBlock *bb = llvm::BasicBlock::Create(context, "entry", f);
    builder.SetInsertPoint(bb);

    // Allocate & clear tape and offset.
    auto tape_a   = builder.CreateAlloca(llvm::ArrayType::get(llvm::Type::getInt8Ty(context), tape_size), 0, "tape");
    auto offset_a = builder.CreateAlloca(llvm::Type::getInt32Ty(context), 0, "offset");
    builder.CreateStore(llvm::ConstantInt::get(context, llvm::APInt(32, initial_offset, false)), offset_a);
    builder.CreateMemSet(builder.CreateGEP(tape_a,
                                           llvm::ConstantInt::get(context, llvm::APInt(32, 0, false))),
                         llvm::ConstantInt::get(context, llvm::APInt(8, 0, false)), tape_size, 1);

    // Stack to keep track of loops we are generating.
    struct LoopFrame {
        llvm::BasicBlock *loopbb;
        llvm::BasicBlock *bodybb;
        llvm::BasicBlock *afterbb;
    };
    std::stack<LoopFrame> stack;

    // Convenience functions for accessing tape & offset.
    auto get_current_offset = [&] {
        return builder.CreateLoad(offset_a, "offset"); };

    auto get_current_ptr = [&] {
        return builder.CreateGEP(tape_a, { llvm::ConstantInt::get(context, llvm::APInt(32, 0, false)),
                                           get_current_offset() }, "ptr"); };
    auto get_current_cell = [&] {
        auto ptr = get_current_ptr();
        return std::tuple { ptr, builder.CreateLoad(ptr, "tmp") }; };

    auto get_current_value = [&] {
        return builder.CreateLoad(get_current_ptr(), "tmp"); };

    // Main loop.
    char ch;
    while ((std::cin >> ch) && ch) {
        if (ch == '>') {
            builder.CreateStore(builder.CreateAdd(get_current_offset(),
                                                  llvm::ConstantInt::get(context, llvm::APInt(32, 1, false)),
                                                  "offset_inc_tmp"), offset_a);

        } else if (ch == '<') {
            builder.CreateStore(builder.CreateSub(get_current_offset(),
                                                  llvm::ConstantInt::get(context, llvm::APInt(32, 1, false)),
                                                  "offset_dec_tmp"), offset_a);

        } else if (ch == '+') {
            auto [ ptr, val ] = get_current_cell();
            builder.CreateStore(builder.CreateAdd(val,
                                                  llvm::ConstantInt::get(context, llvm::APInt(8, 1, false)),
                                                  "inc_tmp"), ptr);

        } else if (ch == '-') {
            auto [ ptr, val ] = get_current_cell();
            builder.CreateStore(builder.CreateSub(val,
                                                  llvm::ConstantInt::get(context, llvm::APInt(8, 1, false)),
                                                  "dec_tmp"), ptr);

        } else if (ch == '.') {
            auto val = get_current_value();

            std::vector<llvm::Type*> args { llvm::Type::getInt32Ty(context) };
            llvm::FunctionType *ft = llvm::FunctionType::get(llvm::Type::getInt32Ty(context), args, false);
            builder.CreateCall(module->getOrInsertFunction("putchar", ft),
                               builder.CreateIntCast(val, llvm::Type::getInt32Ty(context), true));

        } else if (ch == ',') {
            auto ptr = get_current_ptr();

            std::vector<llvm::Type*> args;
            llvm::FunctionType *ft = llvm::FunctionType::get(llvm::Type::getInt32Ty(context), args, false);
            auto result = builder.CreateCall(module->getOrInsertFunction("getchar", ft));

            // Handle EOF.
            auto successbb = llvm::BasicBlock::Create(context, "getchar_success", f);
            auto failbb    = llvm::BasicBlock::Create(context, "getchar_fail");
            auto afterbb   = llvm::BasicBlock::Create(context, "getchar_after");

            builder.CreateCondBr(builder.CreateICmpEQ(result,
                                                      llvm::ConstantInt::get(context, llvm::APInt(32, EOF, true)),
                                                      "getchar_cond"),
                                 failbb, successbb);

            builder.SetInsertPoint(successbb);
            builder.CreateStore(builder.CreateIntCast(result, llvm::Type::getInt8Ty(context), false), ptr);
            builder.CreateBr(afterbb);

            // , returns 0 on EOF/error.
            f->getBasicBlockList().push_back(failbb);
            builder.SetInsertPoint(failbb);
            builder.CreateStore(llvm::ConstantInt::get(context, llvm::APInt(8, 0, false)), ptr);
            builder.CreateBr(afterbb);

            f->getBasicBlockList().push_back(afterbb);
            builder.SetInsertPoint(afterbb);

        } else if (ch == '[') {
            LoopFrame frame {
                llvm::BasicBlock::Create(context, "loop", f),
                llvm::BasicBlock::Create(context, "loop_body"),
                llvm::BasicBlock::Create(context, "loop_exit")
            };

            builder.CreateBr(frame.loopbb);
            builder.SetInsertPoint(frame.loopbb);

            builder.CreateCondBr(builder.CreateICmpEQ(get_current_value(),
                                                      llvm::ConstantInt::get(context, llvm::APInt(8, 0, false)),
                                                      "cond"),
                                 frame.afterbb, frame.bodybb);

            f->getBasicBlockList().push_back(frame.bodybb);
            builder.SetInsertPoint(frame.bodybb);

            stack.push(frame);

        } else if (ch == ']') {
            if (stack.size()) {
                auto frame = stack.top();

                builder.CreateCondBr(builder.CreateICmpEQ(get_current_value(),
                                                          llvm::ConstantInt::get(context, llvm::APInt(8, 0, false)),
                                                          "cond"),
                                     frame.afterbb, frame.bodybb);

                f->getBasicBlockList().push_back(frame.afterbb);
                builder.SetInsertPoint(frame.afterbb);
                stack.pop();

            } else { /* Ignore unmatched ] */ }
        } else { /* Ignore comments and whitespace */ }
    }

    // The exit code will be the current pointed-to value.
    builder.CreateRet(builder.CreateIntCast(get_current_value(),
                                            llvm::Type::getInt32Ty(context), true));

    llvm::verifyFunction(*f);

    // Optimizations à la https://llvm.org/docs/tutorial/LangImpl04.html#llvm-optimization-passes

    auto fpm = std::make_unique<llvm::legacy::FunctionPassManager>(module.get());

    fpm->add(llvm::createInstructionCombiningPass());
    fpm->add(llvm::createReassociatePass());
    fpm->add(llvm::createGVNPass());
    fpm->add(llvm::createCFGSimplificationPass());
    fpm->doInitialization();
    fpm->run(*f);

    module->print(llvm::outs(), nullptr);

    return 0;
}
