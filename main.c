#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    error("expect '%s'", str);
  }
  return token->next;
}

// 返回 TK_NUM Token 的值
static int get_num(Token *token) {
  if (token->kind != TK_NUM) {
    error("expect a number");
  }
  return token->val;
}

// 终结符解析
// head -> token1 -> token2 -> token3
static Token *tokenize(char *p) {
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
    if (*p == '+' || *p == '-') {
      cur->next = new_token(TK_PUNCT, p, p + 1);
      cur = cur->next;
      ++p;
      continue;
    }

    error("invalid token: %c", *p);
  }

  // 解析结束之后追加一个 EOF
  cur->next = new_token(TK_EOF, p, p);
  // head 实际是一个 dummy head
  return head.next;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "%s: invalid number of arguments\n", argv[0]);
    return 1;
  }
  char *p = argv[1];

  Token *token = tokenize(p);

  printf("  .globl main\n");
  printf("main:\n");

  // 假设算式的第一个是数字
  printf("  li a0, %d\n", get_num(token));
  token = token->next;

  // 遍历 token
  while (token->kind != TK_EOF) {
    if (equal(token, "+")) {
      token = token->next;
      printf("  addi a0, a0, %d\n", get_num(token));
      token = token->next;
      continue;
    }

    // 不是 +，则判断 -
    token = skip(token, "-");
    printf("  addi a0, a0, -%d\n", get_num(token));
    token = token->next;
  }
  printf("  ret\n");
  return 0;
}
