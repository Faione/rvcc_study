//
// 四、类型系统
//

// 类型
typedef enum {
  TY_INT,    // int整形
  TY_CHAR,   // char字符
  TY_PTR,    // 指针类型
  TY_FUNC,   // 函数类型
  TY_ARRAY,  // 数组
  TY_STRUCT, // 结构体
} TypeKind;

struct Type {
  TypeKind kind; // 类型
  int size;      // 大小
  Token *token;  // 变量的名称

  union {
    // TY_PTR, TY_ARRAY
    struct {
      Type *base; // 为指针时，指向的类型; 为数组时,下标对应的类型
      int len;    // 为数组时，数组的长度
    };

    // TY_FUNC
    struct {
      Type *ret_type; // 返回值的类型
      Type *params;   // 形参
      Type *next;     // 下一个类型
    };

    // TY_STRUCT
    Member *members;
  };
};

// AST中用于描述结构体成员的数据结构
struct Member {
  Member *next; // 下一成员
  Type *type;   // 类型
  Token *token; // 名称
  int offset;   // 偏移量
};

// Type int
extern Type *TYPE_INT;
// Type char
extern Type *TYPE_CHAR;