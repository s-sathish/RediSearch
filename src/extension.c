#include <dlfcn.h>
#include <stdio.h>
#include "extension.h"
#include "redisearch.h"
#include "rmalloc.h"
#include "redismodule.h"
#include "index_result.h"
#include "triemap/triemap.h"
#include "query.h"
#include <err.h>

///////////////////////////////////////////////////////////////////////////////////////////////

// The registry for query expanders. Initialized by Extensions_Init()
static TrieMap *Extensions::queryExpanders_g = NULL;

// The registry for scorers. Initialized by Extensions_Init()
static TrieMap *Extensions::scorers_g = NULL;

//---------------------------------------------------------------------------------------------

// Init the extension system - currently just create the regsistries

Extensions::Extensions() {
  if (!queryExpanders_g) {
    queryExpanders_g = new TrieMap();
    scorers_g = new TrieMap();
  }
}

//---------------------------------------------------------------------------------------------

static void freeExpanderCb(void *p) {
  rm_free(p);
}

//---------------------------------------------------------------------------------------------

static void freeScorerCb(void *p) {
  rm_free(p);
}

//---------------------------------------------------------------------------------------------

Extensions::~Extensions() {
  if (queryExpanders_g) {
    TrieMap_Free(queryExpanders_g, freeExpanderCb);
    queryExpanders_g = NULL;
  }
  if (scorers_g) {
    TrieMap_Free(scorers_g, freeScorerCb);
    scorers_g = NULL;
  }
}

//---------------------------------------------------------------------------------------------

/* Register a scoring function by its alias. privdata is an optional pointer to a user defined
 * struct. ff is a free function releasing any resources allocated at the end of query execution */
int Ext_RegisterScoringFunction(const char *alias, RSScoringFunction func, RSFreeFunction ff,
                                void *privdata) {
  if (func == NULL || scorers_g == NULL) {
    return REDISEARCH_ERR;
  }
  ExtScoringFunction *ctx = rm_new(ExtScoringFunction);
  ctx->privdata = privdata;
  ctx->ff = ff;
  ctx->sf = func;

  /* Make sure that two scorers are never registered under the same name */
  if (scorers_g->Find((char *)alias, strlen(alias)) != TRIEMAP_NOTFOUND) {
    rm_free(ctx);
    return REDISEARCH_ERR;
  }

  scorers_g->Add((char *)alias, strlen(alias), ctx, NULL);
  return REDISEARCH_OK;
}

//---------------------------------------------------------------------------------------------

/* Register a aquery expander */
int Ext_RegisterQueryExpander(const char *alias, RSQueryTokenExpander exp, RSFreeFunction ff,
                              void *privdata) {
  if (exp == NULL || queryExpanders_g == NULL) {
    return REDISEARCH_ERR;
  }
  ExtQueryExpanderCtx *ctx = rm_new(ExtQueryExpanderCtx);
  ctx->privdata = privdata;
  ctx->ff = ff;
  ctx->exp = exp;

  /* Make sure there are no two query expanders under the same name */
  if (queryExpanders_g->Find((char *)alias, strlen(alias)) != TRIEMAP_NOTFOUND) {
    rm_free(ctx);
    return REDISEARCH_ERR;
  }
  queryExpanders_g->Add((char *)alias, strlen(alias), ctx, NULL);
  return REDISEARCH_OK;
}

//---------------------------------------------------------------------------------------------

/* Load an extension by calling its init function. return REDISEARCH_ERR or REDISEARCH_OK */
int Extension_Load(const char *name, RSExtensionInitFunc func) {
  // bind the callbacks in the context
  RSExtensionCtx ctx = {
      .RegisterScoringFunction = Ext_RegisterScoringFunction,
      .RegisterQueryExpander = Ext_RegisterQueryExpander,
  };

  return func(&ctx);
}

//---------------------------------------------------------------------------------------------

