#include "rvcc.h"
#include <assert.h>
#include <stdio.h>

//
// 三、语义分析,生成代码
//

static void gen_expr(Node *node);

// (1) 函数

// 参数寄存器
static char *func_arg_regs[] = {"a0", "a1", "a2", "a3", "a4", "a5"};
// 当前函数
static Function *CUR_FUNC;

// (2) 栈
// 生成的代码中,利用栈保存中间数据
// 表达式中使用 a0, a1 两个寄存器保存算式中的数字
// 多次运算的结果通过栈来进行保存
// 当前预设数据长度为 64bit/8byte

// 当前分析代码的栈深度
static int STACK_DEPTH;

// 压栈
// 将 a0 寄存器中的值压入栈中
static void push(void) {
  printf("  # 压栈, 将a0的值存入栈顶\n");
  printf("  addi sp, sp, -8\n");
  printf("  sd a0, 0(sp)\n");
  STACK_DEPTH++;
}

// 出栈
// 将栈顶的数据弹出到寄存器 reg 中
static void pop(char *reg) {
  printf("  # 弹栈, 将栈顶的值存入%s\n", reg);
  printf("  ld %s, 0(sp)\n", reg);
  printf("  addi sp, sp, 8\n");
  STACK_DEPTH--;
}

// 每次调用都会生成一个新的 count
// 用来区分不同的代码段
static int count(void) {
  static int I = 1;
  return I++;
}

// 将 n 对其到 align 的整数倍
static int align_to(int n, int align) {
  return (n + align - 1) / align * align;
}

// 为链表中每个 Function 计算本地变量偏移及栈大小
static void assign_local_val_offsets(Function *prog) {

  for (Function *f = prog; f; f = f->next) {
    int offset = 0;
    // 计算每个 local var 相对于栈顶的偏移
    for (Object *var = f->locals; var; var = var->next) {
      offset += var->type->size;
      var->offset = -offset;
    }

    // 计算栈的长度,并对齐到 16
    f->stack_size = align_to(offset, 16);
  }
}

// 计算给定节点的内存地址
// 将地址保存在 a0 寄存器中
static void gen_addr(Node *node) {

  switch (node->kind) {
  case ND_VAR: // Object var 为指针, 在 prog 中被修改后, 同时也能从 Node 访问
    printf("  # 获取变量%s的栈内地址为%d(fp)\n", node->var->name,
           node->var->offset);
    printf("  addi a0, fp, %d\n", node->var->offset);
    return;
  case ND_DEREF: // 对一个解引用expr进行取地址
    gen_expr(node->lhs);
    return;
  default:
    break;
  }

  error_token(node->token, "not an lvalue");
}

// a0 中保存了一个地址
// 将此地址的值加载到 a0 中
static void load(Type *type) {
  if (type->kind == TY_ARRAY)
    return;
  printf("  # 读取a0中存放的地址, 得到的值存入a0\n");
  printf("  ld a0, 0(a0)\n");
}

// 栈保存了一个地址
// 将此地址 pop 到 a1 中
// 将 a0 中的值保存到此地址
static void store(void) {
  pop("a1");
  printf("  # 将a0的值, 写入到 a1 中存放的地址\n");
  printf("  sd a0, 0(a1)\n");
}

