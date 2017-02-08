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
  int nontermTableSize = parse_regex_spec(stdin, &nontermTable);
  build_nfa(nontermTable, nontermTableSize);

  return 0;
}
