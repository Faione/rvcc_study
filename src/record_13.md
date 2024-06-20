# 从文件中读取代码

早期设计中，通过向tokenize传入字符串来启动编译流程，现在修改为传入文件路径，tokenize需要从文件中读取文本再开始编译流程，因此需要增加一个`read_file`函数，用来从文件中读取待编译的代码（文件包括标准输入）

> open_memstream会创建一个动态数组，并支持stream操作接口，在数组伸缩时变更堆上内存大小

```c
static char *read_file(char *path) {
  FILE *in;

  if (strcmp(path, "-") == 0) {
    // 文件名为 "-" 时，从stdin读取文本
    in = stdin;
  } else {
    in = fopen(path, "r");
    if (!in)
      error("can't open %s: %s", path, strerror(errno));
  }

  char *buf;
  size_t buf_len;
  // 创建out数据流，将in中的文本数据读取到流中
  // buf指针指向out中的数据，buf_len为数据长度
  FILE *out = open_memstream(&buf, &buf_len);

  // 使用 read_buf 作为中转，将数据从 in 读取到 out 中
  while (true) {
    char *read_buf[4096];
    int num_read = fread(read_buf, sizeof(char), sizeof(read_buf), in);
    if (num_read == 0)
      break;
    fwrite(read_buf, sizeof(char), num_read, out);
  }

  if (in != stdin)
    fclose(in);

  // 刷新写缓冲区，保证所有内容都写入到 out 中
  fflush(out);

  // 保证以 `\n` 结尾
  if (buf_len == 0 || buf[buf_len - 1] != '\n')
    fputc('\n', out);

  // 满足字符串以 `\0` 结尾的要求
  fputc('\0', out);

  return buf;
}
```

同时，由于文件的引入，需要在编译错误时，给出更明确的错误定位

```c
// 指示错误出现的位置
// foo.c:10: x = y + 1;
//               ^ <错误信息>
static void verror_at(char *loc, char *fmt, va_list va) {
  char *start = loc, *end = loc;

  // 移动 line 到 loc 所在行的起始位置
  // CUR_INPUT是字符串的第一个字符
  while (CUR_INPUT < start && start[-1] != '\n')
    start--;

  // 计算行号
  // start 之前遇到 n 个 '\n'，意味着有 n 行
  // 而 start 位于第 n + 1 行
  int num_line = 1;
  for (char *p = CUR_INPUT; p < start; p++) {
    if (*p == '\n')
      num_line++;
  }

  // filename:line
  // indent记录输出了多少个字符
  int indent = fprintf(stderr, "%s:%d", CUR_FILENAME, num_line);
  // 输出存在错误的行到end为止的文本
  fprintf(stderr, "%.*s\n", (int)(end - start), start);

  // 计算错误信息要添加的位点
  int pos = loc - start + indent;

  // %*s 将会打印 pos 长度的字符串，若参数不满足长度 pos ，则使用空格补全
  fprintf(stderr, "%*s", pos, "");
  // 指示符
  fprintf(stderr, "^ ");
  vfprintf(stderr, fmt, va);
  fprintf(stderr, "\n");
  va_end(va);
}
```

# 增加Println函数

封装 printf，仅仅为了方便

# 支持 -o 与 --help

`-o` 用以指示目标文件路径，`--help`用来显示帮助信息，而在代码中额外增加了参数解析的逻辑，并使用两个全局变量来保存解析结果

```c
static char *OUTPUT_PATH;
static char *INPUT_PATH;

static void parse_args(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    // 解析 -h | --help
    if (!strcmp(argv[i], "-h"))
      usage(0);

    if (!strcmp(argv[i], "--help"))
      usage(0);

    // 解析 -o <path>
    if (!strcmp(argv[i], "-o")) {
      // <path> 为空则报错
      if (!argv[++i])
        usage(1);
      OUTPUT_PATH = argv[i];
      continue;
    }

    // 解析 <file>
    if (argv[i][0] == '-' && argv[i][1] != '\0')
      error("invalid argument: %s", argv[i]);

    INPUT_PATH = argv[i];
  }

  if (!INPUT_PATH)
    error("no input files");
}
```

同时，在codegen中也增加了输出到文件的选项，得益于对printf函数的封装，修改只需要围绕println函数进行

```c
static void println(char *fmt, ...) {
  va_list va;

  // 初始化 va 变量，fmt是最后一个固定参数
  va_start(va, fmt);
  vfprintf(OUTPUT_FILE, fmt, va);
  // 清理 va 变量
  va_end(va);

  fprintf(OUTPUT_FILE, "\n");
}
```

# 支持注释

注释仅用于说明，不参与代码的实际编译，支持注释首先需要确定注释的类型，如用于多行代码的块注释与用于单行代码的行注释，前者需要匹配开始与结尾，后者则需要匹配开头与`\n`，C标准中分别使用`/* ... */` 与 `//` 来标识上述注释。

考虑到注释内容不参与实际编译，因此只需要再词法分析过程中找到响应标识，并去除对应代码文本即可

## 词法分析

在读取文本的时候，跳过注释即可
- strstr函数能够在 haystack 字符串中寻找 needle 字符串的位置（起始指针），没有找到则返回NULL

```c
  while (*p) {
    // 跳过行注释
    if (starts_with(p, "//")) {
      p += 2;
      while (*p != '\n')
        p++;
      continue;
    }

    // 跳过块注释
    if (starts_with(p, "/*")) {
      // 在剩余字符串中寻找 "*/" 的位置
      char *q = strstr(p + 2, "*/");
      if (!q)
        error_at(p, "unclosed block comment");
      p = q + 2;
      continue;
    }
...
  }
```