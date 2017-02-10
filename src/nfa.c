#include "../include/nfa.h"

/// This is an implementation of Thompson's Construction to obtain NFAs
/// from regexs. For more details check "Engineering a Compiler", 2011,
/// Section 2.4.2

// Since for each regex (or a combination of 2 regexs), we need a constant
// number of new epsilon transitions (specifically, 1 for concatination
// , 4 for or, and 4 for Closure), the maximum total number in the final
// NFA will be c * (MAX_NESTED_EXPRS + MAX_NONTERMS) for some constant
// c < 5 (see regex.h for a definition of MAX_NESTED_EXPRS and MAX_NONTERMS).
// I use a constant factor of 10 here because in regex spec a reserved
// word is represented as a single unit while in the NFAs, reserved words
// are split to their individual characters. This gives an average of 6
// characters per reserved word, assuming that all terminals were reserved
// words which is too generous and more than enough.
#define MAX_NFA_EDGES      10 * (MAX_NESTED_EXPRS + MAX_NONTERMS)
#define MAX_EDGES_PER_NODE 128
#define MAX_NFA_STATES     1024
#define MAX_NFAS           MAX_NFA_STATES / 4
#define EPSILON            0
#define DEBUG              1

struct NFAEdge;

typedef enum {
  START,
  INTERNAL,
  ACCEPTING
} NFAStateType;

typedef struct NFAState {
  PoolOffset edges[MAX_EDGES_PER_NODE];
  int numEdges;
  NFAStateType type;
#if DEBUG
  bool visited;
#endif
} NFAState, *NFAStatePtr;

typedef struct NFAEdge {
  PoolOffset target;
  char symbol;
} NFAEdge, *NFAEdgePtr;

typedef struct NFA {
  PoolOffset start;
  PoolOffset accepting;
} NFA, *NFAPtr;

static NFAState nfaStatesPool[MAX_NFA_STATES];
static PoolOffset currentNFAState = 0;

static NFAEdge nfaEdgePool[MAX_NFA_EDGES];
static PoolOffset currentNFAEdge = 0;

static NFA nfaPool[MAX_NFAS];
static PoolOffset currentNFA = 0;

static PoolOffset new_start_state();
static PoolOffset new_state(NFAStateType type);
static PoolOffset new_accepting_state();
static PoolOffset new_edge(PoolOffset target, char symbol);
static PoolOffset new_nfa();
static PoolOffset build_single_symbol_nfa(char symbol);
static void build_concat_nfa(PoolOffset nfa1Idx, PoolOffset nfa2Idx);
static void build_or_nfa(PoolOffset nfa1Idx, PoolOffset nfa2Idx);
static void build_closure_nfa(PoolOffset nfaIdx);
static PoolOffset build_terminal_nfa(char *termianl);
static void print_nfa_graphviz(PoolOffset nfaIdx);
static void update_state_type(PoolOffset stateIdx, NFAStateType newType);

#if DEBUG
static void print_nfa(PoolOffset nfaIdx);
static void print_state(PoolOffset stateIdx);
#endif

void build_nfa(NonTerminalPtr nontermTable, int nontermTableSize) {
  PoolOffset a = build_single_symbol_nfa('a');
  PoolOffset b = build_single_symbol_nfa('b');
  PoolOffset c = build_single_symbol_nfa('c');
  build_or_nfa(a, b);
  build_concat_nfa(a, c);
  build_closure_nfa(a);
  PoolOffset d = build_terminal_nfa("test");
  build_or_nfa(d, a);
  build_closure_nfa(d);
  print_nfa_graphviz(d);
}

/// Build the NFA for a single symbol in the alphabet
///
///        OUTPUT
///    ---  sym   ===
///  >| a | ---> | b |
///    ---        ===
static PoolOffset build_single_symbol_nfa(char symbol) {
  PoolOffset nfaIdx = new_nfa();
  NFA nfa = nfaPool[nfaIdx];
  nfaStatesPool[nfa.start].edges[0] = new_edge(nfa.accepting, symbol);
  nfaStatesPool[nfa.start].numEdges++;
  return nfaIdx;
}

/// Concatinates nfa2 to nfa1
///
///         nfa1      INPUTS      nfa2
///    ---  sym   ===        ---  sym   ===
///  >| a | ---> | b |     >| c | ---> | d |
///    ---        ===        ---        ===
///
///                   OUTPUT
///                    nfa1
///    ---  sym   ---  eps   ---  sym   ===
///  >| a | ---> | b | ---> | c | ---> | d |
///    ---        ---        ---        ===
// TODO nfa2 becomes unsed storage after this, reuse that memory
// Check: https://github.com/KareemErgawy/al-farahidi/issues/2
static void build_concat_nfa(PoolOffset nfa1Idx, PoolOffset nfa2Idx) {
  assert(nfa1Idx != nfa2Idx && "Trying to concat an NFA to itself!\n");
  NFAPtr nfa1 = nfaPool + nfa1Idx;
  NFAPtr nfa2 = nfaPool + nfa2Idx;
  NFAStatePtr nfa1Accepting = nfaStatesPool + nfa1->accepting;
  nfa1Accepting->type = INTERNAL;
  nfa1Accepting->edges[nfa1Accepting->numEdges] = new_edge(nfa2->start,
                                                           (char)EPSILON);
  nfa1Accepting->numEdges++;
  nfa1->accepting = nfa2->accepting;
  update_state_type(nfa2->start, INTERNAL);
}

