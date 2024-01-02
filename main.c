#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 计入当前的输入字符串
static char *CUR_INPUT;

// 输出错误信息
static void error(char *fmt, ...) {
  // 可变参数存储在 va_list 中
  va_list va;
  // 获取 fmt 之后的所有参数到 va 中
  va_start(va, fmt);
  // 输出 va_list 类型的参数
  vfprintf(stderr, fmt, va);
  fprintf(stderr, "\n");
  va_end(va);
  exit(1);
}

// 指示错误出现的位置
static void verror_at(char *loc, char *fmt, va_list va) {
  // 输出源信息
  fprintf(stderr, "%s\n", CUR_INPUT);

  // 计算错误出现的位置并输出错误信息
  int pos = loc - CUR_INPUT;
  // %*s 将会打印 pos 长度的字符串，若参数不满足长度 pos ，则使用空格补全
  fprintf(stderr, "%*s", pos, "");
  // 指示符
  fprintf(stderr, "^ ");
  vfprintf(stderr, fmt, va);
  fprintf(stderr, "\n");
  va_end(va);
}

// 指示错误信息并退出程序
static void error_at(char *loc, char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  verror_at(loc, fmt, va);
  exit(1);
}

//
// 一、词法分析
//

typedef enum {
  TK_PUNCT, // 操作符: + -
  TK_NUM,   // 数字
  TK_EOF,   // 文件终止符
} TokenKind;

typedef struct Token Token;

struct Token {
  TokenKind kind; // 类型
  Token *next;    // 下一个终结符
  int val;        // 值
  char *loc;      // 在被解析字符串中的位置
  int len;        // 长度
};

// 指示 token 解析出错，并退出程序
static void error_token(Token *token, char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  verror_at(token->loc, fmt, va);
  exit(1);
}

// Token 构造函数
static Token *new_token(TokenKind kind, char *start, char *end) {
  Token *token = calloc(1, sizeof(Token));
  token->kind = kind;
  token->loc = start;
  token->len = end - start;
  return token;
}

// 判断 token 的值是否与给定的 char* 值相同
static bool equal(Token *token, char *str) {
  // 比较字符串LHS（左部），RHS（右部）的前N位，S2的长度应大于等于N.
  // 比较按照字典序，LHS<RHS回负值，LHS=RHS返回0，LHS>RHS返回正值
  // 同时确保，此处的Op位数=N
  return memcmp(token->loc, str, token->len) == 0 && str[token->len] == '\0';
}

// 跳过值与 str 相同的 token
static Token *skip(Token *token, char *str) {
  if (!equal(token, str)) {
    error_token(token, "expect '%s'", str);
  }
  return token->next;
}

// 返回 TK_NUM Token 的值
static int get_num(Token *token) {
  if (token->kind != TK_NUM) {
    error_token(token, "expect a number");
  }
  return token->val;
}

// 终结符解析
// head -> token1 -> token2 -> token3
static Token *tokenize() {
  char *p = CUR_INPUT;
  Token head = {};
  Token *cur = &head;
  while (*p) {
    // 跳过空白字符, \t \n
    if (isspace(*p)) {
      ++p;
      continue;
    }

    // 解析数字
    if (isdigit(*p)) {
      // 创建一个 Token 用来保存数字, 此时 start 与 end 并无意义
      cur->next = new_token(TK_NUM, p, p);
      cur = cur->next;
      const char *old_p = p;
      // 执行之后，p指向的是第一个非数字字符
      cur->val = strtoul(p, &p, 10);
      cur->len = p - old_p;
      continue;
    }

    // 解析操作符
    if (ispunct(*p)) {
      cur->next = new_token(TK_PUNCT, p, p + 1);
      cur = cur->next;
      ++p;
      continue;
    }
    error_at(p, "invalid token");
  }

  // 解析结束之后追加一个 EOF
  cur->next = new_token(TK_EOF, p, p);
  // head 实际是一个 dummy head
  return head.next;
}

//
// 二、语法分析， 生成AST
//

// AST 节点种类
typedef enum {
  ND_ADD,
  ND_SUB,
  ND_MUL,
  ND_DIV,
  ND_NUM,
  ND_NEG,
} NodeKind;

typedef struct Node Node;
struct Node {
  NodeKind kind;
  Node *lhs;
  Node *rhs;
  int val;
};

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

// expr = mul ("+" mul | "-" mul)*
//   expr 由多个 mul 相加减构成
// mul = unary ("*" unary | "/" unary)*
//   mul 由多个 primary 相乘除构成
// unary = ("+" | "-") unary | primary
//   unary 支持一元运算符
// primary = "(" expr ")" | num
//   primary 可以是括号内的 expr 或 数字

// 传入 Token** 与 Token*，
// 前者作为结果，让调用者能够感知，后者则作为递归中传递的变量
// 因此以下任何一个函数执行完毕之后
// *rest 都必须指向待分析的下一个 token
// 而参数 Token* 仅是值拷贝，对调用者来说不可感知
static Node *expr(Token **rest, Token *token);
static Node *mul(Token **rest, Token *token);
static Node *unary(Token **rest, Token *token);
static Node *primary(Token **rest, Token *token);

// expr = mul ("+" mul | "-" mul)*
static Node *expr(Token **rest, Token *token) {
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
static Node *mul(Token **rest, Token *token) {
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
static Node *unary(Token **rest, Token *token) {
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

// primary = "(" expr ")" | num
static Node *primary(Token **rest, Token *token) {
  // "(" expr ")"
  if (equal(token, "(")) {
    Node *node = expr(&token, token->next);
    *rest = skip(token, ")");
    return node;
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
static void gen_code(Node *node) {
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
    gen_code(node->rhs);
    printf("  neg a0, a0\n");
    return;
  default:
    break;
  }

  // 递归右节点
  gen_code(node->rhs);
  // 右侧的结果保存在 a0 中，压入到栈
  push();
  // 递归左节点
  gen_code(node->lhs);
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
  default:
    break;
  }
  error("invalid expression");
}

int main(int argc, char **argv) {
  if (argc != 2)
    error("%s: invalid number of arguments", argv[0]);

  CUR_INPUT = argv[1];
  // 1. 词法分析
  Token *token = tokenize();

  // 2. 语法分析
  Node *node = expr(&token, token);

  if (token->kind != TK_EOF)
    error_token(token, "extra token");

  // 3. 语义分析
  printf("  .globl main\n");
  printf("main:\n");

  gen_code(node);

  printf("  ret\n");
  assert(STACK_DEPTH == 0);
  return 0;
}
