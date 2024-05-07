# 支持多个转义字符

转义字符是元字符的一种特殊情况, 一般通过 `\` 标识转义的开始，结合之后的字符来指定特定的字符
- 转义字符是字面量中字符的特殊情况，因此需要在 tokenize 中进行特殊处理

## 词法分析

需要支持对字面量中，形如 `\n` 的处理，即
- 遍历输入，获取字符串字面量的结尾指针(右引号)
- 创建一个 字符串字面量本身长度 + 1 的 buf, 将其中的内容初始化为全 '\0'
- 再次遍历输入，当遇到 `\` 字符时，就进入 转义字符 处理函数，根据随后的一个字符转义得到真正的字符(不在转义列表中，则返回原字符)


>> 字符串中的 \n 是转义字符，本质仍然是字符，而按下回车时，会往文本中插入一个回车，这是转义字符 `\n` 所对应的真正的字符

```c
static char *read_string_literal_end(char *p) {
  char *start = p;
  for (; *p != '"'; p++) {
    if (*p == '\n' || *p == '\0') // 单行结尾
      error_at(start, "unclosed string literal");
  }

  return p;
}
```
带有转义字符串的字符串字面量的解析

```c
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
      buf[real_len++] = read_escaped_char(p + 1);
      p += 2;
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
```

# 支持8进制转义字符

形如 `\20` 被认为时是一个 8 进制的转义字符，其应当被解释为 `16`, 编译器中需要支持最长3位的8进制转义字符解析
- 由于此时不止依赖转义字符的后一位，因此需要在 read_escaped_char 的传入参数中增加一个指针用于移动

## 词法分析

```c
#define CHAR_OCTAL(x) '0' <= x &&x <= '7'

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


  ...
```

# 支持16进制转义字符

形如 `\xaff`即是16进制数，转义过程主要有两个部分
- 将当前字符转义为十进制
- 累计各个位上的值

```c
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
```
