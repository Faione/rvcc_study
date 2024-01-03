#include "rvcc.h"
#include <assert.h>

//
// 三、语义分析，生成代码
//

// 生成的代码中，利用栈保存中间数据
// 表达式中使用 a0, a1 两个寄存器保存算式中的数字
// 多次运算的结果通过栈来进行保存
// 当前预设数据长度为 64bit/8byte
static int STACK_DEPTH;

// 压栈
// 将 a0 寄存器中的值压入栈中
static void push(void) {
  printf("  addi sp, sp, -8\n");
  printf("  sd a0, 0(sp)\n");
  STACK_DEPTH++;
}

// 出栈
// 将栈顶的数据弹出到寄存器 reg 中
static void pop(char *reg) {
  printf("  ld %s, 0(sp)\n", reg);
  printf("  addi sp, sp, 8\n");
  STACK_DEPTH--;
}

// 词法分析
// 生成代码
void gen_expr(Node *node) {
  // 若根节点为数字(叶子节点), 则只加载到 a0 寄存器中
  if (node->kind == ND_NUM) {
  }
  switch (node->kind) {
  case ND_NUM:
    printf("  li a0, %d\n", node->val);
    return;
  case ND_NEG:
    // 一元运算符子为单臂二叉树，子节点保留在右侧
    // 因此向右递归直到遇到数字
    gen_expr(node->rhs);
    printf("  neg a0, a0\n");
    return;
  default:
    break;
  }

  // 递归右节点
  gen_expr(node->rhs);
  // 右侧的结果保存在 a0 中，压入到栈
  push();
  // 递归左节点
  gen_expr(node->lhs);
  // 左侧结果保存在 a0 中
  // 同时由于左侧计算完毕，栈回到递归右侧完毕时的状态
  // 即栈顶的就是右子树的结果
  pop("a1");

  // 此时 a0 保存了 lhs 的结果，而 a1 保存了 rhs 的结果
  switch (node->kind) {
  case ND_ADD:
    printf("  add a0, a0, a1\n");
    return;
  case ND_SUB:
    printf("  sub a0, a0, a1\n");
    return;
  case ND_MUL:
    printf("  mul a0, a0, a1\n");
    return;
  case ND_DIV:
    printf("  div a0, a0, a1\n");
    return;
  case ND_EQ:
  case ND_NE:
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
    printf("  slt a0, a0, a1\n");
    return;
  case ND_LE:
    // a0 <= a1 == !(a1 < a0) == (a1 < a0) xor 1
    printf("  slt a0, a1, a0\n");
    printf("xori a0, a0, 1\n");
    return;
  default:
    break;
  }
  error("invalid expression");
}

static void gen_stmt(Node *node) {
  if (node->kind == ND_EXPR_STMT) {
    gen_expr(node->rhs);
    return;
  }

  error("invalid statement");
}

void codegen(Node *node) {
  printf("  .globl main\n");
  printf("main:\n");

  for (Node *n = node; n; n = n->next) {
    gen_stmt(n);
    assert(STACK_DEPTH == 0);
  }

  printf("  ret\n");
}
