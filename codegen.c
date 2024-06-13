#include "rvcc.h"
#include <stdio.h>

//
// 三、语义分析,生成代码
//

static void gen_expr(Node *node);
static void gen_stmt(Node *node);

// 输出文件
static FILE *OUTPUT_FILE;

// 输出格式化字符串并换行
static void println(char *fmt, ...) {
  va_list va;

  // 初始化 va 变量，fmt是最后一个固定参数
  va_start(va, fmt);
  vfprintf(OUTPUT_FILE, fmt, va);
  // 清理 va 变量
  va_end(va);

  fprintf(OUTPUT_FILE, "\n");
}

// (1) 函数

// 参数寄存器
static char *func_arg_regs[] = {"a0", "a1", "a2", "a3", "a4", "a5"};
// 当前函数
static Object *CUR_FUNC;

// (2) 栈
// 生成的代码中,利用栈保存中间数据
// 表达式中使用 a0, a1 两个寄存器保存算式中的数字
// 多次运算的结果通过栈来进行保存
// 当前预设数据长度为 64bit/8byte

// 当前分析代码的栈深度
static int STACK_DEPTH;

// 压栈
// 将 a0 寄存器中的值压入栈中
static void push(void) {
  println("  # 压栈, 将a0的值存入栈顶");
  println("  addi sp, sp, -8");
  println("  sd a0, 0(sp)");
  STACK_DEPTH++;
}

// 出栈
// 将栈顶的数据弹出到寄存器 reg 中
static void pop(char *reg) {
  println("  # 弹栈, 将栈顶的值存入%s", reg);
  println("  ld %s, 0(sp)", reg);
  println("  addi sp, sp, 8");
  STACK_DEPTH--;
}

// 每次调用都会生成一个新的 count
// 用来区分不同的代码段
static int count(void) {
  static int I = 1;
  return I++;
}

// 将 n 对其到 align 的整数倍
static int align_to(int n, int align) {
  return (n + align - 1) / align * align;
}

// 为链表中每个 Function 计算本地变量偏移及栈大小
static void assign_local_val_offsets(Object *prog) {

  for (Object *f = prog; f; f = f->next) {
    if (!f->is_function)
      continue;

    int offset = 0;

    // 计算每个 local var 相对于栈顶的偏移
    for (Object *var = f->locals; var; var = var->next) {
      offset += var->type->size;
      var->offset = -offset;
    }

    // 计算栈的长度,并对齐到 16
    f->stack_size = align_to(offset, 16);
  }
}

// 计算给定节点的内存地址
// 将地址保存在 a0 寄存器中
static void gen_addr(Node *node) {

  switch (node->kind) {
  case ND_VAR: // Object var 为指针, 在 prog 中被修改后, 同时也能从 Node 访问
    if (node->var->is_local) { // local var
      println("  # 获取变量%s的栈内地址为%d(fp)", node->var->name,
              node->var->offset);
      println("  addi a0, fp, %d", node->var->offset);
    } else { // global var
      println("  # 获取全局变量%s的地址", node->var->name);
      // la 指令是一个伪指令，将 %s symbol 标记的内存地址加载到 a0 中
      println("  la a0, %s", node->var->name);
    }
    return;
  case ND_DEREF: // 对一个解引用expr进行取地址
    gen_expr(node->lhs);
    return;
  default:
    break;
  }

  error_token(node->token, "not an lvalue");
}

// a0 中保存了一个地址
// 将此地址的值加载到 a0 中
static void load(Type *type) {
  if (type->kind == TY_ARRAY)
    return;
  println("  # 读取a0中存放的地址, 得到的值存入a0");
  if (type->size == 1)
    println("  lb a0, 0(a0)");
  else
    println("  ld a0, 0(a0)");
}

// 栈保存了一个地址
// 将此地址 pop 到 a1 中
// 将 a0 中的值保存到此地址
static void store(Type *type) {
  pop("a1");
  println("  # 将a0的值, 写入到 a1 中存放的地址");
  if (type->size == 1)
    println("sb a0, 0(a1)");
  else
    println("  sd a0, 0(a1)");
}

