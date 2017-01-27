#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NONTERMS       256
#define MAX_TOTAL_TERM_LEN 8192
#define MAX_NONTERM_NAME   64
// an average of 4 nested expressions per non-terminal looks like a reasonable
// value, this is multiplied by the maximum # of non-terms we can have
#define MAX_NESTED_EXPRS   4 * MAX_NONTERMS
#define MAX_REGEX_LEN      1024
#define TRUE               1
#define FALSE              0

#define log(msg, ...)                   \
  fprintf(stdout, (msg), ## __VA_ARGS__)

#define fatal_error(msg, ...)                                   \
  fprintf(stderr, "Error %d:%d: ", currentLine, currentColumn); \
  fprintf(stderr, (msg), ## __VA_ARGS__);                       \
  exit(1)

#define warning(msg, ...)                                         \
  fprintf(stderr, "Warning %d:%d: ", currentLine, currentColumn); \
  fprintf(stderr, (msg), ## __VA_ARGS__);

typedef enum {
   NO_OP,
   OR,
   AND,
   ZERO_OR_MORE
} ExpType;

typedef struct Expression {
  // each operand can be either a terminal (char[]), a non-terminal
  // (an instance of NonTerminal struct), or even a nested expression
  //
  // in case there is a nested expression, the first memer will recusively
  // end up being a char[]
  //
  // I am not sure whether this is a good design or not, but one way to
  // tell whether a void* is a term, non-term, or expr, is to check for
  // sizeof of the underlying entity. it's guaranteed that:
  //   1) sizeof(term) < sizeof(expr) sice a term is a char* and an
  //      expr contains 2 of those in addition to other stuff.
  //   2) sizeof(expr) < sizeof(nonterm) since a nonterm owns its exprsssion
  //      rather than a pointer to it.
  void *op1;
  void *op2;

  ExpType type;
} Expression;

// (1) typedef to avoid having to use "struct NonTerminal" everywhere
// a declaration is needed
// (2) According to the spec (C11, section 6.7.2.1, paragraph 15), the members of
// a struct are not reordered. This way an address of a struct is the same as its
// first member. I use this fact to treat non-terminals just like terminals (which
// are simple char[]) in places where is suitable.
typedef struct NonTerminal {
  char name[MAX_NONTERM_NAME];
  // the expression defining the non-terminal
  struct Expression expr;
  // this will be false when a non-terminal is used in the definition of another one
  // before its definition is actually parsed
  _Bool complete;
  // index into the global nonterms array. Only for debugging purposes for now.
  int idx;
} NonTerminal, *NonTerminalPtr;

static NonTerminal nonterms[MAX_NONTERMS];
/// A memory pool for storing all terminals. A '\0' separates a terminal from its
/// next neighbor.
static char termPool[MAX_TOTAL_TERM_LEN];
static char *currentTermStart = termPool;
static int currentLine = 0;
static int currentColumn = 0;
static int currentNonterm = 0;
static int memcpy2(char *dest, char *src, int numBytes, char escapeChar,
            char *toEscape, char *toPut);

// writing this macro as a single statement instead of 2 separate ones is to allow
// it to be use to deference the character we moved to
#define moveRegexPtr(regex)        \
  ((++currentColumn), (++regex))

/*
/// A binary search tree for non-terminals
/// To keep things simple, no balancing is implemented at this phase
typedef struct NonTerminalNode {
  NonTerminalPtr nonterm;
  struct NonTerminalNode* left;
  struct NonTerminalNode* right;
} NonTerminalNode, *NonTerminalNodePtr;
*/


static void parse_regex(char *regex);
static int parse_header(char **regexPtr);
static void parse_body(char **regexPtr, int nontermIdx);
static void *parse_operand(char **regexPtr);
static ExpType parse_operator(char **regexPtr);

void parse_regex_spec(FILE *in) {
  //log("%ld, %ld, %ld\n", sizeof(char*), sizeof(Expression),
  //    sizeof(NonTerminal));
  // log("%d\n", MAX_NESTED_EXPRS);

  char regexSpecLine[MAX_REGEX_LEN];

  while (fgets(regexSpecLine, MAX_REGEX_LEN, in) != NULL) {
    currentLine++;
    currentColumn = 0;
    parse_regex(regexSpecLine);
  }
}

/// Divides a regex into its individual components
static void parse_regex(char *regex) {
  while (isspace(*regex)) {
    moveRegexPtr(regex);
  }

  if (*regex == '\0') {
    return;
  }

  // skip comments
  if (*regex == '!') {
    return;
  }

  int nontermIdx = parse_header(&regex);
  parse_body(&regex, nontermIdx);

  nonterms[nontermIdx].complete = TRUE;
}

static int parse_header(char **regexPtr) {
    if (**regexPtr != '$') {
    fatal_error("Malformed regex spec line. Each line must specify a non-terminal\n\t%s",
                *regexPtr);
  }

  char *nontermNameStart = *regexPtr;
  moveRegexPtr(*regexPtr);

  while (**regexPtr != '\0' && !isspace(**regexPtr)) {
    moveRegexPtr(*regexPtr);
  }

  if (*regexPtr == nontermNameStart+1) {
    fatal_error("Empty non-terminal name\n");
  }

  if (**regexPtr == '\0' || **regexPtr == '\n') {
    fatal_error("Missing definition of a non-termianl\n");
  }

  int nontermNameSize = *regexPtr - nontermNameStart;
  // memcpy is used instead of strcpy because strcpy stops at '\0' which won't
  // be available in our case
  char nontermName[MAX_NONTERM_NAME];
  memcpy(nontermName, nontermNameStart, nontermNameSize);
  nontermName[nontermNameSize] = '\0';

  log("Line %d: Found a nonterm: >>%s<<\n", currentLine, nontermName);
  int nontermIdx = -1;

  // check if the non-term was encountered before
  for (int i=0 ; i<currentNonterm ; i++) {
    if (strcmp(nontermName, nonterms[i].name) == 0) {
      if (nonterms[i].complete) {
        fatal_error("Re-definition of a non-terminal: %s\n", nontermName);
      } else {
        nontermIdx = i;
        break;
      }
    }
  }

  if (nontermIdx == -1) {
    nontermIdx = currentNonterm++;
  }

  log("Nonterm index: %d\n", nontermIdx);
  strcpy(nonterms[nontermIdx].name, nontermName);
  nonterms[nontermIdx].idx = nontermIdx;

  while (isspace(**regexPtr)) {
    moveRegexPtr(*regexPtr);
  }

  if (**regexPtr != ':' || *moveRegexPtr(*regexPtr) != '=') {
    fatal_error("Missing definition of a non-termianl\n");
  }

  moveRegexPtr(*regexPtr);

  while (isspace(**regexPtr) && **regexPtr != '\n') {
    moveRegexPtr(*regexPtr);
  }

  if (**regexPtr == '\0' || **regexPtr == '\n') {
    fatal_error("Missing definition of a non-termianl\n");
  }

  return nontermIdx;
}

static void parse_body(char **regexPtr, int nontermIdx) {
  void *op = NULL;
  while ((op = parse_operand(regexPtr)) != NULL) {
    log("%s ", op);

    ExpType opCode = parse_operator(regexPtr);
    switch (opCode) {
    case NO_OP:
      log(" NO_OP ");
      break;
    case OR:
      log(" OR ");
      break;
    case AND:
      log(" AND ");
      break;
    case ZERO_OR_MORE:
      log(" ZERO_OR_MORE ");
      break;
    }
  }

  log("\n");
}

static void *parse_operand(char **regexPtr) {
  while (isspace(**regexPtr) && **regexPtr != '\n') {
    moveRegexPtr(*regexPtr);
  }

  // last operand is (supposidly) parsed already
  // if there was a problem it should have been caught in parse_regex
  if (**regexPtr == '\0' || **regexPtr == '\n') {
    return NULL;
  }

  if (**regexPtr == '|' || **regexPtr == '*') {
    fatal_error("An operator without an operand\n");
  }

  char* operandStart = *regexPtr;

  while (**regexPtr != '\0' && !isspace(**regexPtr)) {
    moveRegexPtr(*regexPtr);
  }

  if (*(*regexPtr-1) == '*' && *(*regexPtr-2) != '@') {
    --(*regexPtr);
    --currentColumn;
  }

  int operandNameSize = *regexPtr - operandStart;

  if (*operandStart == '$') {
    if (operandNameSize == 1) {
      fatal_error("Empty non-terminal name\n");
    }

    int opIdx = -1;

    for (int i=0 ; i<currentNonterm ; i++) {
      if (memcmp(nonterms[i].name, operandStart, operandNameSize) == 0) {
        opIdx = i;
        break;
      }
    }

    if (opIdx == -1) {
      opIdx = currentNonterm++;
      memcpy(nonterms[opIdx].name, operandStart, operandNameSize);
      nonterms[opIdx].name[operandNameSize] = '\0';
      nonterms[opIdx].complete = FALSE;
      nonterms[opIdx].idx = opIdx;
    }

    return (void*)(&nonterms[opIdx]);
  } else {

    int size = memcpy2(currentTermStart, operandStart, operandNameSize,
                       '@', "_@|*$", " @|*$");
    currentTermStart[size] = '\0';
    currentTermStart += (operandNameSize + 1);
    //log("op name: %s\n", (currentTermStart-(operandNameSize+1)));

    return currentTermStart - (operandNameSize+1);
  }
}

static ExpType parse_operator(char **regexPtr) {
  while (isspace(**regexPtr) && **regexPtr != '\n') {
    moveRegexPtr(*regexPtr);
  }

  ExpType opCode = NO_OP;

  if (**regexPtr == '\n' || **regexPtr == '\0') {
    opCode = NO_OP;
  } else if (**regexPtr == '|') {
    opCode = OR;
    moveRegexPtr(*regexPtr);
  } else if (**regexPtr == '*') {
    opCode = ZERO_OR_MORE;
    moveRegexPtr(*regexPtr);
  } else {
    // we currently hit the next operand, this must be an AND
    // don't move to next character
    opCode = AND;
  }

  return opCode;
}

/// Similar to std memcpy but with an additional support for escape sequences
/// An escapeChar that marks the start of an escape sequence is provided
/// and a list a valid escaped character is provided to check the validity
/// of the provided sequence.
///
/// It is assumed that the caller know what he is doing and that src numBytes
/// actuaully has numBytes of accissable memory. No seg fault should happen
/// between src and src+numBytes. Same goes for dest, we assume it has the
/// enough allocated memory
///
/// Returns the number of bytes copied to dest (might be less than numBytes)
/// because of escape sequences.
static int memcpy2(char *dest, char *src, int numBytes, char escapeChar,
            char *toEscape, char *toPut) {
  int copied = numBytes;
  int c;

  while (--numBytes >= 0) {
    if (*src == escapeChar) {
      // avoid seg faults by not going beyond the end of the given src
      // block of memory
      if (numBytes <= 0) {
        fatal_error("An escape sequence at the end of a string\n");
      }

      src++;
      char *pos = strchr(toEscape, *src);

      if (pos == NULL) {
        warning("Incorrect escape sequence\n");
        // copy whatever char we found
        c = *src;
      } else {
        // replace the found char with the corresponding one.
        c = *(toPut + (pos-toEscape));
      }

      copied--;
    }

    *(char*)dest = *(char*)src;
    dest++;
    src++;
  }

  return copied;
}
