#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "parse.h"
#include "parser.h"
#include "../query_node.h"
#include "../stopwords.h"

/* forward declarations of stuff generated by lemon */

#define RSQuery_Parse RSQueryParser_ // weird Lemon quirk.. oh well..
#define RSQuery_ParseAlloc RSQueryParser_Alloc
#define RSQuery_ParseFree RSQueryParser_Free

void RSQuery_Parse(void *yyp, int yymajor, QueryToken yyminor, QueryParse *ctx);
void *RSQuery_ParseAlloc(void *(*mallocProc)(size_t));
void RSQuery_ParseFree(void *p, void (*freeProc)(void *));

%%{

machine query;

inf = ['+\-']? 'inf' $ 3;
number = '-'? digit+('.' digit+)? (('E'|'e') '-'? digit+)? $ 2;

quote = '"';
or = '|';
lp = '(';
rp = ')';
lb = '{';
rb = '}';
colon = ':';
semicolon = ';';
arrow = '=>';
minus = '-';
tilde = '~';
star = '*';
percent = '%';
rsqb = ']';
lsqb = '[';
escape = '\\';
escaped_character = escape (punct | space | escape);
term = (((any - (punct | cntrl | space | escape)) | escaped_character) | '_')+  $ 0 ;
prefix = term.star $1;
mod = '@'.term $ 1;
attr = '$'.term $ 1;

main := |*

  number => {
    tok.s = ts;
    tok.len = te-ts;
    char *ne = (char*)te;
    tok.numval = strtod(tok.s, &ne);
    tok.pos = ts-q->raw;
    RSQuery_Parse(pParser, NUMBER, tok, q);
    if (!QPCTX_ISOK(q)) {
      fbreak;
    }

  };
  mod => {
    tok.pos = ts-q->raw;
    tok.len = te - (ts + 1);
    tok.s = ts+1;
    RSQuery_Parse(pParser, MODIFIER, tok, q);
    if (!QPCTX_ISOK(q)) {
      fbreak;
    }
  };
  attr => {
    tok.pos = ts-q->raw;
    tok.len = te - (ts + 1);
    tok.s = ts+1;
    RSQuery_Parse(pParser, ATTRIBUTE, tok, q);
    if (!QPCTX_ISOK(q)) {
      fbreak;
    }
  };
  arrow => {
    tok.pos = ts-q->raw;
    tok.len = te - ts;
    tok.s = ts+1;
    RSQuery_Parse(pParser, ARROW, tok, q);
    if (!QPCTX_ISOK(q)) {
      fbreak;
    }
  };
  inf => {
    tok.pos = ts-q->raw;
    tok.s = ts;
    tok.len = te-ts;
    tok.numval = *ts == '-' ? -INFINITY : INFINITY;
    RSQuery_Parse(pParser, NUMBER, tok, q);
    if (!QPCTX_ISOK(q)) {
      fbreak;
    }
  };

  quote => {
    tok.pos = ts-q->raw;
    RSQuery_Parse(pParser, QUOTE, tok, q);
    if (!QPCTX_ISOK(q)) {
      fbreak;
    }
  };
  or => {
    tok.pos = ts-q->raw;
    RSQuery_Parse(pParser, OR, tok, q);
    if (!QPCTX_ISOK(q)) {
      fbreak;
    }
  };
  lp => {
    tok.pos = ts-q->raw;
    RSQuery_Parse(pParser, LP, tok, q);
    if (!QPCTX_ISOK(q)) {
      fbreak;
    }
  };

  rp => {
    tok.pos = ts-q->raw;
    RSQuery_Parse(pParser, RP, tok, q);
    if (!QPCTX_ISOK(q)) {
      fbreak;
    }
  };
  lb => {
    tok.pos = ts-q->raw;
    RSQuery_Parse(pParser, LB, tok, q);
    if (!QPCTX_ISOK(q)) {
      fbreak;
    }
  };
  rb => {
    tok.pos = ts-q->raw;
    RSQuery_Parse(pParser, RB, tok, q);
    if (!QPCTX_ISOK(q)) {
      fbreak;
    }
  };
   colon => {
     tok.pos = ts-q->raw;
     RSQuery_Parse(pParser, COLON, tok, q);
    if (!QPCTX_ISOK(q)) {
      fbreak;
    }
   };
    semicolon => {
     tok.pos = ts-q->raw;
     RSQuery_Parse(pParser, SEMICOLON, tok, q);
    if (!QPCTX_ISOK(q)) {
      fbreak;
    }
   };

  minus =>  {
    tok.pos = ts-q->raw;
    RSQuery_Parse(pParser, MINUS, tok, q);
    if (!QPCTX_ISOK(q)) {
      fbreak;
    }
  };
  tilde => {
    tok.pos = ts-q->raw;
    RSQuery_Parse(pParser, TILDE, tok, q);
    if (!QPCTX_ISOK(q)) {
      fbreak;
    }
  };
 star => {
    tok.pos = ts-q->raw;
    RSQuery_Parse(pParser, STAR, tok, q);
    if (!QPCTX_ISOK(q)) {
      fbreak;
    }
  };
   percent => {
    tok.pos = ts-q->raw;
    RSQuery_Parse(pParser, PERCENT, tok, q);
    if (!QPCTX_ISOK(q)) {
      fbreak;
    }
  };
  lsqb => {
    tok.pos = ts-q->raw;
    RSQuery_Parse(pParser, LSQB, tok, q);
    if (!QPCTX_ISOK(q)) {
      fbreak;
    }
  };
  rsqb => {
    tok.pos = ts-q->raw;
    RSQuery_Parse(pParser, RSQB, tok, q);
    if (!QPCTX_ISOK(q)) {
      fbreak;
    }
  };
  space;
  punct;
  cntrl;

  term => {
    tok.len = te-ts;
    tok.s = ts;
    tok.numval = 0;
    tok.pos = ts-q->raw;
    if (!StopWordList_Contains(q->opts->stopwords, tok.s, tok.len)) {
      RSQuery_Parse(pParser, TERM, tok, q);
    } else {
      RSQuery_Parse(pParser, STOPWORD, tok, q);
    }
    if (!QPCTX_ISOK(q)) {
      fbreak;
    }
  };
  prefix => {
    tok.len = te-ts - 1;
    tok.s = ts;
    tok.numval = 0;
    tok.pos = ts-q->raw;

    RSQuery_Parse(pParser, PREFIX, tok, q);

    if (!QPCTX_ISOK(q)) {
      fbreak;
    }
  };


*|;
}%%

%% write data;

QueryNode *QueryParse::ParseRaw() {
  void *pParser = RSQuery_ParseAlloc(rm_malloc);


  int cs, act;
  const char* ts = raw;
  const char* te = raw + len;
  %% write init;
  QueryToken tok = {.len = 0, .pos = 0, .s = 0};

  //parseCtx ctx = {.root = NULL, .ok = 1, .errorMsg = NULL, .q = q};
  const char* p = raw;
  const char* pe = raw + len;
  const char* eof = pe;

  %% write exec;

  if (QPCTX_ISOK(q)) {
    RSQuery_Parse(pParser, 0, tok, q);
  }
  RSQuery_ParseFree(pParser, rm_free);
  if (!QPCTX_ISOK(q) && root) {
    QueryNode_Free(root);
    root = NULL;
  }
  return root;
}

