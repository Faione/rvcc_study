#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// 输出错误信息
void error(char *fmt, ...);
// 指示错误信息并退出程序
void error_at(char *loc, char *fmt, ...);
// 指示 token 解析出错，并退出程序
void error_token(Token *token, char *fmt, ...);

// 判断 token 的值是否与给定的 char* 值相同
bool equal(Token *token, char *str);
// 跳过值与 str 相同的 token
Token *skip(Token *token, char *str);
// 终结符解析
// head -> token1 -> token2 -> token3
Token *tokenize(char *p);

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
  ND_EQ,
  ND_NE,
  ND_LT,
  ND_LE,
} NodeKind;

typedef struct Node Node;
struct Node {
  NodeKind kind;
  Node *lhs;
  Node *rhs;
  int val;
};

// 语法解析入口函数
Node *parse(Token *token);

//
// 三、语义分析，生成代码
//

// 代码生成入口函数
void codegen(Node *node);