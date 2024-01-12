# 支持 if 语句

## 词法分析

增加 `if` 关键字

```c
static char *keywords[] = {"return", "if", "else"};

// 判断 ident token 是否在 keywords 中
static bool is_keyword(Token *token) {
  for (int i = 0; i < sizeof(keywords) / sizeof(*keywords); i++) {
    if (equal(token, keywords[i])) {
      return true;
    }
  }

  return false;
}
```

## 语法分析

推导式增加
```
// stmt = "return" expr ";"|
//        "{" compoundStmt |
//        expr_stmt |
//        "if" "(" expr ")" stmt ("else" stmt)?
```

增加 node_if， 并扩充 Node 中的成员 cond, then, else

```c
  // 解析 if 语句
  if (equal(token, "if")) {
    Node *node = new_node(ND_IF);
    token = skip(token->next, "(");
    node->cond = expr(&token, token);
    token = skip(token, ")");
    node->then = stmt(&token, token);
    if (equal(token, "else"))
      node->els = stmt(&token, token);
    *rest = token;
    return node;
  }
```


## 语义分析

对于 ND_IF 的节点，汇编的模板格式如下, 首先生成 condition expr, 随后插入一条 `beqz` 指令判断是否要跳转到 else

```
//   condition expr
//   beqz to else label
//   then stmt
// else label:
//   else stmt
// end label:
```

当存在多个 if 时， 需要通过 c 标记来区别不同的label

```c
  case ND_IF: {
    int c = count();

    // condition
    gen_expr(node->cond);
    // a0为0则跳转到 else
    printf("  beqz a0, .L.else.%d\n", c);

    // then 逻辑
    gen_stmt(node->then);
    // 跳转到 if 语句末尾
    printf("  j .L.end.%d\n", c);

    // else 逻辑
    printf(".L.else.%d:\n", c);
    if (node->els)
      gen_stmt(node->els);
    // end 标签
    printf(".L.end.%d:\n", c);
    return;
```

## 支持 for 语句

**词法分析**

增加 for 关键字

## 语法分析

推导式增加

增加 node_for， 并扩充 Node 中的成员 init, inc, 即 for init cond inc


## 语义分析

对于 ND_FOR 的节点，汇编的模板格式如下

```
//   init stmt
// begin label:
//   condition expr
//   beqz to end label
//   then stmt
//   inc expr
//   j to cond label
// end label:
```


## 支持 while 语句

while 语句就是只用 cond 和 then 的 for 循环语句