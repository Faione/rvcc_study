#include "rvcc.h"
#include <stdlib.h>
#include <string.h>

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

// 获取标识符字符串
static char *get_ident(Token *token) {
  if (token->kind != TK_IDENT)
    error_token(token, "expected an identifier");
  return strndup(token->loc, token->len);
}

// 创建以 Local 变量，并头插到 LOCALS 中
// 头插保证了每次 LOCALS 更新后，LOCALS 链表头都会变
static Object *new_local_var(char *name, Type *type) {
  Object *var = calloc(1, sizeof(Object));
  var->name = name;
  var->type = type;

  // 头插法
  var->next = LOCALS;
  LOCALS = var;
  return var;
}

// 递归地将函数形参加入到 Local 中
static void insert_param_to_locals(Type *param) {
  if (param) {
    insert_param_to_locals(param->next);
    new_local_var(get_ident(param->token), param);
  }
}

// program = function*
// function = declspec declarator "{" compound_stmt*
// declspec = "int"
// declarator = "*"* ident type_suf
// type_suf = ("(" func_params? ")")?
// func_params = param ("," param)*
// param = declspec declarator

// compound_stmt = (declaration | stmt)* "}"
// declaration =
//         declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"
// stmt = "return" expr ";"|
//        "if" "(" expr ")" stmt ("else" stmt)? |
//        "for" "(" expr_stmt expr? ";" expr? ")" stmt |
//        "while" "(" expr ")" stmt |
//        "{" compoundStmt |
//        expr_stmt
// expr_stmt = expr? ";"
// expr = assign
// assign = equality ("=" assign)?
// equality = relational ("==" relational | "!=" relational)*
// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
// add = mul ("+" mul | "-" mul)*
// mul = unary ("*" unary | "/" unary)*
// unary = ("+" | "-" | "*" | "&") unary | primary
// primary = "(" expr ")" | ident | fncall | num
// fncall = ident "(" (assign ("," assign)*)? ")"

// 传入 Token** 与 Token*，
// 前者作为结果，让调用者能够感知，后者则作为递归中传递的变量
// 因此以下任何一个函数执行完毕之后
// *rest 都必须指向待分析的下一个 token
// 而参数 Token* 仅是值拷贝，对调用者来说不可感知
#define PARSER_DEFINE(name) static Node *(name)(Token * *rest, Token * token)
PARSER_DEFINE(declaration);
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

static Type *declspec(Token **rest, Token *token);
static Type *declarator(Token **rest, Token *token, Type *type);

// type_suf = ("(" func_params? ")")?
// func_params = param ("," param)*
// param = declspec declarator
// Type *type 为基础类型(如 int)
static Type *type_suf(Token **rest, Token *token, Type *type) {
  if (equal(token, "(")) { // 函数
    token = token->next;

    // 存储形参
    Type head = {};
    Type *cur = &head;

    while (!equal(token, ")")) {
      if (cur != &head)
        token = skip(token, ",");

      Type *base_type = declspec(&token, token);
      // 不可将 declspec 嵌套的原因是
      // declarator 的前几个参数会先准备好，然后再调用 declspec
      // 而因此导致的 token 变化无法被 declarator 感知
      // 因此不能将 declspec 进行嵌套
      Type *dec_type = declarator(&token, token, base_type);

      // dec_type 为局部变量，地址不会改变，因此每次都需要拷贝，否则链表就会成环
      cur->next = copy_type(dec_type);
      cur = cur->next;
    }

    // 将参数加入到函数 Type 中
    type = func_type(type);
    type->params = head.next;

    *rest = token->next;
    return type;
  }

  *rest = token;
  return type;
}

// declspec = "int"
static Type *declspec(Token **rest, Token *token) {
  *rest = skip(token, "int");
  return TYPE_INT;
}

// declarator = "*"* ident type_suf
// Type *type 为基础类型(如 int)
static Type *declarator(Token **rest, Token *token, Type *type) {
  // 处理多个 *
  // var, * -> * -> * -> * -> base_type
  while (consume(&token, token, "*")) {
    type = pointer_to(type);
  }

  if (token->kind != TK_IDENT)
    error_token(token, "expected a variable name");

  // 若是变量，则保有传入的 type
  // 若是函数，则type会变为 FUNC， 并指向传入的类型
  type = type_suf(rest, token->next, type);

  // 将 TK_IDENT token 保存到 type 中
  type->token = token;

  return type;
}

