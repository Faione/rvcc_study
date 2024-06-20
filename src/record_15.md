# 用C重写测试

当前编译器已具备初步功能，能够与gcc编译工具链协作进行简单的代码编译，因此可以使用C来重新编写测试代码。

测试程序都会引用 `test.h` 头文件，并首先由gcc编译器进行预处理，再交给rvcc编译为汇编代码。而最后的汇编代码会与common一起由gcc编译器编译并静态链接为一个二进制文件，交给qemu-riscv模拟器执行

```c
// test.h
// assert函数在common中，并随后静态链接到最终代码中
#define ASSERT(x, y) assert(x, y, #y)

// arith.c
#include "test.h"

int main() {
  // [1] 返回指定数值
  ASSERT(0, 0);
  ASSERT(42, 42);
  // [2] 支持 + - 运算符
  ASSERT(21, 5 + 20 - 4);
  ...
}

```

assert函数的具体作用如下，其由gcc编译器编译并静态链接到最终的二进制文件中。

```c
// common
#include <stdio.h>
#include <stdlib.h>

void assert(int expected, int actual, char *code) {
  if (expected == actual) {
    printf("%s => %d\n", code, actual);
  } else {
    printf("%s => %d expected but got %d\n", code, expected, actual);
    exit(1);
  }
}
```


> `-E`选项要求gcc编译器只进行预处理，`-P`选项要求编译器忽略`#line`指示符，`-C`要求gcc编译器保留注释，而 `-xc` 则要求gcc将输入文件视为c文件并进行编译

测试脚本修改
- `test/%.out` 规则：用于编译可由qemu-riscv执行的完整二进制文件
- `test`：遍历所有编译好的`.out`文件，并调用qemu解释执行

```makefile
# 测试标签，运行测试
test/%.out: rvcc test/%.c
	$(RISCV)/bin/riscv64-unknown-linux-gnu-gcc -o- -E -P -C test/$*.c | ./rvcc -o test/$*.s -
	$(RISCV)/bin/riscv64-unknown-linux-gnu-gcc -static -o $@ test/$*.s -xc test/common

test: $(TESTS)
	for i in $^; do echo $$i; $(RISCV)/bin/qemu-riscv64 -L $(RISCV)/sysroot ./$$i || exit 1; echo; done
	test/driver.sh
```