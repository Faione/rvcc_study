#include "rvcc.h"

static char *OUTPUT_PATH;
static char *INPUT_PATH;

static void usage(int status) {
  fprintf(stderr, "rvcc [ -o <path> ] <file>\n");
  exit(status);
}

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

static FILE *open_file(char *path) {
  if (!path || strcmp(path, "-") == 0)
    return stdout;

  // 以只写模式打开文件
  FILE *out = fopen(path, "w");
  if (!out)
    error("can't open output file: %s, error: %s", path, strerror(errno));

  return out;
}

int main(int argc, char **argv) {
  // 解析传入参数
  parse_args(argc, argv);

  // 1. 词法分析
  Token *token = tokenize_file(INPUT_PATH);

  // 2. 语法分析
  Object *prog = parse(token);

  // 3. 语义分析
  FILE *out = open_file(OUTPUT_PATH);
  codegen(prog, out);

  return 0;
}
