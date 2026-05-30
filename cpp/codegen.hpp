#pragma once

#include "compiler.hpp"
#include "helpers/vector.hpp"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <memory>

// ============================================================================
// Typed value wrapper
// ============================================================================

struct TypedAddr {
    llvm::Value *addr = nullptr;
    llvm::Type  *elem = nullptr;
};

// ============================================================================
// LLVMCodegen — main code generation class
// ============================================================================

class LLVMCodegen
{
public:
    explicit LLVMCodegen(struct compile_process *process);
    int run();

private:
    struct compile_process *m_process;

    std::unique_ptr<llvm::LLVMContext> m_ctx;
    std::unique_ptr<llvm::Module>      m_module;
    std::unique_ptr<llvm::IRBuilder<>> m_builder;

    // Variable scope
    std::vector<std::map<std::string, TypedAddr>> m_scopes;
    void       push_scope();
    void       pop_scope();
    void       define_var(const std::string &name, llvm::AllocaInst *ai,
                          llvm::Type *elem_ty);
    TypedAddr  lookup_var(const std::string &name);

    // Loop / switch break/continue stack
    struct LoopCtx {
        llvm::BasicBlock *break_bb    = nullptr;
        llvm::BasicBlock *continue_bb = nullptr;
    };
    std::vector<LoopCtx> m_loops;
    void             push_loop(llvm::BasicBlock *brk, llvm::BasicBlock *cont);
    void             pop_loop();
    llvm::BasicBlock *break_bb();
    llvm::BasicBlock *continue_bb();

    // goto / label support
    std::map<std::string, llvm::BasicBlock *> m_labels;
    struct PendingGoto {
        std::string       label;
        llvm::BasicBlock *from_bb;
        llvm::BranchInst *placeholder;
    };
    std::vector<PendingGoto> m_pending_gotos;
    void resolve_gotos();

    // String literal cache
    std::map<std::string, llvm::GlobalVariable *> m_str_cache;

    // Type helpers
    llvm::Type *dtype_to_llvm(struct datatype *dt);
    llvm::Type *prim_llvm(int dt);
    llvm::Type *i8ty()  { return llvm::Type::getInt8Ty(*m_ctx); }
    llvm::Type *i16ty() { return llvm::Type::getInt16Ty(*m_ctx); }
    llvm::Type *i32ty() { return llvm::Type::getInt32Ty(*m_ctx); }
    llvm::Type *i64ty() { return llvm::Type::getInt64Ty(*m_ctx); }
    llvm::Type *voidty(){ return llvm::Type::getVoidTy(*m_ctx); }
    llvm::PointerType *ptrty() {
        return llvm::PointerType::getUnqual(*m_ctx);
    }

    // Top-level generators
    void gen_top(struct node *n);
    void gen_global(struct node *n);
    void gen_func(struct node *n);

    // Statement generators
    void gen_body(struct node *n);
    void gen_stmt(struct node *n);
    void gen_if(struct node *n);
    void gen_while(struct node *n);
    void gen_do_while(struct node *n);
    void gen_for(struct node *n);
    void gen_switch(struct node *n);
    void gen_return(struct node *n);
    void gen_label(struct node *n);
    void gen_goto(struct node *n);

    // Expression generators
    llvm::Value *gen_node(struct node *n);
    llvm::Value *gen_expr(struct node *n);
    llvm::Value *gen_number(struct node *n);
    llvm::Value *gen_string(const char *str);
    llvm::Value *gen_ident(const char *name);
    llvm::Value *gen_var_decl(struct node *n);
    llvm::Value *gen_call(struct node *callee, struct node *args);
    llvm::Value *gen_assign(struct node *n);
    llvm::Value *gen_binary(struct node *n);
    llvm::Value *gen_prefix(const char *op, struct node *operand);
    llvm::Value *gen_postfix(const char *op, struct node *operand);
    llvm::Value *gen_ternary(struct node *n);
    llvm::Value *gen_cast(struct node *n);
    llvm::Value *gen_parens(struct node *n);

    // lvalue / address helpers
    TypedAddr lvalue_of(struct node *n);
    llvm::Value *load_from(TypedAddr ta);

    // Misc helpers
    llvm::AllocaInst *entry_alloca(llvm::Function *fn, const std::string &name,
                                   llvm::Type *ty);
    llvm::Value *to_bool(llvm::Value *v);
    llvm::Value *coerce(llvm::Value *v, llvm::Type *target);
    bool        is_terminated();
    llvm::Constant *global_init(struct node *val, llvm::Type *ty);
    llvm::FunctionCallee declare_func(const std::string &name,
                                      llvm::FunctionType *ft);
};
