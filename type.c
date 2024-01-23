#include "rvcc.h"
#include <stdlib.h>

// 复合字面量声明了一个仅初始化了 kind 的匿名Type结构体，使用TYPE_INT指向他
Type *TYPE_INT = &(Type){TY_INT, 8};

// 判断是否为 Type int
bool is_integer(Type *type) { return type->kind == TY_INT; }

// 复制类型
// 浅拷贝，仅复制栈上数据
Type *copy_type(Type *type) {
  Type *rlt = calloc(1, sizeof(Type));
  *rlt = *type;

  return rlt;
}

// 创建一个指针类型，并指向 base
Type *pointer_type(Type *base) {
  Type *type = calloc(1, sizeof(Type));
  type->kind = TY_PTR;
  type->size = 8;
  type->base = base;

  return type;
}

// 创建一个函数类型， 且返回值为ret_type
Type *func_type(Type *ret_type) {
  Type *type = calloc(1, sizeof(Type));
  type->kind = TY_FUNC;
  type->ret_type = ret_type;

  return type;
}

// 创建一个数组类型, 基类为 base， 长度为 len
Type *array_type(Type *base, int len) {
  Type *type = calloc(1, sizeof(Type));

  type->kind = TY_ARRAY;
  type->base = base;
  type->size = base->size * len;
  type->len = len;

  return type;
}

// 遍历 AST 并为所有 expr 及以下 NODE 增加类型
void add_type(Node *node) {
  // 节点为空，或者类型已经设置
  if (!node || node->type)
    return;

  // 递归访问所有的子节点
  switch (node->kind) {
  case ND_ADD:
  case ND_SUB:
  case ND_MUL:
  case ND_DIV:
  case ND_NEG:
  case ND_ASSIGN:
  case ND_EQ:
  case ND_NE:
  case ND_LT:
  case ND_LE:
  case ND_RETURN:
  case ND_ADDR:
  case ND_DEREF:
  case ND_EXPR_STMT:
    add_type(node->lhs);
    add_type(node->rhs);
    break;
  case ND_IF:
    add_type(node->cond);
    add_type(node->then);
    add_type(node->els);
    break;
  case ND_FOR:
    add_type(node->cond);
    add_type(node->then);
    add_type(node->init);
    add_type(node->inc);
    break;
  case ND_FNCALL:
    // 访问所有参数以增加类型
    for (Node *n = node->args; n; n = n->next)
      add_type(n);
    break;
  case ND_BLOCK:
    // 遍历 stmt 链表
    for (Node *n = node->body; n; n = n->next)
      add_type(n);
  default: // ND_VAR, ND_NUM
    break;
  }

  switch (node->kind) {
  case ND_ADD:
  case ND_SUB:
  case ND_MUL:
  case ND_DIV:
  case ND_NEG:
    node->type = node->lhs->type;
    return;
  case ND_ASSIGN:
    if (node->lhs->type->kind == TY_ARRAY) // 暂不允许对数组直接赋值
      error_token(node->lhs->token, "not an lvalue");
    // ADD、SUB、MUL、DIV、NEG、ASSIGN都与左子节点(单臂)的类型相同
    node->type = node->lhs->type;
    return;
  case ND_EQ:
  case ND_NE:
  case ND_LT:
  case ND_LE:
  case ND_NUM:
  case ND_FNCALL:
    // EQ、NE、LT、LE、VAR、NUM、FNCALL都设置为 TYPE_INT
    node->type = TYPE_INT;
    return;
  case ND_VAR:
    // 变量节点的类型与变量节点中保存的 Object Var 的类型相同
    node->type = node->var->type;
    return;
  case ND_ADDR: {
    Type *type = node->lhs->type;

    // 取地址节点的类型根据单臂所指向节点的类型来决定
    // 如果是数组，&的结果为指向 base 的指针
    if (type->kind == TY_ARRAY)
      node->type = pointer_type(type->base);
    else
      node->type = pointer_type(type);
    return;
  }
  case ND_DEREF:
    // ND_DEREF 的单臂必须有基类
    if (!node->lhs->type->base)
      error_token(node->token, "invalid pointer dereference");

    // ND_DEREF 的类型为指针指向的类型
    node->type = node->lhs->type->base;
    return;
  default:
    break;
  }
}