// 词法分析
// 生成代码
void gen_expr(Node *node) {
  switch (node->kind) {
  case ND_NUM:
    // 若根节点为数字(叶子节点), 则只加载到 a0 寄存器中
    printf("  # 将%d加载到a0中\n", node->val);
    printf("  li a0, %d\n", node->val);
    return;
  case ND_NEG:
    // 一元运算符子为单臂二叉树,子节点保留在左侧
    // 因此向左递归直到遇到数字
    gen_expr(node->lhs);
    printf("  # 对a0值进行取反\n");
    printf("  neg a0, a0\n");
    return;
  case ND_ADDR:
    // 计算单臂指向的变量的地址，保存到 a0 中
    gen_addr(node->lhs);
    return;
  case ND_DEREF:
    // 解引用向右递归
    gen_expr(node->lhs);
    load(node->type);
    return;
  case ND_VAR:
    gen_addr(node);
    load(node->type);
    return;
  case ND_ASSIGN:
    // 左值
    gen_addr(node->lhs);
    push();
    // 右值
    gen_expr(node->rhs);
    store();
    return;
  case ND_FNCALL: {
    int argc = 0;

    // 遍历所有参数，并将参数逐个压入栈中
    for (Node *arg = node->args; arg; arg = arg->next) {
      gen_expr(arg);
      push();
      argc++;
    }

    // 上述指令执行完毕时，参数必然按顺序放置在栈上
    // 反向弹栈
    for (int i = argc - 1; i >= 0; i--) {
      pop(func_arg_regs[i]);
    }

    printf("  # 调用函数%s\n", node->func_name);
    printf("  call %s\n", node->func_name);

    return;
  }
  default:
    break;
  }

  // 递归右节点
  gen_expr(node->rhs);
  // 右侧的结果保存在 a0 中,压入到栈
  push();
  // 递归左节点
  gen_expr(node->lhs);
  // 左侧结果保存在 a0 中
  // 同时由于左侧计算完毕,栈回到递归右侧完毕时的状态
  // 即栈顶的就是右子树的结果
  pop("a1");

  // 此时 a0 保存了 lhs 的结果,而 a1 保存了 rhs 的结果
  switch (node->kind) {
  case ND_ADD:
    printf("  # a0+a1,结果写入a0\n");
    printf("  add a0, a0, a1\n");
    return;
  case ND_SUB:
    printf("  # a0-a1,结果写入a0\n");
    printf("  sub a0, a0, a1\n");
    return;
  case ND_MUL:
    printf("  # a0×a1,结果写入a0\n");
    printf("  mul a0, a0, a1\n");
    return;
  case ND_DIV:
    printf("  # a0÷a1,结果写入a0\n");
    printf("  div a0, a0, a1\n");
    return;
  case ND_EQ:
  case ND_NE:
    printf("  # 判断是否a0%sa1\n", node->kind == ND_EQ ? "=" : "≠");
    printf("  xor a0, a0 ,a1\n");
    if (node->kind == ND_EQ) {
      // a0 = 1 if a0 = 0
      printf("  seqz a0, a0\n");
    } else {
      // a0 = 1 if a0 != 0
      printf("  snez a0, a0\n");
    }
    return;
  case ND_LT:
    printf("  # 判断a0<a1\n");
    printf("  slt a0, a0, a1\n");
    return;
  case ND_LE:
    // a0 <= a1 == !(a1 < a0) == (a1 < a0) xor 1
    printf("  # 判断是否a0≤a1\n");
    printf("  slt a0, a1, a0\n");
    printf("xori a0, a0, 1\n");
    return;
  default:
    break;
  }
  error_token(node->token, "invalid expression");
}

