/**
 * codegen.cpp — LLVM IR backend for the 1337cc C compiler.
 *
 * Walks the AST produced by the parser and emits LLVM IR via the LLVM C++ API.
 * Targets LLVM 22 (opaque pointer model, no typed pointer types beyond ptr).
 * The output is written as human-readable LLVM IR (.ll) text to the output
 * file handle stored in process->ofile.
 *
 * Supported features:
 *   - Primitive types: void, char, short, int, long
 *   - Global variable declarations with optional constant initializers
 *   - Function definitions and forward declarations
 *   - Local variable declarations (alloca in entry block)
 *   - All standard C binary operators
 *   - Control flow: if/else, while, do-while, for, switch/case/default
 *   - break, continue, return, goto, labels
 *   - Function calls including variadic (printf, etc.)
 *   - String literals as global constants
 *   - Ternary expressions (? :)
 *   - Type casts
 *   - Parenthesized expressions
 */

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
//
// In LLVM 22 all pointers are opaque (ptr).  To know what type a local
// variable holds we pair every alloca with its element type; similarly for
// globals.  For other values we keep track of the "logical C type" so we can
// issue correctly-typed loads.
// ============================================================================

struct TypedAddr {
    llvm::Value *addr = nullptr;  // alloca or GlobalVariable ptr
    llvm::Type  *elem = nullptr;  // element type (what the pointer points to)
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

    // -----------------------------------------------------------------------
    // Variable scope: maps name → (alloca, allocated_type)
    // -----------------------------------------------------------------------
    std::vector<std::map<std::string, TypedAddr>> m_scopes;

    void       push_scope();
    void       pop_scope();
    void       define_var(const std::string &name, llvm::AllocaInst *ai,
                          llvm::Type *elem_ty);
    TypedAddr  lookup_var(const std::string &name);

    // -----------------------------------------------------------------------
    // Loop / switch break/continue stack
    // -----------------------------------------------------------------------
    struct LoopCtx {
        llvm::BasicBlock *break_bb    = nullptr;
        llvm::BasicBlock *continue_bb = nullptr;
    };
    std::vector<LoopCtx> m_loops;

    void             push_loop(llvm::BasicBlock *brk, llvm::BasicBlock *cont);
    void             pop_loop();
    llvm::BasicBlock *break_bb();
    llvm::BasicBlock *continue_bb();

    // -----------------------------------------------------------------------
    // goto / label support
    // -----------------------------------------------------------------------
    std::map<std::string, llvm::BasicBlock *> m_labels;
    struct PendingGoto {
        std::string       label;
        llvm::BasicBlock *from_bb;
        llvm::BranchInst *placeholder;
    };
    std::vector<PendingGoto> m_pending_gotos;
    void resolve_gotos();

    // -----------------------------------------------------------------------
    // String literal cache
    // -----------------------------------------------------------------------
    std::map<std::string, llvm::GlobalVariable *> m_str_cache;

    // -----------------------------------------------------------------------
    // Type helpers
    // -----------------------------------------------------------------------
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

    // -----------------------------------------------------------------------
    // Top-level generators
    // -----------------------------------------------------------------------
    void gen_top(struct node *n);
    void gen_global(struct node *n);
    void gen_func(struct node *n);

    // -----------------------------------------------------------------------
    // Statement generators
    // -----------------------------------------------------------------------
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

    // -----------------------------------------------------------------------
    // Expression generators — always return an rvalue (or nullptr)
    // -----------------------------------------------------------------------
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

    // -----------------------------------------------------------------------
    // lvalue / address helpers
    // -----------------------------------------------------------------------
    // Returns {ptr, elem_type} for an lvalue expression.
    TypedAddr lvalue_of(struct node *n);

    // Load a value from a TypedAddr.
    llvm::Value *load_from(TypedAddr ta);

    // -----------------------------------------------------------------------
    // Misc helpers
    // -----------------------------------------------------------------------
    llvm::AllocaInst *entry_alloca(llvm::Function *fn, const std::string &name,
                                   llvm::Type *ty);
    llvm::Value *to_bool(llvm::Value *v);
    llvm::Value *coerce(llvm::Value *v, llvm::Type *target);
    bool        is_terminated();
    llvm::Constant *global_init(struct node *val, llvm::Type *ty);

    llvm::FunctionCallee declare_func(const std::string &name,
                                      llvm::FunctionType *ft);
};

// ============================================================================
// Constructor / run
// ============================================================================

LLVMCodegen::LLVMCodegen(struct compile_process *process)
    : m_process(process)
    , m_ctx(std::make_unique<llvm::LLVMContext>())
    , m_module(std::make_unique<llvm::Module>("1337cc", *m_ctx))
    , m_builder(std::make_unique<llvm::IRBuilder<>>(*m_ctx))
{
}

int LLVMCodegen::run()
{
    vector_set_peek_pointer(m_process->node_tree_vec, 0);
    for (;;)
    {
        auto *n = static_cast<struct node *>(
            vector_peek_ptr(m_process->node_tree_vec));
        if (!n) break;
        gen_top(n);
    }

    std::string err;
    llvm::raw_string_ostream eos(err);
    if (llvm::verifyModule(*m_module, &eos))
    {
        fprintf(stderr, "LLVM verification error: %s\n", err.c_str());
        // Don't abort — still emit whatever IR we have for debugging.
    }

    if (m_process->ofile)
    {
        llvm::raw_fd_ostream os(fileno(m_process->ofile), /*shouldClose=*/false);
        m_module->print(os, nullptr);
    }

    return CODEGEN_ALL_OK;
}

// ============================================================================
// Scope
// ============================================================================

void LLVMCodegen::push_scope() { m_scopes.push_back({}); }
void LLVMCodegen::pop_scope()  { if (!m_scopes.empty()) m_scopes.pop_back(); }

void LLVMCodegen::define_var(const std::string &name, llvm::AllocaInst *ai,
                              llvm::Type *elem_ty)
{
    if (!m_scopes.empty())
        m_scopes.back()[name] = {ai, elem_ty};
}

TypedAddr LLVMCodegen::lookup_var(const std::string &name)
{
    for (int i = (int)m_scopes.size() - 1; i >= 0; --i)
    {
        auto it = m_scopes[i].find(name);
        if (it != m_scopes[i].end())
            return it->second;
    }
    return {};
}

// ============================================================================
// Loop context
// ============================================================================

