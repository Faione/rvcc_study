#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Node Node;
typedef struct Type Type;

//
// 字符串处理
//

char *format(char *fmt, ...);

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

typedef struct Token Token;
struct Token {
  TokenKind kind; // 类型
  Token *next;    // 下一个终结符
  char *loc;      // 在被解析字符串中的位置
  int len;        // 长度

  union {
    // TK_NUM
    int val; // 值

    // TK_STR
    struct {
      Type *type;
      char *str; // 字符串字面量，包括 '\0'
    };
  };
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
// 尝试跳过 str, rest保存跳过之后的 Token*, 返回值表示是否跳过成功
bool consume(Token **rest, Token *token, char *str);
// 终结符解析
// token1 -> token2 -> token3
Token *tokenize_file(char *path);

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
  ND_ADDR,      // 取地址
  ND_DEREF,     // 解引用
  ND_RETURN,    // 返回
  ND_IF,        // 条件判断
  ND_FOR,       //  for / while 循环
  ND_BLOCK,     // { ... } 代码块
  ND_FNCALL,    // 函数调用
  ND_EXPR_STMT, // 表达式语句
  ND_STMT_EXPR, // 语句表达式
  ND_VAR,       // 变量
  ND_NUM,
} NodeKind;

// 本地变量
typedef struct Object Object;

struct Object {
  Object *next; // 下一个Object
  char *name;   // 名称
  Type *type;   // 类型

  bool is_local;    // 局部变量与否
  bool is_function; // ObjectMember 属性

  union {
    // Var
    int offset; // 相对栈顶的偏移量

    // Function
    struct {
      Object *params; // 形参
      Node *body;     // 函数体(AST)
      Object *locals; // 本地变量
      int stack_size; // 栈大小
    };

    // String Literal
    char *init_data; // 初始值
  };
};

struct Node {
  Token *token;  // 节点对应终结符
  NodeKind kind; // 节点的类型
  Type *type;    // 节点中数据的类型
  Node *next;

  union {

    // [l/r] ND_ADD, ND_SUB, ND_MUL, ND_DIV
    //       ND_ASSIGN, ND_EQ,ND_NE,ND_LT,ND_LE
    // [unary] ND_RETURN, ND_DEREF, ND_ADDR, ND_EXPR_STMT
    struct {
      Node *lhs;
      Node *rhs;
    };

    // ND_VAR
    Object *var; // 存储 ND_VAR 的变量信息

    // ND_NUM;
    int val; // 存储 ND_NUM 的值

    // ND_BLOCK
    Node *body; // 代码块

    // ND_FNCALL
    struct {
      char *func_name; // 函数名称
      Node *args;      // 函数参数
    };

    // ND_IF | ND_FOR
    struct {
      Node *cond; // 条件
      Node *then; // 判断成立
      union {
        Node *els; // 判断失败

        struct {      // ND_FOR
          Node *init; // 循环初始化语句
          Node *inc;  // 循环变量变化语句
        };
      };
    };
  };
};

// 语法解析入口函数
Object *parse(Token *token);

//
// 三、语义分析，生成代码
//

// 代码生成入口函数
void codegen(Object *prog, FILE *out);

//
// 四、类型系统
//

// 类型
typedef enum {
  TY_INT,   // int整形
  TY_CHAR,  // char字符
  TY_PTR,   // 指针类型
  TY_FUNC,  // 函数类型
  TY_ARRAY, // 数组
} TypeKind;

struct Type {
  TypeKind kind; // 类型
  int size;      // 大小
  Token *token;  // 变量的名称

  union {
    // TY_PTR, TY_ARRAY
    struct {
      Type *base; // 为指针时，指向的类型; 为数组时,下标对应的类型
      int len; // 为数组时，数组的长度
    };

    // TY_FUNC
    struct {
      Type *ret_type; // 返回值的类型
      Type *params;   // 形参
      Type *next;     // 下一个类型
    };
  };
};

// Type int
extern Type *TYPE_INT;
// Type char
extern Type *TYPE_CHAR;

// 判断是否为 Type int
bool is_integer(Type *type);

// 复制类型
Type *copy_type(Type *type);

// 创建一个指针类型，并指向 base
Type *pointer_type(Type *base);

// 创建一个函数类型， 且返回值为ret_type
Type *func_type(Type *ret_type);

// 创建一个数组类型, 基类为 base， 长度为 len
Type *array_type(Type *base, int len);

// 遍历 AST 并为所有 NODE 增加类型
void add_type(Node *node);
