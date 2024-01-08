#define _POSIX_C_SOURCE 200809L

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
  TK_IDENT,   // 标记符，可以为变量名、函数名等
  TK_PUNCT,   // 操作符: + -
  TK_KEYWORD, // 关键字
  TK_NUM,     // 数字
  TK_EOF,     // 文件终止符
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
  ND_NEG,
  ND_EQ,
  ND_NE,
  ND_LT,
  ND_LE,
  ND_ASSIGN,    // 赋值
  ND_RETURN,    // 返回
  ND_IF,        // 条件判断
  ND_FOR,       //  for / while 循环
  ND_BLOCK,     // { ... } 代码块
  ND_EXPR_STMT, // 表达式
  ND_VAR,       // 变量
  ND_NUM,
} NodeKind;

// AST 节点
typedef struct Node Node;

// 本地变量
typedef struct Object Object;
struct Object {
  Object *next;
  char *name; // 变量名称
  int offset; // 相对栈顶的偏移量
};

// 函数
typedef struct Function Function;
struct Function {
  Node *body;     // 函数体(AST)
  Object *locals; // 本地变量链表头
  int stack_size; // 栈大小
};

struct Node {
  NodeKind kind;
  Node *next;
  Node *lhs;
  Node *rhs;

  Node *cond; // 条件
  Node *then; // 判断成立
  Node *els;  // 判断失败
  Node *init; // 循环初始化语句
  Node *inc;  // 循环变量变化语句

  Node *body; // 代码块

  Object *var; // 存储 ND_VAR 种类的变量信息
  int val;
};

// 语法解析入口函数
Function *parse(Token *token);

//
// 三、语义分析，生成代码
//

// 代码生成入口函数
void codegen(Function *prog);