#include "rvcc.h"

//
// 二、语法分析， 生成AST
//

// 局部和全局变量的域
typedef struct VarScope VarScope;
struct VarScope {
  VarScope *next; // 下一变量域
  char *name;     // 变量域名称
  Object *var;    // 对应的变量
};

// 代码块域
typedef struct BlockScope BlockScope;
struct BlockScope {
  BlockScope *next; // 指向上一级的域
  VarScope *vars;   // 指向当前域内的变量
};

// 所有域的链表
static BlockScope *BLOCK_SCOPES = &(BlockScope){};

/**
 * 进入块域
 *
 * 进入一个域，则将此域以头插法插入到BLOCK_SCOPES中
 */
static void enter_scope(void) {
  BlockScope *scope = calloc(1, sizeof(BlockScope));
  scope->next = BLOCK_SCOPES;
  BLOCK_SCOPES = scope;
}

/**
 * 离开块域
 *
 * 移动BLOCK_SCOPES至上一个块域（链表头的next）
 */
static void leave_scope(void) { BLOCK_SCOPES = BLOCK_SCOPES->next; }

/**
 * 向块域插入变量
 *
 * @param name 变量域的名称
 * @param var 要插入块域的变量
 *
 * @return 构造好的变量域
 */
static VarScope *push_scope(char *name, Object *var) {
  VarScope *var_scope = calloc(1, sizeof(VarScope));
  var_scope->name = name;
  var_scope->var = var;

  // 将变量域以头插法的形式插入到当前的块域中
  var_scope->next = BLOCK_SCOPES->vars;
  BLOCK_SCOPES->vars = var_scope;
  return var_scope;
}

/**
 * 在所有块域中搜索与 ident token 同名的变量
 *
 * @param token 要检索的变量所属的token
 *
 * @return 匹配到的变量，没有找到则返回NULL
 */
static Object *find_var_by_token(Token *token) {

  // 从当前块域开始检索
  for (BlockScope *scope = BLOCK_SCOPES; scope; scope = scope->next) {
    // 遍历此块域的所有变量
    for (VarScope *var_scope = scope->vars; var_scope;
         var_scope = var_scope->next) {
      if (equal(token, var_scope->name))
        return var_scope->var;
    }
  }
  return NULL;
}

// 变量实例均保存在全局的 LOCALS 链表中
Object *LOCALS;
Object *GLOBALS;

// 获取标识符字符串
static char *get_ident(Token *token) {
  if (token->kind != TK_IDENT)
    error_token(token, "expected an identifier");
  return strndup(token->loc, token->len);
}

// 获取数字
static int get_num(Token *token) {
  if (token->kind != TK_NUM)
    error_token(token, "expected a number");

  return token->val;
}

// 判断是否为类型名称
static bool is_typename(Token *token) {
  return equal(token, "char") | equal(token, "int");
}

/**
 * 创建新的变量
 *
 * @param name 变量的名称
 * @param type 变量的类型
 *
 * @return 构造号的变量
 */
static Object *new_var(char *name, Type *type) {
  Object *var = calloc(1, sizeof(Object));
  var->name = name;
  var->type = type;

  // 创建一个与变量名称相同的变量域，并插入到当前块域中
  push_scope(name, var);
  return var;
}

// 创建 Local 变量，并头插到 LOCALS 中
// 头插保证了每次 LOCALS 更新后，LOCALS 链表头都会变
static Object *new_local_var(char *name, Type *type) {
  Object *var = new_var(name, type);
  var->is_local = true;

  // 头插法
  var->next = LOCALS;
  LOCALS = var;
  return var;
}

// 创建 Global 变量，并头插到 GLOBALS 中
static Object *new_global_var(char *name, Type *type) {
  Object *var = new_var(name, type);
  var->is_local = false;

  // 头插法
  var->next = GLOBALS;
  GLOBALS = var;
  return var;
}

// 生成唯一的变量名称(对匿名变量而言)
static char *new_unique_name(void) {
  static int id = 0;
  return format(".L..%d", id++);
}

// 创建匿名的 Global 变量
static Object *new_anon_glabol_var(Type *type) {
  return new_global_var(new_unique_name(), type);
}

// 新增字符串字面量
static Object *new_string_literal(char *str, Type *type) {
  Object *var = new_anon_glabol_var(type);
  // 字面量的初始值为双引号包裹的部分，而不包括双引号
  var->init_data = str;
  return var;
}