// compoundStmt = stmt* "}"
PARSER_DEFINE(compound_stmt) {
  Node head = {};
  Node *cur = &head;
  Node *node = new_node(ND_BLOCK, token);

  while (!equal(token, "}")) {
    if (equal(token, "int"))
      cur->next = declaration(&token, token);
    else
      cur->next = stmt(&token, token);
    cur = cur->next;
    // 构造 stmt AST 之后，进行 add_type
    add_type(cur);
  }

  node->body = head.next;
  *rest = token->next;
  return node;
}

// declaration =
//         declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"
PARSER_DEFINE(declaration) {
  Type *base_type = declspec(&token, token);

  // 处理多个 declarator ("=" expr)?
  Node head = {};
  Node *cur = &head;

  int i = 0;
  while (!equal(token, ";")) {

    // 除第一个以外，在开始时都要跳过 ","
    if (i++ > 0)
      token = skip(token, ",");

    // 获取变量类型
    Type *type = declarator(&token, token, base_type);
    // 构造一个变量
    Object *var = new_local_var(get_ident(type->token), type);

    // 不存在赋值，则进行跳过(这种情况下cur不会进行更新，因此不能通过cur判断是否要跳过`,`)
    if (!equal(token, "="))
      continue;

    // 左值为变量
    Node *lhs = new_node_var(var, type->token);
    // 解析赋值语句
    Node *rhs = assign(&token, token->next);
    Node *node = new_node_bin(ND_ASSIGN, lhs, rhs, token);

    cur->next = new_node_unary(ND_EXPR_STMT, node, token);
    cur = cur->next;
  }

  Node *node = new_node(ND_BLOCK, token);
  node->body = head.next;
  *rest = token;
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

// fncall = ident "(" (assign ("," assign)*)? ")"
PARSER_DEFINE(fncall) {
  Token *start = token;
  token = token->next->next;

  Node head = {};
  Node *cur = &head;

  // 构造参数
  while (!equal(token, ")")) {
    if (cur != &head)
      token = skip(token, ",");

    cur->next = assign(&token, token);
    cur = cur->next;
  }

  Node *node = new_node(ND_FNCALL, start);
  node->args = head.next;
  node->func_name = strndup(start->loc, start->len);

  // 跳过 ")"
  *rest = skip(token, ")");
  return node;
}

// primary = "(" expr ")" | ident | fncall | num
PARSER_DEFINE(primary) {
  // "(" expr ")"
  if (equal(token, "(")) {
    Node *node = expr(&token, token->next);
    *rest = skip(token, ")");
    return node;
  }

  // ident
  if (token->kind == TK_IDENT) {
    // fncall
    if (equal(token->next, "(")) {
      return fncall(rest, token);
    }

    // ident var
    Object *var = find_var_by_token(token);
    if (!var) // 变量在声明中定义，必须存在
      error_token(token, "undefined variable");

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

// function = declspec declarator "{" compoundStmt*
static Function *function(Token **rest, Token *token) {
  // 返回值的基础类型
  Type *type = declspec(&token, token);

  // type为函数类型
  // 指向 return type, 同时判断指针
  // type->token 指向了 ident 对应的 token
  type = declarator(&token, token, type);

  // 清空局部变量
  LOCALS = NULL;

  Function *func = calloc(1, sizeof(Function));
  func->name = get_ident(type->token);
  // 函数参数
  insert_param_to_locals(type->params);
  func->params = LOCALS;

  token = skip(token, "{");
  func->body = compound_stmt(rest, token);
  func->locals = LOCALS;
  return func;
}

// program = function*
Function *parse(Token *token) {

  Function head = {};
  Function *cur = &head;

  while (token->kind != TK_EOF) {
    cur->next = function(&token, token);
    cur = cur->next;
  }

  return head.next;
}