# 处理域

当前编译器支持代码块，但并未处理其中的变量，导致代码块可能出现重复的变量声明。重复的变量都属于局部变量。在parse旧的逻辑中，变量使用头插法插入到LOCALS中，越晚声明的变量越早被检索到。因此存在代码块中的变量作用域的问题。

```c
static Object *find_var_by_token(Token *token) {
  for (Object *var = LOCALS; var; var = var->next) {
    // 简单判断 -> 负载判断, 提升效率
    if (strlen(var->name) == token->len &&
        !strncmp(token->loc, var->name, token->len))
      return var;
  }

  // 查找全局变量中是否有同名变量
  for (Object *var = GLOBALS; var; var = var->next) {
    if (strlen(var->name) == token->len &&
        !strncmp(token->loc, var->name, token->len))
      return var;
  }
}
```  

而为解决这一问题，引入代码域，使得块中的变量能够借助域来区分作用的范围。

![scope](./images/scope.svg)

### 语法分析

为描述域，引入域这一概念，包含块域与变量域。
- 变量域：对变量作用域的描述。变量域通过链表连接，可以理解为是代码块的局部变量
- 块域：用于描述代码块的链表

```c
// 局部和全局变量的域
typedef struct VarScope VarScope;
struct VarScope {
  VarScope *next; // 下一变量域
  char *name;     // 变量域名称
  Object *var;    // 对应的变量
};

// 代码块域
typedef struct BlockScope BlockScope;
struct BlockScope {
  BlockScope *next; // 指向上一级的域
  VarScope *vars;   // 指向当前域内的变量
};
```

BLOCK_SCOPES维护了块域的链表，BLOCK_SCOPES指针指向当前正在处理的块域
- `enter_scope`: 对应与`{}`包含的部分，创建一个块域，并使用头插法插入
- `leave_scope`: 退出当前块域

```c
// 所有域的链表
static BlockScope *BLOCK_SCOPES = &(BlockScope){};

/**
 * 进入块域
 *
 * 进入一个域，则将此域以头插法插入到BLOCK_SCOPES中
 */
static void enter_scope(void) {
  BlockScope *scope = calloc(1, sizeof(BlockScope));
  scope->next = BLOCK_SCOPES;
  BLOCK_SCOPES = scope;
}

/**
 * 离开块域
 *
 * 移动BLOCK_SCOPES至上一个块域（链表头的next）
 */
static void leave_scope(void) { BLOCK_SCOPES = BLOCK_SCOPES->next; }
```

引入域之后的变量检索逻辑
- 变量检索时，从当前块域开始，依次向外层的块域进行
- 子块域变量对父块域不可见，因此保证了变量的作用域

```c
/**
 * 在所有块域中搜索与 ident token 同名的变量
 *
 * @param token 要检索的变量所属的token
 *
 * @return 匹配到的变量，没有找到则返回NULL
 */
static Object *find_var_by_token(Token *token) {

  // 从当前块域开始检索
  for (BlockScope *scope = BLOCK_SCOPES; scope; scope = scope->next) {
    // 遍历此块域的所有变量
    for (VarScope *var_scope = scope->vars; var_scope;
         var_scope = var_scope->next) {
      if (equal(token, var_scope->name))
        return var_scope->var;
    }
  }
  return NULL;
}
```

处理代码块的额外操作，即在`{}`处进行块域的创建、进入与退出

```c
// compound_stmt = (declaration | stmt)* "}"
PARSER_DEFINE(compound_stmt) {
  ...
  enter_scope();
  ...
  leave_scope();
  ...
}
```