/* Dynamically load a RediSearch extension by .so file path. Returns REDISMODULE_OK or ERR */
int Extension_LoadDynamic(const char *path, char **errMsg) {
  int (*init)(struct RSExtensionCtx *);
  void *handle;
  *errMsg = NULL;
  handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (handle == NULL) {
    FMT_ERR(errMsg, "Extension %s failed to load: %s", path, dlerror());
    return REDISMODULE_ERR;
  }
  init = (int (*)(struct RSExtensionCtx *))(unsigned long)dlsym(handle, "RS_ExtensionInit");
  if (init == NULL) {
    FMT_ERR(errMsg,
            "Extension %s does not export RS_ExtensionInit() "
            "symbol. Module not loaded.",
            path);
    return REDISMODULE_ERR;
  }

  if (Extension_Load(path, init) == REDISEARCH_ERR) {
    FMT_ERR(errMsg, "Could not register extension %s", path);
    return REDISMODULE_ERR;
  }

  return REDISMODULE_OK;
}

//---------------------------------------------------------------------------------------------

// Get a scoring function by name
static ExtScoringFunction *Extensions::GetScoringFunction(ScoringFunctionArgs *fnargs, const char *name) {
  if (!scorers_g) return NULL;

  /* lookup the scorer by name (case sensitive) */
  ExtScoringFunction *p = scorers_g->Find((char *)name, strlen(name));
  if (p && (void *)p != TRIEMAP_NOTFOUND) {
    /* if no ctx was given, we just return the scorer */
    if (fnargs) {
      fnargs->extdata = p->privdata;
      fnargs->GetSlop = IndexResult::MinOffsetDelta;
    }
    return p;
  }
  return NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////

/* The implementation of the actual query expansion. This function either turns the current node
 * into a union node with the original token node and new token node as children. Or if it is
 * already a union node (in consecutive calls), it just adds a new token node as a child to it */

void RSQueryExpander::ExpandToken(const char *str, size_t len, RSTokenFlags flags) {

  QueryAST *q = qast;
  QueryNode *qn = *currentNode;

  // Replace current node with a new union node if needed
  if (qn->type != QN_UNION) {
    QueryUnionNode *un;

    un->opts.fieldMask = qn->opts.fieldMask;

    // Append current node to the new union node as a child
    un->AddChild(qn);
    *currentNode = un;
  }

  QueryNode *exp = NewTokenNodeExpanded(q, str, len, flags);
  exp->opts.fieldMask = qn->opts.fieldMask;
  // Now the current node must be a union node - so we just add a new token node to it
  *currentNode->AddChild(exp);
  // q->numTokens++;
}

//---------------------------------------------------------------------------------------------

/* The implementation of the actual query expansion. This function either turns the current node
 * into a union node with the original token node and new token node as children. Or if it is
 * already a union node (in consecutive calls), it just adds a new token node as a child to it */

void RSQueryExpander::ExpandTokenWithPhrase(const char **toks, size_t num, RSTokenFlags flags,
                                            int replace, int exact) {

  QueryAST *q = qast;
  QueryNode *qn = *currentNode;

  QueryPhraseNode *ph = new QueryPhraseNode(exact);
  for (size_t i = 0; i < num; i++) {
    ph->AddChild(NewTokenNodeExpanded(q, toks[i], strlen(toks[i]), flags));
  }

  // if we're replacing - just set the expanded phrase instead of the token
  if (replace) {
    deletet qn;

    *ctx->currentNode = ph;
  } else {

    // Replace current node with a new union node if needed
    if (qn->type != QN_UNION) {
      QueryUnionNode *un;

      // Append current node to the new union node as a child
      un->AddChild(qn);
      *currentNode = un;
    }
    // Now the current node must be a union node - so we just add a new token node to it
    *currentNode->AddChild(ph);
  }
}

//---------------------------------------------------------------------------------------------

// Set the query payload
void RSQueryExpander::SetPayload(RSPayload payload) {
  qast->udata = payload.data;
  qast->udatalen = payload.len;
}

///////////////////////////////////////////////////////////////////////////////////////////////

// Get an expander by name

static ExtQueryExpander *Extensions::GetQueryExpander(RSQueryExpander *ctx, const char *name) {

  if (!queryExpanders_g) return NULL;

  ExtQueryExpander *p = queryExpanders_g->Find((char *)name, strlen(name));

  if (p && (void *)p != TRIEMAP_NOTFOUND) {
    ctx->ExpandToken = Ext_ExpandToken;
    ctx->SetPayload = Ext_SetPayload;
    ctx->ExpandTokenWithPhrase = Ext_ExpandTokenWithPhrase;
    ctx->privdata = p->privdata;
    return p;
  }
  return NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////