// 词法分析
// 生成代码
void gen_expr(Node *node) {
  switch (node->kind) {
  case ND_NUM:
    // 若根节点为数字(叶子节点), 则只加载到 a0 寄存器中
    println("  # 将%d加载到a0中", node->val);
    println("  li a0, %d", node->val);
    return;
  case ND_NEG:
    // 一元运算符子为单臂二叉树,子节点保留在左侧
    // 因此向左递归直到遇到数字
    gen_expr(node->lhs);
    println("  # 对a0值进行取反");
    println("  neg a0, a0");
    return;
  case ND_ADDR:
    // 计算单臂指向的变量的地址，保存到 a0 中
    gen_addr(node->lhs);
    return;
  case ND_DEREF:
    // 解引用向右递归
    gen_expr(node->lhs);
    load(node->type);
    return;
  case ND_VAR:
    gen_addr(node);
    load(node->type);
    return;
  case ND_ASSIGN:
    // 左值
    gen_addr(node->lhs);
    push();
    // 右值
    gen_expr(node->rhs);
    store(node->type);
    return;
  case ND_STMT_EXPR:
    for (Node *n = node->body; n; n = n->next)
      gen_stmt(n);
    return;
  case ND_FNCALL: {
    int argc = 0;

    // 遍历所有参数，并将参数逐个压入栈中
    for (Node *arg = node->args; arg; arg = arg->next) {
      gen_expr(arg);
      push();
      argc++;
    }

    // 上述指令执行完毕时，参数必然按顺序放置在栈上
    // 反向弹栈
    for (int i = argc - 1; i >= 0; i--) {
      pop(func_arg_regs[i]);
    }

    println("  # 调用函数%s", node->func_name);
    println("  call %s", node->func_name);

    return;
  }
  default:
    break;
  }

  // 递归右节点
  gen_expr(node->rhs);
  // 右侧的结果保存在 a0 中,压入到栈
  push();
  // 递归左节点
  gen_expr(node->lhs);
  // 左侧结果保存在 a0 中
  // 同时由于左侧计算完毕,栈回到递归右侧完毕时的状态
  // 即栈顶的就是右子树的结果
  pop("a1");

  // 此时 a0 保存了 lhs 的结果,而 a1 保存了 rhs 的结果
  switch (node->kind) {
  case ND_ADD:
    println("  # a0+a1,结果写入a0");
    println("  add a0, a0, a1");
    return;
  case ND_SUB:
    println("  # a0-a1,结果写入a0");
    println("  sub a0, a0, a1");
    return;
  case ND_MUL:
    println("  # a0×a1,结果写入a0");
    println("  mul a0, a0, a1");
    return;
  case ND_DIV:
    println("  # a0÷a1,结果写入a0");
    println("  div a0, a0, a1");
    return;
  case ND_EQ:
  case ND_NE:
    println("  # 判断是否a0%sa1", node->kind == ND_EQ ? "=" : "≠");
    println("  xor a0, a0 ,a1");
    if (node->kind == ND_EQ) {
      // a0 = 1 if a0 = 0
      println("  seqz a0, a0");
    } else {
      // a0 = 1 if a0 != 0
      println("  snez a0, a0");
    }
    return;
  case ND_LT:
    println("  # 判断a0<a1");
    println("  slt a0, a0, a1");
    return;
  case ND_LE:
    // a0 <= a1 == !(a1 < a0) == (a1 < a0) xor 1
    println("  # 判断是否a0≤a1");
    println("  slt a0, a1, a0");
    println("xori a0, a0, 1");
    return;
  default:
    break;
  }
  error_token(node->token, "invalid expression");
}

static void gen_stmt(Node *node) {
  switch (node->kind) {
  case ND_EXPR_STMT:
    gen_expr(node->lhs);
    return;
  case ND_RETURN:
    println("# 返回语句");
    gen_expr(node->lhs);
    println("  # 跳转到.L.return.%s段", CUR_FUNC->name);
    println("  j .L.return.%s", CUR_FUNC->name);
    return;
  case ND_BLOCK:
    for (Node *n = node->body; n; n = n->next) {
      gen_stmt(n);
    }
    return;
  case ND_IF: {
    int c = count();

    // condition
    println("\n# =====分支语句%d==============", c);
    println("\n# cond表达式%d", c);
    gen_expr(node->cond);
    println("  # 若a0为0,则跳转到分支%d的.L.else.%d段", c, c);
    println("  beqz a0, .L.else.%d", c);

    println("\n# Then语句%d", c);
    gen_stmt(node->then);
    println("\n# Else语句%d", c);
    println("# 分支%d的.L.else.%d段标签", c, c);
    println("  j .L.end.%d", c);

    // else 逻辑
    println(".L.else.%d:", c);
    if (node->els)
      gen_stmt(node->els);
    // end 标签
    println("\n# 分支%d的.L.end.%d段标签", c, c);
    println(".L.end.%d:", c);
    return;
  }
  case ND_FOR: { // 生成 for 或 while 循环代码
    int c = count();

    println("\n# =====循环语句%d===============", c);
    if (node->init) {
      println("\n# Init语句%d", c);
      gen_stmt(node->init);
    }

    println("\n# 循环%d的.L.begin.%d段标签", c, c);
    println(".L.begin.%d:", c);
    // 循环条件
    if (node->cond) {
      println("# Cond表达式%d", c);
      gen_expr(node->cond);
      println("  # 若a0为0,则跳转到循环%d的.L.end.%d段", c, c);
      println("  beqz a0, .L.end.%d", c);
    }

    println("\n# Then语句%d", c);
    gen_stmt(node->then);
    // 循环递增语句
    if (node->inc) {
      println("\n# Inc语句%d", c);
      gen_expr(node->inc);
    }

    println("  # 跳转到循环%d的.L.begin.%d段", c, c);
    println("  j .L.begin.%d", c);
    println("\n# 循环%d的.L.end.%d段标签", c, c);
    println(".L.end.%d:", c);
    return;
  }
  default:
    break;
  }

  error_token(node->token, "invalid statement");
}

