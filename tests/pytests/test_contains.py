from includes import *
from common import *
import os



def testBasicContains(env):
    env.skip()
    r = env
    env.assertOk(r.execute_command(
        'ft.create', 'idx', 'schema', 'title', 'text', 'body', 'text'))
    env.expect('HSET', 'doc1', 'title', 'hello world', 'body', 'this is a test') \
                .equal(2L)

    # prefix
    res = r.execute_command('ft.search', 'idx', 'worl*')
    env.assertEqual(res[0:2], [1L, 'doc1'])
    env.assertEqual(set(res[2]), set(['title', 'hello world', 'body', 'this is a test']))

    # suffix
    res = r.execute_command('ft.search', 'idx', '*orld')
    env.assertEqual(res[0:2], [1L, 'doc1'])
    env.assertEqual(set(res[2]), set(['title', 'hello world', 'body', 'this is a test']))

    # contains
    res = r.execute_command('ft.search', 'idx', '*orl*')
    env.assertEqual(res[0:2], [1L, 'doc1'])
    env.assertEqual(set(res[2]), set(['title', 'hello world', 'body', 'this is a test']))

def testSanity(env):
    item_qty = 10000000
    query_qty = 100

    conn = getConnectionByEnv(env)
    env.cmd('ft.create', 'idx', 'SCHEMA', 't', 'TEXT')

    for i in range(item_qty):
        conn.execute_command('HSET', 'doc%d' % i, 't', 'foo%d' % i)

    #env.expect('ft.search', 'idx', '*').equal(item_qty)

    for _ in range(100):
        start = time.time()
        for i in range(query_qty):
            # res = env.execute_command('ft.search', 'idx', '555*', 'LIMIT', 0 , 1000)
            # env.assertEqual(res, 1000)

            #res = env.execute_command('ft.profile', 'idx', 'search', 'limited', 'query', '555*', 'LIMIT', 0 , 0)
            #res = env.execute_command('ft.profile', 'idx', 'search', 'limited', 'query', '23*', 'LIMIT', 0 , 0)
            #env.assertEqual(res, 1000)
            #res = env.execute_command('ft.profile', 'idx', 'search', 'limited', 'query', 'foo55*', 'LIMIT', 0 , 0)
            #env.assertEqual(res, 1000)
            #res = env.execute_command('ft.profile', 'idx', 'search', 'limited', 'query', 'foo555*', 'LIMIT', 0 , 0)
            #env.assertEqual(res, 1000)
            #res = env.execute_command('ft.profile', 'idx', 'search', 'limited', 'query', 'oo555*', 'LIMIT', 0 , 0)
            #env.assertEqual(res, 1000)
            #res = env.execute_command('ft.profile', 'idx', 'search', 'limited', 'query', 'o555*', 'LIMIT', 0 , 0)
            #env.assertEqual(res, 1000)


            res = env.execute_command('ft.search', 'idx', '555*', 'LIMIT', 0 , 0)
            #res = env.execute_command('ft.search', 'idx', '23*', 'LIMIT', 0 , 0)
            #env.assertEqual(res, 1000)
            res = env.execute_command('ft.search', 'idx', 'foo55*', 'LIMIT', 0 , 0)
            #env.assertEqual(res, 1000)
            res = env.execute_command('ft.search', 'idx', 'foo555*', 'LIMIT', 0 , 0)
            #env.assertEqual(res, 1000)
            res = env.execute_command('ft.search', 'idx', 'oo555*', 'LIMIT', 0 , 0)
            #env.assertEqual(res, 1000)
            res = env.execute_command('ft.search', 'idx', 'o555*', 'LIMIT', 0 , 0)

        print (time.time() - start)
        raw_input('pause')