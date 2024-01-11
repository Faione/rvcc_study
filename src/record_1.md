# 一元运算符

一元运算符(只有一个操作数), 如 +1，-1
- 支持一元运算符号即拓展 primary 的内容，即存在 unary = （"+" | "-"）unary | primary

注意，+ 一元运算符不会对结果有任何影响，因此实现时，仅需考虑 - , 故语法分析遇到一元运算符 + 跳过即可


## 词法分析

+/-本身能够识别，无需增加

## 语法分析

新的算式形式语言
- mul 之后的 + | - 被认为是常规运算符
```
// expr = mul ("+" mul | "-" mul)*
// mul = unary ("*" unary | "/" unary)*
// unary = ("+" | "-") unary | primary
// primary = "(" expr ")" | num
```

```c
// unary = ("+" | "-") unary | primary
static Node *unary(Token **rest, Token *token) {
  // + 一元运算符无影响，跳过即可
  // unary无论如何都会调用 primary 进行 rest 的设置
  // 因此递归调用 unary 时，传入 rest 即可
  // 否则则需要在最后手动设置 rest
  if (equal(token, "+")) {
    return unary(rest, token->next);
  }

  if (equal(token, "-")) {
    return new_node_unary(ND_NEG, unary(rest, token->next));
  }

  return primary(rest, token);
}
```

## 语义分析

对于 ND_NEG 节点，递归直到遇到数字，将数字加载到 a0 之后，增加`-`数量的 neg 指令即可

```c
static void gen_code(Node *node) {
  // 若根节点为数字(叶子节点), 则只加载到 a0 寄存器中
  if (node->kind == ND_NUM) {
  }
  switch (node->kind) {
  case ND_NUM:
    printf("  li a0, %d\n", node->val);
    return;
  case ND_NEG:
    // 一元运算符子为单臂二叉树，子节点保留在右侧
    // 因此向右递归直到遇到数字
    gen_code(node->rhs);
    printf("  neg a0, a0\n");
    return;
  default:
    break;
  }
```

## 关系运算符

支持 `==` `!=` `<=` `>=`

## 词法分析

增加`read_punct`函数，读取运算符并返回长度

```c
int punct_len = read_punct(p);
if (punct_len) {
    cur->next = new_token(TK_PUNCT, p, p + punct_len);
    cur = cur->next;

    p += punct_len;
    continue;
}
```

## 语法分析

表达式形式语言修改

```
// expr = equality
// equality = relational ("==" relational | "!=" relational)*
// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
// add = mul ("+" mul | "-" mul)*
// mul = unary ("*" unary | "/" unary)*
// unary = ("+" | "-") unary | primary
// primary = "(" expr ")" | num
```

因此需要增加 equality 与 relational
- 存在 ND_LT 和 ND_NE 存在的情况下，不需要 ND_GT 和 ND_GE, 因为 lhs 和 rhs 位置就代表了一种意义


```c
  while (true) {
    if (equal(token, "==")) {
      node = new_node_bin(ND_EQ, node, relational(&token, token->next));
      continue;
    }

    if (equal(token, "!=")) {
      node = new_node_bin(ND_NE, node, relational(&token, token->next));
      continue;
    }
    break;
  }
```


## 语义分析

使用 `xor`, `seqz`, `snez`, `slt`, `xori` 指令来实现比较
- `xor` 若两数相等，则值为 0, 因此可以通过判断是否为0来判断是否相等, 利用 seqz 与 snez 指令
- `slt` 判断是否小于, 小于等于则等同于不大于，利用 slt 与取反来实现

```C
case ND_EQ:
case ND_NE:
  printf("  xor a0, a0 ,a1\n");
  if (node->kind == ND_EQ) {
    // a0 = 1 if a0 = 0
    printf("  seqz a0, a0\n");
  } else {
    // a0 = 1 if a0 != 0
    printf("  snez a0, a0\n");
  }
  return;
case ND_LT:
  printf("  slt a0, a0, a1\n");
  return;
case ND_LE:
  // a0 <= a1 == !(a1 < a0) == (a1 < a0) xor 1
  printf("  slt a0, a1, a0\n");
  printf("  xori a0, a0, 1\n");
  return;
```