// 生成 .data 段
//
// 存放 全局变量
static void emit_data(Object *prog) {
  for (Object *var = prog; var; var = var->next) {
    if (var->is_function)
      continue;

    println("  # 数据段标签");
    println("  .data");

    // 判断变量是否有初始值
    if (var->init_data) {
      println("%s:", var->name);
      // 将初始值内容进行打印
      for (int i = 0; i < var->type->size; i++) {
        char c = var->init_data[i];
        if (isprint(c))
          println("  .byte %d\t# 字符:  %c", c, c);
        else
          println("  .byte %d", c);
      }
    } else {
      println("  .globl %s", var->name);
      println("  # 全局变量%s", var->name);
      println("%s:", var->name);
      println("  # 零填充%d位", var->type->size);
      println("  .zero %d", var->type->size);
    }
  }
}

// 生成 .text 段
//
// 存放代码(Function)
static void emit_text(Object *prog) {
  for (Object *f = prog; f; f = f->next) {
    if (!f->is_function)
      continue;

    println("  # 定义全局%s段", f->name);
    println("  .globl %s", f->name);
    println("  .text");
    println("# =====%s段开始===============", f->name);
    println("# %s段标签", f->name);
    println("%s:", f->name);

    CUR_FUNC = f;

    // 栈布局
    //-------------------------------// sp
    //              ra
    //-------------------------------// ra = sp-8
    //              fp
    //-------------------------------// fp = sp-16
    //             变量
    //-------------------------------// sp = sp-16-StackSize
    //           表达式计算
    //-------------------------------//

    // Prologue
    println("  addi sp, sp, -16");
    println("  # 将ra压栈");
    println("  sd ra, 8(sp)");

    println("  # 将fp压栈,fp属于“被调用者保存”的寄存器,需要恢复原值");
    println("  sd fp, 0(sp)");

    println("  # 将sp的值写入fp");
    println("  mv fp, sp");

    println("  # sp腾出StackSize大小的栈空间");
    println("  addi sp, sp, -%d", f->stack_size);

    int i = 0;
    for (Object *var = f->params; var; var = var->next) {
      println("  # 将%s寄存器的值存入%s的栈地址", func_arg_regs[i], var->name);
      if (var->type->size == 1)
        println("  sb %s, %d(fp)", func_arg_regs[i++], var->offset);
      else
        println("  sd %s, %d(fp)", func_arg_regs[i++], var->offset);
    }

    println("\n# =====%s段主体===============", f->name);
    gen_stmt(f->body);
    assert(STACK_DEPTH == 0);

    // Epilogue
    println("\n# =====%s段结束===============", f->name);
    println("# %s return段标签", f->name);
    println(".L.return.%s:", f->name);

    println("  # 将fp的值写回sp");
    println("  mv sp, fp");

    println("  # 恢复fp、ra和sp");
    println("  ld fp, 0(sp)");
    println("  ld ra, 8(sp)");
    println("  addi sp, sp, 16");

    println("  # 返回a0值给系统调用");
    println("  ret");
  }
}

void codegen(Object *prog, FILE *out) {
  OUTPUT_FILE = out;

  // 计算每个 Function 中的局部变量偏移
  assign_local_val_offsets(prog);

  // 生成 .data 段
  emit_data(prog);

  // 生成 .text 段
  emit_text(prog);
}
