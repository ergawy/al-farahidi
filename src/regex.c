#include "../include/utils.h"
#include "../include/regex.h"

#define fatal_error(msg, ...)                                   \
  fprintf(stderr, "Error %d:%d: ", currentLine, currentColumn); \
  fprintf(stderr, (msg), ## __VA_ARGS__);                       \
  exit(1)

#define warning(msg, ...)                                         \
  fprintf(stderr, "Warning %d:%d: ", currentLine, currentColumn); \
  fprintf(stderr, (msg), ## __VA_ARGS__);

static NonTerminal nonterms[MAX_NONTERMS];

/// A memory pool for storing all terminals. A '\0' separates a terminal from its
/// next neighbor.
static char termPool[MAX_TOTAL_TERM_LEN];
static char *currentTermStart = termPool;

static Expression exprPool[MAX_NESTED_EXPRS];
static int freeExprIdx = 0;

static int currentLine = 0;
static int currentColumn = 0;
static int currentNonterm = 0;
static int memcpy2(char *dest, char *src, int numBytes, char escapeChar,
            char *toEscape, char *toPut);

// writing this macro as a single statement instead of 2 separate ones is to allow
// it to be use to deference the character we moved to
#define moveRegexPtr(regex)        \
  ((++currentColumn), (++regex))

static void parse_regex(char *regex);
static int parse_header(char **regexPtr);
static void parse_body(char **regexPtr, int nontermIdx);
static void *parse_operand(char **regexPtr);
static OperatorType parse_operator(char **regexPtr);
static void log_expr(Expression* expr);

int parse_regex_spec(FILE *in, NonTerminalPtr *nontermTable) {
  char regexSpecLine[MAX_REGEX_LEN];

  while (fgets(regexSpecLine, MAX_REGEX_LEN, in) != NULL) {
    currentLine++;
    currentColumn = 0;
    parse_regex(regexSpecLine);
  }

  if (nontermTable != NULL) {
    *nontermTable = nonterms;  
  }

  return currentNonterm;
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
  assert(nontermNameSize <= MAX_NONTERM_NAME && "Non-terminal name is too"
         " long!\n");

  // memcpy is used instead of strcpy because strcpy stops at '\0' which won't
  // be available in our case
  char nontermName[MAX_NONTERM_NAME];
  memcpy(nontermName, nontermNameStart, nontermNameSize);
  nontermName[nontermNameSize] = '\0';

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
    assert(currentNonterm < MAX_NONTERMS && "Exceeded maximum number"
           " of non-terminals!\n");
  }

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
  Expression *currentExpr = &(nonterms[nontermIdx].expr);
  Expression *prevExpr = currentExpr;

  while ((op = parse_operand(regexPtr)) != NULL) {
    OperatorType opCode = parse_operator(regexPtr);
    currentExpr->type = opCode;
    currentExpr->op1 = op;
    currentExpr->op1Type = LEAF_OPERATOR;

    // found a suffix operator
    // parse the next operator
    //
    // the * expression will be the 1st operand of a new expression
    // this new expression is actually the 2nd operand of prevExpr
    // in other words, it contains the * expression
    //
    // example: (a b* ...) ==> (a & (b * (...))) will be replaced with
    // (a & ((b*) & (...)))
    if (opCode == ZERO_OR_MORE) {
      currentExpr->op2 = NULL;
      currentExpr->op2Type = NOTHING;

      assert(freeExprIdx < MAX_NESTED_EXPRS && "Expression pool is "
             "out of memory!\n");
      Expression* newExpr = &(exprPool[freeExprIdx]);
      newExpr->type = parse_operator(regexPtr);
      newExpr->op1 = currentExpr;
      newExpr->op1Type = NESTED_EXPRESSION;

      prevExpr->op2 = newExpr;
      prevExpr->op2Type = NESTED_EXPRESSION;

      currentExpr = newExpr;
      freeExprIdx++;
    }

    assert(freeExprIdx < MAX_NESTED_EXPRS && "Expression pool is "
           "out of memory!\n");
    prevExpr = currentExpr;
    currentExpr = prevExpr->op2 = &(exprPool[freeExprIdx]);
    prevExpr->op2Type = NESTED_EXPRESSION;
    freeExprIdx++;
  }

  assert((prevExpr->type == NO_OP || prevExpr->type == ZERO_OR_MORE)
         && "Last expression should be a no op\n");

  // we requested 1 extra expression from the pool at last iteration
  // return it back and delete it from the 2nd operand of the last
  // actual expression (should be a no op or unary expression).
  freeExprIdx--;
  prevExpr->op2 = NULL;

  /* log("+++++++++++++++++++++++++\n"); */
  /* log("%s:\n", nonterms[nontermIdx].name); */
  /* log_expr(&(nonterms[nontermIdx].expr)); */
  /* log("\n"); */
  /* log("-------------------------\n"); */
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
      assert(currentNonterm < MAX_NONTERMS && "Exceeded maximum number"
             " of non-terminals!\n");
      memcpy(nonterms[opIdx].name, operandStart, operandNameSize);
      nonterms[opIdx].name[operandNameSize] = '\0';
      nonterms[opIdx].complete = FALSE;
      nonterms[opIdx].idx = opIdx;
    }

    return (void*)(&nonterms[opIdx]);
  } else {
    assert(currentTermStart+operandNameSize-termPool <= MAX_TOTAL_TERM_LEN
           && "Terminal pool is out of memory!\n");
    int size = memcpy2(currentTermStart, operandStart, operandNameSize,
                       '@', "_@|*$", " @|*$");
    currentTermStart[size] = '\0';
    currentTermStart += (operandNameSize + 1);

    return currentTermStart - (operandNameSize+1);
  }
}

static OperatorType parse_operator(char **regexPtr) {
  while (isspace(**regexPtr) && **regexPtr != '\n') {
    moveRegexPtr(*regexPtr);
  }

  OperatorType opCode = NO_OP;

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
        fatal_error("An incomplete escape sequence at the end of a "
                    "string\n");
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

static void log_expr(Expression* expr) {
  if (expr == NULL) {
    return;
  }

  log("(");

  switch(expr->op1Type) {
  case NESTED_EXPRESSION:
    log_expr(expr->op1);
    break;
  case LEAF_OPERATOR:
    log("%s", (char*)(expr->op1));
    break;
  case NOTHING:
    log("");
  }

  switch(expr->type) {
  case NO_OP:
    log("");
    break;
  case OR:
    log(" | ");
    break;
  case AND:
    log(" & ");
    break;
  case ZERO_OR_MORE:
    log("*");
    break;
  }

  switch(expr->op2Type) {
  case NESTED_EXPRESSION:
    log_expr(expr->op2);
    break;
  case LEAF_OPERATOR:
    log("%s", (char*)(expr->op2));
    break;
  case NOTHING:
    log("");
  }

  log(")");
}
