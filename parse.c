#include "rvcc.h"
#include <stdlib.h>
#include <string.h>

//
// 二、语法分析， 生成AST
//

// Node 的构造方法
static Node *new_node(NodeKind kind) {
  Node *node = calloc(1, sizeof(Node));
  node->kind = kind;
  return node;
}

static Node *new_node_unary(NodeKind kind, Node *expr) {
  Node *node = new_node(kind);
  node->rhs = expr;
  return node;
}

static Node *new_node_bin(NodeKind kind, Node *lhs, Node *rhs) {
  Node *node = new_node(kind);
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

static Node *new_node_num(int val) {
  Node *node = new_node(ND_NUM);
  node->val = val;
  return node;
}

static Node *new_node_var(Object *var) {
  Node *node = new_node(ND_VAR);
  node->var = var;
  return node;
}

// 变量实例均保存在全局的 LOCALS 链表中
Object *LOCALS;

// 创建以 Local 变量，并头插到 LOCALS 中
static Object *new_local_var(char *name) {
  Object *var = calloc(1, sizeof(Object));
  var->name = name;

  // 头插法
  var->next = LOCALS;
  LOCALS = var;
  return var;
}

// 寻找 LOCALS 中是否有与 ident token 同名的变量
static Object *find_var_by_token(Token *token) {
  for (Object *var = LOCALS; var; var = var->next) {
    // 简单判断 -> 负载判断, 提升效率
    if (strlen(var->name) == token->len &&
        !strncmp(token->loc, var->name, token->len))
      return var;
  }

  return NULL;
}

// program = "{" compoundStmt
// compoundStmt = stmt* "}"
// stmt = "return" expr ";"| "{" compoundStmt | expr_stmt
// expr_stmt = expr? ";"
// expr = assign
// assign = equality ("=" assign)?
// equality = relational ("==" relational | "!=" relational)*
// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
// add = mul ("+" mul | "-" mul)*
// mul = unary ("*" unary | "/" unary)*
// unary = ("+" | "-") unary | primary
// primary = "(" expr ")" | ident｜ num

// 传入 Token** 与 Token*，
// 前者作为结果，让调用者能够感知，后者则作为递归中传递的变量
// 因此以下任何一个函数执行完毕之后
// *rest 都必须指向待分析的下一个 token
// 而参数 Token* 仅是值拷贝，对调用者来说不可感知
#define PARSER_DEFINE(name) static Node *(name)(Token * *rest, Token * token)
PARSER_DEFINE(compound_stmt);
PARSER_DEFINE(stmt);
PARSER_DEFINE(expr_stmt);
PARSER_DEFINE(expr);
PARSER_DEFINE(assign);
PARSER_DEFINE(equality);
PARSER_DEFINE(relational);
PARSER_DEFINE(add);
PARSER_DEFINE(mul);
PARSER_DEFINE(unary);
PARSER_DEFINE(primary);

// compoundStmt = stmt* "}"
PARSER_DEFINE(compound_stmt) {
  Node head = {};
  Node *cur = &head;
  while (!equal(token, "}")) {
    cur->next = stmt(&token, token);
    cur = cur->next;
  }

  Node *node = new_node(ND_BLOCK);
  node->body = head.next;
  *rest = token->next;
  return node;
}

// stmt = "return" expr ";"| "{" compoundStmt | expr_stmt
PARSER_DEFINE(stmt) {

  if (token->kind == TK_KEYWORD && equal(token, "return")) {
    Node *node = new_node_unary(ND_RETURN, expr(&token, token->next));
    *rest = skip(token, ";");
    return node;
  }

  if (equal(token, "{")) {
    return compound_stmt(rest, token->next);
  }

  return expr_stmt(rest, token);
}

// expr_stmt = expr? ";"
PARSER_DEFINE(expr_stmt) {
  if (equal(token, ";")) {
    *rest = token->next;
    return new_node(ND_BLOCK);
  }

  Node *node = new_node_unary(ND_EXPR_STMT, expr(&token, token));
  *rest = skip(token, ";");
  return node;
}

// expr = assign
PARSER_DEFINE(expr) { return assign(rest, token); }

// assign = equality ("=" assign)?
PARSER_DEFINE(assign) {
  Node *node = equality(&token, token);

  // a=b=1;
  if (equal(token, "="))
    node = new_node_bin(ND_ASSIGN, node, assign(&token, token->next));
  *rest = token;
  return node;
}

// equality = relational ("==" relational | "!=" relational)*
PARSER_DEFINE(equality) {
  Node *node = relational(&token, token);
  while (true) {
    if (equal(token, "==")) {
      node = new_node_bin(ND_EQ, node, relational(&token, token->next));
      continue;
    }

    if (equal(token, "!=")) {
      node = new_node_bin(ND_NE, node, relational(&token, token->next));
      continue;
    }
    break;
  }

  *rest = token;
  return node;
}

// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
PARSER_DEFINE(relational) {
  Node *node = add(&token, token);

  while (true) {
    if (equal(token, "<")) {
      node = new_node_bin(ND_LT, node, add(&token, token->next));
      continue;
    }

    if (equal(token, "<=")) {
      node = new_node_bin(ND_LE, node, add(&token, token->next));
      continue;
    }

    // lhs > rhs == rhs < lhs
    if (equal(token, ">")) {
      node = new_node_bin(ND_LT, add(&token, token->next), node);
      continue;
    }

    if (equal(token, ">=")) {
      node = new_node_bin(ND_LE, add(&token, token->next), node);
      continue;
    }
    break;
  }

  *rest = token;
  return node;
}

// add = mul ("+" mul | "-" mul)*
PARSER_DEFINE(add) {
  Node *node = mul(&token, token);

  // 遍历并构造多个 mul
  // expr 由多个 mul 相加减构成
  // 因此在生成一个 mul 之后，需要判断后续的 token 是否为 +|-
  // 来决定是否继续生成 mul，直到不能构成 mul
  while (true) {
    if (equal(token, "+")) {
      node = new_node_bin(ND_ADD, node, mul(&token, token->next));
      continue;
    }

    if (equal(token, "-")) {
      node = new_node_bin(ND_SUB, node, mul(&token, token->next));
      continue;
    }
    break;
  }

  // 完成一个 expr 构造后，设置rest指向的为 不是 expr 的第一个 token
  // 返回 expr AST 的根节点
  *rest = token;
  return node;
}

// mul = unary ("*" unary | "/" unary)*
PARSER_DEFINE(mul) {
  Node *node = unary(&token, token);

  while (true) {
    if (equal(token, "*")) {
      node = new_node_bin(ND_MUL, node, unary(&token, token->next));
      continue;
    }
    if (equal(token, "/")) {
      node = new_node_bin(ND_DIV, node, unary(&token, token->next));
      continue;
    }
    break;
  }

  *rest = token;
  return node;
}

// unary = ("+" | "-") unary | primary
PARSER_DEFINE(unary) {
  // + 一元运算符无影响，跳过即可
  // unary无论如何都会调用 primary 进行 rest 的设置
  // 因此递归调用 unary 时，传入 rest 即可
  // 否则则需要在每次生成新节点后，手动地再设置 rest
  if (equal(token, "+")) {
    return unary(rest, token->next);
  }

  if (equal(token, "-")) {
    return new_node_unary(ND_NEG, unary(rest, token->next));
  }

  return primary(rest, token);
}

// primary = "(" expr ")" | ident｜ num
PARSER_DEFINE(primary) {
  // "(" expr ")"
  if (equal(token, "(")) {
    Node *node = expr(&token, token->next);
    *rest = skip(token, ")");
    return node;
  }

  // ident
  if (token->kind == TK_IDENT) {
    Object *var = find_var_by_token(token);
    if (!var)
      // token变量名称并不是字符串，因此在此处拷贝一份并生成字符串
      var = new_local_var(strndup(token->loc, token->len));

    *rest = token->next;
    return new_node_var(var);
  }

  // num
  if (token->kind == TK_NUM) {
    Node *node = new_node_num(token->val);
    *rest = token->next;
    return node;
  }

  error_token(token, "expected an expression");
  return NULL;
}

Function *parse(Token *token) {

  // 代码块必须以 { 开头
  token = skip(token, "{");

  Function *prog = calloc(1, sizeof(Function));
  prog->body = compound_stmt(&token, token);
  prog->locals = LOCALS;
  return prog;
}