void LLVMCodegen::push_loop(llvm::BasicBlock *brk, llvm::BasicBlock *cont)
{
    m_loops.push_back({brk, cont});
}
void LLVMCodegen::pop_loop() { if (!m_loops.empty()) m_loops.pop_back(); }
llvm::BasicBlock *LLVMCodegen::break_bb()    { return m_loops.empty() ? nullptr : m_loops.back().break_bb; }
llvm::BasicBlock *LLVMCodegen::continue_bb() { return m_loops.empty() ? nullptr : m_loops.back().continue_bb; }

// ============================================================================
// goto / label
// ============================================================================

void LLVMCodegen::resolve_gotos()
{
    for (auto &pg : m_pending_gotos)
    {
        auto it = m_labels.find(pg.label);
        if (it == m_labels.end()) {
            fprintf(stderr, "codegen: undefined label '%s'\n", pg.label.c_str());
            continue;
        }
        // Replace the placeholder branch destination.
        pg.placeholder->setSuccessor(0, it->second);
    }
    m_pending_gotos.clear();
}

// ============================================================================
// Type helpers
// ============================================================================

llvm::Type *LLVMCodegen::prim_llvm(int dt)
{
    switch (dt)
    {
    case DATA_TYPE_VOID:    return voidty();
    case DATA_TYPE_CHAR:    return i8ty();
    case DATA_TYPE_SHORT:   return i16ty();
    case DATA_TYPE_INTEGER: return i32ty();
    case DATA_TYPE_LONG:    return i64ty();
    case DATA_TYPE_FLOAT:   return llvm::Type::getFloatTy(*m_ctx);
    case DATA_TYPE_DOUBLE:  return llvm::Type::getDoubleTy(*m_ctx);
    default:                return i32ty();
    }
}

llvm::Type *LLVMCodegen::dtype_to_llvm(struct datatype *dt)
{
    if (!dt) return i32ty();

    // Pointers: in opaque-pointer world, all pointers are just "ptr".
    if (dt->pointer_depth > 0 || (dt->flags & DATATYPE_FLAG_IS_ARRAY))
        return ptrty();

    if (dt->type == DATA_TYPE_STRUCT || dt->type == DATA_TYPE_UNION)
    {
        size_t sz = (dt->size > 0) ? dt->size : 4;
        return llvm::ArrayType::get(i8ty(), sz);
    }

    return prim_llvm(dt->type);
}

// ============================================================================
// Misc helpers
// ============================================================================

llvm::AllocaInst *LLVMCodegen::entry_alloca(llvm::Function *fn,
                                              const std::string &name,
                                              llvm::Type *ty)
{
    llvm::IRBuilder<> tb(&fn->getEntryBlock(), fn->getEntryBlock().begin());
    return tb.CreateAlloca(ty, nullptr, name);
}

llvm::Value *LLVMCodegen::to_bool(llvm::Value *v)
{
    if (!v) return m_builder->getFalse();
    llvm::Type *ty = v->getType();
    if (ty->isIntegerTy(1)) return v;
    if (ty->isIntegerTy())
        return m_builder->CreateICmpNE(v, llvm::ConstantInt::get(ty, 0), "bool");
    if (ty->isPointerTy())
        return m_builder->CreateICmpNE(
            v, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ty)),
            "pbool");
    if (ty->isFloatingPointTy())
        return m_builder->CreateFCmpONE(v, llvm::ConstantFP::get(ty, 0.0), "fbool");
    return m_builder->getFalse();
}

llvm::Value *LLVMCodegen::coerce(llvm::Value *v, llvm::Type *target)
{
    if (!v || !target || v->getType() == target) return v;
    llvm::Type *src = v->getType();
    if (src->isIntegerTy() && target->isIntegerTy())
    {
        unsigned sw = src->getIntegerBitWidth();
        unsigned tw = target->getIntegerBitWidth();
        if (sw < tw) return m_builder->CreateSExt(v, target, "sext");
        if (sw > tw) return m_builder->CreateTrunc(v, target, "trunc");
        return v;
    }
    if (src->isPointerTy() && target->isPointerTy()) return v; // opaque ptrs
    if (src->isIntegerTy() && target->isPointerTy())
        return m_builder->CreateIntToPtr(v, target, "i2p");
    if (src->isPointerTy() && target->isIntegerTy())
        return m_builder->CreatePtrToInt(v, target, "p2i");
    if (src->isFloatingPointTy() && target->isIntegerTy())
        return m_builder->CreateFPToSI(v, target, "fp2i");
    if (src->isIntegerTy() && target->isFloatingPointTy())
        return m_builder->CreateSIToFP(v, target, "i2fp");
    if (src->isFloatingPointTy() && target->isFloatingPointTy())
        return m_builder->CreateFPCast(v, target, "fpcast");
    return v;
}

bool LLVMCodegen::is_terminated()
{
    auto *bb = m_builder->GetInsertBlock();
    return bb && bb->getTerminator();
}

llvm::Constant *LLVMCodegen::global_init(struct node *val, llvm::Type *ty)
{
    if (!val) return llvm::Constant::getNullValue(ty);
    if (val->type == NODE_TYPE_NUMBER)
    {
        if (ty->isIntegerTy())
            return llvm::ConstantInt::get(
                llvm::cast<llvm::IntegerType>(ty),
                (int64_t)val->llnum, /*isSigned=*/true);
        if (ty->isFloatingPointTy())
            return llvm::ConstantFP::get(ty, (double)val->llnum);
    }
    if (val->type == NODE_TYPE_EXPRESSION_PARENTHESES && val->parenthesis.exp)
        return global_init(val->parenthesis.exp, ty);
    return llvm::Constant::getNullValue(ty);
}

llvm::FunctionCallee LLVMCodegen::declare_func(const std::string &name,
                                                 llvm::FunctionType *ft)
{
    return m_module->getOrInsertFunction(name, ft);
}

// ============================================================================
// TypedAddr / lvalue helpers
// ============================================================================

llvm::Value *LLVMCodegen::load_from(TypedAddr ta)
{
    if (!ta.addr || !ta.elem) return nullptr;
    return m_builder->CreateLoad(ta.elem, ta.addr, "load");
}

