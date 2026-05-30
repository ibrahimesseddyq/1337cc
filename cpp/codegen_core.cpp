// codegen_core.cpp — Constructor, run(), scope, loop/goto, type helpers,
//                    misc helpers, and lvalue utilities.

#include "codegen.hpp"

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
        fprintf(stderr, "LLVM verification error: %s\n", err.c_str());

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
        if (it == m_labels.end())
        {
            fprintf(stderr, "codegen: undefined label '%s'\n", pg.label.c_str());
            continue;
        }
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
    if (src->isPointerTy() && target->isPointerTy()) return v;
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
// lvalue / address helpers
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
        TypedAddr ta = lookup_var(n->sval);
        if (ta.addr) return ta;
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
        if (S_EQ(op, "*") && !n->exp.left)
        {
            llvm::Value *ptr = gen_node(n->exp.right);
            return {ptr, i32ty()};
        }
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
