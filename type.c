#include "rvcc.h"
#include <stdlib.h>

// 复合字面量声明了一个仅初始化了 kind 的匿名Type结构体，使用TYPE_INT指向他
Type *TYPE_INT = &(Type){TY_INT};

// 判断是否为 Type int
bool is_integer(Type *type) { return type->kind == TY_INT; }

// 创建一个指针类型，并指向 base
static Type *pointer_to(Type *base) {
  Type *type = calloc(1, sizeof(Type));
  type->kind = TY_PTR;
  type->base = base;
  return type;
}

// 遍历 AST 并为所有 expr 及以下 NODE 增加类型
void add_type(Node *node) {
  // 节点为空，或者类型已经设置
  if (!node || node->type)
    return;

  // 递归访问所有的子节点
  add_type(node->lhs);
  add_type(node->rhs);
  add_type(node->cond);
  add_type(node->then);
  add_type(node->els);
  add_type(node->init);
  add_type(node->inc);

  // 遍历 stmt 链表
  for (Node *n = node->body; n; n = n->next)
    add_type(n);

  switch (node->kind) {
  case ND_ADD:
  case ND_SUB:
  case ND_MUL:
  case ND_DIV:
  case ND_NEG:
  case ND_ASSIGN:
    // ADD、SUB、MUL、DIV、NEG、ASSIGN都与左子节点(单臂)的类型相同
    node->type = node->lhs->type;
    return;
  case ND_EQ:
  case ND_NE:
  case ND_LT:
  case ND_LE:
  case ND_VAR:
  case ND_NUM:
    // EQ、NE、LT、LE、VAR、NUM 都设置为 TYPE_INT
    node->type = TYPE_INT;
    return;
  case ND_ADDR:
    // 取地址节点的类型根据单臂所指向节点的类型来决定
    node->type = pointer_to(node->lhs->type);
    return;
  case ND_DEREF:
    if (node->lhs->type->kind == TY_PTR) // 嵌套解引用
      node->type = node->lhs->type->base;
    else // 当前只有 INT 数据类型
      node->type = TYPE_INT;
    return;
  default:
    break;
  }
}