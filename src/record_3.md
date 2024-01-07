## 支持多字母本地变量

本地变量名称满足 `[a-zA-Z_][a-zA-Z0-9_]*`
- 增加 Object 结构体用来保存本地变量
- 增加 Function 结构体用来指示代码

```c
struct Object {
  Object *next;
  char *name; // 变量名称
  int offset; // 相对栈顶的偏移量
};

struct Function {
  Node *body;     // 函数体(AST)
  Object *locals; // 本地变量链表头
  int stack_size; // 栈大小
};
```

**词法分析**

首先需要判断字符是否为 变量名称头，其次还要判断剩余部分是否符合剩余部分的规范，最后再构成token

**语法分析**

parse返回值变为 Funcion， 因此需要生成好 body 与 locals
- 生成 body 的逻辑几乎无变化
- locals 在生成的过程中变化，下降到 primary 时，如发现是一个 ident token，则首先遍历 locals 中是否已经有同名 Node， 否则则创建一个并加入到 locals 中

```c
// 变量实例均保存在全局的 LOCALS 链表中
Object *LOCALS;

// 创建以 Local 变量，并头插到 LOCALS 中
static Object *new_local_var(char *name) {
  Object *var = calloc(1, sizeof(Object));
  var->name = name;

  // 头插法
  var->next = LOCALS;
  LOCALS = var;
  return var;
}

// 寻找 LOCALS 中是否有与 ident token 同名的变量
static Object *find_var_by_token(Token *token) {
  for (Object *var = LOCALS; var; var = var->next) {
    // 简单判断 -> 负载判断, 提升效率
    if (strlen(var->name) == token->len &&
        !strncmp(token->loc, var->name, token->len))
      return var;
  }

  return NULL;
}
```

**语义分析**

更新后的栈布局

```
  // 栈布局
  //-------------------------------// sp
  //              fp
  //-------------------------------// fp = sp-8
  //             变量
  //-------------------------------// sp = sp-8-StackSize
  //           表达式计算
  //-------------------------------//
```

为 prog 中的变量设置栈空间

```c
static void assign_local_val_offsets(Function *prog) {
  int offset = 0;
  for (Object *var = prog->locals; var; var = var->next) {
    offset += 8;
    var->offset = -offset;
  }

  prog->stack_size = align_to(offset, 16);
}

// 计算给定节点的内存地址
static void gen_addr(Node *node) {
  // var 为指针，在 prog 中被修改后，同时也保存在 Node 中
  if (node->kind == ND_VAR) {
    printf("  addi a0, fp, %d\n", node->var->offset);
    return;
  }

  error("not an lvalue");
}
```

> 注意 `#define _POSIX_C_SOURCE 200809L`, 才能够正常使用 `strndup` 函数