static void gen_stmt(Node *node) {
  switch (node->kind) {
  case ND_EXPR_STMT:
    gen_expr(node->lhs);
    return;
  case ND_RETURN:
    printf("# 返回语句\n");
    gen_expr(node->lhs);
    printf("  # 跳转到.L.return.%s段\n", CUR_FUNC->name);
    printf("  j .L.return.%s\n", CUR_FUNC->name);
    return;
  case ND_BLOCK:
    for (Node *n = node->body; n; n = n->next) {
      gen_stmt(n);
    }
    return;
  case ND_IF: {
    int c = count();

    // condition
    printf("\n# =====分支语句%d==============\n", c);
    printf("\n# cond表达式%d\n", c);
    gen_expr(node->cond);
    printf("  # 若a0为0,则跳转到分支%d的.L.else.%d段\n", c, c);
    printf("  beqz a0, .L.else.%d\n", c);

    printf("\n# Then语句%d\n", c);
    gen_stmt(node->then);
    printf("\n# Else语句%d\n", c);
    printf("# 分支%d的.L.else.%d段标签\n", c, c);
    printf("  j .L.end.%d\n", c);

    // else 逻辑
    printf(".L.else.%d:\n", c);
    if (node->els)
      gen_stmt(node->els);
    // end 标签
    printf("\n# 分支%d的.L.end.%d段标签\n", c, c);
    printf(".L.end.%d:\n", c);
    return;
  }
  case ND_FOR: { // 生成 for 或 while 循环代码
    int c = count();

    printf("\n# =====循环语句%d===============\n", c);
    if (node->init) {
      printf("\n# Init语句%d\n", c);
      gen_stmt(node->init);
    }

    printf("\n# 循环%d的.L.begin.%d段标签\n", c, c);
    printf(".L.begin.%d:", c);
    // 循环条件
    if (node->cond) {
      printf("# Cond表达式%d\n", c);
      gen_expr(node->cond);
      printf("  # 若a0为0,则跳转到循环%d的.L.end.%d段\n", c, c);
      printf("  beqz a0, .L.end.%d\n", c);
    }

    printf("\n# Then语句%d\n", c);
    gen_stmt(node->then);
    // 循环递增语句
    if (node->inc) {
      printf("\n# Inc语句%d\n", c);
      gen_expr(node->inc);
    }

    printf("  # 跳转到循环%d的.L.begin.%d段\n", c, c);
    printf("  j .L.begin.%d\n", c);
    printf("\n# 循环%d的.L.end.%d段标签\n", c, c);
    printf(".L.end.%d:\n", c);
    return;
  }
  default:
    break;
  }

  error_token(node->token, "invalid statement");
}

void codegen(Function *prog) {
  assign_local_val_offsets(prog);

  // 为每个函数单独生成代码
  for (Function *f = prog; f; f = f->next) {
    printf("  # 定义全局%s段\n", f->name);
    printf("  .globl %s\n", f->name);
    printf("\n# =====%s段开始===============\n", f->name);
    printf("# %s段标签\n", f->name);
    printf("%s:\n", f->name);
    CUR_FUNC = f;

    // 栈布局
    //-------------------------------// sp
    //              ra
    //-------------------------------// ra = sp-8
    //              fp
    //-------------------------------// fp = sp-16
    //             变量
    //-------------------------------// sp = sp-16-StackSize
    //           表达式计算
    //-------------------------------//

    // Prologue
    printf("  addi sp, sp, -16\n");
    printf("  # 将ra压栈\n");
    printf("  sd ra, 8(sp)\n");

    printf("  # 将fp压栈,fp属于“被调用者保存”的寄存器,需要恢复原值\n");
    printf("  sd fp, 0(sp)\n");

    printf("  # 将sp的值写入fp\n");
    printf("  mv fp, sp\n");

    printf("  # sp腾出StackSize大小的栈空间\n");
    printf("  addi sp, sp, -%d\n", f->stack_size);

    int i = 0;
    for (Object *var = f->params; var; var = var->next) {
      printf("  # 将%s寄存器的值存入%s的栈地址\n", func_arg_regs[i], var->name);
      printf("  sd %s, %d(fp)\n", func_arg_regs[i++], var->offset);
    }

    printf("\n# =====%s段主体===============\n", f->name);
    gen_stmt(f->body);
    assert(STACK_DEPTH == 0);

    // Epilogue
    printf("\n# =====%s段结束===============\n", f->name);
    printf("# %s return段标签\n", f->name);
    printf(".L.return.%s:\n", f->name);

    printf("  # 将fp的值写回sp\n");
    printf("  mv sp, fp\n");

    printf("  # 恢复fp、ra和sp\n");
    printf("  ld fp, 0(sp)\n");
    printf("  ld ra, 8(sp)\n");
    printf("  addi sp, sp, 16\n");

    printf("  # 返回a0值给系统调用\n");
    printf("  ret\n");
  }
}