// 递归地将函数形参加入到 Local 中
static void insert_param_to_locals(Type *type) {
  if (type) {
    insert_param_to_locals(type->next);
    new_local_var(get_ident(type->token), type);
  }
}

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
// 未声明 Type，因为调用者会使类型与 lhs 相同
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

  // 将 ptr + num 转化为 ptr + (num * size) 从而计算地址
  // size 为 ptr 所对应 base 的 size
  rhs = new_node_bin(ND_MUL, rhs, new_node_num(lhs->type->base->size, token),
                     token);
  return new_node_bin(ND_ADD, lhs, rhs, token);
}

// 创建SUB节点
// num | ptr - num | ptr
// 未声明 Type，因为调用者会使类型与 lhs 相同
static Node *new_node_sub(Node *lhs, Node *rhs, Token *token) {
  add_type(lhs);
  add_type(rhs);

  // num + num
  if (is_integer(lhs->type) && is_integer(rhs->type)) {
    return new_node_bin(ND_SUB, lhs, rhs, token);
  }

  // ptr - num
  if (lhs->type->base && is_integer(rhs->type)) {
    rhs = new_node_bin(ND_MUL, rhs, new_node_num(lhs->type->base->size, token),
                       token);
    return new_node_bin(ND_SUB, lhs, rhs, token);
  }

  // ptr - ptr
  // 计算两个指针之间由多少元素
  if (lhs->type->base && rhs->type->base) {
    Node *node = new_node_bin(ND_SUB, lhs, rhs, token);
    // 注意 ptr - ptr 的类型应当为 INT, 这样才有意义
    node->type = TYPE_INT;
    return new_node_bin(ND_DIV, node,
                        new_node_num(lhs->type->base->size, token), token);
  }

  // num - ptr
  error_token(token, "invalid operands");
  return NULL;
}

// 传入 Token** 与 Token*，
// 前者作为结果，让调用者能够感知，后者则作为递归中传递的变量
// 因此以下任何一个函数执行完毕之后
// *rest 都必须指向待分析的下一个 token
// 而参数 Token* 仅是值拷贝，对调用者来说不可感知
#define PARSER_DEFINE(name) static Node *(name)(Token * *rest, Token * token)

// program = (function_def | global_variable_def) *
// function_def = declspec function
// function = declspec declarator "{" compound_stmt*
// global_variable_def = declspec global_variable
// global_variable = (declarator ("," declarator))* ";")*
// declspec = "char" | "int"
// declarator = "*"* ident type_suf
// type_suf = "(" func_params | "[" num "]" type_suf | ε
// func_params = param ("," param)*)? ")"
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
// unary = ("+" | "-" | "*" | "&") unary | postfix
// postfix = primary ("[" expr "]")*
// primary = "(" "{" stmt+ "}" ")"
//           | "(" expr ")"
//           | "sizeof" unary
//           | ident
//           | fncall
//           | str
//           | num
// fncall = ident "(" (assign ("," assign)*)? ")"
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
PARSER_DEFINE(postfix);
PARSER_DEFINE(primary);

static Type *declspec(Token **rest, Token *token);
static Type *declarator(Token **rest, Token *token, Type *type);

/**
 * func_params = param ("," param)*)? ")"
 * param = declspec declarator
 *
 * @param rest 指向剩余token指针的指针
 * @param token 正在处理的 token
 * @param type 函数返回参数类型
 *
 * @return 构造好的函数的 Type
 */