/// OR nfa1 and nfa2 into nfa1
///
///         nfa1      INPUTS      nfa2
///    ---  sym   ===        ---  sym   ===
///  >| a | ---> | b |     >| c | ---> | d |
///    ---        ===        ---        ===
///
///                   OUTPUT
///                    nfa1
///         eps   ---  sym   ---  eps
///         ---> | a | ---> | b | ---
///         |     ---        ---     |
///        ---                       |      ===
///      >| e |                       ---> | f |
///        ---         nfa2          |      ===
///         |     ---  sym   ---     |
///         ---> | c | ---> | d | ---
///         eps   ---        ---  eps
static void build_or_nfa(PoolOffset nfa1Idx, PoolOffset nfa2Idx) {
  assert(nfa1Idx != nfa2Idx && "Trying to OR an NFA to itself!\n");
  PoolOffset newStartIdx = new_start_state();
  NFAStatePtr newStart = nfaStatesPool + newStartIdx;
  PoolOffset newAcceptingIdx = new_accepting_state();

  NFAPtr nfa1 = nfaPool + nfa1Idx;
  NFAPtr nfa2 = nfaPool + nfa2Idx;
  PoolOffset nfa1StartIdx = nfa1->start;
  PoolOffset nfa1AcceptingIdx = nfa1->accepting;
  PoolOffset nfa2StartIdx = nfa2->start;
  PoolOffset nfa2AcceptingIdx = nfa2->accepting;

  // Update old start and accepting states to be internal
  update_state_type(nfa1StartIdx, INTERNAL);
  update_state_type(nfa1AcceptingIdx, INTERNAL);
  update_state_type(nfa2StartIdx, INTERNAL);
  update_state_type(nfa2AcceptingIdx, INTERNAL);

  // Connect the new start with the old 2 starts
  newStart->edges[0] = new_edge(nfa1StartIdx, EPSILON);
  newStart->edges[1] = new_edge(nfa2StartIdx, EPSILON);
  newStart->numEdges = 2;

  // Connect the 2 old accepting states with the new accepting
  NFAStatePtr nfa1Accepting = nfaStatesPool + nfa1AcceptingIdx;
  NFAStatePtr nfa2Accepting = nfaStatesPool + nfa2AcceptingIdx;
  nfa1Accepting->edges[nfa1Accepting->numEdges] =
    new_edge(newAcceptingIdx, EPSILON);
  nfa1Accepting->numEdges++;
  nfa2Accepting->edges[nfa2Accepting->numEdges] =
    new_edge(newAcceptingIdx, EPSILON);
  nfa2Accepting->numEdges++;

  // Update nfa1 with the new start and accepting states
  nfa1->start = newStartIdx;
  nfa1->accepting = newAcceptingIdx;
}

/// Build the NFA for r* for some regular expresion r expressed by the
/// argument NFA
///
///                  INPUT
///              ---  sym   ===
///            >| a | ---> | b |
///              ---        ===
///
///                  OUTPUT
///                    eps
///              ---------------
///             |               |
///    ---  eps |   ---  sym   ---  eps   ===
///  >| c | -----> | a | ---> | c | ---> | d |
///    ---          ---        ---   |    ===
///     |                            |
///      ----------------------------
///                    eps
static void build_closure_nfa(PoolOffset nfaIdx) {
  PoolOffset newStartIdx = new_start_state();
  NFAStatePtr newStart = nfaStatesPool + newStartIdx;
  PoolOffset newAcceptingIdx = new_accepting_state();

  NFAPtr nfa = nfaPool + nfaIdx;
  PoolOffset nfaStartIdx = nfa->start;
  PoolOffset nfaAcceptingIdx = nfa->accepting;
  NFAStatePtr nfaAccepting = nfaStatesPool + nfaAcceptingIdx;

  // Update old start and accepting to be internal states
  update_state_type(nfaStartIdx, INTERNAL);
  update_state_type(nfaAcceptingIdx, INTERNAL);

  // Add 2 epsilon transitions from new start to old start and new
  // accepting
  newStart->edges[0] = new_edge(nfaStartIdx, EPSILON);
  newStart->edges[1] = new_edge(newAcceptingIdx, EPSILON);
  newStart->numEdges = 2;

  // Add 2 epsilon transitions from old accepting to old start and new
  // accepting states
  nfaAccepting->edges[nfaAccepting->numEdges++] =
    new_edge(nfaStartIdx, EPSILON);
  nfaAccepting->edges[nfaAccepting->numEdges++] =
    new_edge(newAcceptingIdx, EPSILON);

  nfa->start = newStartIdx;
  nfa->accepting = newAcceptingIdx;
}

