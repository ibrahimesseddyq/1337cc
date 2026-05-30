// codegen_expr.cpp — Expression code generation (literals, variables,
//                    unary/binary ops, calls, ternary, cast, assignment).

#include "codegen.hpp"

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
        TypedAddr ta = lvalue_of(n);
        if (ta.addr)
            return load_from(ta);
        return gen_ident(n->sval);
    }
    case NODE_TYPE_EXPRESSION:
        return gen_expr(n);
    case NODE_TYPE_EXPRESSION_PARENTHESES:
        return gen_parens(n);
    case NODE_TYPE_CAST:
        return gen_cast(n);
    case NODE_TYPE_TENARY:
        return nullptr;
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
    return m_builder->CreateGlobalStringPtr(str, ".str", 0, m_module.get());
}

llvm::Value *LLVMCodegen::gen_ident(const char *name)
{
    if (!name) return nullptr;
    llvm::Function *fn = m_module->getFunction(name);
    if (fn) return fn;
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
        llvm::Value *ptr = gen_node(lhs);
        if (ptr && rval)
            m_builder->CreateStore(coerce(rval, rval->getType()), ptr);
        return rval;
    }

    if (!S_EQ(op, "="))
    {
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

    std::string callee_name;
    if (callee_n->type == NODE_TYPE_IDENTIFIER)
        callee_name = callee_n->sval;

    llvm::Function *fn = callee_name.empty()
        ? nullptr : m_module->getFunction(callee_name);

    if (fn)
    {
        auto pts = fn->getFunctionType()->params();
        for (size_t i = 0; i < argv.size() && i < pts.size(); ++i)
            argv[i] = coerce(argv[i], pts[i]);
        llvm::Type *rty = fn->getReturnType();
        return m_builder->CreateCall(fn, argv, rty->isVoidTy() ? "" : "call");
    }

    llvm::FunctionType *ft = llvm::FunctionType::get(i32ty(), {}, /*vararg=*/true);
    llvm::FunctionCallee fc = m_module->getOrInsertFunction(
        callee_name.empty() ? "__indirect" : callee_name, ft);
    return m_builder->CreateCall(fc, argv, "call");
}

// ============================================================================
// Prefix / postfix unary
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
        return ta.addr;
    }
    if (S_EQ(op, "*"))
    {
        llvm::Value *ptr = gen_node(operand);
        if (!ptr) return nullptr;
        return m_builder->CreateLoad(i32ty(), ptr, "deref");
    }
    return gen_node(operand);
}

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
    return cur;
}

// ============================================================================
// Ternary / cast / parens
// ============================================================================

llvm::Value *LLVMCodegen::gen_ternary(struct node *n)
{
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

llvm::Value *LLVMCodegen::gen_cast(struct node *n)
{
    llvm::Value *v = gen_node(n->cast.operand);
    if (!v) return nullptr;
    return coerce(v, dtype_to_llvm(&n->cast.dtype));
}

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

    if (S_EQ(op, "=")   || S_EQ(op, "+=") || S_EQ(op, "-=") ||
        S_EQ(op, "*=")  || S_EQ(op, "/=") || S_EQ(op, "%=") ||
        S_EQ(op, "&=")  || S_EQ(op, "|=") || S_EQ(op, "^=") ||
        S_EQ(op, "<<=") || S_EQ(op, ">>="))
        return gen_assign(n);

    if (S_EQ(op, "()")) return gen_call(lhs, rhs);
    if (S_EQ(op, "?"))  return gen_ternary(n);
    if (S_EQ(op, ","))  { gen_node(lhs); return gen_node(rhs); }
    if (S_EQ(op, ".") || S_EQ(op, "->")) return nullptr;

    if (S_EQ(op, "[]"))
    {
        TypedAddr ta = lvalue_of(n);
        if (!ta.addr) return nullptr;
        return m_builder->CreateLoad(ta.elem, ta.addr, "arrload");
    }

    if (!lhs) return gen_prefix(op, rhs);

    if ((S_EQ(op, "++") || S_EQ(op, "--")) && !rhs)
        return gen_postfix(op, lhs);

    llvm::Value *L = gen_node(lhs);
    llvm::Value *R = gen_node(rhs);

    if (!L || !R) return L ? L : R;

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
