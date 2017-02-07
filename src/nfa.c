#include "../include/nfa.h"

/// This is an implementation of Thompson's Construction to obtain NFAs
/// from regexs. For more details check "Engineering a Compiler", 2011,
/// Section 2.4.2

// Since for each regex (or a combination of 2 regexs), we need a constant
// number of new epsilon transitions (specifically, 1 for concatination
// , 4 for or, and 4 for Kleene), the maximum total number in the final
// NFA will be c * (MAX_NESTED_EXPRS + MAX_NONTERMS) for some constant
// c < 5 (see regex.h for a definition of MAX_NESTED_EXPRS and MAX_NONTERMS).
// I use a constant factor of 10 here because in regex spec a reserved
// word is represented as a single unit while in the NFAs, reserved words
// are split to their individual characters. This gives an average of 6
// characters per reserved word, assuming that all terminals were reserved
// words which is too generous and more than enough.
#define MAX_NFA_EDGES      10 * (MAX_NESTED_EXPRS + MAX_NONTERMS)
#define MAX_EDGES_PER_NODE 128

struct NFAEdge;

typedef enum {
  START,
  INTENAL,
  ACCEPTING
} NFANodeType;

typedef struct NFANode {
  struct NFAEdge *edges[MAX_EDGES_PER_NODE];
  NFANodeType type;
} NFANode, *NFANodePtr;

typedef struct NFAEdge {
  NFANodePtr target;
  char transChar;
} NFAEdge, *NFAEdgePtr;