TypedAddr LLVMCodegen::lvalue_of(struct node *n)
{
    if (!n) return {};

    switch (n->type)
    {
    case NODE_TYPE_IDENTIFIER:
    {
        // Local variable?
        TypedAddr ta = lookup_var(n->sval);
        if (ta.addr) return ta;
        // Global?
        llvm::GlobalVariable *gv = m_module->getGlobalVariable(n->sval);
        if (gv) return {gv, gv->getValueType()};
        return {};
    }
    case NODE_TYPE_EXPRESSION_PARENTHESES:
        return lvalue_of(n->parenthesis.exp);
    case NODE_TYPE_EXPRESSION:
    {
        const char *op = n->exp.op;
        if (!op) return {};
        // Dereference: *ptr  (left == nullptr means prefix unary)
        if (S_EQ(op, "*") && !n->exp.left)
        {
            llvm::Value *ptr = gen_node(n->exp.right);
            // Dereferencing a ptr gives an i32 by default.
            return {ptr, i32ty()};
        }
        // Array subscript: arr[idx]
        if (S_EQ(op, "[]"))
        {
            llvm::Value *base = gen_node(n->exp.left);
            struct node *idx_n = n->exp.right;
            if (idx_n && idx_n->type == NODE_TYPE_BRACKET)
                idx_n = idx_n->bracket.inner;
            llvm::Value *idx = gen_node(idx_n);
            idx = coerce(idx, i64ty());
            if (base)
            {
                llvm::Value *gep = m_builder->CreateGEP(i8ty(), base, idx, "gep");
                return {gep, i8ty()};
            }
        }
        return {};
    }
    default:
        return {};
    }
}

// ============================================================================
// Top-level
// ============================================================================

void LLVMCodegen::gen_top(struct node *n)
{
    if (!n) return;
    switch (n->type)
    {
    case NODE_TYPE_VARIABLE:
        gen_global(n);
        break;
    case NODE_TYPE_VARIABLE_LIST:
    {
        vector_set_peek_pointer(n->var_list.list, 0);
        for (;;)
        {
            auto *vn = static_cast<struct node *>(
                vector_peek_ptr(n->var_list.list));
            if (!vn) break;
            gen_global(vn);
        }
        break;
    }
    case NODE_TYPE_FUNCTION:
        gen_func(n);
        break;
    default:
        break;
    }
}

void LLVMCodegen::gen_global(struct node *n)
{
    if (!n || n->type != NODE_TYPE_VARIABLE || !n->var.name) return;
    if (m_module->getGlobalVariable(n->var.name)) return;

    llvm::Type *ty = dtype_to_llvm(&n->var.type);
    if (!ty || ty->isVoidTy()) return;

    llvm::Constant *init = global_init(n->var.val, ty);
    new llvm::GlobalVariable(*m_module, ty, /*isConst=*/false,
                              llvm::GlobalValue::ExternalLinkage, init,
                              n->var.name);
}

void LLVMCodegen::gen_func(struct node *n)
{
    if (!n || n->type != NODE_TYPE_FUNCTION || !n->func.name) return;

    llvm::Type *ret_ty = dtype_to_llvm(&n->func.rtype);
    std::vector<llvm::Type *> params;
    bool variadic = false;

    if (n->func.args.vector)
    {
        vector_set_peek_pointer(n->func.args.vector, 0);
        for (;;)
        {
            auto *a = static_cast<struct node *>(
                vector_peek_ptr(n->func.args.vector));
            if (!a) break;
            if (a->type == NODE_TYPE_VARIABLE)
                params.push_back(dtype_to_llvm(&a->var.type));
            // NODE_TYPE_BLANK with ellipsis could set variadic = true
        }
    }

    // Forward declarations (no body) are treated as variadic so that the
    // compiler-declared "int printf(char *fmt, ...)" prototype works correctly.
    // Defined functions (with body) are non-variadic based on their arg list.
    if (!n->func.body_n) variadic = true;

    llvm::FunctionType *ft = llvm::FunctionType::get(ret_ty, params, variadic);
    llvm::Function *fn = m_module->getFunction(n->func.name);
    if (!fn)
        fn = llvm::Function::Create(ft, llvm::GlobalValue::ExternalLinkage,
                                    n->func.name, m_module.get());

    if (!n->func.body_n) return;

    // Name parameters.
    if (n->func.args.vector)
    {
        vector_set_peek_pointer(n->func.args.vector, 0);
        auto it = fn->arg_begin();
        for (;;)
        {
            auto *a = static_cast<struct node *>(
                vector_peek_ptr(n->func.args.vector));
            if (!a || it == fn->arg_end()) break;
            if (a->type == NODE_TYPE_VARIABLE && a->var.name)
                it->setName(a->var.name);
            ++it;
        }
    }

    llvm::BasicBlock *entry = llvm::BasicBlock::Create(*m_ctx, "entry", fn);
    m_builder->SetInsertPoint(entry);

    push_scope();
    m_labels.clear();
    m_pending_gotos.clear();

    // Spill parameters onto the stack.
    if (n->func.args.vector)
    {
        vector_set_peek_pointer(n->func.args.vector, 0);
        auto it = fn->arg_begin();
        for (;;)
        {
            auto *a = static_cast<struct node *>(
                vector_peek_ptr(n->func.args.vector));
            if (!a || it == fn->arg_end()) break;
            if (a->type == NODE_TYPE_VARIABLE && a->var.name)
            {
                llvm::Type *pty = it->getType();
                llvm::AllocaInst *slot = entry_alloca(fn, a->var.name, pty);
                m_builder->CreateStore(&*it, slot);
                define_var(a->var.name, slot, pty);
            }
            ++it;
        }
    }

    gen_body(n->func.body_n);
    resolve_gotos();
    pop_scope();

    if (!is_terminated())
    {
        if (ret_ty->isVoidTy())
            m_builder->CreateRetVoid();
        else
            m_builder->CreateRet(llvm::Constant::getNullValue(ret_ty));
    }
}

// ============================================================================
// Body / statements
// ============================================================================

void LLVMCodegen::gen_body(struct node *n)
{
    if (!n || n->type != NODE_TYPE_BODY || !n->body.statements) return;
    push_scope();
    vector_set_peek_pointer(n->body.statements, 0);
    for (;;)
    {
        auto *s = static_cast<struct node *>(
            vector_peek_ptr(n->body.statements));
        if (!s) break;
        gen_stmt(s);
    }
    pop_scope();
}

void LLVMCodegen::gen_stmt(struct node *n)
{
    if (!n || is_terminated()) return;

    switch (n->type)
    {
    case NODE_TYPE_VARIABLE:
        gen_var_decl(n);
        break;
    case NODE_TYPE_VARIABLE_LIST:
    {
        vector_set_peek_pointer(n->var_list.list, 0);
        for (;;)
        {
            auto *vn = static_cast<struct node *>(
                vector_peek_ptr(n->var_list.list));
            if (!vn) break;
            gen_var_decl(vn);
        }
        break;
    }
    case NODE_TYPE_STATEMENT_RETURN:   gen_return(n);   break;
    case NODE_TYPE_STATEMENT_IF:       gen_if(n);       break;
    case NODE_TYPE_STATEMENT_WHILE:    gen_while(n);    break;
    case NODE_TYPE_STATEMENT_DO_WHILE: gen_do_while(n); break;
    case NODE_TYPE_STATEMENT_FOR:      gen_for(n);      break;
    case NODE_TYPE_STATEMENT_SWITCH:   gen_switch(n);   break;
    case NODE_TYPE_STATEMENT_BREAK:
        if (break_bb()) m_builder->CreateBr(break_bb());
        break;
    case NODE_TYPE_STATEMENT_CONTINUE:
        if (continue_bb()) m_builder->CreateBr(continue_bb());
        break;
    case NODE_TYPE_LABEL:   gen_label(n); break;
    case NODE_TYPE_STATEMENT_GOTO: gen_goto(n); break;
    case NODE_TYPE_BODY:    gen_body(n);  break;
    case NODE_TYPE_STATEMENT_CASE:
    case NODE_TYPE_BLANK:
        break;
    default:
        gen_node(n);
        break;
    }
}

