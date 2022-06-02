from common import *
from includes import *
from RLTest import Env


def testSanityText(env):
    env.skipOnCluster()
    env.expect('ft.config', 'set', 'DEFAULT_DIALECT', 2).ok()
    env.expect('ft.config', 'set', 'MINPREFIX', 1).ok()
    env.expect('ft.config', 'set', 'TIMEOUT', 100000).ok()
    env.expect('ft.config', 'set', 'MAXEXPANSIONS', 10000000).equal('OK')
    item_qty = 100

    index_list = ['idx_bf', 'idx_suffix']
    env.cmd('ft.create', 'idx_bf', 'SCHEMA', 't', 'TEXT')
    env.cmd('ft.create', 'idx_suffix', 'SCHEMA', 't', 'TEXT', 'WITHSUFFIXTRIE')

    conn = getConnectionByEnv(env)

    start = time.time()
    pl = conn.pipeline()
    for i in range(item_qty):
        pl.execute_command('HSET', 'doc%d' % i, 't', 'foo%d' % i)
        pl.execute_command('HSET', 'doc%d' % (i + item_qty), 't', 'fooo%d' % i)
        pl.execute_command('HSET', 'doc%d' % (i + item_qty * 2), 't', 'foooo%d' % i)
        pl.execute_command('HSET', 'doc%d' % (i + item_qty * 3), 't', 'foofo%d' % i)
        pl.execute()

    for i in range(2):
        env.expect('ft.search', index_list[i], "'*foo*'=>{$type:wildcard}", 'LIMIT', 0 , 0).equal([40000])

def testSanityTag(env):
    env.skipOnCluster()
    env.expect('ft.config', 'set', 'DEFAULT_DIALECT', 2).ok()
    env.expect('ft.config', 'set', 'MINPREFIX', 1).ok()
    env.expect('ft.config', 'set', 'TIMEOUT', 100000).ok()
    env.expect('ft.config', 'set', 'MAXEXPANSIONS', 10000000).equal('OK')
    item_qty = 100

    index_list = ['idx_bf', 'idx_suffix']
    env.cmd('ft.create', 'idx_bf', 'SCHEMA', 't', 'TAG')
    env.cmd('ft.create', 'idx_suffix', 'SCHEMA', 't', 'TAG', 'WITHSUFFIXTRIE')

    conn = getConnectionByEnv(env)

    start = time.time()
    pl = conn.pipeline()
    for i in range(item_qty):
        pl.execute_command('HSET', 'doc%d' % i, 't', 'foo%d' % i)
        pl.execute_command('HSET', 'doc%d' % (i + item_qty), 't', 'fooo%d' % i)
        pl.execute_command('HSET', 'doc%d' % (i + item_qty * 2), 't', 'foooo%d' % i)
        pl.execute_command('HSET', 'doc%d' % (i + item_qty * 3), 't', 'foofo%d' % i)
        pl.execute()

    for i in range(2):
        env.expect('ft.search', index_list[i], "@t:{'*22*'}=>{$type:wildcard}", 'LIMIT', 0 , 0).equal([400])
