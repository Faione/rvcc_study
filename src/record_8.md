# 支持一维数组

形如 `int x[3]` 这样的声明，构造了一个长度为3，类型为 int 的数组 x, 因此支持一维数据实际是增加了一种 Type, 在 Type 枚举中增加 `TY_ARRAY` 类型，对应这样的类型，还需要在 Type 结构体中增加 `size` 与 `len` 成员
- `int x[` 形如 `int x(`， 因此在 type_sub 处进行处理

## 类型分析

由于 `Type` 现在拥有了 `size`(对所有类型而言) 与 `len`(对数组而言) 成员, 因此至少要为 Pointer、Interger、Arrary增加相应的初始值

```c
// 创建一个函数类型， 且返回值为ret_type
Type *func_type(Type *ret_type) {
  Type *type = calloc(1, sizeof(Type));
  type->kind = TY_FUNC;
  type->ret_type = ret_type;

  return type;
}
```
对于数组而言，取地址获取的是 int* 类型的值，解引用获取的则是 int 类型的值
- 对ARRAY取地址的操作获取的是 base type 的指针，也是数组的起始地址
- 对ARRAY的解引用操作获取的则是 base type

```c
  case ND_ADDR: {
    Type *type = node->lhs->type;

    // 取地址节点的类型根据单臂所指向节点的类型来决定
    // 如果是数组，&的结果为指向 base 的指针
    if (type->kind == TY_ARRAY)
      node->type = pointer_type(type->base);
    else
      node->type = pointer_type(type);
    return;
  }
  case ND_DEREF:
    // ND_DEREF 的单臂必须有基类
    if (!node->lhs->type->base)
      error_token(node->token, "invalid pointer dereference");

    // ND_DEREF 的类型为指针指向的类型
    node->type = node->lhs->type->base;
    return;
```

## 语法分析

推导式更新, 定义一个类型之后，可能出现的情况有
- `(` 开头的函数
- `[` 开头的数组
- 以上都不符合，则为一个基础类型，返回 type 即可

```
// type_suf = "(" func_params | "[" num "]" | ε
// func_params = param ("," param)*)? ")"
```

增加一个从 token 读取数字的方法, 从而处理`int x[3]`中静态大小声明

```c
// 获取数字
static int get_num(Token *token) {
  if (token->kind != TK_NUM)
    error_token(token, "expected a number");

  return token->val;
}
```

提取 `func_params` 逻辑, 同时处理数组声明

```c
/**
 * func_params(Token **rest, Token *token, Type *type)
 *
 * type_suf = "(" func_params | "[" num "]" | ε
 *
 * @param rest 指向剩余token指针的指针
 * @param token 正在处理的 token
 * @param type type_suf 之前所识别出来的类型 indent
 *
 * @return 构造好的 Type
 */
static Type *type_suf(Token **rest, Token *token, Type *type) {
  if (equal(token, "(")) // 函数
    return func_params(rest, token->next, type);

  if (equal(token, "[")) {
    int len = get_num(token->next);
    *rest = skip(token->next->next, "]");
    return array_type(type, len);
  }

  *rest = token;
  return type;
}
```
由于 ARRAY 不是 INT, 因此对于 Type 为 ARRARY 的NODE 的加减 会按照 ptr 的方式进行处理

## 语义分析

由于可读的变量类型也包括了数组，因此抽象对于地址 load\store 操作，并修改 `assign_local_val_offsets` 方法，支持不同的 var_type_size

```c
// a0 中保存了一个地址
// 将此地址的值加载到 a0 中
static void load(Type *type) {
  if (type->kind == TY_ARRAY)
    return;
  printf("  # 读取a0中存放的地址, 得到的值存入a0\n");
  printf("  ld a0, 0(a0)\n");
}

// 栈保存了一个地址
// 将此地址 pop 到 a1 中
// 将 a0 中的值保存到此地址
static void store(void) {
  pop("a1");
  printf("  # 将a0的值, 写入到 a1 中存放的地址\n");
  printf("  sd a0, 0(a1)\n");
}
```

# 支持多维数组

`int x[3][4]` 这样的多维数组声明，可以理解为递归的进行 `(x[3])[4]` -> `x[3])` 构造，由于在一维数组中，已经支持了对 `x[3]` 的类型构造，那么在 int 的基础上，只需要递归地解析即可
- 递归最深处， type->base 为 int ，size 为 4 * int
- 递归次深处, type->base 为上述构造的 arrary type， size 为 3 * int[4]


# 支持[]操作符

允许通过 `x[i]` 进行数组成员的读写

## 语法分析

推导式增加, `x[i]` 即是一种 postfix

```
// unary = ("+" | "-" | "*" | "&") unary | postfix
// postfix = primary ("[" expr "]")*
```
增加的 postfix 多了匹配 `x[]` 的逻辑
- `Node *index`记录了下标计算的表达式
- `new_node_add(node, index, start)` 生成了一个加法节点，将当前node(最开始是变量 node) 与 index 相加(地址)
- 对于 `x[3]` new_node_add 生成的就是 x_addr + 3
- 由于生成的是一个相对于数组起始地址的偏移，因此需要通过解引用来获取值，实现`[]`操作符的逻辑

`int x[3][4]` 声明了一个 二维数组, `x[1][2]` 获取的(0,0)上的值
- 对于这个示例，首先可知道，最右边定义了一个 4 * int大小的一维数组，然后则定义了一个 3 * (4*int) 大小的数组
- postfix循环中，中首先读取 `[1]` 计算得到第1个 4 * int大小 数组的地址, 然后再计算`[1][2]`得到第 2 个 int 的地址
- 解引用即可获取(1,2)上的值

```c
// postfix = primary ("[" expr "]")*
PARSER_DEFINE(postfix) {
  Node *node = primary(&token, token);

  // x[][]...[]
  while (equal(token, "[")) {
    Token *start = token;
    Node *index = expr(&token, token->next);
    token = skip(token, "]");
    node = new_node_unary(ND_DEREF, new_node_add(node, index, start), start);
  }

  *rest = token;
  return node;
}
```