import time
import unittest

from RLTest import Env
from includes import *
from common import *

def testExpireIndex(env):
    if env.isCluster():
        raise unittest.SkipTest()
    env.cmd('ft.create', 'idx', 'TEMPORARY', '4', 'ON', 'HASH', 'SCHEMA', 'test', 'TEXT', 'SORTABLE')
    ttl = env.cmd('ft.debug', 'TTL', 'idx')
    env.assertTrue(ttl > 2)

    while ttl > 2:
        ttl = env.cmd('ft.debug', 'TTL', 'idx')
        time.sleep(1)
    env.cmd('ft.add', 'idx', 'doc1', '1.0', 'FIELDS', 'test', 'this is a simple test')
    ttl = env.cmd('ft.debug', 'TTL', 'idx')
    env.assertTrue(ttl > 2)

    while ttl > 2:
        ttl = env.cmd('ft.debug', 'TTL', 'idx')
        time.sleep(1)
    env.cmd('ft.search', 'idx', 'simple')
    ttl = env.cmd('ft.debug', 'TTL', 'idx')
    env.assertTrue(ttl > 2)

    while ttl > 2:
        ttl = env.cmd('ft.debug', 'TTL', 'idx')
        time.sleep(1)
    env.cmd('ft.aggregate', 'idx', 'simple', 'LOAD', '1', '@test')
    ttl = env.cmd('ft.debug', 'TTL', 'idx')
    env.assertTrue(ttl > 2)

    try:
        while True:
            ttl = env.cmd('ft.debug', 'TTL', 'idx')
            time.sleep(1)
    except Exception as e:
        env.assertEqual(str(e), 'Unknown index name')

def testExpireHashes():
    env = Env(moduleArgs='GC_POLICY FORK FORK_GC_CLEAN_THRESHOLD 0 FORK_GC_RUN_INTERVAL 5')
    conn = getConnectionByEnv(env)
    env.assertOk(conn.execute_command('ft.create', 'idx', 'schema', 't', 'text'))

    repeats = 10000
    docs = 100

    conn.execute_command('hset', 'stay', 't', 'hello')

    for i in range(repeats):

        for j in range(docs):
            docid = i * repeats + j
            conn.execute_command('hset', docid, 't', 'hello')
            conn.execute_command('EXPIRE', docid, 2)
        
        start_time = time.time()
        for _ in range(100):
            res = conn.execute_command('FT.SEARCH', 'idx', 'hello')
        print (time.time() - start_time)
        print (res)
        time.sleep(1)

    pass