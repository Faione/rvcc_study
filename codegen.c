#include "rvcc.h"
#include <assert.h>
#include <stdio.h>

//
// 三、语义分析,生成代码
//

// 生成的代码中,利用栈保存中间数据
// 表达式中使用 a0, a1 两个寄存器保存算式中的数字
// 多次运算的结果通过栈来进行保存
// 当前预设数据长度为 64bit/8byte
static int STACK_DEPTH;

static void gen_expr(Node *node);

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

// 计算每个 local var 相对于栈顶的便宜
// 计算栈的长度,并对齐到 16
static void assign_local_val_offsets(Function *prog) {
  int offset = 0;
  for (Object *var = prog->locals; var; var = var->next) {
    offset += 8;
    var->offset = -offset;
  }

  prog->stack_size = align_to(offset, 16);
}

// 计算给定节点的内存地址
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
    printf("  # 读取 a0 中间接引用的值，存入到 a0中\n");
    printf("  ld a0, 0(a0)\n");
    return;
  case ND_VAR:
    gen_addr(node);
    // 将变量代表的内存地址中的值读入到 a0 中
    printf("  # 读取a0中存放的地址, 得到的值存入a0\n");
    printf("  ld a0, 0(a0)\n");
    return;
  case ND_ASSIGN:
    // 左值
    gen_addr(node->lhs);
    push();
    // 右值
    gen_expr(node->rhs);
    // 栈上保存的是左值内存地址, 弹出到 a1 寄存器
    pop("a1");
    printf("  # 将a0的值, 写入到a1中存放的地址\n");
    printf("  sd a0, 0(a1)\n");
    return;
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
    printf("  # 跳转到.L.return段\n");
    printf("  j .L.return\n");
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
  printf("  # 定义全局main段\n");
  printf("  .globl main\n");
  printf("\n# =====程序开始===============\n");
  printf("# main段标签,也是程序入口段\n");
  printf("main:\n");

  // 栈布局
  //-------------------------------// sp
  //              fp
  //-------------------------------// fp = sp-8
  //             变量
  //-------------------------------// sp = sp-8-StackSize
  //           表达式计算
  //-------------------------------//

  printf("  # 将fp压栈,fp属于“被调用者保存”的寄存器,需要恢复原值\n");
  printf("  addi sp, sp, -8\n");
  printf("  sd fp, 0(sp)\n");
  printf("  # 将sp的值写入fp\n");
  printf("  mv fp, sp\n");

  printf("  # sp腾出StackSize大小的栈空间\n");
  printf("  addi sp, sp, -%d\n", prog->stack_size);

  printf("\n# =====程序主体===============\n");
  gen_stmt(prog->body);
  assert(STACK_DEPTH == 0);

  printf("\n# =====程序结束===============\n");
  printf("# return段标签\n");
  printf(".L.return:\n");

  printf("  # 将fp的值写回sp\n");
  printf("  mv sp, fp\n");
  // 恢复上一个 fp
  printf("  ld fp, 0(sp)\n");
  printf("  addi sp, sp, 8\n");

  printf("  # 返回a0值给系统调用\n");
  printf("  ret\n");
}