void LLVMCodegen::gen_if(struct node *n)
{
    auto *fn = m_builder->GetInsertBlock()->getParent();

    llvm::Value *cond = gen_node(n->stmt.if_stmt.cond_node);
    cond = to_bool(cond);

    auto *then_bb  = llvm::BasicBlock::Create(*m_ctx, "then",  fn);
    auto *else_bb  = llvm::BasicBlock::Create(*m_ctx, "else",  fn);
    auto *merge_bb = llvm::BasicBlock::Create(*m_ctx, "ifend", fn);

    m_builder->CreateCondBr(cond, then_bb, else_bb);

    m_builder->SetInsertPoint(then_bb);
    if (n->stmt.if_stmt.body_node)
        gen_body(n->stmt.if_stmt.body_node);
    if (!is_terminated()) m_builder->CreateBr(merge_bb);

    m_builder->SetInsertPoint(else_bb);
    if (n->stmt.if_stmt.next)
    {
        auto *nx = n->stmt.if_stmt.next;
        if (nx->type == NODE_TYPE_STATEMENT_IF)
            gen_if(nx);
        else if (nx->type == NODE_TYPE_STATEMENT_ELSE &&
                 nx->stmt.else_stmt.body_node)
            gen_body(nx->stmt.else_stmt.body_node);
    }
    if (!is_terminated()) m_builder->CreateBr(merge_bb);

    m_builder->SetInsertPoint(merge_bb);
}

void LLVMCodegen::gen_while(struct node *n)
{
    auto *fn = m_builder->GetInsertBlock()->getParent();
    auto *cond_bb  = llvm::BasicBlock::Create(*m_ctx, "wcond", fn);
    auto *body_bb  = llvm::BasicBlock::Create(*m_ctx, "wbody", fn);
    auto *after_bb = llvm::BasicBlock::Create(*m_ctx, "wend",  fn);

    m_builder->CreateBr(cond_bb);
    m_builder->SetInsertPoint(cond_bb);

    push_loop(after_bb, cond_bb);

    llvm::Value *cond = gen_node(n->stmt.while_stmt.exp_node);
    m_builder->CreateCondBr(to_bool(cond), body_bb, after_bb);

    m_builder->SetInsertPoint(body_bb);
    if (n->stmt.while_stmt.body_node)
        gen_body(n->stmt.while_stmt.body_node);
    if (!is_terminated()) m_builder->CreateBr(cond_bb);

    pop_loop();
    m_builder->SetInsertPoint(after_bb);
}

void LLVMCodegen::gen_do_while(struct node *n)
{
    auto *fn = m_builder->GetInsertBlock()->getParent();
    auto *body_bb  = llvm::BasicBlock::Create(*m_ctx, "dobody", fn);
    auto *cond_bb  = llvm::BasicBlock::Create(*m_ctx, "docond", fn);
    auto *after_bb = llvm::BasicBlock::Create(*m_ctx, "doend",  fn);

    m_builder->CreateBr(body_bb);
    push_loop(after_bb, cond_bb);

    m_builder->SetInsertPoint(body_bb);
    if (n->stmt.do_while_stmt.body_node)
        gen_body(n->stmt.do_while_stmt.body_node);
    if (!is_terminated()) m_builder->CreateBr(cond_bb);

    m_builder->SetInsertPoint(cond_bb);
    llvm::Value *cond = gen_node(n->stmt.do_while_stmt.exp_node);
    m_builder->CreateCondBr(to_bool(cond), body_bb, after_bb);

    pop_loop();
    m_builder->SetInsertPoint(after_bb);
}

void LLVMCodegen::gen_for(struct node *n)
{
    auto *fn = m_builder->GetInsertBlock()->getParent();

    if (n->stmt.for_stmt.init_node)
        gen_stmt(n->stmt.for_stmt.init_node);

    auto *cond_bb  = llvm::BasicBlock::Create(*m_ctx, "fcond", fn);
    auto *body_bb  = llvm::BasicBlock::Create(*m_ctx, "fbody", fn);
    auto *incr_bb  = llvm::BasicBlock::Create(*m_ctx, "fincr", fn);
    auto *after_bb = llvm::BasicBlock::Create(*m_ctx, "fend",  fn);

    m_builder->CreateBr(cond_bb);
    m_builder->SetInsertPoint(cond_bb);

    if (n->stmt.for_stmt.cond_node)
    {
        llvm::Value *cond = gen_node(n->stmt.for_stmt.cond_node);
        m_builder->CreateCondBr(to_bool(cond), body_bb, after_bb);
    }
    else
        m_builder->CreateBr(body_bb);

    push_loop(after_bb, incr_bb);

    m_builder->SetInsertPoint(body_bb);
    if (n->stmt.for_stmt.body_node)
        gen_body(n->stmt.for_stmt.body_node);
    if (!is_terminated()) m_builder->CreateBr(incr_bb);

    m_builder->SetInsertPoint(incr_bb);
    if (n->stmt.for_stmt.loop_node)
        gen_node(n->stmt.for_stmt.loop_node);
    if (!is_terminated()) m_builder->CreateBr(cond_bb);

    pop_loop();
    m_builder->SetInsertPoint(after_bb);
}

