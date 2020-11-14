
#include "aggregate.h"
#include "reducer.h"

#include "query.h"
#include "extension.h"
#include "result_processor.h"
#include "ext/default.h"
// #include "extension.h"
#include "query_error.h"

#include "util/arr.h"
#include "rmutil/util.h"

///////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Ensures that the user has not requested one of the 'extended' features. Extended
 * in this case refers to reducers which re-create the search results.
 * @param areq the request
 * @param name the name of the option that requires simple mode. Used for error
 *   formatting
 * @param status the error object
 */
void AREQ::ensureSimpleMode() {
  RS_LOG_ASSERT(!(reqflags & QEXEC_F_IS_EXTENDED), "Single mod test failed");
  reqflags |= QEXEC_F_IS_SEARCH;
}

//---------------------------------------------------------------------------------------------

/**
 * Like @ref ensureSimpleMode(), but does the opposite -- ensures that one of the
 * 'simple' options - i.e. ones which rely on the field to be the exact same as
 * found in the document - was not requested.
 */
int AREQ::ensureExtendedMode(const char *name, QueryError *status) {
  if (reqflags & QEXEC_F_IS_SEARCH) {
    status->SetErrorFmt(QUERY_EINVAL,
                        "option `%s` is mutually exclusive with simple (i.e. search) options",
                        name);
    return 0;
  }
  reqflags |= QEXEC_F_IS_EXTENDED;
  return 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////

static int parseSortby(PLN_ArrangeStep *arng, ArgsCursor *ac, QueryError *status, int allowLegacy);

//---------------------------------------------------------------------------------------------

ReturnedField::~ReturnedField() {
  rm_free(highlightSettings.openTag);
  rm_free(highlightSettings.closeTag);
  rm_free(summarizeSettings.separator);
}

//---------------------------------------------------------------------------------------------

FieldList::~FieldList() {
  for (size_t ii = 0; ii < numFields; ++ii) {
    ReturnedField_Free(&fields->fields[ii]);
  }
  ReturnedField_Free(&fields->defaultField);
  rm_free(fields->fields);
}

//---------------------------------------------------------------------------------------------

ReturnedField *FieldList::GetCreateField(const char *name) {
  size_t foundIndex = -1;
  for (size_t ii = 0; ii < numFields; ++ii) {
    if (!strcasecmp(fields[ii].name, name)) {
      return &fields[ii];
    }
  }

  fields = rm_realloc(fields, sizeof(*fields) * ++numFields);
  ReturnedField *ret = &fields[numFields - 1];
  memset(ret, 0, sizeof *ret);
  ret->name = name;
  return ret;
}

//---------------------------------------------------------------------------------------------

void FieldList::RestrictReturn() {
  if (!explicitReturn) {
    return;
  }

  size_t oix = 0;
  for (size_t ii = 0; ii < numFields; ++ii) {
    if (fields[ii].explicitReturn == 0) {
      ReturnedField_Free(fields->fields + ii);
    } else if (ii != oix) {
      fields->fields[oix++] = fields->fields[ii];
    } else {
      ++oix;
    }
  }
  fields->numFields = oix;
}

//---------------------------------------------------------------------------------------------

int AREQ::parseCursorSettings(ArgsCursor *ac, QueryError *status) {
  ACArgSpec specs[] = {{name: "MAXIDLE",
                        type: AC_ARGTYPE_UINT,
                        target: &cursorMaxIdle,
                        intflags: AC_F_GE1},
                       {name: "COUNT",
                        type: AC_ARGTYPE_UINT,
                        target: &cursorChunkSize,
                        intflags: AC_F_GE1},
                       {NULL}};

  int rv;
  ACArgSpec *errArg = NULL;
  if ((rv = AC_ParseArgSpec(ac, specs, &errArg)) != AC_OK && rv != AC_ERR_ENOENT) {
    QERR_MKBADARGS_AC(status, errArg->name, rv);
    return REDISMODULE_ERR;
  }

  if (cursorMaxIdle == 0 || cursorMaxIdle > RSGlobalConfig.cursorMaxIdle) {
    cursorMaxIdle = RSGlobalConfig.cursorMaxIdle;
  }
  reqflags |= QEXEC_F_IS_CURSOR;
  return REDISMODULE_OK;
}

//---------------------------------------------------------------------------------------------

#define ARG_HANDLED 1
#define ARG_ERROR -1
#define ARG_UNKNOWN 0

int AREQ::handleCommonArgs(ArgsCursor *ac, bool allowLegacy, QueryError *status) {
  int rv;
  // This handles the common arguments that are not stateful
  if (AC_AdvanceIfMatch(ac, "LIMIT")) {
    PLN_ArrangeStep *arng = AGPLN_GetOrCreateArrangeStep(&ap);
    // Parse offset, length
    if (AC_NumRemaining(ac) < 2) {
      status->SetError(QUERY_EPARSEARGS, "LIMIT requires two arguments");
      return ARG_ERROR;
    }
    if ((rv = AC_GetU64(ac, &arng->offset, 0)) != AC_OK ||
        (rv = AC_GetU64(ac, &arng->limit, 0)) != AC_OK) {
      status->SetError(QUERY_EPARSEARGS, "LIMIT needs two numeric arguments");
      return ARG_ERROR;
    }

    if (arng->limit == 0) {
      // LIMIT 0 0
      reqflags |= QEXEC_F_NOROWS;
    } else if ((arng->limit > SEARCH_REQUEST_RESULTS_MAX) && (reqflags & QEXEC_F_IS_SEARCH)) {
      status->SetErrorFmt(QUERY_ELIMIT, "LIMIT exceeds maximum of %llu",
                             SEARCH_REQUEST_RESULTS_MAX);
      return ARG_ERROR;
    }
  } else if (AC_AdvanceIfMatch(ac, "SORTBY")) {
    PLN_ArrangeStep *arng = AGPLN_GetOrCreateArrangeStep(&ap);
    if ((parseSortby(arng, ac, status, reqflags & QEXEC_F_IS_SEARCH)) != REDISMODULE_OK) {
      return ARG_ERROR;
    }
  } else if (AC_AdvanceIfMatch(ac, "ON_TIMEOUT")) {
    if (AC_NumRemaining(ac) < 1) {
      status->SetError(QUERY_EPARSEARGS, "Need argument for ON_TIMEOUT");
      return ARG_ERROR;
    }
    const char *policystr = AC_GetStringNC(ac, NULL);
    tmoPolicy = TimeoutPolicy_Parse(policystr, strlen(policystr));
    if (tmoPolicy == TimeoutPolicy_Invalid) {
      status->SetErrorFmt(QUERY_EPARSEARGS, "'%s' is not a valid timeout policy",
                             policystr);
      return ARG_ERROR;
    }
  } else if (AC_AdvanceIfMatch(ac, "WITHCURSOR")) {
    if (parseCursorSettings(ac, status) != REDISMODULE_OK) {
      return ARG_ERROR;
    }
  } else if (AC_AdvanceIfMatch(ac, "_NUM_SSTRING")) {
    reqflags |= QEXEC_F_TYPED;
  } else if (AC_AdvanceIfMatch(ac, "WITHRAWIDS")) {
    reqflags |= QEXEC_F_SENDRAWIDS;
  } else {
    return ARG_UNKNOWN;
  }

  return ARG_HANDLED;
}

//---------------------------------------------------------------------------------------------

static int parseSortby(PLN_ArrangeStep *arng, ArgsCursor *ac, QueryError *status, int isLegacy) {
  // Assume argument is at 'SORTBY'
  ArgsCursor subArgs = {0};
  int rv;
  int legacyDesc = 0;

  // We build a bitmap of maximum 64 sorting parameters. 1 means asc, 0 desc
  // By default all bits are 1. Whenever we encounter DESC we flip the corresponding bit
  uint64_t ascMap = SORTASCMAP_INIT;
  const char **keys = NULL;

  if (isLegacy) {
    if (AC_NumRemaining(ac) > 0) {
      // Mimic subArgs to contain the single field we already have
      AC_GetSlice(ac, &subArgs, 1);
      if (AC_AdvanceIfMatch(ac, "DESC")) {
        legacyDesc = 1;
      } else if (AC_AdvanceIfMatch(ac, "ASC")) {
        legacyDesc = 0;
      }
    } else {
      goto err;
    }
  } else {
    rv = AC_GetVarArgs(ac, &subArgs);
    if (rv != AC_OK) {
      QERR_MKBADARGS_AC(status, "SORTBY", rv);
      goto err;
    }
  }

  keys = array_new(const char *, 8);

  if (isLegacy) {
    // Legacy demands one field and an optional ASC/DESC parameter. Both
    // of these are handled above, so no need for argument parsing
    const char *s = AC_GetStringNC(&subArgs, NULL);
    keys = array_append(keys, s);

    if (legacyDesc) {
      SORTASCMAP_SETDESC(ascMap, 0);
    }
  } else {
    while (!AC_IsAtEnd(&subArgs)) {

      const char *s = AC_GetStringNC(&subArgs, NULL);
      if (*s == '@') {
        if (array_len(keys) >= SORTASCMAP_MAXFIELDS) {
          QERR_MKBADARGS_FMT(status, "Cannot sort by more than %lu fields", SORTASCMAP_MAXFIELDS);
          goto err;
        }
        s++;
        keys = array_append(keys, s);
        continue;
      }

      if (!strcasecmp(s, "ASC")) {
        SORTASCMAP_SETASC(ascMap, array_len(keys) - 1);
      } else if (!strcasecmp(s, "DESC")) {
        SORTASCMAP_SETDESC(ascMap, array_len(keys) - 1);
      } else {
        // Unknown token - neither a property nor ASC/DESC
        QERR_MKBADARGS_FMT(status, "MISSING ASC or DESC after sort field (%s)", s);
        goto err;
      }
    }
  }

  // Parse optional MAX
  // MAX is not included in the normal SORTBY arglist.. so we need to switch
  // back to `ac`
  if (AC_AdvanceIfMatch(ac, "MAX")) {
    unsigned mx = 0;
    if ((rv = AC_GetUnsigned(ac, &mx, 0) != AC_OK)) {
      QERR_MKBADARGS_AC(status, "MAX", rv);
      goto err;
    }
    arng->limit = mx;
  }

  arng->sortAscMap = ascMap;
  arng->sortKeys = keys;
  return REDISMODULE_OK;
err:
  QERR_MKBADARGS_FMT(status, "Bad SORTBY arguments");
  if (keys) {
    array_free(keys);
  }
  return REDISMODULE_ERR;
}

//---------------------------------------------------------------------------------------------

static int parseQueryLegacyArgs(ArgsCursor *ac, RSSearchOptions *options, QueryError *status) {
  if (AC_AdvanceIfMatch(ac, "FILTER")) {
    // Numeric filter
    NumericFilter **curpp = array_ensure_tail(&options->legacy.filters, NumericFilter *);
    try {
      *curpp = new NumericFilter(ac, status);
    } catch (Error &x) {
      *curpp = NULL;
    }
    if (!*curpp) {
      return ARG_ERROR;
    }
  } else if (AC_AdvanceIfMatch(ac, "GEOFILTER")) {
    try {
      options->legacy.gf = new GeoFilter(ac, status);
    } catch (Error &x) {
      return ARG_ERROR;
    }
  } else {
    return ARG_UNKNOWN;
  }
  return ARG_HANDLED;
}

//---------------------------------------------------------------------------------------------

int AREQ::parseQueryArgs(ArgsCursor *ac, RSSearchOptions *searchOpts, AggregatePlan *plan,
                         QueryError *status) {
  // Parse query-specific arguments..
  const char *languageStr = NULL;
  ArgsCursor returnFields = {0};
  ArgsCursor inKeys = {0};
  ArgsCursor inFields = {0};
  ACArgSpec querySpecs[] = {
      {name: "INFIELDS", type: AC_ARGTYPE_SUBARGS, target: &inFields},  // Comment
      {name: "SLOP",     type: AC_ARGTYPE_INT,     target: &searchOpts->slop, intflags: AC_F_COALESCE},
      {name: "LANGUAGE", type: AC_ARGTYPE_STRING,  target: &languageStr},
      {name: "EXPANDER", type: AC_ARGTYPE_STRING,  target: &searchOpts->expanderName},
      {name: "INKEYS",   type: AC_ARGTYPE_SUBARGS, target: &inKeys},
      {name: "SCORER",   type: AC_ARGTYPE_STRING,  target: &searchOpts->scorerName},
      {name: "RETURN",   type: AC_ARGTYPE_SUBARGS, target: &returnFields},
      {AC_MKBITFLAG("INORDER", &searchOpts->flags, Search_InOrder)},
      {AC_MKBITFLAG("VERBATIM", &searchOpts->flags, Search_Verbatim)},
      {AC_MKBITFLAG("WITHSCORES", &reqflags, QEXEC_F_SEND_SCORES)},
      {AC_MKBITFLAG("WITHSORTKEYS", &reqflags, QEXEC_F_SEND_SORTKEYS)},
      {AC_MKBITFLAG("WITHPAYLOADS", &reqflags, QEXEC_F_SEND_PAYLOADS)},
      {AC_MKBITFLAG("NOCONTENT", &reqflags, QEXEC_F_SEND_NOFIELDS)},
      {AC_MKBITFLAG("NOSTOPWORDS", &searchOpts->flags, Search_NoStopwrods)},
      {AC_MKBITFLAG("EXPLAINSCORE", &reqflags, QEXEC_F_SEND_SCOREEXPLAIN)},
      {name: "PAYLOAD", type: AC_ARGTYPE_STRING, target: &ast.udata, len: &ast.udatalen},
      {0}};

  while (!AC_IsAtEnd(ac)) {
    ACArgSpec *errSpec = NULL;
    int rv = AC_ParseArgSpec(ac, querySpecs, &errSpec);
    if (rv == AC_OK) {
      continue;
    }

    if (rv != AC_ERR_ENOENT) {
      QERR_MKBADARGS_AC(status, errSpec->name, rv);
      return REDISMODULE_ERR;
    }

    // See if this is one of our arguments which requires special handling
    if (AC_AdvanceIfMatch(ac, "SUMMARIZE")) {
      ensureSimpleMode();
      if (ParseSummarize(ac, &outFields) == REDISMODULE_ERR) {
        QERR_MKBADARGS_FMT(status, "Bad arguments for SUMMARIZE");
        return REDISMODULE_ERR;
      }
      reqflags |= QEXEC_F_SEND_HIGHLIGHT;

    } else if (AC_AdvanceIfMatch(ac, "HIGHLIGHT")) {
      ensureSimpleMode();
      if (ParseHighlight(ac, &outFields) == REDISMODULE_ERR) {
        QERR_MKBADARGS_FMT(status, "Bad arguments for HIGHLIGHT");
        return REDISMODULE_ERR;
      }
      reqflags |= QEXEC_F_SEND_HIGHLIGHT;

    } else if ((reqflags & QEXEC_F_IS_SEARCH) &&
               ((rv = parseQueryLegacyArgs(ac, searchOpts, status)) != ARG_UNKNOWN)) {
      if (rv == ARG_ERROR) {
        return REDISMODULE_ERR;
      }
    } else {
      int rv = handleCommonArgs(ac, true, status);
      if (rv == ARG_HANDLED) {
        // nothing
      } else if (rv == ARG_ERROR) {
        return REDISMODULE_ERR;
      } else {
        break;
      }
    }
  }

  searchOpts->inkeys = (const char **)inKeys.objs;
  searchOpts->ninkeys = inKeys.argc;
  searchOpts->legacy.infields = (const char **)inFields.objs;
  searchOpts->legacy.ninfields = inFields.argc;
  searchOpts->language = RSLanguage_Find(languageStr);

  if (AC_IsInitialized(&returnFields)) {
    ensureSimpleMode();

    outFields.explicitReturn = 1;
    if (returnFields.argc == 0) {
      reqflags |= QEXEC_F_SEND_NOFIELDS;
    }

    while (!AC_IsAtEnd(&returnFields)) {
      const char *name = AC_GetStringNC(&returnFields, NULL);
      ReturnedField *f = FieldList_GetCreateField(&outFields, name);
      f->explicitReturn = 1;
    }
  }

  FieldList_RestrictReturn(&outFields);
  return REDISMODULE_OK;
}

//---------------------------------------------------------------------------------------------

static char *getReducerAlias(PLN_GroupStep *g, const char *func, const ArgsCursor *args) {

  sds out = sdsnew("__generated_alias");
  out = sdscat(out, func);
  // only put parentheses if we actually have args
  char buf[255];
  ArgsCursor tmp = *args;
  while (!AC_IsAtEnd(&tmp)) {
    size_t l;
    const char *s = AC_GetStringNC(&tmp, &l);
    while (*s == '@') {
      // Don't allow the leading '@' to be included as an alias!
      ++s;
      --l;
    }
    out = sdscatlen(out, s, l);
    if (!AC_IsAtEnd(&tmp)) {
      out = sdscat(out, ",");
    }
  }

  // only put parentheses if we actually have args
  sdstolower(out);

  // duplicate everything. yeah this is lame but this function is not in a tight loop
  char *dup = rm_strndup(out, sdslen(out));
  sdsfree(out);
  return dup;
}

//---------------------------------------------------------------------------------------------

static void groupStepFree(PLN_BaseStep *base) {
  PLN_GroupStep *g = (PLN_GroupStep *)base;
  if (g->reducers) {
    size_t nreducers = array_len(g->reducers);
    for (size_t ii = 0; ii < nreducers; ++ii) {
      PLN_Reducer *gr = g->reducers + ii;
      rm_free(gr->alias);
    }
    array_free(g->reducers);
  }

  RLookup_Cleanup(&g->lookup);
  rm_free(base);
}

//---------------------------------------------------------------------------------------------

static RLookup *groupStepGetLookup(PLN_BaseStep *bstp) {
  return &((PLN_GroupStep *)bstp)->lookup;
}

//---------------------------------------------------------------------------------------------

/**
 * Adds a reducer (with its arguments) to the group step
 * @param gstp the group step
 * @param name the name of the reducer
 * @param ac arguments to the reducer; if an alias is used, it is provided
 *  here as well.
 */

int PLNGroupStep_AddReducer(PLN_GroupStep *gstp, const char *name, ArgsCursor *ac,
                            QueryError *status) {
  // Just a list of functions..
  PLN_Reducer *gr = array_ensure_tail(&gstp->reducers, PLN_Reducer);

  gr->name = name;
  int rv = AC_GetVarArgs(ac, &gr->args);
  if (rv != AC_OK) {
    QERR_MKBADARGS_AC(status, name, rv);
    goto error;
  }

  const char *alias = NULL;
  // See if there is an alias
  if (AC_AdvanceIfMatch(ac, "AS")) {
    rv = AC_GetString(ac, &alias, NULL, 0);
    if (rv != AC_OK) {
      QERR_MKBADARGS_AC(status, "AS", rv);
      goto error;
    }
  }
  if (alias == NULL) {
    gr->alias = getReducerAlias(gstp, name, &gr->args);
  } else {
    gr->alias = rm_strdup(alias);
  }
  return REDISMODULE_OK;

error:
  array_pop(gstp->reducers);
  return REDISMODULE_ERR;
}

//---------------------------------------------------------------------------------------------

static void genericStepFree(PLN_BaseStep *p) {
  rm_free(p);
}

//---------------------------------------------------------------------------------------------

PLN_GroupStep *PLNGroupStep_New(const char **properties, size_t nproperties) {
  PLN_GroupStep *gstp = rm_calloc(1, sizeof(*gstp));
  gstp->properties = properties;
  gstp->nproperties = nproperties;
  gstp->base.dtor = groupStepFree;
  gstp->base.getLookup = groupStepGetLookup;
  gstp->base.type = PLN_T_GROUP;
  return gstp;
}

//---------------------------------------------------------------------------------------------

int AREQ::parseGroupby(ArgsCursor *ac, QueryError *status) {
  ArgsCursor groupArgs = {0};
  const char *s;
  AC_GetString(ac, &s, NULL, AC_F_NOADVANCE);
  int rv = AC_GetVarArgs(ac, &groupArgs);
  if (rv != AC_OK) {
    QERR_MKBADARGS_AC(status, "GROUPBY", rv);
    return REDISMODULE_ERR;
  }

  // Number of fields.. now let's see the reducers
  PLN_GroupStep *gstp = PLNGroupStep_New((const char **)groupArgs.objs, groupArgs.argc);
  AGPLN_AddStep(&ap, &gstp->base);

  while (AC_AdvanceIfMatch(ac, "REDUCE")) {
    const char *name;
    if (AC_GetString(ac, &name, NULL, 0) != AC_OK) {
      QERR_MKBADARGS_AC(status, "REDUCE", rv);
      return REDISMODULE_ERR;
    }
    if (PLNGroupStep_AddReducer(gstp, name, ac, status) != REDISMODULE_OK) {
      goto error;
    }
  }
  return REDISMODULE_OK;

error:
  return REDISMODULE_ERR;
}

//---------------------------------------------------------------------------------------------

static void freeFilterStep(PLN_BaseStep *bstp) {
  PLN_MapFilterStep *fstp = (PLN_MapFilterStep *)bstp;
  if (fstp->parsedExpr) {
    ExprAST_Free(fstp->parsedExpr);
  }
  if (fstp->shouldFreeRaw) {
    rm_free((char *)fstp->rawExpr);
  }
  rm_free((void *)fstp->base.alias);
  rm_free(bstp);
}

//---------------------------------------------------------------------------------------------

PLN_MapFilterStep *PLNMapFilterStep_New(const char *expr, int mode) {
  PLN_MapFilterStep *stp = rm_calloc(1, sizeof(*stp));
  stp->base.dtor = freeFilterStep;
  stp->base.type = mode;
  stp->rawExpr = expr;
  return stp;
}

//---------------------------------------------------------------------------------------------

int AREQ::handleApplyOrFilter(ArgsCursor *ac, QueryError *status, int isApply) {
  // Parse filters!
  const char *expr = NULL;
  int rv = AC_GetString(ac, &expr, NULL, 0);
  if (rv != AC_OK) {
    QERR_MKBADARGS_AC(status, "APPLY/FILTER", rv);
    return REDISMODULE_ERR;
  }

  PLN_MapFilterStep *stp = PLNMapFilterStep_New(expr, isApply ? PLN_T_APPLY : PLN_T_FILTER);
  AGPLN_AddStep(&ap, &stp->base);

  if (isApply) {
    if (AC_AdvanceIfMatch(ac, "AS")) {
      const char *alias;
      if (AC_GetString(ac, &alias, NULL, 0) != AC_OK) {
        QERR_MKBADARGS_FMT(status, "AS needs argument");
        goto error;
      }
      stp->base.alias = rm_strdup(alias);
    } else {
      stp->base.alias = rm_strdup(expr);
    }
  }
  return REDISMODULE_OK;

error:
  if (stp) {
    AGPLN_PopStep(&ap, &stp->base);
    stp->base.dtor(&stp->base);
  }
  return REDISMODULE_ERR;
}

//---------------------------------------------------------------------------------------------

static void loadDtor(PLN_BaseStep *bstp) {
  PLN_LoadStep *lstp = (PLN_LoadStep *)bstp;
  rm_free(lstp->keys);
  rm_free(lstp);
}

//---------------------------------------------------------------------------------------------

int AREQ::handleLoad(ArgsCursor *ac, QueryError *status) {
  ArgsCursor loadfields = {0};
  int rc = AC_GetVarArgs(ac, &loadfields);
  if (rc != AC_OK) {
    QERR_MKBADARGS_AC(status, "LOAD", rc);
    return REDISMODULE_ERR;
  }
  PLN_LoadStep *lstp = rm_calloc(1, sizeof(*lstp));
  lstp->base.type = PLN_T_LOAD;
  lstp->base.dtor = loadDtor;
  lstp->args = loadfields;
  lstp->keys = rm_calloc(loadfields.argc, sizeof(*lstp->keys));

  AGPLN_AddStep(&ap, &lstp->base);
  return REDISMODULE_OK;
}

//---------------------------------------------------------------------------------------------

/**
 * Create a new aggregate request. The request's lifecycle consists of several
 * stages:
 *
 * 1) New - creates a blank request
 *
 * 2) Compile - this gathers the request options from the commandline, creates
 *    the basic abstract plan.
 *
 * 3) ApplyContext - This is the second stage of Compile, and applies
 *    a stateful context. The reason for this state remaining separate is
 *    the ability to test parsing and option logic without having to worry
 *    that something might touch the underlying index.
 *    Compile also provides a place to optimize or otherwise rework the plan
 *    based on information known only within the query itself.
 *
 * 4) BuildPipeline: This lines up all the iterators so that it can be read from.
 *
 * 5) Execute: This step is optional, and iterates through the result iterator,
 *    formatting the output and sending it to the network client. This step is
 *    optional, since the iterator can be obtained directly via AREQ::RP and processed directly.
 *
 * 6) Free: This releases all resources consumed by the request
 */

//---------------------------------------------------------------------------------------------

/**
 * Compile the request given the arguments. This does not rely on
 * Redis-specific states and may be unit-tested. This largely just
 * compiles the options and parses the commands..
 */

int AREQ::Compile(RedisModuleString **argv, int argc, QueryError *status) {
  args = rm_malloc(sizeof(*args) * argc);
  nargs = argc;
  for (size_t ii = 0; ii < argc; ++ii) {
    size_t n;
    const char *s = RedisModule_StringPtrLen(argv[ii], &n);
    args[ii] = sdsnewlen(s, n);
  }

  // Parse the query and basic keywords first..
  ArgsCursor ac = {0};
  ArgsCursor_InitSDS(&ac, args, nargs);

  if (AC_IsAtEnd(&ac)) {
    status->SetError(QUERY_EPARSEARGS, "No query string provided");
    return REDISMODULE_ERR;
  }

  query = AC_GetStringNC(&ac, NULL);
  AGPLN_Init(&ap);

  if (parseQueryArgs(&ac, &searchopts, &ap, status) != REDISMODULE_OK) {
    goto error;
  }

  int hasLoad = 0;

  // Now we have a 'compiled' plan. Let's get some more options..

  while (!AC_IsAtEnd(&ac)) {
    int rv = handleCommonArgs(&ac, !!(reqflags & QEXEC_F_IS_SEARCH), status);
    if (rv == ARG_HANDLED) {
      continue;
    } else if (rv == ARG_ERROR) {
      goto error;
    }

    if (AC_AdvanceIfMatch(&ac, "GROUPBY")) {
      if (!ensureExtendedMode("GROUPBY", status)) {
        goto error;
      }
      if (parseGroupby(&ac, status) != REDISMODULE_OK) {
        goto error;
      }
    } else if (AC_AdvanceIfMatch(&ac, "APPLY")) {
      if (handleApplyOrFilter(&ac, status, 1) != REDISMODULE_OK) {
        goto error;
      }
    } else if (AC_AdvanceIfMatch(&ac, "LOAD")) {
      if (handleLoad(&ac, status) != REDISMODULE_OK) {
        goto error;
      }
    } else if (AC_AdvanceIfMatch(&ac, "FILTER")) {
      if (handleApplyOrFilter(&ac, status, 0) != REDISMODULE_OK) {
        goto error;
      }
    } else {
      status->FmtUnknownArg(&ac, "<main>");
      goto error;
    }
  }
  return REDISMODULE_OK;

error:
  return REDISMODULE_ERR;
}

//---------------------------------------------------------------------------------------------

void QueryAST::applyGlobalFilters(RSSearchOptions &opts, const RedisSearchCtx *sctx) {
  // The following blocks will set filter options on the entire query
  if (opts.legacy.filters) {
    for (size_t ii = 0; ii < array_len(opts.legacy.filters); ++ii) {
      QAST_GlobalFilterOptions legacyFilterOpts = {.numeric = opts.legacy.filters[ii]};
      SetGlobalFilters(legacyFilterOpts);
    }
    array_clear(opts.legacy.filters);  // so AREQ_Free() doesn't free the filters themselves, which
                                       // are now owned by the query object
  }
  if (opts.legacy.gf) {
    QAST_GlobalFilterOptions legacyOpts = {.geo = opts.legacy.gf};
    SetGlobalFilters(legacyOpts);
  }

  if (opts.inkeys) {
    opts.inids = rm_malloc(sizeof(*opts.inids) * opts.ninkeys);
    for (size_t ii = 0; ii < opts.ninkeys; ++ii) {
      t_docId did = DocTable_GetId(&sctx->spec->docs, opts.inkeys[ii], strlen(opts.inkeys[ii]));
      if (did) {
        opts.inids[opts.nids++] = did;
      }
    }
    GlobalFilterOptions filterOpts = {.ids = opts.inids, .nids = opts.nids};
    SetGlobalFilters(filterOpts);
  }
}

//---------------------------------------------------------------------------------------------

/**
 * This stage will apply the context to the request. During this phase, the
 * query will be parsed (and matched according to the schema), and the reducers
 * will be loaded and analyzed.
 *
 * This consumes a refcount of the context used.
 *
 * Note that this function consumes a refcount even if it fails!
 */

int AREQ::ApplyContext(QueryError *status) {
  // Sort through the applicable options:
  IndexSpec *index = sctx->spec;
  RSSearchOptions &opts = searchopts;

  if ((index->flags & Index_StoreByteOffsets) == 0 && (reqflags & QEXEC_F_SEND_HIGHLIGHT)) {
    status->SetError(QUERY_EINVAL,
                     "Cannot use highlight/summarize because NOOFSETS was specified at index level");
    return REDISMODULE_ERR;
  }

  // Go through the query options and see what else needs to be filled in!
  // 1) INFIELDS
  if (opts.legacy.ninfields) {
    opts.fieldmask = 0;
    for (size_t ii = 0; ii < opts.legacy.ninfields; ++ii) {
      const char *s = opts.legacy.infields[ii];
      t_fieldMask bit = IndexSpec_GetFieldBit(index, s, strlen(s));
      opts.fieldmask |= bit;
    }
  }

  if (opts.language == RS_LANG_UNSUPPORTED) {
    status->SetError(QUERY_EINVAL, "No such language");
    return REDISMODULE_ERR;
  }

  if (opts.scorerName && Extensions_GetScoringFunction(NULL, opts.scorerName) == NULL) {
    status->SetErrorFmt(QUERY_EINVAL, "No such scorer %s", opts.scorerName);
    return REDISMODULE_ERR;
  }

  if (!(opts.flags & Search_NoStopwrods)) {
    opts.stopwords = sctx->spec->stopwords;
  }

  int rv = ast->Parse(sctx, searchopts, query, strlen(query), status);
  if (rv != REDISMODULE_OK) {
    return REDISMODULE_ERR;
  }

  ast->applyGlobalFilters(opts, sctx);

  if (!(opts.flags & Search_Verbatim)) {
    if (ast->Expand(opts.expanderName, opts, sctx, status) != REDISMODULE_OK) {
      return REDISMODULE_ERR;
    }
  }

  conc = std::make_unique<ConcurrentSearchCtx>(sctx->redisCtx);
  rootiter = ast->Iterate(opts, sctx, conc);
  RS_LOG_ASSERT(rootiter, "QAST_Iterate failed");

  return REDISMODULE_OK;
}

//---------------------------------------------------------------------------------------------

static ResultProcessor *buildGroupRP(PLN_GroupStep *gstp, RLookup *srclookup, QueryError *err) {
  const RLookupKey *srckeys[gstp->nproperties], *dstkeys[gstp->nproperties];
  for (size_t ii = 0; ii < gstp->nproperties; ++ii) {
    const char *fldname = gstp->properties[ii] + 1;  // account for the @-
    srckeys[ii] = RLookup_GetKey(srclookup, fldname, RLOOKUP_F_NOINCREF);
    if (!srckeys[ii]) {
      err->SetErrorFmt(QUERY_ENOPROPKEY, "No such property `%s`", fldname);
      return NULL;
    }
    dstkeys[ii] = RLookup_GetKey(&gstp->lookup, fldname, RLOOKUP_F_OCREAT | RLOOKUP_F_NOINCREF);
  }

  Grouper *grp = Grouper_New(srckeys, dstkeys, gstp->nproperties);

  size_t nreducers = array_len(gstp->reducers);
  for (size_t ii = 0; ii < nreducers; ++ii) {
    // Build the actual reducer
    PLN_Reducer *pr = gstp->reducers + ii;
    ReducerOptions options = REDUCEROPTS_INIT(pr->name, &pr->args, srclookup, err);
    ReducerFactory ff = RDCR_GetFactory(pr->name);
    if (!ff) {
      // No such reducer!
      Grouper_Free(grp);
      err->SetErrorFmt(QUERY_ENOREDUCER, "No such reducer: %s", pr->name);
      return NULL;
    }
    Reducer *rr = ff(&options);
    if (!rr) {
      Grouper_Free(grp);
      return NULL;
    }

    // Set the destination key for the grouper!
    RLookupKey *dstkey =
        RLookup_GetKey(&gstp->lookup, pr->alias, RLOOKUP_F_OCREAT | RLOOKUP_F_NOINCREF);
    Grouper_AddReducer(grp, rr, dstkey);
  }

  return Grouper_GetRP(grp);
}

//---------------------------------------------------------------------------------------------

/** Pushes a processor up the stack. Returns the newly pushed processor
  * @param rp the processor to push
 * @param rpUpstream previous processor (used as source for rp)
 * @return the processor passed in `rp`.
 */
ResultProcessor *AREQ::pushRP(ResultProcessor *rp, ResultProcessor *rpUpstream) {
  rp->upstream = rpUpstream;
  rp->parent = &qiter;
  qiter.endProc = rp;
  return rp;
}

//---------------------------------------------------------------------------------------------

ResultProcessor *AREQ::getGroupRP(PLN_GroupStep *gstp, ResultProcessor *rpUpstream,
                                  QueryError *status) {
  AGGPlan *pln = &ap;
  RLookup *lookup = AGPLN_GetLookup(pln, &gstp->base, AGPLN_GETLOOKUP_PREV);
  ResultProcessor *groupRP = buildGroupRP(gstp, lookup, status);

  if (!groupRP) {
    return NULL;
  }

  // See if we need a LOADER group here...?
  RLookup *firstLk = AGPLN_GetLookup(pln, &gstp->base, AGPLN_GETLOOKUP_FIRST);

  if (firstLk == lookup) {
    // See if we need a loader step?
    const RLookupKey **kklist = NULL;
    for (RLookupKey *kk = firstLk->head; kk; kk = kk->next) {
      if ((kk->flags & RLOOKUP_F_DOCSRC) && (!(kk->flags & RLOOKUP_F_SVSRC))) {
        *array_ensure_tail(&kklist, const RLookupKey *) = kk;
      }
    }
    if (kklist != NULL) {
      ResultProcessor *rpLoader = RPLoader_New(firstLk, kklist, array_len(kklist));
      array_free(kklist);
      RS_LOG_ASSERT(rpLoader, "RPLoader_New failed");
      rpUpstream = pushRP(rpLoader, rpUpstream);
    }
  }

  return pushRP(groupRP, rpUpstream);
}

//---------------------------------------------------------------------------------------------

#define DEFAULT_LIMIT 10

ResultProcessor *AREQ::getArrangeRP(AGGPlan *pln, const PLN_BaseStep *stp, ResultProcessor *up,
                                    QueryError *status) {
  ResultProcessor *rp = NULL;
  PLN_ArrangeStep astp_s = {.base = {.type = PLN_T_ARRANGE}};
  PLN_ArrangeStep *astp = (PLN_ArrangeStep *)stp;

  if (!astp) {
    astp = &astp_s;
  }

  size_t limit = astp->offset + astp->limit;
  if (!limit) {
    limit = DEFAULT_LIMIT;
  }

  if (astp->sortKeys) {
    size_t nkeys = array_len(astp->sortKeys);
    astp->sortkeysLK = rm_malloc(sizeof(*astp->sortKeys) * nkeys);

    const RLookupKey **sortkeys = astp->sortkeysLK;

    RLookup *lk = AGPLN_GetLookup(pln, stp, AGPLN_GETLOOKUP_PREV);

    for (size_t ii = 0; ii < nkeys; ++ii) {
      sortkeys[ii] = RLookup_GetKey(lk, astp->sortKeys[ii], RLOOKUP_F_NOINCREF);
      if (!sortkeys[ii]) {
        status->SetErrorFmt(QUERY_ENOPROPKEY, "Property `%s` not loaded nor in schema",
                               astp->sortKeys[ii]);
        return NULL;
      }
    }

    rp = RPSorter_NewByFields(limit, sortkeys, nkeys, astp->sortAscMap);
    up = pushRP(rp, up);
  }

  // No sort? then it must be sort by score, which is the default.
  if (rp == NULL && (reqflags & QEXEC_F_IS_SEARCH)) {
    rp = RPSorter_NewByScore(limit);
    up = pushRP(rp, up);
  }

  if (astp->offset || (astp->limit && !rp)) {
    rp = RPPager_New(astp->offset, astp->limit);
    up = pushRP(rp, up);
  }

  return rp;
}

//---------------------------------------------------------------------------------------------

ResultProcessor *AREQ::getScorerRP() {
  const char *scorer = searchopts.scorerName;
  if (!scorer) {
    scorer = DEFAULT_SCORER_NAME;
  }
  ScoringFunctionArgs scargs = {0};
  if (reqflags & QEXEC_F_SEND_SCOREEXPLAIN) {
    scargs.scrExp = rm_calloc(1, sizeof(RSScoreExplain));
  }
  ExtScoringFunctionCtx *fns = Extensions_GetScoringFunction(&scargs, scorer);
  RS_LOG_ASSERT(fns, "Extensions_GetScoringFunction failed");
  IndexSpec_GetStats(sctx->spec, &scargs.indexStats);
  scargs.qdata = ast.udata;
  scargs.qdatalen = ast.udatalen;
  ResultProcessor *rp = RPScorer_New(fns, &scargs);
  return rp;
}

//---------------------------------------------------------------------------------------------

static int hasQuerySortby(const AGGPlan *pln) {
  const PLN_BaseStep *bstp = AGPLN_FindStep(pln, NULL, NULL, PLN_T_GROUP);
  if (bstp != NULL) {
    const PLN_ArrangeStep *arng = (PLN_ArrangeStep *)AGPLN_FindStep(pln, NULL, bstp, PLN_T_ARRANGE);
    if (arng && arng->sortKeys) {
      return 1;
    }
  } else {
    // no group... just see if we have an arrange step
    const PLN_ArrangeStep *arng = (PLN_ArrangeStep *)AGPLN_FindStep(pln, NULL, NULL, PLN_T_ARRANGE);
    return arng && arng->sortKeys;
  }
  return 0;
}

//---------------------------------------------------------------------------------------------

#define PUSH_RP()                      \
  rpUpstream = pushRP(rp, rpUpstream); \
  rp = NULL;

//---------------------------------------------------------------------------------------------

/**
 * Builds the implicit pipeline for querying and scoring, and ensures that our
 * subsequent execution stages actually have data to operate on.
 */
void AREQ::buildImplicitPipeline(QueryError *Status) {
  qiter.conc = &conc;
  qiter.sctx = sctx;
  qiter.err = Status;

  IndexSpecCache *cache = IndexSpec_GetSpecCache(sctx->spec);
  RS_LOG_ASSERT(cache, "IndexSpec_GetSpecCache failed")
  RLookup *first = AGPLN_GetLookup(&ap, NULL, AGPLN_GETLOOKUP_FIRST);

  RLookup_Init(first, cache);

  ResultProcessor *rp = RPIndexIterator_New(rootiter);
  ResultProcessor *rpUpstream = NULL;
  qiter.rootProc = qiter.endProc = rp;
  PUSH_RP();

  // Create a scorer if there is no subsequent sorter within this grouping
  if (!hasQuerySortby(&ap) && (reqflags & QEXEC_F_IS_SEARCH)) {
    rp = getScorerRP();
    PUSH_RP();
  }
}

//---------------------------------------------------------------------------------------------

/**
 * This handles the RETURN and SUMMARIZE keywords, which operate on the result
 * which is about to be returned. It is only used in FT.SEARCH mode
 */
int AREQ::buildOutputPipeline(QueryError *status) {
  AGGPlan *pln = &ap;
  ResultProcessor *rp = NULL, *rpUpstream = qiter.endProc;

  RLookup *lookup = AGPLN_GetLookup(pln, NULL, AGPLN_GETLOOKUP_LAST);
  // Add a LOAD step...
  const RLookupKey **loadkeys = NULL;
  if (outFields.explicitReturn) {
    // Go through all the fields and ensure that each one exists in the lookup stage
    for (size_t ii = 0; ii < outFields.numFields; ++ii) {
      const ReturnedField *rf = &outFields.fields[ii];
      RLookupKey *lk = RLookup_GetKey(lookup, rf->name, RLOOKUP_F_NOINCREF | RLOOKUP_F_OCREAT);
      if (!lk) {
        // TODO: this is a dead code
        status->SetErrorFmt(QUERY_ENOPROPKEY, "Property '%s' not loaded or in schema", rf->name);
        goto error;
      }
      *array_ensure_tail(&loadkeys, const RLookupKey *) = lk;
      // assign explicit output flag
      lk->flags |= RLOOKUP_F_EXPLICITRETURN;
    }
  }
  rp = RPLoader_New(lookup, loadkeys, loadkeys ? array_len(loadkeys) : 0);
  if (loadkeys) {
    array_free(loadkeys);
  }
  PUSH_RP();

  if (reqflags & QEXEC_F_SEND_HIGHLIGHT) {
    RLookup *lookup = AGPLN_GetLookup(pln, NULL, AGPLN_GETLOOKUP_LAST);
    for (size_t ii = 0; ii < outFields.numFields; ++ii) {
      ReturnedField *ff = &outFields.fields[ii];
      RLookupKey *kk = RLookup_GetKey(lookup, ff->name, 0);
      if (!kk) {
        status->SetErrorFmt(QUERY_ENOPROPKEY, "No such property `%s`", ff->name);
        goto error;
      } else if (!(kk->flags & (RLOOKUP_F_DOCSRC | RLOOKUP_F_SVSRC))) {
        // TODO: this is a dead code
        status->SetErrorFmt(QUERY_EINVAL, "Property `%s` is not in document", ff->name);
        goto error;
      }
      ff->lookupKey = kk;
    }
    rp = RPHighlighter_New(&earchopts, &outFields, lookup);
    PUSH_RP();
  }

  return REDISMODULE_OK;
error:
  return REDISMODULE_ERR;
}

//---------------------------------------------------------------------------------------------

/**
 * Constructs the pipeline objects needed to actually start processing the requests.
 * This does not yet start iterating over the objects
 */

int AREQ::BuildPipeline(BuildPipelineOptions options, QueryError *status) {
  if (!(options & AREQ_BUILDPIPELINE_NO_ROOT)) {
    buildImplicitPipeline(status);
  }

  AGGPlan *pln = &ap;
  ResultProcessor *rp = NULL, *rpUpstream = qiter.endProc;

  // Whether we've applied a SORTBY yet..
  int hasArrange = 0;

  for (const DLLIST_node *nn = pln->steps.next; nn != &pln->steps; nn = nn->next) {
    const PLN_BaseStep *stp = DLLIST_ITEM(nn, PLN_BaseStep, llnodePln);

    switch (stp->type) {
      case PLN_T_GROUP: {
        rpUpstream = getGroupRP((PLN_GroupStep *)stp, rpUpstream, status);
        if (!rpUpstream) {
          goto error;
        }
        break;
      }

      case PLN_T_ARRANGE: {
        rp = getArrangeRP(pln, stp, rpUpstream, status);
        if (!rp) {
          goto error;
        }
        hasArrange = 1;
        rpUpstream = rp;
        break;
      }

      case PLN_T_APPLY:
      case PLN_T_FILTER: {
        PLN_MapFilterStep *mstp = (PLN_MapFilterStep *)stp;
        // Ensure the lookups can actually find what they need
        RLookup *curLookup = AGPLN_GetLookup(pln, stp, AGPLN_GETLOOKUP_PREV);
        mstp->parsedExpr = ExprAST_Parse(mstp->rawExpr, strlen(mstp->rawExpr), status);
        if (!mstp->parsedExpr) {
          goto error;
        }

        if (!ExprAST_GetLookupKeys(mstp->parsedExpr, curLookup, status)) {
          goto error;
        }

        if (stp->type == PLN_T_APPLY) {
          RLookupKey *dstkey =
              RLookup_GetKey(curLookup, stp->alias, RLOOKUP_F_OCREAT | RLOOKUP_F_NOINCREF);
          rp = RPEvaluator_NewProjector(mstp->parsedExpr, curLookup, dstkey);
        } else {
          rp = RPEvaluator_NewFilter(mstp->parsedExpr, curLookup);
        }
        PUSH_RP();
        break;
      }

      case PLN_T_LOAD: {
        PLN_LoadStep *lstp = (PLN_LoadStep *)stp;
        RLookup *curLookup = AGPLN_GetLookup(pln, stp, AGPLN_GETLOOKUP_PREV);
        RLookup *rootLookup = AGPLN_GetLookup(pln, NULL, AGPLN_GETLOOKUP_FIRST);
        if (curLookup != rootLookup) {
          status->SetError(QUERY_EINVAL,
                              "LOAD cannot be applied after projectors or reducers");
          goto error;
        }
        // Get all the keys for this lookup...
        while (!AC_IsAtEnd(&lstp->args)) {
          const char *s = AC_GetStringNC(&lstp->args, NULL);
          if (*s == '@') {
            s++;
          }
          const RLookupKey *kk = RLookup_GetKey(curLookup, s, RLOOKUP_F_OEXCL | RLOOKUP_F_OCREAT);
          if (!kk) {
            // We only get a NULL return if the key already exists, which means
            // that we don't need to retrieve it again.
            continue;
          }
          lstp->keys[lstp->nkeys++] = kk;
        }
        if (lstp->nkeys) {
          rp = RPLoader_New(curLookup, lstp->keys, lstp->nkeys);
          PUSH_RP();
        }
        break;
      }
      case PLN_T_ROOT:
        // Placeholder step for initial lookup
        break;
      case PLN_T_DISTRIBUTE:
        // This is the root already
        break;

      case PLN_T_INVALID:
      case PLN_T__MAX:
        // not handled yet
        abort();
    }
  }

  // If no LIMIT or SORT has been applied, do it somewhere here so we don't
  // return the entire matching result set!
  if (!hasArrange && (reqflags & QEXEC_F_IS_SEARCH)) {
    rp = getArrangeRP(pln, NULL, rpUpstream, status);
    if (!rp) {
      goto error;
    }
    rpUpstream = rp;
  }

  // If this is an FT.SEARCH command which requires returning of some of the
  // document fields, handle those options in this function
  if ((reqflags & QEXEC_F_IS_SEARCH) && !(reqflags & QEXEC_F_SEND_NOFIELDS)) {
    if (buildOutputPipeline(status) != REDISMODULE_OK) {
      goto error;
    }
  }

  return REDISMODULE_OK;
error:
  return REDISMODULE_ERR;
}

//---------------------------------------------------------------------------------------------

AREQ::~AREQ() {
  // First, free the result processors
  ResultProcessor *rp = qiter->endProc;
  while (rp) {
    ResultProcessor *next = rp->upstream;
    rp->Free(rp);
    rp = next;
  }

  if (rootiter) {
    delete rootiter;
    rootiter = NULL;
  }

  // Go through each of the steps and free it..
  AGPLN_FreeSteps(&ap);

  if (searchopts.stopwords) {
    StopWordList_Unref((StopWordList *)searchopts.stopwords);
  }

  // Finally, free the context. 
  // If we are a cursor, some more cleanup is required since we also now own the
  // detached ("Thread Safe") context.
  RedisModuleCtx *thctx = NULL;
  if (sctx) {
    if (reqflags & QEXEC_F_IS_CURSOR) {
      thctx = sctx->redisCtx;
      sctx->redisCtx = NULL;
    }
    SearchCtx_Decref(sctx);
  }

  for (size_t ii = 0; ii < nargs; ++ii) {
    sdsfree(args[ii]);
  }
  if (searchopts.legacy.filters) {
    for (size_t ii = 0; ii < array_len(searchopts.legacy.filters); ++ii) {
      NumericFilter *nf = searchopts.legacy.filters[ii];
      if (nf) {
        delete searchopts.legacy.filters[ii];
      }
    }
    array_free(searchopts.legacy.filters);
  }
  rm_free(searchopts.inids);
  FieldList_Free(&outFields);

  if (thctx) {
    RedisModule_FreeThreadSafeContext(thctx);
  }
  rm_free(args);
}

///////////////////////////////////////////////////////////////////////////////////////////////