static Type *func_params(Token **rest, Token *token, Type *type) {
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

/**
 * type_suf = "(" func_params | "[" num "]" type_suf | ε
 *
 * @param rest 指向剩余token指针的指针
 * @param token 正在处理的 token
 * @return 构造好的 Type。
 */
static Type *type_suf(Token **rest, Token *token, Type *type) {
  if (equal(token, "(")) // 函数
    return func_params(rest, token->next, type);

  if (equal(token, "[")) {
    int len = get_num(token->next);
    token = skip(token->next->next, "]");
    type = type_suf(rest, token, type);
    return array_type(type, len);
  }

  *rest = token;
  return type;
}

/**
 * declspec = "char" | "int"
 *
 * @param rest 指向剩余token指针的指针
 * @param token 正在处理的 token
 * @return 构造好的 Type。
 */
static Type *declspec(Token **rest, Token *token) {
  if (equal(token, "char")) {
    *rest = token->next;
    return TYPE_CHAR;
  }
  *rest = skip(token, "int");
  return TYPE_INT;
}

// declarator = "*"* ident type_suf
// Type *type 为基础类型(如 int)

/**
 * declarator = "*"* ident type_suf
 *
 * @param rest 指向剩余token指针的指针
 * @param token 正在处理的 token
 * @return 构造好的 Type。
 */
static Type *declarator(Token **rest, Token *token, Type *type) {
  // 处理多个 *
  // var, * -> * -> * -> * -> base_type
  while (consume(&token, token, "*")) {
    type = pointer_type(type);
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

// compound_stmt = (declaration | stmt)* "}"
PARSER_DEFINE(compound_stmt) {
  Node head = {};
  Node *cur = &head;
  Node *node = new_node(ND_BLOCK, token);

  // 进入当前块域
  enter_scope();

  while (!equal(token, "}")) {
    if (is_typename(token))
      cur->next = declaration(&token, token);
    else
      cur->next = stmt(&token, token);
    cur = cur->next;
    // 构造 stmt AST 之后，进行 add_type
    add_type(cur);
  }

  // 离开当前块域
  leave_scope();

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

// unary = ("+" | "-" | "*" | "&") unary | postfix
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

  return postfix(rest, token);
}

// postfix = primary ("[" expr "]")*
PARSER_DEFINE(postfix) {
  Node *node = primary(&token, token);

  // x[][]...[]
  while (equal(token, "[")) {
    Token *start = token;
    Node *index = expr(&token, token->next);
    token = skip(token, "]");
    node = new_node_unary(ND_DEREF, new_node_add(node, index, start), start);
  }

  *rest = token;
  return node;
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

// primary = "(" expr ")" | "sizeof" unary | ident | fncall | str | num
PARSER_DEFINE(primary) {
  // "(" "{" stmt+ "}" ")"
  if (equal(token, "(") && equal(token->next, "{")) {
    Node *node = new_node(ND_STMT_EXPR, token);
    node->body = compound_stmt(&token, token->next->next)->body;
    *rest = skip(token, ")");
    return node;
  }

  // "(" expr ")"
  if (equal(token, "(")) {
    Node *node = expr(&token, token->next);
    *rest = skip(token, ")");
    return node;
  }

  // "sizeof" unary
  if (equal(token, "sizeof")) {
    Node *node = unary(rest, token->next);
    add_type(node);
    return new_node_num(node->type->size, token);
  }

  // ident
  if (token->kind == TK_IDENT) {
    // fncall
    if (equal(token->next, "(")) {
      return fncall(rest, token);
    }

    // ident var
    Object *var = find_var_by_token(token);
    if (!var) // 变量在声明中定义，必须存在`
      error_token(token, "undefined variable");

    *rest = token->next;
    return new_node_var(var, token);
  }

  // str
  if (token->kind == TK_STR) {
    Object *var = new_string_literal(token->str, token->type);
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

/*
 * global_variable = (declarator ("," declarator))* ";")*
 *
 * 当前仅支持全局变量的声明
 * @param type 为基础类型，如 int
 */
static Token *global_variable(Token *token, Type *base) {
  bool is_first = true;
  while (!consume(&token, token, ";")) {
    // 处理 int x,y 格式
    if (!is_first)
      token = skip(token, ",");
    is_first = false;

    Type *type = declarator(&token, token, base);
    new_global_var(get_ident(type->token), type);
  }

  return token;
}

/*
 * function = declarator "{" compoundStmt*
 *
 * @param base 为基础类型，即返回值的类型
 */
static Token *function(Token *token, Type *base) {
  // type为函数类型
  // 指向 return type, 同时判断指针
  // type->token 指向了 ident 对应的 token
  Type *type = declarator(&token, token, base);
  Object *func = new_global_var(get_ident(type->token), type);
  func->is_function = true;

  // 清空局部变量
  LOCALS = NULL;

  // 函数参数
  insert_param_to_locals(type->params);
  func->params = LOCALS;

  token = skip(token, "{");
  func->body = compound_stmt(&token, token);
  func->locals = LOCALS;
  return token;
}

// 尝试生成 declarator 来判断是否是 FUNC
static bool is_function(Token *token) {
  Type dummy = {};
  Type *type = declarator(&token, token, &dummy);
  return type->kind == TY_FUNC;
}

// program = (function_def | global_variable_def) *
// function_def = declspec function
// global_variable_def = declspec global_variable
Object *parse(Token *token) {
  GLOBALS = NULL;

  while (token->kind != TK_EOF) {
    // 函数返回值类型
    Type *type = declspec(&token, token);

    // function
    if (is_function(token)) {
      token = function(token, type);
      continue;
    }

    // global_variable
    token = global_variable(token, type);
  }

  return GLOBALS;
}