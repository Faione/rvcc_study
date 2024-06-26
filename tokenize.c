#include "rvcc.h"

//
// 一、词法分析
//

// 记录当前正在处理的文件名
static char *CUR_FILENAME;

// 记录当前的输入字符串
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
// foo.c:10: x = y + 1;
//               ^ <错误信息>
static void verror_at(int line, char *loc, char *fmt, va_list va) {
  char *start = loc, *end = loc;

  // 移动 line 到 loc 所在行的起始位置
  // CUR_INPUT是字符串的第一个字符
  while (CUR_INPUT < start && start[-1] != '\n')
    start--;
  // filename:line
  // indent记录输出了多少个字符
  int indent = fprintf(stderr, "%s:%d: ", CUR_FILENAME, line);
  // 输出存在错误的行到end为止的文本
  fprintf(stderr, "%.*s\n", (int)(end - start), start);

  // 计算错误信息要添加的位点
  int pos = loc - start + indent;

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
  int line = 1;
  for (char *p = CUR_INPUT; p < loc; p++) {
    if (*p == '\n')
      line++;
  }

  va_list va;
  va_start(va, fmt);
  verror_at(line, loc, fmt, va);
  exit(1);
}

// 指示 token 解析出错，并退出程序
void error_token(Token *token, char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  verror_at(token->line, token->loc, fmt, va);
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

// 尝试跳过 str, rest保存跳过之后的 Token*, 返回值表示是否跳过成功
bool consume(Token **rest, Token *token, char *str) {
  if (equal(token, str)) {
    // 移动到下一个
    *rest = token->next;
    return true;
  }

  *rest = token;
  return false;
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

// 判断 ident token 是否在 keywords 中
static bool is_keyword(Token *token) {
  static char *keywords[] = {"return", "if",     "else", "for",
                             "while",  "sizeof", "int",  "char"};

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
    if (is_keyword(t)) {
      t->kind = TK_KEYWORD;
    }
  }
}

#define CHAR_OCTAL(x) '0' <= x &&x <= '7'

// 返回一位十六进制转十进制W
//
// hexDigit = [0-9a-fA-F]W
//
// 16: 0 1 2 3 4 5 6 7 8 9  A  B  C  D  E  F
//
// 10: 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
static int from_hex(char c) {
  if ('0' <= c && c <= '9')
    return c - '0';
  if ('a' <= c && c <= 'f')
    return c - 'a' + 10;
  return c - 'A' + 10;
}

// 读取转义字符
// 返回字符本意的 char
static int read_escaped_char(char **pos, char *p) {
  if (CHAR_OCTAL(*p)) {
    int num = *p++ - '0';

    // 限制处理3位的8进制数
    for (; CHAR_OCTAL(*p) && p - *pos < 4; p++) {
      num = (num << 3) + (*p - '0');
    }

    *pos = p;
    return num;
  }

  if (*p == 'x') {
    p++;
    if (!isxdigit(*p))
      error_at(p, "invalid hex escape sequence");

    int num = 0;
    // 读取一位或多位十六进制数字
    // \xWXYZ = ((W*16+X)*16+Y)*16+Z
    for (; isxdigit(*p); p++) {
      num = (num << 4) + from_hex(*p);
    }
    *pos = p;
    return num;
  }

  *pos = p + 1;

  switch (*p) {
  case 'a': // 响铃（警报）
    return '\a';
  case 'b': // 退格
    return '\b';
  case 't': // 水平制表符，tab
    return '\t';
  case 'n': // 换行
    return '\n';
  case 'v': // 垂直制表符
    return '\v';
  case 'f': // 换页
    return '\f';
  case 'r': // 回车
    return '\r';
  // 属于GNU C拓展
  case 'e': // 转义符
    return 27;
  default: // 默认将原字符返回
    return *p;
  }
}

// 读取到字面量的结尾(右引号)
//
// 返回时，p 指向 右引号
static char *read_string_literal_end(char *p) {
  char *start = p;
  for (; *p != '"'; p++) {
    if (*p == '\n' || *p == '\0') // 单行结尾
      error_at(start, "unclosed string literal");
    if (*p == '\\') // 跳过转义符号，随后也会跳过待转义的字符
      p++;
  }

  return p;
}

// 读取字符串字面量
//
// 形如 "foo" 的 token 即是 string literal
static Token *read_string_literal(char *start) {
  char *end = read_string_literal_end(start + 1);

  // 存储处理之后的字符串字面量, buf 大小为 总字符数 + 1
  char *buf = calloc(1, end - start);
  int real_len = 0;

  // 遍历双引号包裹的部分
  for (char *p = start + 1; p < end;) {
    if (*p == '\\') { // 解析转义
      buf[real_len++] = read_escaped_char(&p, p + 1);
    } else
      buf[real_len++] = *p++;
  }

  Token *token = new_token(TK_STR, start, end + 1);

  // 字符串字面量类型为 char[]，包括了双引号
  // 末尾多出的一位是 '\0' (calloc时进行的初始化)
  token->type = array_type(TYPE_CHAR, real_len + 1);
  token->str = buf;
  return token;
}

static void add_line_numbers(Token *token) {
  char *p = CUR_INPUT;

  // 计算行号
  // start 之前遇到 n 个 '\n'，意味着有 n 行
  // 而 start 位于第 n + 1 行
  int n = 1;

  do {
    if (p == token->loc) {
      token->line = n;
      token = token->next;
    }
    if (*p == '\n')
      n++;

  } while (*p++);
}

// 终结符解析
// head -> token1 -> token2 -> token3
Token *tokenize(char *filename, char *p) {
  CUR_FILENAME = filename;
  CUR_INPUT = p;
  Token head = {};
  Token *cur = &head;

  while (*p) {
    // 跳过行注释
    if (starts_with(p, "//")) {
      p += 2;
      while (*p != '\n')
        p++;
      continue;
    }

    // 跳过块注释
    if (starts_with(p, "/*")) {
      // 在剩余字符串中寻找 "*/" 的位置
      char *q = strstr(p + 2, "*/");
      if (!q)
        error_at(p, "unclosed block comment");
      p = q + 2;
      continue;
    }

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

    // 解析字符串字面量
    if (*p == '"') {
      cur->next = read_string_literal(p);
      cur = cur->next;
      p += cur->len;
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

  // 为所有token增加行号
  add_line_numbers(head.next);
  // 将所有keyword字符token类型修改为keyword
  convert_keywords(head.next);

  // head 实际是一个 dummy head
  return head.next;
}

// 从文件中读取文本到字符数组中
static char *read_file(char *path) {
  FILE *in;

  if (strcmp(path, "-") == 0) {
    // 文件名为 "-" 时，从stdin读取文本
    in = stdin;
  } else {
    in = fopen(path, "r");
    if (!in)
      error("can't open %s: %s", path, strerror(errno));
  }

  char *buf;
  size_t buf_len;
  // 创建out数据流，将in中的文本数据读取到流中
  // buf指针指向out中的数据，buf_len为数据长度
  FILE *out = open_memstream(&buf, &buf_len);

  // 使用 read_buf 作为中转，将数据从 in 读取到 out 中
  while (true) {
    char *read_buf[4096];
    int num_read = fread(read_buf, sizeof(char), sizeof(read_buf), in);
    if (num_read == 0)
      break;
    fwrite(read_buf, sizeof(char), num_read, out);
  }

  if (in != stdin)
    fclose(in);

  // 刷新写缓冲区，保证所有内容都写入到 out 中
  fflush(out);

  // 保证以 `\n` 结尾
  if (buf_len == 0 || buf[buf_len - 1] != '\n')
    fputc('\n', out);

  // 满足字符串以 `\0` 结尾的要求
  fputc('\0', out);

  return buf;
}

Token *tokenize_file(char *path) { return tokenize(path, read_file(path)); }