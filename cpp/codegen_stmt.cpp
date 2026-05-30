// codegen_stmt.cpp — Statement and control-flow code generation.

#include "codegen.hpp"

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
    case NODE_TYPE_LABEL:               gen_label(n); break;
    case NODE_TYPE_STATEMENT_GOTO:      gen_goto(n);  break;
    case NODE_TYPE_BODY:                gen_body(n);  break;
    case NODE_TYPE_STATEMENT_CASE:
    case NODE_TYPE_BLANK:
        break;
    default:
        gen_node(n);
        break;
    }
}

// ============================================================================
// Control flow
// ============================================================================

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

    m_builder->SetInsertPoint(default_bb);
    m_builder->CreateBr(after_bb);

    struct node *body = n->stmt.switch_stmt.body;
    if (body && body->type == NODE_TYPE_BODY && body->body.statements)
    {
        auto *pre = llvm::BasicBlock::Create(*m_ctx, "swpre", fn);
        m_builder->SetInsertPoint(pre);
        m_builder->CreateBr(after_bb);

        llvm::BasicBlock *open_bb = nullptr;

        vector_set_peek_pointer(body->body.statements, 0);
        for (;;)
        {
            auto *s = static_cast<struct node *>(
                vector_peek_ptr(body->body.statements));
            if (!s) break;

            if (s->type == NODE_TYPE_STATEMENT_CASE)
            {
                auto *case_bb = llvm::BasicBlock::Create(*m_ctx, "swcase", fn);
                if (open_bb)
                    m_builder->CreateBr(case_bb);
                open_bb = case_bb;
                m_builder->SetInsertPoint(case_bb);

                llvm::Value *cv = gen_node(s->stmt._case.exp);
                if (auto *ci = llvm::dyn_cast<llvm::ConstantInt>(cv))
                    sw->addCase(ci, case_bb);
            }
            else if (s->type == NODE_TYPE_STATEMENT_DEFAULT)
            {
                if (open_bb)
                    m_builder->CreateBr(default_bb);

                llvm::Instruction *prov = default_bb->getTerminator();
                if (prov) prov->eraseFromParent();

                open_bb = default_bb;
                m_builder->SetInsertPoint(default_bb);
            }
            else
            {
                if (open_bb)
                {
                    if (m_builder->GetInsertBlock() != open_bb)
                        m_builder->SetInsertPoint(open_bb);
                    gen_stmt(s);
                    if (m_builder->GetInsertBlock()->getTerminator())
                        open_bb = nullptr;
                }
            }
        }
        if (open_bb && !m_builder->GetInsertBlock()->getTerminator())
            m_builder->CreateBr(after_bb);
    }

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
        auto *fn = m_builder->GetInsertBlock()->getParent();
        auto *tmp = llvm::BasicBlock::Create(*m_ctx, "goto_fwd", fn);
        auto *ph  = m_builder->CreateBr(tmp);
        m_pending_gotos.push_back({name, m_builder->GetInsertBlock(), ph});
        m_builder->SetInsertPoint(tmp);
    }
}
