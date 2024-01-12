#include "rvcc.h"

//
// 二、语法分析， 生成AST
//

// Node 的构造方法
static Node *new_node(NodeKind kind, Token *token) {
  Node *node = calloc(1, sizeof(Node));
  node->kind = kind;
  node->token = token;
  return node;
}

static Node *new_node_unary(NodeKind kind, Node *expr, Token *token) {
  Node *node = new_node(kind, token);
  // 单臂默认使用 lhs
  node->lhs = expr;
  return node;
}

static Node *new_node_bin(NodeKind kind, Node *lhs, Node *rhs, Token *token) {
  Node *node = new_node(kind, token);
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

static Node *new_node_num(int val, Token *token) {
  Node *node = new_node(ND_NUM, token);
  node->val = val;
  return node;
}

static Node *new_node_var(Object *var, Token *token) {
  Node *node = new_node(ND_VAR, token);
  node->var = var;
  return node;
}

// 创建ADD节点
// num | ptr + num | ptr
// 未声明 Type，上层会使类型与 lhs 相同
static Node *new_node_add(Node *lhs, Node *rhs, Token *token) {
  add_type(lhs);
  add_type(rhs);

  // num + num
  if (is_integer(lhs->type) && is_integer(rhs->type)) {
    return new_node_bin(ND_ADD, lhs, rhs, token);
  }

  // ptr + ptr
  // invalid
  if (lhs->type->base && rhs->type->base)
    error_token(token, "invalid operands");

  // num + ptr
  // change to  ptr + num
  if (!lhs->type->base && rhs->type->base) {
    Node *temp = lhs;
    lhs = rhs;
    rhs = temp;
  }

  // 将 ptr + num 转化为 ptr + (num * 8) 从而计算地址
  rhs = new_node_bin(ND_MUL, rhs, new_node_num(8, token), token);
  return new_node_bin(ND_ADD, lhs, rhs, token);
}

// 创建SUB节点
// num | ptr - num | ptr
// 未声明 Type，上层会使类型与 lhs 相同
static Node *new_node_sub(Node *lhs, Node *rhs, Token *token) {
  add_type(lhs);
  add_type(rhs);

  // num + num
  if (is_integer(lhs->type) && is_integer(rhs->type)) {
    return new_node_bin(ND_SUB, lhs, rhs, token);
  }

  // ptr - num
  if (lhs->type->base && is_integer(rhs->type)) {
    rhs = new_node_bin(ND_MUL, rhs, new_node_num(8, token), token);
    return new_node_bin(ND_SUB, lhs, rhs, token);
  }

  // ptr - ptr
  // 计算两个指针之间由多少元素
  if (lhs->type->base && rhs->type->base) {
    Node *node = new_node_bin(ND_SUB, lhs, rhs, token);
    // 注意 ptr - ptr 的类型应当为 INT, 这样才有意义
    node->type = TYPE_INT;
    return new_node_bin(ND_DIV, node, new_node_num(8, token), token);
  }

  // num - ptr
  error_token(token, "invalid operands");
  return NULL;
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
// stmt = "return" expr ";"|
//        "if" "(" expr ")" stmt ("else" stmt)?
//        "for" "(" expr_stmt expr? ";" expr? ")" stmt
//        "while" "(" expr ")" stmt
//        "{" compoundStmt |
//        expr_stmt |
// expr_stmt = expr? ";"
// expr = assign
// assign = equality ("=" assign)?
// equality = relational ("==" relational | "!=" relational)*
// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
// add = mul ("+" mul | "-" mul)*
// mul = unary ("*" unary | "/" unary)*
// unary = ("+" | "-" | "*" | "&") unary | primary
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
  Node *node = new_node(ND_BLOCK, token);

  while (!equal(token, "}")) {
    cur->next = stmt(&token, token);
    cur = cur->next;
    // 构造 stmt AST 之后，进行 add_type
    add_type(cur);
  }

  node->body = head.next;
  *rest = token->next;
  return node;
}

// stmt = "return" expr ";"|
//        "if" "(" expr ")" stmt ("else" stmt)?
//        "for" "(" expr_stmt expr? ";" expr? ")" stmt
//        "while" "(" expr ")" stmt
//        "{" compoundStmt |
//        expr_stmt |
PARSER_DEFINE(stmt) {

  // 解析 return 语句
  if (equal(token, "return")) {
    Node *node = new_node(ND_RETURN, token);
    node->lhs = expr(&token, token->next);
    *rest = skip(token, ";");
    return node;
  }

  // 解析 if 语句
  if (equal(token, "if")) {
    Node *node = new_node(ND_IF, token);
    token = skip(token->next, "(");
    node->cond = expr(&token, token);
    token = skip(token, ")");
    node->then = stmt(&token, token);
    if (equal(token, "else"))
      node->els = stmt(&token, token->next);
    *rest = token;
    return node;
  }

  // 解析 for 语句
  if (equal(token, "for")) {
    Node *node = new_node(ND_FOR, token);
    token = skip(token->next, "(");
    node->init = expr_stmt(&token, token);

    if (!equal(token, ";"))
      node->cond = expr(&token, token);
    token = skip(token, ";");

    if (!equal(token, ")"))
      node->inc = expr(&token, token);
    token = skip(token, ")");

    node->then = stmt(rest, token);
    return node;
  }

  // 解析 while 语句
  if (equal(token, "while")) {
    Node *node = new_node(ND_FOR, token);
    token = skip(token->next, "(");
    node->cond = expr(&token, token);
    token = skip(token, ")");
    node->then = stmt(rest, token);
    return node;
  }

  // 解析代码块
  if (equal(token, "{")) {
    return compound_stmt(rest, token->next);
  }

  // 解析 expr
  return expr_stmt(rest, token);
}

// expr_stmt = expr? ";"
PARSER_DEFINE(expr_stmt) {
  if (equal(token, ";")) {
    *rest = token->next;
    return new_node(ND_BLOCK, token);
  }

  Node *node = new_node(ND_EXPR_STMT, token);
  node->lhs = expr(&token, token);
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
    return new_node_bin(ND_ASSIGN, node, assign(rest, token->next), token);
  *rest = token;
  return node;
}

// equality = relational ("==" relational | "!=" relational)*
PARSER_DEFINE(equality) {
  Node *node = relational(&token, token);
  while (true) {
    Token *start = token;
    if (equal(token, "==")) {
      node = new_node_bin(ND_EQ, node, relational(&token, token->next), start);
      continue;
    }

    if (equal(token, "!=")) {
      node = new_node_bin(ND_NE, node, relational(&token, token->next), start);
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
    Token *start = token;
    if (equal(token, "<")) {
      node = new_node_bin(ND_LT, node, add(&token, token->next), start);
      continue;
    }

    if (equal(token, "<=")) {
      node = new_node_bin(ND_LE, node, add(&token, token->next), start);
      continue;
    }

    // lhs > rhs == rhs < lhs
    if (equal(token, ">")) {
      node = new_node_bin(ND_LT, add(&token, token->next), node, start);
      continue;
    }

    if (equal(token, ">=")) {
      node = new_node_bin(ND_LE, add(&token, token->next), node, start);
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
    Token *start = token;
    if (equal(token, "+")) {
      node = new_node_add(node, mul(&token, token->next), start);
      continue;
    }

    if (equal(token, "-")) {
      node = new_node_sub(node, mul(&token, token->next), start);
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
    Token *start = token;
    if (equal(token, "*")) {
      node = new_node_bin(ND_MUL, node, unary(&token, token->next), start);
      continue;
    }
    if (equal(token, "/")) {
      node = new_node_bin(ND_DIV, node, unary(&token, token->next), start);
      continue;
    }
    break;
  }

  *rest = token;
  return node;
}

// unary = ("+" | "-" | "*" | "&") unary | primary
PARSER_DEFINE(unary) {
  // + 一元运算符无影响，跳过即可
  // unary无论如何都会调用 primary 进行 rest 的设置
  // 因此递归调用 unary 时，传入 rest 即可
  // 否则则需要在每次生成新节点后，手动地再设置 rest

  // "+" unary
  if (equal(token, "+"))
    return unary(rest, token->next);

  // "+" unary
  if (equal(token, "-"))
    return new_node_unary(ND_NEG, unary(rest, token->next), token);

  // "*" unary
  if (equal(token, "*"))
    return new_node_unary(ND_DEREF, unary(rest, token->next), token);

  // "&" unary
  if (equal(token, "&"))
    return new_node_unary(ND_ADDR, unary(rest, token->next), token);

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
    return new_node_var(var, token);
  }

  // num
  if (token->kind == TK_NUM) {
    Node *node = new_node_num(token->val, token);
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