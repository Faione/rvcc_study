#include "rvcc.h"
#include <stdbool.h>
//
// 一、词法分析
//

// 计入当前的输入字符串
static char *CUR_INPUT;

// 输出错误信息
void error(char *fmt, ...) {
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
void error_at(char *loc, char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  verror_at(loc, fmt, va);
  exit(1);
}

// 指示 token 解析出错，并退出程序
void error_token(Token *token, char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  verror_at(token->loc, fmt, va);
  exit(1);
}

// Token 构造函数
// [start, end)
static Token *new_token(TokenKind kind, char *start, char *end) {
  Token *token = calloc(1, sizeof(Token));
  token->kind = kind;
  token->loc = start;
  token->len = end - start;
  return token;
}

// 判断 token 的值是否与给定的 char* 值相同
bool equal(Token *token, char *str) {
  // 比较字符串LHS（左部），RHS（右部）的前N位，S2的长度应大于等于N.
  // 比较按照字典序，LHS<RHS回负值，LHS=RHS返回0，LHS>RHS返回正值
  // 同时确保，此处的Op位数=N
  return memcmp(token->loc, str, token->len) == 0 && str[token->len] == '\0';
}

// 跳过值与 str 相同的 token
Token *skip(Token *token, char *str) {
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

// 比较 str 是否以 sub_str 为开头
static bool starts_with(char *str, char *sub_str) {
  return strncmp(str, sub_str, strlen(sub_str)) == 0;
}

// 标识符首字母判断
// [a-zA-Z_]
static bool is_ident_head(char c) {
  return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || '_' == c;
}

// 标识符非首字母判断
// [a-zA-Z0-9_]
static bool is_ident_rest(char c) {
  return is_ident_head(c) || ('0' <= c && c <= '9');
}

// 返回运算符长度
static int read_punct(char *p) {
  // 判断长度是否为 2
  if (starts_with(p, "==") || starts_with(p, "!=") || starts_with(p, "<=") ||
      starts_with(p, ">=")) {
    return 2;
  }

  return ispunct(*p) ? 1 : 0;
}

static char *keywords[] = {"return", "if", "else"};

// 判断 ident token 是否在 keywords 中
static bool is_keyword(Token *token) {
  for (int i = 0; i < sizeof(keywords) / sizeof(*keywords); i++) {
    if (equal(token, keywords[i])) {
      return true;
    }
  }

  return false;
}

// 将符合关键字的 token 类型修改为 TK_KEYWORD
static void convert_keywords(Token *token) {
  for (Token *t = token; t; t = t->next) {
    if (is_keyword(token)) {
      t->kind = TK_KEYWORD;
    }
  }
}

// 终结符解析
// head -> token1 -> token2 -> token3
Token *tokenize(char *p) {
  CUR_INPUT = p;
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

    // 解析标记符
    // [a-zA-Z_][a-zA-Z0-9_]*
    if (is_ident_head(*p)) {
      char *start = p;
      do {
        ++p;
      } while (is_ident_rest(*p));
      cur->next = new_token(TK_IDENT, start, p);
      cur = cur->next;
      continue;
    }

    // 解析操作符
    int punct_len = read_punct(p);
    if (punct_len) {
      cur->next = new_token(TK_PUNCT, p, p + punct_len);
      cur = cur->next;
      p += punct_len;
      continue;
    }

    error_at(p, "invalid token");
  }

  // 解析结束之后追加一个 EOF
  cur->next = new_token(TK_EOF, p, p);

  convert_keywords(head.next);

  // head 实际是一个 dummy head
  return head.next;
}