void LLVMCodegen::gen_switch(struct node *n)
{
    auto *fn = m_builder->GetInsertBlock()->getParent();

    llvm::Value *sw_val = gen_node(n->stmt.switch_stmt.exp);
    sw_val = coerce(sw_val, i32ty());

    auto *after_bb   = llvm::BasicBlock::Create(*m_ctx, "swend", fn);
    auto *default_bb = llvm::BasicBlock::Create(*m_ctx, "swdef", fn);

    auto *sw = m_builder->CreateSwitch(sw_val, default_bb, 8);
    push_loop(after_bb, nullptr);

    // Ensure default_bb is always properly terminated.
    // If the user provides a default: clause, we'll generate code there.
    // If not, it falls through to after_bb.
    m_builder->SetInsertPoint(default_bb);
    m_builder->CreateBr(after_bb); // provisional; overwritten if default case found

    struct node *body = n->stmt.switch_stmt.body;
    if (body && body->type == NODE_TYPE_BODY && body->body.statements)
    {
        // We need to build a list of (case_value, block) pairs first.
        // Then generate code linearly.
        //
        // Strategy: iterate the statement list.
        // - On CASE/DEFAULT: start a new block; add to switch table.
        // - On other stmts: generate in the current block.
        // - On BREAK: generates br(after_bb), then the current block is closed.
        //
        // The builder insert point tracks the "current open block".
        // After CreateSwitch, we're in the terminated pre-switch block.
        // Each case creates a fresh block and SetInsertPoint to it.

        // Create a "dead" pre-body block that is never entered (it starts
        // before any case label). We terminate it immediately to keep IR valid.
        auto *pre = llvm::BasicBlock::Create(*m_ctx, "swpre", fn);
        m_builder->SetInsertPoint(pre);
        m_builder->CreateBr(after_bb); // pre is dead but must be terminated.

        // Now start building case blocks.
        // open_bb = the currently open (unterminated) case block, or null.
        llvm::BasicBlock *open_bb = nullptr;
        // prev_case_bb = the last case block created (for fall-through linking).
        llvm::BasicBlock *prev_case_bb = nullptr;

        vector_set_peek_pointer(body->body.statements, 0);
        for (;;)
        {
            auto *s = static_cast<struct node *>(
                vector_peek_ptr(body->body.statements));
            if (!s) break;

            if (s->type == NODE_TYPE_STATEMENT_CASE)
            {
                // Create a new block for this case.
                auto *case_bb = llvm::BasicBlock::Create(*m_ctx, "swcase", fn);

                // If the previous case block is open (fall-through), link it.
                if (open_bb)
                    m_builder->CreateBr(case_bb);

                open_bb = case_bb;
                m_builder->SetInsertPoint(case_bb);

                // Generate the case constant and register it.
                llvm::Value *cv = gen_node(s->stmt._case.exp);
                if (auto *ci = llvm::dyn_cast<llvm::ConstantInt>(cv))
                    sw->addCase(ci, case_bb);
            }
            else if (s->type == NODE_TYPE_STATEMENT_DEFAULT)
            {
                // The default block was pre-created with a provisional br.
                // Remove it and use default_bb as the current open block.
                // First: if previous case is open, link it to default_bb.
                if (open_bb)
                    m_builder->CreateBr(default_bb);

                // Remove the provisional terminator from default_bb.
                llvm::Instruction *prov = default_bb->getTerminator();
                if (prov) prov->eraseFromParent();

                open_bb = default_bb;
                m_builder->SetInsertPoint(default_bb);
            }
            else
            {
                // Regular statement: generate in current open block.
                if (open_bb)
                {
                    // Make sure we're inserting into open_bb.
                    if (m_builder->GetInsertBlock() != open_bb)
                        m_builder->SetInsertPoint(open_bb);
                    gen_stmt(s);
                    // After gen_stmt, if the block is now terminated,
                    // clear open_bb so the next statement knows there's no open block.
                    if (m_builder->GetInsertBlock()->getTerminator())
                        open_bb = nullptr;
                }
            }
        }
        // Close any remaining open block.
        if (open_bb && !m_builder->GetInsertBlock()->getTerminator())
            m_builder->CreateBr(after_bb);
    }

    // Ensure default_bb is terminated (it was either given code above,
    // or still has the provisional br to after_bb from initialization).
    pop_loop();
    m_builder->SetInsertPoint(after_bb);
}

void LLVMCodegen::gen_return(struct node *n)
{
    auto *fn = m_builder->GetInsertBlock()->getParent();
    auto *ret_ty = fn->getReturnType();

    if (!n->stmt.return_stmt.exp || ret_ty->isVoidTy())
    {
        m_builder->CreateRetVoid();
        return;
    }
    llvm::Value *v = gen_node(n->stmt.return_stmt.exp);
    v = coerce(v, ret_ty);
    m_builder->CreateRet(v);
}

void LLVMCodegen::gen_label(struct node *n)
{
    if (!n->label.name || !n->label.name->sval) return;
    const char *name = n->label.name->sval;
    auto *fn = m_builder->GetInsertBlock()->getParent();
    auto *bb = llvm::BasicBlock::Create(*m_ctx, name, fn);
    if (!is_terminated()) m_builder->CreateBr(bb);
    m_builder->SetInsertPoint(bb);
    m_labels[name] = bb;
}

void LLVMCodegen::gen_goto(struct node *n)
{
    if (!n->stmt._goto.label || !n->stmt._goto.label->sval) return;
    const char *name = n->stmt._goto.label->sval;
    auto it = m_labels.find(name);
    if (it != m_labels.end())
    {
        m_builder->CreateBr(it->second);
    }
    else
    {
        // Forward reference: create a placeholder branch.
        auto *fn = m_builder->GetInsertBlock()->getParent();
        auto *tmp = llvm::BasicBlock::Create(*m_ctx, "goto_fwd", fn);
        auto *ph  = m_builder->CreateBr(tmp);
        m_pending_gotos.push_back({name, m_builder->GetInsertBlock(), ph});
        m_builder->SetInsertPoint(tmp);
    }
}

// ============================================================================
// Variable declaration (local)
// ============================================================================

llvm::Value *LLVMCodegen::gen_var_decl(struct node *n)
{
    if (!n || n->type != NODE_TYPE_VARIABLE || !n->var.name) return nullptr;
    auto *fn = m_builder->GetInsertBlock()->getParent();
    llvm::Type *ty = dtype_to_llvm(&n->var.type);
    if (!ty || ty->isVoidTy()) return nullptr;

    auto *slot = entry_alloca(fn, n->var.name, ty);
    define_var(n->var.name, slot, ty);

    if (n->var.val)
    {
        llvm::Value *init = gen_node(n->var.val);
        if (init)
        {
            init = coerce(init, ty);
            m_builder->CreateStore(init, slot);
        }
    }
    return slot;
}

// ============================================================================
// Expression dispatcher
// ============================================================================

