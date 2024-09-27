#pragma once

//
// 一、词法分析
//

typedef enum {
  TK_IDENT,   // 标记符，可以为变量名、函数名等
  TK_PUNCT,   // 操作符: + -
  TK_KEYWORD, // 关键字
  TK_STR,     // 字符串字面量
  TK_NUM,     // 数字
  TK_EOF,     // 文件终止符
} TokenKind;

typedef struct Token {
  TokenKind kind; // 类型
  Token *next;    // 下一个终结符
  char *loc;      // 在被解析字符串中的位置
  int len;        // 长度
  int line;       // 行号

  union {
    // TK_NUM
    int val; // 值

    // TK_STR
    struct {
      Type *type;
      char *str; // 字符串字面量，包括 '\0'
    };
  };
} Token;