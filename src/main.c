/***************************************************************
 *
 * CODING CONVENTIONS:
 * variableName, CONSTANT_NAME, function_name, StructName
 *
 **************************************************************/

#include <stdio.h>
#include "../include/regex.h"
#include "../include/nfa.h"

int main(int argc, char** argv) {
  NonTerminalPtr nontermTable = NULL;
  ExpressionPtr exprTable = NULL;
  char *termTable = NULL;

  int nontermTableSize = parse_regex_spec(stdin, &nontermTable,
                                          &exprTable, &termTable);
  build_nfa(nontermTable, nontermTableSize, exprTable, termTable);

  return 0;
}