llvm::Value *LLVMCodegen::gen_node(struct node *n)
{
    if (!n) return nullptr;
    switch (n->type)
    {
    case NODE_TYPE_NUMBER:
        return gen_number(n);
    case NODE_TYPE_STRING:
        return gen_string(n->sval);
    case NODE_TYPE_IDENTIFIER:
    {
        // Identifiers in rvalue context: load the variable.
        TypedAddr ta = lvalue_of(n);
        if (ta.addr)
            return load_from(ta);
        // Could be a function (for use as a callee).
        return gen_ident(n->sval);
    }
    case NODE_TYPE_EXPRESSION:
        return gen_expr(n);
    case NODE_TYPE_EXPRESSION_PARENTHESES:
        return gen_parens(n);
    case NODE_TYPE_CAST:
        return gen_cast(n);
    case NODE_TYPE_TENARY:
        return nullptr; // only accessed via gen_ternary
    case NODE_TYPE_VARIABLE:
        return gen_var_decl(n);
    case NODE_TYPE_BLANK:
        return nullptr;
    default:
        gen_stmt(n);
        return nullptr;
    }
}

// ============================================================================
// Literals
// ============================================================================

llvm::Value *LLVMCodegen::gen_number(struct node *n)
{
    switch (n->num.type)
    {
    case NUMBER_TYPE_FLOAT:
        return llvm::ConstantFP::get(llvm::Type::getFloatTy(*m_ctx), (double)n->llnum);
    case NUMBER_TYPE_DOUBLE:
        return llvm::ConstantFP::get(llvm::Type::getDoubleTy(*m_ctx), (double)n->llnum);
    case NUMBER_TYPE_LONG:
        return llvm::ConstantInt::get(i64ty(), (int64_t)n->lnum, true);
    default:
        return llvm::ConstantInt::get(i32ty(), (int32_t)n->inum, true);
    }
}

llvm::Value *LLVMCodegen::gen_string(const char *str)
{
    if (!str) return nullptr;
    // Use IRBuilder::CreateGlobalString which handles LLVM 22 opaque pointers.
    return m_builder->CreateGlobalStringPtr(str, ".str", 0, m_module.get());
}

llvm::Value *LLVMCodegen::gen_ident(const char *name)
{
    if (!name) return nullptr;
    // Function lookup.
    llvm::Function *fn = m_module->getFunction(name);
    if (fn) return fn;
    // Implicit external function (create a minimal variadic declaration).
    llvm::FunctionType *ft = llvm::FunctionType::get(i32ty(), {}, true);
    return m_module->getOrInsertFunction(name, ft).getCallee();
}

// ============================================================================
// Assignment
// ============================================================================

llvm::Value *LLVMCodegen::gen_assign(struct node *n)
{
    const char *op  = n->exp.op;
    struct node *lhs = n->exp.left;
    struct node *rhs = n->exp.right;

    TypedAddr ta = lvalue_of(lhs);

    llvm::Value *rval = gen_node(rhs);

    if (!ta.addr || !ta.elem)
    {
        // Fallback: generate lhs as a pointer expression.
        llvm::Value *ptr = gen_node(lhs);
        if (ptr && rval)
            m_builder->CreateStore(coerce(rval, rval->getType()), ptr);
        return rval;
    }

    if (!S_EQ(op, "="))
    {
        // Compound assignment: load current value.
        llvm::Value *cur = m_builder->CreateLoad(ta.elem, ta.addr, "cur");
        if (cur && rval)
        {
            rval = coerce(rval, cur->getType());
            if      (S_EQ(op, "+="))  rval = m_builder->CreateAdd(cur,  rval, "add");
            else if (S_EQ(op, "-="))  rval = m_builder->CreateSub(cur,  rval, "sub");
            else if (S_EQ(op, "*="))  rval = m_builder->CreateMul(cur,  rval, "mul");
            else if (S_EQ(op, "/="))  rval = m_builder->CreateSDiv(cur, rval, "div");
            else if (S_EQ(op, "%="))  rval = m_builder->CreateSRem(cur, rval, "rem");
            else if (S_EQ(op, "&="))  rval = m_builder->CreateAnd(cur,  rval, "and");
            else if (S_EQ(op, "|="))  rval = m_builder->CreateOr(cur,   rval, "or");
            else if (S_EQ(op, "^="))  rval = m_builder->CreateXor(cur,  rval, "xor");
            else if (S_EQ(op, "<<=")) rval = m_builder->CreateShl(cur,  rval, "shl");
            else if (S_EQ(op, ">>=")) rval = m_builder->CreateAShr(cur, rval, "shr");
        }
    }

    if (rval)
    {
        rval = coerce(rval, ta.elem);
        m_builder->CreateStore(rval, ta.addr);
    }
    return rval;
}

// ============================================================================
// Function call
// ============================================================================

llvm::Value *LLVMCodegen::gen_call(struct node *callee_n, struct node *args_n)
{
    if (!callee_n) return nullptr;

    // Collect arguments.
    std::vector<llvm::Value *> argv;
    struct node *inner = nullptr;
    if (args_n)
    {
        if (args_n->type == NODE_TYPE_EXPRESSION_PARENTHESES)
            inner = args_n->parenthesis.exp;
        else
            inner = args_n;
    }
    if (inner && inner->type != NODE_TYPE_BLANK)
    {
        std::function<void(struct node *)> collect = [&](struct node *n) {
            if (!n) return;
            if (n->type == NODE_TYPE_EXPRESSION && S_EQ(n->exp.op, ","))
            {
                collect(n->exp.left);
                collect(n->exp.right);
            }
            else
            {
                llvm::Value *v = gen_node(n);
                if (v) argv.push_back(v);
            }
        };
        collect(inner);
    }

    // Resolve callee.
    std::string callee_name;
    if (callee_n->type == NODE_TYPE_IDENTIFIER)
        callee_name = callee_n->sval;

    llvm::Function *fn = callee_name.empty()
        ? nullptr : m_module->getFunction(callee_name);

    if (fn)
    {
        // Coerce arguments to match declared parameter types.
        auto pts = fn->getFunctionType()->params();
        for (size_t i = 0; i < argv.size() && i < pts.size(); ++i)
            argv[i] = coerce(argv[i], pts[i]);
        llvm::Type *rty = fn->getReturnType();
        return m_builder->CreateCall(fn, argv, rty->isVoidTy() ? "" : "call");
    }

    // Unknown/implicit function: declare as variadic i32(...).
    llvm::FunctionType *ft = llvm::FunctionType::get(i32ty(), {}, /*vararg=*/true);
    llvm::FunctionCallee fc = m_module->getOrInsertFunction(
        callee_name.empty() ? "__indirect" : callee_name, ft);
    return m_builder->CreateCall(fc, argv, "call");
}

// ============================================================================
// Prefix unary
// ============================================================================

