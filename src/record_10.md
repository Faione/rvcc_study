# 支持Char类型

char 类型与 int 类型的不同之处在于占用的内存大小，当前类型系统中, int 占用 8byte 内存(与地址相同), 而 char 占用 1byte 内存

## 类型分析

增加 `TY_CHAR` 枚举与 `TYPE_CHAR` 全局指针

```c
Type *TYPE_CHAR = &(Type){TY_CHAR, 1};
```

## 词法分析

增加关键字 "char"

## 语法分析

推导式修改，类型声明中支持了 "char"

```
// declspec = "char" | "int"
```

由于类型不再局限于 int, 因此增加 `is_typename` 方法来判断当前 token 是否是一个 type 关键字

```c
static bool is_typename(Token *token) {
  return equal(token, "char") | equal(token, "int");
}
```

## 语义分析

riscv 提供了操作不同位宽数据的能力，如对于 8bit 的char数据，可以使用 sb\lb 指令进行操作，相较于 sd\ld 指令，执行速度更快，因此有必要对不同大小type的数据，使用不同的指令进行优化

```c
// a0 中保存了一个地址
// 将此地址的值加载到 a0 中
static void load(Type *type) {
  if (type->kind == TY_ARRAY)
    return;
  printf("  # 读取a0中存放的地址, 得到的值存入a0\n");
  if (type->size == 1)
    printf("  lb a0, 0(a0)\n");
  else
    printf("  ld a0, 0(a0)\n");
}

// 栈保存了一个地址
// 将此地址 pop 到 a1 中
// 将 a0 中的值保存到此地址
static void store(Type *type) {
  pop("a1");
  printf("  # 将a0的值, 写入到 a1 中存放的地址\n");
  if (type->size == 1)
    printf("sb a0, 0(a1)\n");
  else
    printf("  sd a0, 0(a1)\n");
}
```
对局部变量也进行类似的处理

```c
    for (Object *var = f->params; var; var = var->next) {
      printf("  # 将%s寄存器的值存入%s的栈地址\n", func_arg_regs[i], var->name);
      if (var->type->size == 1)
        printf("  sb %s, %d(fp)\n", func_arg_regs[i++], var->offset);
      else
        printf("  sd %s, %d(fp)\n", func_arg_regs[i++], var->offset);
    }
```

# 支持字符串字面量

形如 `"foo"` 这样硬编码在远代码中的字符串就是一种字面量
- 注意: `char a = "foo"` 中，仅 "foo" 是字面量，在编译过程中，实际上将 "foo" 作为一个匿名的全局变量进行管理
- 字面量的声明与赋值
- 与早先实现的任何变量不同，字面量是一个匿名全局变量，需要编译器支持 匿名全局变量 的功能

## 词法分析

字符串字面量为内嵌在代码中的 char 数组，词法分析过程中，遇到 `"` 字符，便可认为是一个字符串字面量的起始
- `TK_STR` 字面量的起始以 起/止 两个 `"` 为界
- `TK_STR` token 的 type 为
- `str` 作为 `TK_STR` token 独有的字段，指向双引号所包括的字符数组(strndup进行保存)
- `type` 作为 `TK_STR` token 独有的字段，在词法分析阶段，标记了字面量的类型为 array

```c
// 读取字符串字面量
static Token *read_string_literal(char *start) {
  // 形如 "foo" 的 token 即是 string literal
  // 字符串中不能出现 \n 或 \0
  char *p = start + 1;
  for (; *p != '"'; p++) {
    if (*p == '\n' || *p == '\0')
      error_at(start, "unclosed string literal");
  }

  Token *token = new_token(TK_STR, start, p + 1);

  // 字符串字面量类型为 char[]，包括了双引号
  token->type = array_type(TYPE_CHAR, p - start);

  // 将双引号内的内容拷贝到 token 中的 str 字段
  token->str = strndup(start + 1, p - start - 1);
  return token;
}
```

## 语法分析

推导式变化

```
// primary = "(" expr ")" | "sizeof" unary | ident | fncall | str | num
```

`TK_STR` token 需要以变量进行处理, 而区别于其他变量，字面量是一个全局匿名变量
- 匿名变量同样需要唯一的名称，只不过由编译器生成


```c
// 生成唯一的变量名称(对匿名变量而言)
static char *new_unique_name(void) {
  static int id = 0;
  char *buf = calloc(1, 20);

  sprintf(buf, ".L..%d", id++);
  return buf;
}

// 创建匿名的 Global 变量
static Object *new_anon_glabol_var(Type *type) {
  return new_global_var(new_unique_name(), type);
}

// 新增字符串字面量
static Object *new_string_literal(char *str, Type *type) {
  Object *var = new_anon_glabol_var(type);
  // 字面量的初始值为双引号包裹的部分，而不包括双引号
  var->init_data = str;
  return var;
}
```

由于字符串字面量的值在词法分析阶段就能够确认，因此在语法分析过程中，只需要生成变量节点即可

```c
  if (token->kind == TK_STR) {
    Object *var = new_string_literal(token->str, token->type);
    *rest = token->next;
    return new_node_var(var, token);
  }
```

## 语义分析

早先实现的变量仅声明了对内存的使用，即指定了一片 栈上 或 .data 段上的内存空间，向这些内存写入内容需要显示地通过 赋值语句 来完成，而字面量是一个具有 初始值 的变量，或者说其在代码中的值是预编码的，因此在语义分析阶段，需要支持将初始值编码到代码中
- 当前汇编中整体都为一个模块，因此任意 label 都可以被全局地进行访问
- `.globl` 仅对修饰的 `main` 有意义，其所修饰的函数、全局变量，意味着能在连接过程中被其他代码引用，当前并无此作用

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

    // 判断变量是否有初始值
    if (var->init_data) {
      printf("%s:\n", var->name);
      // 将初始值内容进行打印
      for (int i = 0; i < var->type->size; i++) {
        char c = var->init_data[i];
        if (isprint(c))
          printf("  .byte %d\t# 字符:  %c\n", c, c);
        else
          printf("  .byte %d\n", c);
      }
    } else {
      printf("  .globl %s\n", var->name);
      printf("  # 全局变量%s\n", var->name);
      printf("%s:\n", var->name);
      printf("  # 零填充%d位\n", var->type->size);
      printf("  .zero %d\n", var->type->size);
    }
  }
}
```

# 增加实用函数

`sprintf` 使用起来并不方便，因此增加一个 `format` 函数用来处理字符串

```c
// 格式化字符串
char *format(char *fmt, ...) {
  char *buf;
  size_t len;

  // 申请堆上内存并将起始地址保存到 buf 中
  FILE *out = open_memstream(&buf, &len);

  va_list va;
  va_start(va, fmt);
  vfprintf(out, fmt, va);
  va_end(va);

  fclose(out);
  return buf;
}
```