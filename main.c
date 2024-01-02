#include "rvcc.h"

int main(int argc, char **argv) {
  if (argc != 2)
    error("%s: invalid number of arguments", argv[0]);

  // 1. 词法分析
  Token *token = tokenize(argv[1]);

  // 2. 语法分析
  Node *node = parse(token);

  // 3. 语义分析
  codegen(node);

  return 0;
}