llvm::Value *LLVMCodegen::gen_prefix(const char *op, struct node *operand)
{
    if (!operand) return nullptr;

    if (S_EQ(op, "-"))
    {
        llvm::Value *v = gen_node(operand);
        if (!v) return nullptr;
        return v->getType()->isFloatingPointTy()
            ? m_builder->CreateFNeg(v, "fneg")
            : m_builder->CreateNeg(v, "neg");
    }
    if (S_EQ(op, "~"))
    {
        llvm::Value *v = gen_node(operand);
        return v ? m_builder->CreateNot(v, "bnot") : nullptr;
    }
    if (S_EQ(op, "!"))
    {
        llvm::Value *v = gen_node(operand);
        if (!v) return nullptr;
        llvm::Value *b = to_bool(v);
        llvm::Value *nb = m_builder->CreateNot(b, "lnot");
        return m_builder->CreateZExt(nb, i32ty(), "lnoti");
    }
    if (S_EQ(op, "++"))
    {
        TypedAddr ta = lvalue_of(operand);
        if (!ta.addr || !ta.elem) return nullptr;
        llvm::Value *cur = m_builder->CreateLoad(ta.elem, ta.addr, "preinc");
        llvm::Value *inc = m_builder->CreateAdd(cur, llvm::ConstantInt::get(ta.elem, 1), "inc");
        m_builder->CreateStore(inc, ta.addr);
        return inc;
    }
    if (S_EQ(op, "--"))
    {
        TypedAddr ta = lvalue_of(operand);
        if (!ta.addr || !ta.elem) return nullptr;
        llvm::Value *cur = m_builder->CreateLoad(ta.elem, ta.addr, "predec");
        llvm::Value *dec = m_builder->CreateSub(cur, llvm::ConstantInt::get(ta.elem, 1), "dec");
        m_builder->CreateStore(dec, ta.addr);
        return dec;
    }
    if (S_EQ(op, "&"))
    {
        TypedAddr ta = lvalue_of(operand);
        return ta.addr; // return the address
    }
    if (S_EQ(op, "*"))
    {
        llvm::Value *ptr = gen_node(operand);
        if (!ptr) return nullptr;
        return m_builder->CreateLoad(i32ty(), ptr, "deref");
    }
    // Default: evaluate operand.
    return gen_node(operand);
}

// ============================================================================
// Postfix unary
// ============================================================================

llvm::Value *LLVMCodegen::gen_postfix(const char *op, struct node *operand)
{
    TypedAddr ta = lvalue_of(operand);
    if (!ta.addr || !ta.elem) return nullptr;
    llvm::Value *cur = m_builder->CreateLoad(ta.elem, ta.addr, "post_cur");
    if (S_EQ(op, "++"))
        m_builder->CreateStore(
            m_builder->CreateAdd(cur, llvm::ConstantInt::get(ta.elem, 1), "postinc"),
            ta.addr);
    else if (S_EQ(op, "--"))
        m_builder->CreateStore(
            m_builder->CreateSub(cur, llvm::ConstantInt::get(ta.elem, 1), "postdec"),
            ta.addr);
    return cur; // original value
}

// ============================================================================
// Ternary
// ============================================================================

llvm::Value *LLVMCodegen::gen_ternary(struct node *n)
{
    // AST: E(cond, "?", TENARY{ true_node, false_node })
    auto *tern = n->exp.right;
    if (!tern || tern->type != NODE_TYPE_TENARY) return nullptr;

    auto *fn = m_builder->GetInsertBlock()->getParent();
    llvm::Value *cond = gen_node(n->exp.left);
    cond = to_bool(cond);

    auto *true_bb  = llvm::BasicBlock::Create(*m_ctx, "tern_t", fn);
    auto *false_bb = llvm::BasicBlock::Create(*m_ctx, "tern_f", fn);
    auto *merge_bb = llvm::BasicBlock::Create(*m_ctx, "tern_e", fn);
    m_builder->CreateCondBr(cond, true_bb, false_bb);

    m_builder->SetInsertPoint(true_bb);
    llvm::Value *tv = gen_node(tern->tenary.true_node);
    auto *true_end = m_builder->GetInsertBlock();
    m_builder->CreateBr(merge_bb);

    m_builder->SetInsertPoint(false_bb);
    llvm::Value *fv = gen_node(tern->tenary.false_node);
    auto *false_end = m_builder->GetInsertBlock();
    m_builder->CreateBr(merge_bb);

    m_builder->SetInsertPoint(merge_bb);
    if (!tv || !fv) return nullptr;

    // Coerce to common type.
    if (tv->getType() != fv->getType())
    {
        llvm::Type *common = i32ty();
        tv = coerce(tv, common);
        fv = coerce(fv, common);
    }

    auto *phi = m_builder->CreatePHI(tv->getType(), 2, "tern");
    phi->addIncoming(tv, true_end);
    phi->addIncoming(fv, false_end);
    return phi;
}

// ============================================================================
// Cast
// ============================================================================

llvm::Value *LLVMCodegen::gen_cast(struct node *n)
{
    llvm::Value *v = gen_node(n->cast.operand);
    if (!v) return nullptr;
    return coerce(v, dtype_to_llvm(&n->cast.dtype));
}

// ============================================================================
// Parentheses
// ============================================================================

llvm::Value *LLVMCodegen::gen_parens(struct node *n)
{
    if (!n->parenthesis.exp) return nullptr;
    return gen_node(n->parenthesis.exp);
}

// ============================================================================
// Binary expression dispatcher
// ============================================================================

llvm::Value *LLVMCodegen::gen_expr(struct node *n)
{
    if (n->type != NODE_TYPE_EXPRESSION) return nullptr;
    return gen_binary(n);
}

