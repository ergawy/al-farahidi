#ifndef REGEX_H
#define REGEX_H

#include "utils.h"

#define MAX_NONTERMS       256
#define MAX_TOTAL_TERM_LEN 8192
#define MAX_NONTERM_NAME   64
// an average of 4 nested expressions per non-terminal looks like a reasonable
// value, this is multiplied by the maximum # of non-terms we can have
#define MAX_NESTED_EXPRS   4 * MAX_NONTERMS
#define MAX_REGEX_LEN      1024

typedef enum {
   NO_OP,
   OR,
   AND,
   ZERO_OR_MORE
} OperatorType;

typedef enum {
  NESTED_EXPRESSION,
  NON_TERMINAL,
  TERMINAL,
  NOTHING
} OperandType;

typedef struct Expression {
  // each operand can be either a terminal (char[]), a non-terminal
  // (an instance of NonTerminal struct), or even a nested expression
  PoolOffset op1;
  PoolOffset op2;

  OperandType op1Type;
  OperandType op2Type;
  OperatorType type;
} Expression, *ExpressionPtr;

// (1) typedef to avoid having to use "struct NonTerminal" everywhere
// a declaration is needed
// (2) According to the spec (C11, section 6.7.2.1, paragraph 15), the members of
// a struct are not reordered. This way an address of a struct is the same as its
// first member. I use this fact to treat non-terminals just like terminals (which
// are simple char[]) in places where is suitable.
typedef struct NonTerminal {
  char name[MAX_NONTERM_NAME];
  // the expression defining the non-terminal
  PoolOffset expr;
  // this will be false when a non-terminal is used in the definition of another one
  // before its definition is actually parsed
  _Bool complete;
  // index into the global nonterms array. Only for debugging purposes for now.
  int idx;
} NonTerminal, *NonTerminalPtr;

/*
/// A binary search tree for non-terminals
/// To keep things simple, no balancing is implemented at this phase
typedef struct NonTerminalNode {
NonTerminalPtr nonterm;
struct NonTerminalNode* left;
struct NonTerminalNode* right;
} NonTerminalNode, *NonTerminalNodePtr;
*/

/// Takes an input stream that provides the regex spec
/// Returns 2 values:
///   * If nontermTable != NULL, it's filled with a pointer to heap
///     allocated storage that stores the non-terminals
///   * Actually returns an int value containing the total numer of
///     non-terminals in the nontermTable
int parse_regex_spec(FILE* in, NonTerminalPtr* nontermTable,
                     ExpressionPtr *exprTable, char **termTale);

#endif
