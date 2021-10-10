#pragma once
#include "geo_index.h"
#include "query_parser/tokenizer.h"
#include "param.h"
#include "vector_index.h"

struct QueryParseCtx;

typedef enum {
  QP_GEO_FILTER,
  QP_NUMERIC_FILTER,
  QP_RANGE_NUMBER,
  QP_VEC_FILTER,
  } QueryParamType;

typedef struct {
  union {
    GeoFilter *gf;
    NumericFilter *nf;
    RangeNumber *rn;
    VectorFilter *vf;
  };
  QueryParamType type;
  Param *params;
} QueryParam;


QueryParam *NewQueryParam(QueryParamType type);
QueryParam *NewTokenQueryParam(QueryToken *qt);
QueryParam *NewGeoFilterQueryParam(GeoFilter *gf);
QueryParam *NewGeoFilterQueryParam_WithParams(struct QueryParseCtx *q, QueryToken *lon, QueryToken *lat, QueryToken *radius, QueryToken *unit);

QueryParam *NewNumericFilterQueryParam(NumericFilter *nf);
QueryParam *NewNumericFilterQueryParam_WithParams(struct QueryParseCtx *q, QueryToken *min, QueryToken *max, int inclusiveMin, int inclusiveMax);

QueryParam *NewVectorFilterQueryParam(struct VectorFilter *vf);
QueryParam *NewVectorFilterQueryParam_WithParams(struct QueryParseCtx *q, QueryToken *vec, QueryToken *type, QueryToken *value);


#define QueryParam_NumParams(p) ((p)->params ? array_len((p)->params) : 0)
#define QueryParam_GetParam(p, ix) (QueryParam_NumParams(p) > ix ? (p)->params[ix] : NULL)

void QueryParam_InitParams(QueryParam *p, size_t num);
void QueryParam_Free(QueryParam *p);

/*
 * Resolve the value of a param
 * Return 0 if not parameterized
 * Return 1 if value was resolved successfully
 * Return -1 if param is missing or its kind is wrong
 */
int QueryParam_Resolve(Param *param, dict *params, QueryError *status);

/*
 * Set the `target` Param according to `source`
 * Return true if `source` is parameterized (not a concrete value)
 * Return false otherwise
 */
bool QueryParam_SetParam(struct QueryParseCtx *q, Param *target_param, void *target_value,
                         size_t *target_len, QueryToken *source);