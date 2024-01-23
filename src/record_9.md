# 支持 sizeof

`sizeof` 为一个新增的关键字, 返回指定类型变量的内存大小

## 词法分析

增加 `sizeof` 关键字支持

## 语法分析

`sizeof` 作为一种对于变量的特殊运算，在编译时将其转化为变量的内存大小即可

新增推导式

```
// primary = "(" expr ")" | "sizeof" unary | ident | fncall | num
```

这意味着 `sizeof` 实际上会构造一个 num 节点，num保存的 val 在编译时通过类型系统获得
- 获取 sizeof(unary) 中的 unary 需要构造一个 unary 节点，然后通过 add_type 生成次此节点的类型

```c
  // "sizeof" unary
  if (equal(token, "sizeof")) {
    Node *node = unary(rest, token->next);
    add_type(node);
    return new_node_num(node->type->size, token);
  }
```

# 融合Function和Var

函数/变量结构体有许多相似之处，因此可以选择将两者融合
- 这部分更多地是在进行代码重构
- 由于Function也可作为一种Object, 区别于 LOCALS, Function这类的Object存放在 GLOBALS 中

结合匿名 union 与 匿名 struct 优化内存占用

```c
struct Object {
  Object *next; // 下一个Object
  char *name;   // 名称
  Type *type;   // 类型

  bool is_local;    // 局部变量与否
  bool is_function; // ObjectMember 属性

  // union ObjectMember member;

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
  };
};
```

```c
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
```

```c
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
      Type *next;     //下一个类型
    };
  };
};
```

## 语法分析

Function 转化为 Object， 并通过 GLOBALS 维护

```c
// program = function*
Object *parse(Token *token) {
  GLOBALS = NULL;

  while (token->kind != TK_EOF) {
    // 函数返回值类型
    Type *type = declspec(&token, token);
    token = function(token, type);
  }

  return GLOBALS;
}
```

# 支持全局变量

之前的设计中，只用 Function 是全局的(保存在 GLOBAL 中), 现在要将普通的变量也加入其中， 因此需要在 语法分析 与 语义分析上进行修改

## 语法分析

推导式变化
- global_variable_def 也能够作为 program 的一种

```
// program = (function_def | global_variable_def) *
// function_def = declspec function
// function = declspec declarator "{" compound_stmt*
// global_variable_def = declspec global_variable
// global_variable = (declarator ("," declarator))* ";")*
```

全局变量解析

```c
/*
 * global_variable = (declarator ("," declarator))* ";")*
 *
 * 当前仅支持全局变量的声明
 * @param type 为基础类型，如 int
 */
static Token *global_variable(Token *token, Type *base) {
  bool is_first = true;
  while (!consume(&token, token, ";")) {
    // 处理 int x,y 格式
    if (!is_first)
      token = skip(token, ",");
    is_first = false;

    Type *type = declarator(&token, token, base);
    new_global_var(get_ident(type->token), type);
  }

  return token;
}
```
prog 类型判断

```c
// 尝试生成 declarator 来判断是否是 FUNC
static bool is_function(Token *token) {
  Type dummy = {};
  Type *type = declarator(&token, token, &dummy);
  return type->kind == TY_FUNC;
}
```

新的 parse

```c
// program = (function_def | global_variable_def) *
// function_def = declspec function
// global_variable_def = declspec global_variable
Object *parse(Token *token) {
  GLOBALS = NULL;

  while (token->kind != TK_EOF) {
    // 函数返回值类型
    Type *type = declspec(&token, token);

    // function
    if (is_function(token)) {
      token = function(token, type);
      continue;
    }

    // global_variable
    token = global_variable(token, type);
  }

  return GLOBALS;
}
```


## 语义分析

由于全局变量的引入，现在 codegen 由三部分组成
- `assign_local_val_offsets`: 计算每个函数中局部变量的偏移
- `emit_data`: 遍历全局变量构造 `.data` 段
- `emit_text`: 遍历Function构造 `.text` 段

`.data` 段目前仅支持为每个全局变量创建一个标签，并零填充一段内存空间

```c
// 生成 .data 段
//
// 存放 全局变量
static void emit_data(Object *prog) {
  for (Object *var = prog; var; var = var->next) {
    if (var->is_function)
      continue;

    printf("  # 数据段标签\n");
    printf("  .data\n");
    printf("  .globl %s\n", var->name);
    printf("  # 全局变量%s\n", var->name);
    printf("%s:\n", var->name);
    printf("  # 零填充%d位\n", var->type->size);
    printf("  .zero %d\n", var->type->size);
  }
}
```

对于 `ND_VAR` 地址的处理也区分了 local 与 global

```c
  case ND_VAR: // Object var 为指针, 在 prog 中被修改后, 同时也能从 Node 访问
    if (node->var->is_local) { // local var
      printf("  # 获取变量%s的栈内地址为%d(fp)\n", node->var->name,
             node->var->offset);
      printf("  addi a0, fp, %d\n", node->var->offset);
    } else { // global var
      printf("  # 获取全局变量%s的地址\n", node->var->name);
      // la 指令是一个伪指令，将 %s symbol 标记的内存地址加载到 a0 中
      printf("  la a0, %s\n", node->var->name);
    }
    return;
```
