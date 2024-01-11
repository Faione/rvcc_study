# 支持一元&*运算

![addr-w150](./images/addr.svg)

## 词法分析

支持 `*` `&` 两种符号， 而考虑到 tokenize 本身就能够解析符号，因此不必在 tokenize 阶段做修改

## 语法分析

与 `-` 类似，单臂节点中增加了 `*` `&` 两种类型的节点， 因此推导式修改为

```
// unary = ("+" | "-" | "*" | "&") unary | primary
```

相应地在 unary 中也要增加处理逻辑

```c
// unary = ("+" | "-" | "*" | "&") unary | primary
PARSER_DEFINE(unary) {
  // + 一元运算符无影响，跳过即可
  // unary无论如何都会调用 primary 进行 rest 的设置
  // 因此递归调用 unary 时，传入 rest 即可
  // 否则则需要在每次生成新节点后，手动地再设置 rest

  // "+" unary
  if (equal(token, "+"))
    return unary(rest, token->next);

  // "+" unary
  if (equal(token, "-"))
    return new_node_unary(ND_NEG, unary(rest, token->next), token);

  // "*" unary
  if (equal(token, "*"))
    return new_node_unary(ND_DEREF, unary(rest, token->next), token);

  // "&" unary
  if (equal(token, "&"))
    return new_node_unary(ND_ADDR, unary(rest, token->next), token);

  return primary(rest, token);
}
```

## 语义分析

与 ND_NEG 类似，DEREF 和 ADDR 都在 expr 中进行单独处理
- 注意存在嵌套解引用的情况，因此 DEREF 单臂所指向的也可能是一个 DEREF 节点，因此需要递归处理
- 取地址不存在嵌套，但存在对一个解引用的expr进行取地址的情况，因此在计算地址时需要单独处理


```c
  case ND_ADDR:
    // 计算单臂指向的变量的地址，保存到 a0 中
    gen_addr(node->rhs);
    return;
  case ND_DEREF:
    // 解引用向右递归
    gen_expr(node->rhs);
    printf("  # 读取 a0 中间接引用的值，存入到 a0中\n");
    printf("  ld a0, 0(a0)\n");
```

```c
static void gen_addr(Node *node) {

  switch (node->kind) {
  case ND_VAR: // Object var 为指针, 在 prog 中被修改后, 同时也能从 Node 访问
    printf("  # 获取变量%s的栈内地址为%d(fp)\n", node->var->name,
           node->var->offset);
    printf("  addi a0, fp, %d\n", node->var->offset);
    return;
  case ND_DEREF: // 对一个解引用expr进行取地址
    gen_expr(node->rhs);
    return;
  default:
    break;
  }

  error_token(node->token, "not an lvalue");
}
```


# 支持指针的算术运算

## 词法分析

## 语法分析

## 语义分析

# 支持int关键字以定义变量

## 词法分析

## 语法分析

## 语义分析
