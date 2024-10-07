# 支持结构体标签

先前设计中，结构体只能在使用时进行完整声明，这是因为当前编译器还不支持用户自定义类型的扩展。而为支持结构体类型的声明，需要在语法分析阶段存储这些临时的类型。同时，考虑到结构体类型的声明具有作用域特征，因此需采用类似变量域的方式进行处理。

## 语法分析

增加`TagScope`类型来维护当前BlockScope中的所有临时结构体声明
- 采用uthash来存储当前BlockScope中的所有Tag
- BLOCK_SCOPES使用头插法维护，因此Tag检索的优先级是从当前作用域开始，到最外层的作用域

```c
typedef struct TagScope {
  char *name; // 变量域名称
  Type *type; // 对应的变量
  UT_hash_handle hh;
} TagScope;

/**
 * 向块域插入Tag
 *
 * @param name Tag域的名称
 * @param type 要插入块域的Tag
 *
 */
static void push_tag_scope(char *name, Type *type) {
  TagScope *tag_scope = calloc(1, sizeof(TagScope));
  tag_scope->name = name;
  tag_scope->type = type;

  HASH_ADD_STR(BLOCK_SCOPES->tags, name, tag_scope);
}

/**
 * 在所有块域中搜索与 ident token 同名的Tag
 *
 * @param token 要检索的变量所属的token
 *
 * @return 匹配到的Tag，没有找到则返回NULL
 */
static Type *find_tag_by_token(Token *token) {
  // 从当前块域开始检索
  for (BlockScope *block_scope = BLOCK_SCOPES; block_scope;
       block_scope = block_scope->next) {

    TagScope *tag_scope;
    HASH_FIND(hh, block_scope->tags, token->loc, token->len, tag_scope);
    if (tag_scope != NULL) {
      return tag_scope->type;
    }
  }
  return NULL;
}
```
`struct_decl`中，增加对于`struct foo`语法的识别，并从当前的Tag域中检索是否有已经存在的Tag声明，并能够将新生成的Tag存储到当前TagScope中

```c
static Type *struct_decl(Token **rest, Token *token)
{
    Token *tag = NULL;
    if (token->kind == TK_IDENT) {
        tag   = token;
        token = token->next;
    }

    // 使用了struct tag，且不是一个struct声明
    if (tag != NULL && !equal(token, "{")) {
        Type *type = find_tag_by_token(tag);
        if (type == NULL)
            error_token(tag, "undefined struct type: %s", get_ident(tag));
        *rest = token;
        return type;
    }
    ...
    // 处理非匿名结构体
    if (tag != NULL)
        push_tag_scope(get_ident(tag), type);
    return type;
}
```