/// Build a chain NFA out of a mutli-characher terminal. Every symbol is
/// concatenated to the next one.
static PoolOffset build_terminal_nfa(char *terminal) {
  size_t len = strlen(terminal);
  assert(len > 0 && "Trying to build an NFA for an empty terminal");
  PoolOffset startIdx = new_start_state();
  PoolOffset prevStateIdx = startIdx;

  while(*terminal != '\0') {
    NFAStatePtr prevState = nfaStatesPool + prevStateIdx;
    PoolOffset currentStateIdx = new_state(INTERNAL);

    assert(prevState->numEdges == 0 && "This state should have 0 edges\n");
    prevState->edges[0] = new_edge(currentStateIdx, *terminal);
    prevState->numEdges = 1;

    prevStateIdx = currentStateIdx;
    terminal++;
  }

  update_state_type(prevStateIdx, ACCEPTING);

  PoolOffset nfaIdx = new_nfa();
  NFAPtr nfa = nfaPool + nfaIdx;
  nfa->start = startIdx;
  nfa->accepting = prevStateIdx;
  return nfaIdx;
}

/// Gets a free state from the pool and returns its index
static PoolOffset new_start_state() {
  return new_state(START);
}

/// Gets a free state from the pool and returns its index
static PoolOffset new_accepting_state() {
  return new_state(ACCEPTING);
}

static PoolOffset new_state(NFAStateType type) {
  assert(currentNFAState < MAX_NFA_STATES && "NFA states pool ran out of"
         "memory!\n");
  nfaStatesPool[currentNFAState].type = type;
  nfaStatesPool[currentNFAState].numEdges = 0;
  return currentNFAState++;
}

static PoolOffset new_edge(PoolOffset target, char symbol) {
  assert(currentNFAEdge < MAX_NFA_EDGES && "NFA edges pool ran out of"
         "memory!\n");
  nfaEdgePool[currentNFAEdge].target = target;
  nfaEdgePool[currentNFAEdge].symbol = symbol;
  return currentNFAEdge++;
}

static PoolOffset new_nfa() {
  assert(currentNFA < MAX_NFAS && "NFA pool ran out of memory!\n");
  nfaPool[currentNFA].start = new_start_state();
  nfaPool[currentNFA].accepting = new_accepting_state();
  return currentNFA++;
}

static void update_state_type(PoolOffset stateIdx, NFAStateType newType) {
  NFAStatePtr state = nfaStatesPool + stateIdx;
  state->type = newType;
}

#if DEBUG
static void print_nfa(PoolOffset nfaIdx) {
  print_state(nfaPool[nfaIdx].start);
}

static void print_state(PoolOffset stateIdx) {
  NFAStatePtr state = nfaStatesPool + stateIdx;

  if (state->visited) {
    return;
  }

  state->visited = TRUE;
  log("State %d ", stateIdx);

  switch(state->type) {
  case START:
    log("<start>");
    break;
  case ACCEPTING:
    log("<accept>");
    break;
  case INTERNAL:
    break;
  }

  log("\n");

  for (int i=0 ; i<state->numEdges ; i++) {
    NFAEdge edge = nfaEdgePool[state->edges[i]];
    log ("\t==(Symbol %c)==> State %d\n", edge.symbol, edge.target);
  }

  for (int i=0 ; i<state->numEdges ; i++) {
    NFAEdge edge = nfaEdgePool[state->edges[i]];
    print_state(edge.target);
  }

  /* state->visited = FALSE; */
}
#endif

static void print_state_graphviz(PoolOffset stateIdx) {
  NFAStatePtr state = nfaStatesPool + stateIdx;

  if (state->visited) {
    return;
  }

  state->visited = TRUE;

  switch (state->type) {
  case START:
    log("\tS%d [shape=box,style=filled,color=\".0 .7 .3\"];\n", stateIdx);
    break;
  case INTERNAL:
    break;
  case ACCEPTING:
    log("\tS%d [shape=box,style=filled,color=\".7 .0 .3\"];\n", stateIdx);
    break;
  }

  for (int i=0 ; i<state->numEdges ; i++) {
    NFAEdge edge = nfaEdgePool[state->edges[i]];
    if (edge.symbol == '\0') {
      log("\tS%d -> S%d [label=\"eps\"];\n", stateIdx, edge.target);
    } else {
      log("\tS%d -> S%d [label=\"%c\"];\n", stateIdx, edge.target,
          edge.symbol);
    }
  }

  for (int i=0 ; i<state->numEdges ; i++) {
    NFAEdge edge = nfaEdgePool[state->edges[i]];
    print_state_graphviz(edge.target);
  }

  /* state->visited = FALSE; */ 
}

static void print_nfa_graphviz(PoolOffset nfaIdx) {
  log("digraph NFA {\n");
  print_state_graphviz(nfaPool[nfaIdx].start);
  log("}\n");
}
