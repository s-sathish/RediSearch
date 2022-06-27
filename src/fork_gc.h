
#pragma once

#include "gc.h"
#include "redismodule.h"
#include "object.h"

///////////////////////////////////////////////////////////////////////////////////////////////

struct ForkGCStats {
  // total bytes collected by the GC
  size_t totalCollected;
  // number of cycle ran
  size_t numCycles;

  long long totalMSRun;
  long long lastRunTimeMs;

  uint64_t gcNumericNodesMissed;
  uint64_t gcBlocksDenied;
};

//---------------------------------------------------------------------------------------------

enum FGCType {
  FGC_TYPE_INKEYSPACE,
  FGC_TYPE_NOKEYSPACE
};

enum FGCError {
  // Terms have been collected
  FGC_COLLECTED,
  // No more terms remain
  FGC_DONE,
  // Pipe error, child probably crashed
  FGC_CHILD_ERROR,
  // Error on the parent
  FGC_PARENT_ERROR
};

//---------------------------------------------------------------------------------------------

struct MSG_IndexInfo {
  // Number of blocks prior to repair
  uint32_t nblocksOrig;
  // Number of blocks repaired
  uint32_t nblocksRepaired;
  // Number of bytes cleaned in inverted index
  uint64_t nbytesCollected;
  // Number of document records removed
  uint64_t ndocsCollected;

  // Specific information about the _last_ index block
  size_t lastblkDocsRemoved;
  size_t lastblkBytesCollected;
  size_t lastblkNumDocs;
};

// Structure sent describing an index block
struct MSG_RepairedBlock {
  IndexBlock blk;
  int64_t oldix;  // Old position of the block
  int64_t newix;  // New position of the block
  // the actual content of the block follows...
};

struct MSG_DeletedBlock {
  void *ptr;       // Address of the buffer to free
  uint32_t oldix;  // Old index of deleted block
  uint32_t _pad;   // Uninitialized reads, otherwise
};

//---------------------------------------------------------------------------------------------

KHASH_MAP_INIT_INT64(cardvals, size_t)

struct numCbCtx {
  const IndexBlock *lastblk;
  khash_t(cardvals) * delLast;
  khash_t(cardvals) * delRest;
};

typedef union {
  uint64_t u64;
  double d48;
} numUnion;

//---------------------------------------------------------------------------------------------

struct tagNumHeader {
  const char *field;
  const void *curPtr;
  uint64_t uniqueId;
  int sentFieldName;
};

//---------------------------------------------------------------------------------------------

struct InvIdxBuffers {
  MSG_DeletedBlock *delBlocks;
  size_t numDelBlocks;

  MSG_RepairedBlock *changedBlocks;

  IndexBlock *newBlocklist;
  size_t newBlocklistSize;
  int lastBlockIgnored;
};

//---------------------------------------------------------------------------------------------

struct NumGcInfo {
  // Node in the tree that was GC'd
  NumericRangeNode *node;
  CardinalityValue *lastBlockDeleted;
  CardinalityValue *restBlockDeleted;
  size_t nlastBlockDel;
  size_t nrestBlockDel;
  InvIdxBuffers idxbufs;
  MSG_IndexInfo info;
};

//---------------------------------------------------------------------------------------------

// Internal definition of the garbage collector context (each index has one)
struct ForkGC : public Object, public GCAPI {
  ForkGC(const RedisModuleString *k, uint64_t specUniqueId);
  ForkGC(IndexSpec *sp, uint64_t specUniqueId);

  void ctor(const RedisModuleString *k, uint64_t specUniqueId);

  // inverted index key name for reopening the index
  union {
    const RedisModuleString *keyName;
    IndexSpec *sp;
  };

  RedisModuleCtx *ctx;

  FGCType type;

  uint64_t specUniqueId;

  // statistics for reporting
  ForkGCStats stats;

  // flag for rdb loading. Set to 1 initially, but unce it's set to 0 we don't need to check anymore
  int rdbPossiblyLoading;
  // Whether the gc has been requested for deletion
  volatile bool deleting;
  int pipefd[2];
  volatile uint32_t pauseState;
  volatile uint32_t execState;

  struct timespec retryInterval;
  volatile size_t deletedDocsFromLastRun;