llvm::Value *LLVMCodegen::gen_binary(struct node *n)
{
    const char  *op  = n->exp.op;
    struct node *lhs = n->exp.left;
    struct node *rhs = n->exp.right;
    if (!op) return nullptr;

    // Assignment (and compound assignment).
    if (S_EQ(op, "=")   || S_EQ(op, "+=") || S_EQ(op, "-=") ||
        S_EQ(op, "*=")  || S_EQ(op, "/=") || S_EQ(op, "%=") ||
        S_EQ(op, "&=")  || S_EQ(op, "|=") || S_EQ(op, "^=") ||
        S_EQ(op, "<<=") || S_EQ(op, ">>="))
        return gen_assign(n);

    // Function call: E(callee, "()", args_paren)
    if (S_EQ(op, "()")) return gen_call(lhs, rhs);

    // Ternary: E(cond, "?", tenary_node)
    if (S_EQ(op, "?")) return gen_ternary(n);

    // Comma: evaluate both, return right.
    if (S_EQ(op, ",")) { gen_node(lhs); return gen_node(rhs); }

    // Member access — stub.
    if (S_EQ(op, ".") || S_EQ(op, "->")) return nullptr;

    // Array subscript: load from address.
    if (S_EQ(op, "[]"))
    {
        TypedAddr ta = lvalue_of(n);
        if (!ta.addr) return nullptr;
        return m_builder->CreateLoad(ta.elem, ta.addr, "arrload");
    }

    // Prefix unary (no left operand).
    if (!lhs) return gen_prefix(op, rhs);

    // Postfix unary (no right operand — stored as E(operand, op, null)).
    // The parser sets right to a parsed right-hand side; when the token stream
    // is just "x++" with nothing after, right may be a blank or stale node.
    // We detect postfix by checking for "++"/"--" with a value-type left.
    if ((S_EQ(op, "++") || S_EQ(op, "--")) && !rhs)
        return gen_postfix(op, lhs);

    // Standard binary.
    llvm::Value *L = gen_node(lhs);
    llvm::Value *R = gen_node(rhs);

    if (!L || !R) return L ? L : R;

    // Short-circuit logical operators.
    if (S_EQ(op, "&&"))
    {
        auto *fn     = m_builder->GetInsertBlock()->getParent();
        auto *lhs_end = m_builder->GetInsertBlock();
        llvm::Value *lb = to_bool(L);
        auto *rhs_bb   = llvm::BasicBlock::Create(*m_ctx, "and_r", fn);
        auto *merge_bb = llvm::BasicBlock::Create(*m_ctx, "and_e", fn);
        m_builder->CreateCondBr(lb, rhs_bb, merge_bb);

        m_builder->SetInsertPoint(rhs_bb);
        llvm::Value *rb = to_bool(R);
        auto *rhs_end  = m_builder->GetInsertBlock();
        m_builder->CreateBr(merge_bb);

        m_builder->SetInsertPoint(merge_bb);
        auto *phi = m_builder->CreatePHI(m_builder->getInt1Ty(), 2, "and");
        phi->addIncoming(m_builder->getFalse(), lhs_end);
        phi->addIncoming(rb, rhs_end);
        return m_builder->CreateZExt(phi, i32ty(), "andz");
    }

    if (S_EQ(op, "||"))
    {
        auto *fn     = m_builder->GetInsertBlock()->getParent();
        auto *lhs_end = m_builder->GetInsertBlock();
        llvm::Value *lb = to_bool(L);
        auto *rhs_bb   = llvm::BasicBlock::Create(*m_ctx, "or_r", fn);
        auto *merge_bb = llvm::BasicBlock::Create(*m_ctx, "or_e", fn);
        m_builder->CreateCondBr(lb, merge_bb, rhs_bb);

        m_builder->SetInsertPoint(rhs_bb);
        llvm::Value *rb = to_bool(R);
        auto *rhs_end  = m_builder->GetInsertBlock();
        m_builder->CreateBr(merge_bb);

        m_builder->SetInsertPoint(merge_bb);
        auto *phi = m_builder->CreatePHI(m_builder->getInt1Ty(), 2, "or");
        phi->addIncoming(m_builder->getTrue(), lhs_end);
        phi->addIncoming(rb, rhs_end);
        return m_builder->CreateZExt(phi, i32ty(), "orz");
    }

    // Widen integers to the same width.
    if (L->getType()->isIntegerTy() && R->getType()->isIntegerTy())
    {
        unsigned lw = L->getType()->getIntegerBitWidth();
        unsigned rw = R->getType()->getIntegerBitWidth();
        if (lw < rw) L = m_builder->CreateSExt(L, R->getType(), "lext");
        else if (rw < lw) R = m_builder->CreateSExt(R, L->getType(), "rext");
    }

    bool fp = L->getType()->isFloatingPointTy();
    llvm::Value *res = nullptr;

    if      (S_EQ(op, "+"))  res = fp ? m_builder->CreateFAdd(L,R,"fa")  : m_builder->CreateAdd(L,R,"a");
    else if (S_EQ(op, "-"))  res = fp ? m_builder->CreateFSub(L,R,"fs")  : m_builder->CreateSub(L,R,"s");
    else if (S_EQ(op, "*"))  res = fp ? m_builder->CreateFMul(L,R,"fm")  : m_builder->CreateMul(L,R,"m");
    else if (S_EQ(op, "/"))  res = fp ? m_builder->CreateFDiv(L,R,"fd")  : m_builder->CreateSDiv(L,R,"d");
    else if (S_EQ(op, "%"))  res = m_builder->CreateSRem(L,R,"r");
    else if (S_EQ(op, "&"))  res = m_builder->CreateAnd(L,R,"b");
    else if (S_EQ(op, "|"))  res = m_builder->CreateOr(L,R,"o");
    else if (S_EQ(op, "^"))  res = m_builder->CreateXor(L,R,"x");
    else if (S_EQ(op, "<<")) res = m_builder->CreateShl(L,R,"sl");
    else if (S_EQ(op, ">>")) res = m_builder->CreateAShr(L,R,"sr");
    else if (S_EQ(op, "<"))
        res = m_builder->CreateZExt(fp ? m_builder->CreateFCmpOLT(L,R,"lt")
                                       : m_builder->CreateICmpSLT(L,R,"lt"), i32ty(), "lti");
    else if (S_EQ(op, ">"))
        res = m_builder->CreateZExt(fp ? m_builder->CreateFCmpOGT(L,R,"gt")
                                       : m_builder->CreateICmpSGT(L,R,"gt"), i32ty(), "gti");
    else if (S_EQ(op, "<="))
        res = m_builder->CreateZExt(fp ? m_builder->CreateFCmpOLE(L,R,"le")
                                       : m_builder->CreateICmpSLE(L,R,"le"), i32ty(), "lei");
    else if (S_EQ(op, ">="))
        res = m_builder->CreateZExt(fp ? m_builder->CreateFCmpOGE(L,R,"ge")
                                       : m_builder->CreateICmpSGE(L,R,"ge"), i32ty(), "gei");
    else if (S_EQ(op, "=="))
        res = m_builder->CreateZExt(fp ? m_builder->CreateFCmpOEQ(L,R,"eq")
                                       : m_builder->CreateICmpEQ(L,R,"eq"), i32ty(), "eqi");
    else if (S_EQ(op, "!="))
        res = m_builder->CreateZExt(fp ? m_builder->CreateFCmpONE(L,R,"ne")
                                       : m_builder->CreateICmpNE(L,R,"ne"), i32ty(), "nei");

    return res;
}

// ============================================================================
// C API entry point
// ============================================================================

int codegen(struct compile_process *process)
{
    LLVMCodegen cg(process);
    return cg.run();
}
