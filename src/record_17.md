# 对齐结构体成员变量

常见的体系结构中，load指令用于将内存中的数据读入到寄存器中，以供后续操作使用。而对于给定的内存地址，load指令能够读入的大小通常为1byte、2byte、4byte、8byte。因此，为避免内存中的数据跨内存块存放，导致需要更多次的load操作才能读取其中的数据，因此需要对数据的内存布局进行调整。

先前的设计中，结构体成员变量的内存布局是紧凑的（按1byte对齐），存在跨内存块的隐患。对齐的含义是保证数据的内存地址总是align的整数倍
- 对于初等类型，如char、int，其本身有明确的align值（自身大小），通过对齐到align，能够保证一些情况下，其内存地址总是align的整数倍
- 对于复合类型，如struct，其align值能够通过递归的计算成员变量的algin值得出（成员中最大的align），而通常align总是2的次幂，因此能够保证当结构体对齐到align时，所有内部成员也都能对齐到各自的align。

## 类型分析

为每个Type增加align字段，用于保存其对齐值

## 语法分析

构造结构体类型时，计算每个成员变量的offset时，都按照其所需的align进行对齐   

```c
int align_to(int n, int align) { return (n + align - 1) / align * align; }

static Type *struct_decl(Token **rest, Token *token)
{
    ...
    type->align = 1;
    int offset  = 0;
    for (Member *member = type->members; member; member = member->next) {

        offset         = align_to(offset, member->type->align);
        member->offset = offset;
        offset += member->type->size;

        if (type->align < member->type->align)
            type->align = member->type->align;
    }
    type->size = align_to(offset, type->align);
    ...
}

```

# 对齐局部变量

## 语义分析

与结构体类似，函数的局部变量也存在调整内存布局的需要，但不同之处在于，结构体中成员变量的地址是从小到大递增，而栈中每个局部变量的地址则是从大到小减少的，因此需要先移动offset到足够容纳数据的位置，再进行对齐

```c
static void assign_local_val_offsets(Object *prog) {

  for (Object *f = prog; f; f = f->next) {
    if (!f->is_function)
      continue;

    int offset = 0;

    // 计算每个 local var 相对于栈顶的偏移
    for (Object *var = f->locals; var; var = var->next) {
      offset += var->type->size;
      offset = align_to(offset, var->type->align);
      var->offset = -offset;
    }

    // 计算栈的长度,并对齐到 16
    f->stack_size = align_to(offset, 16);
  }
}
```