  virtual bool PeriodicCallback(RedisModuleCtx* ctx);
  virtual void RenderStats(RedisModuleCtx* ctx);
  virtual void OnDelete();
  virtual void OnTerm();
  virtual void Kill();
  virtual struct timespec GetInterval();
  virtual RedisModuleCtx *GetRedisCtx() { return ctx; }

  // Indicate that the gc should wait immediately prior to forking.
  // This is in order to perform some commands which may not be visible by the fork gc engine.
  // This function will return before the fork is performed.
  // You must call WaitAtApply or WaitClear to allow the GC to resume functioning.
  void WaitAtFork();

  // Indicate that the GC should unpause from WaitAtFork, and instead wait before the changes are applied.
  // This is in order to change the state of the index at the parent.
  void WaitAtApply();

  // Don't perform diagnostic waits
  void WaitClear();

private:
  void unlock(RedisModuleCtx *ctx);
  RedisSearchCtx *getSctx(RedisModuleCtx *ctx);
  void updateStats(RedisSearchCtx *sctx, size_t recordsRemoved, size_t bytesCollected);
  void sendFixed(const void *buff, size_t len);
  void sendVar(const void v) { sendFixed(&v, sizeof v); }
  void sendBuffer(const void *buff, size_t len);
  void sendTerminator();

  int recvFixed(void *buf, size_t len);
  int tryRecvFixed(void *obj, size_t len); //@@ Why do we need it for?
  int recvBuffer(void **buf, size_t *len);
  int tryRecvBuffer(void **buf, size_t *len); //@@ looks like nobody was using it
  int recvInvIdx(InvIdxBuffers *bufs, MSG_IndexInfo *info);

  bool childRepairInvidx(RedisSearchCtx *sctx, InvertedIndex *idx, void (*headerCallback)(ForkGC *, void *),
                         void *hdrarg, IndexRepairParams *params);
  void sendHeaderString(void *arg);
  void sendNumericTagHeader(void *arg);

  void childScanIndexes();
  void childCollectTerms(RedisSearchCtx *sctx);
  void childCollectNumeric(RedisSearchCtx *sctx);
  void childCollectTags(RedisSearchCtx *sctx);

  int parentHandleFromChild();
  FGCError parentHandleTerms(RedisModuleCtx *rctx);
  FGCError parentHandleNumeric(RedisModuleCtx *rctx);
  FGCError parentHandleTags(RedisModuleCtx *rctx);
  FGCError recvNumericTagHeader(char **fieldName, size_t *fieldNameLen, uint64_t *id);
  FGCError recvNumIdx(NumGcInfo *ninfo);

  static bool haveRedisFork();

  void sendKht(const khash_t(cardvals) *kh);
  void checkLastBlock(InvIdxBuffers *idxData, MSG_IndexInfo *info, InvertedIndex *idx);
  void applyInvertedIndex(InvIdxBuffers *idxData, MSG_IndexInfo *info, InvertedIndex *idx);
  void applyNumIdx(RedisSearchCtx *sctx, NumGcInfo *ninfo);

  int recvCardvals(CardinalityValue **tgt, size_t *len);
};

//---------------------------------------------------------------------------------------------

enum FGCPauseFlags {
  // Normal "open" state. No pausing will happen
  FGC_PAUSED_UNPAUSED = 0x00,
  // Prevent invoking the child. The child is not invoked until this flag is
  // cleared
  FGC_PAUSED_CHILD = 0x01,
  // Prevent the parent reading from the child. The results from the child are
  // not read until this flag is cleared.
  FGC_PAUSED_PARENT = 0x02
};

//---------------------------------------------------------------------------------------------

enum FGCState {
  // Idle, "normal" state
  FGC_STATE_IDLE = 0,

  // Set when the PAUSED_CHILD flag is set, indicates that we are
  // awaiting this flag to be cleared.
  FGC_STATE_WAIT_FORK,

  // Set when the child has been launched, but before the first results have
  // been applied.
  FGC_STATE_SCANNING,

  // Set when the PAUSED_PARENT flag is set. The results will not be
  // scanned until the PAUSED_PARENT flag is unset
  FGC_STATE_WAIT_APPLY,

  // Set when results are being applied from the child to the parent
  FGC_STATE_APPLYING
};

///////////////////////////////////////////////////////////////////////////////////////////////
