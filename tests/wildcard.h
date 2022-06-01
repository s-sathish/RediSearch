#pragma once

#include <string.h>

#include "trie/rune_util.h"

typedef enum {
  FULL_MATCH,
  PARTIAL_MATCH,
  NO_MATCH,
} match_t;

match_t WildcardMatchChar(const char *pattern, size_t p_len, const char *str, size_t str_len);
match_t WildcardMatchRune(const rune *pattern, size_t p_len, const char *str, size_t str_len);
