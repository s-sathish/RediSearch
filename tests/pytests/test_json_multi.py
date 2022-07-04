import json

from common import *
from includes import *

doc1_content = r'''{
    "name": "wonderbar",
    "category": ["mathematics and computer science", "logic", "programming", "database"],
    "books": [ 
        {
            "name": "Structure and Interpretation of Computer Programs",
            "authors": [
                "Harold Abelson", "Gerald Jay Sussman", "Julie Sussman"
            ]
        },
        {
            "name": "The Art of Computer Programming",
            "authors": [
                "Donald Knuth"
            ]
        },
        {
            "name": "Introduction to Algorithms",
            "authors": [
                "Thomas H. Cormen", "Charles E. Leiserson", "Ronald L. Rivest", "Clifford Stein"
            ]
        },
        {
            "name": "Classical Mathematical Logic: The Semantic Foundations of Logic",
            "authors": [
                "Richard L. Epstein"
            ]
        },
        {
            "name": "Design Patterns: Elements of Reusable Object-Oriented Software",
            "authors": [
                "Erich Gamma", "Richard Helm", "Ralph Johnson", "John Vlissides"
            ]
        },
        {
            "name": "Redis Microservices for Dummies",
            "authors": [
                "Redis Ltd."
            ]
        },
        {
            "name": "Redis 4.x Cookbook",
            "authors": [
                "Pengcheng Huang", "Zuofei Wang"
            ]
        }
    ]}
'''

doc2_content = r'''{
    "name": "foo",
    "category": ["database", "high performance"],
    "books": [ 
        {
            "name": "Redis for Dummies",
            "authors": [
                "Redis Ltd."
            ]
        },
        {
            "name": "Redis Microservices for Dummies",
            "authors": [
                "Redis Ltd."
            ]
        },
        {
            "name": "Systems Performance - Enterprise and the Cloud",
            "authors": [
                "Brendan Gregg"
            ]
        }
    ]}
'''

doc3_content = r'''{
    "name": "bar",
    "category": ["performance", "cloud"],
    "books": [ 
        {
            "name": "Redis for Dummies",
            "authors": [
                "Redis Ltd."
            ]
        },
        {
            "name": "Designing Data-Intensive Applications",
            "authors": [
                "Martin Kleppmann"
            ]
        },
        {
            "name": "Kubernetes: Up and Running",
            "authors": [
                "Kelsey Hightower", "Brendan Burns", "Joe Beda"
            ]
        }
    ]}
'''

doc_non_text_content = r'''{
    "attr1": ["first", "second", null, "third", null , "null", null],
    "attr2": "third",
    "attr3": [null, null],
    "attr4": [],
    "attr5": null,
    "attr6": ["first", "second", null, "third", null, 2.04 ],
    "attr7": ["first", "second", null, "third", null, false ],
    "attr8": ["first", "second", null, "third", null, {"obj": "ection"} ],
    "attr9": ["first", "second", null, "third", null, ["recursi", "on"] ],
    "attr10": ["first", "second", null, "third", null, ["recursi", 50071] ]
}
'''

def testMultiTag(env):
    """ test multiple TAG values (array of strings) """
    conn = getConnectionByEnv(env)
    env.expect('JSON.SET', 'doc:1', '$', doc1_content).ok()
    env.expect('JSON.SET', 'doc:2', '$', doc2_content).ok()
    env.expect('JSON.SET', 'doc:3', '$', doc3_content).ok()

    # Index multi flat values
    env.expect('FT.CREATE', 'idx1', 'ON', 'JSON', 'SCHEMA', '$.category[*]', 'AS', 'category', 'TAG').ok()
    # Index an array
    env.expect('FT.CREATE', 'idx2', 'ON', 'JSON', 'SCHEMA', '$.category', 'AS', 'category', 'TAG').ok()
    
    waitForIndex(env, 'idx1')
    waitForIndex(env, 'idx2')
    
    res1 = [1, 'doc:1', ['category', 'mathematics and computer science']]
    res2 = [1, 'doc:1', ['category_arr', '["mathematics and computer science","logic","programming","database"]']]
    
    # Currently return a single value (only the first value)
    env.expect('FT.SEARCH', 'idx1', '@category:{mathematics\ and\ computer\ science}', 'RETURN', '1', 'category').equal(res1)
    env.expect('FT.SEARCH', 'idx1', '@category:{logic}', 'RETURN', '1', 'category').equal(res1)
    env.expect('FT.SEARCH', 'idx1', '@category:{logic}', 'RETURN', '3', '$.category', 'AS', 'category_arr').equal(res2)

    # Not indexing array
    env.assertEqual(int(index_info(env, 'idx2')['hash_indexing_failures']), 3)


def testMultiText(env):
    """ test multiple TEXT values at root level (array of strings) """
    
    conn = getConnectionByEnv(env)
    env.expect('JSON.SET', 'doc:1', '$', json.dumps(json.loads(doc1_content)['category'])).ok()
    env.expect('JSON.SET', 'doc:2', '$', json.dumps(json.loads(doc2_content)['category'])).ok()
    env.expect('JSON.SET', 'doc:3', '$', json.dumps(json.loads(doc3_content)['category'])).ok()

    # Index multi flat values
    env.execute_command('FT.CREATE', 'idx_category_flat', 'ON', 'JSON', 'SCHEMA', '$.[*]', 'AS', 'category', 'TEXT')
    # Index an array
    env.execute_command('FT.CREATE', 'idx_category_arr', 'ON', 'JSON', 'SCHEMA', '$', 'AS', 'category', 'TEXT')
    # Index both multi flat values and an array
    env.execute_command('FT.CREATE', 'idx_category_arr_author_flat', 'ON', 'JSON', 'SCHEMA',
        '$.[*]', 'AS', 'author', 'TEXT', # testing root path, so reuse the single top-level value
        '$', 'AS', 'category', 'TEXT')
    
    waitForIndex(env, 'idx_category_flat')
    waitForIndex(env, 'idx_category_arr')
    waitForIndex(env, 'idx_category_arr_author_flat')
    
    searchMultiTextCategory(env)

def testMultiTextNested(env):
    """ test multiple TEXT values at inner level (array of strings) """

    conn = getConnectionByEnv(env)
    env.expect('JSON.SET', 'doc:1', '$', doc1_content).ok()
    env.expect('JSON.SET', 'doc:2', '$', doc2_content).ok()
    env.expect('JSON.SET', 'doc:3', '$', doc3_content).ok()

    # Index multi flat values
    env.execute_command('FT.CREATE', 'idx_category_flat', 'ON', 'JSON', 'SCHEMA', '$.category[*]', 'AS', 'category', 'TEXT')
    env.execute_command('FT.CREATE', 'idx_author_flat', 'ON', 'JSON', 'SCHEMA', '$.books[*].authors[*]', 'AS', 'author', 'TEXT')
    # Index an array
    env.execute_command('FT.CREATE', 'idx_category_arr', 'ON', 'JSON', 'SCHEMA', '$.category', 'AS', 'category', 'TEXT')
    # Index an array of arrays
    env.execute_command('FT.CREATE', 'idx_author_arr', 'ON', 'JSON', 'SCHEMA', '$.books[*].authors', 'AS', 'author', 'TEXT')
    # Index both multi flat values and an array
    env.execute_command('FT.CREATE', 'idx_category_arr_author_flat', 'ON', 'JSON', 'SCHEMA',
        '$.books[*].authors[*]', 'AS', 'author', 'TEXT',
        '$.category', 'AS', 'category', 'TEXT')
    
    waitForIndex(env, 'idx_category_flat')
    waitForIndex(env, 'idx_author_flat')
    waitForIndex(env, 'idx_category_arr')
    waitForIndex(env, 'idx_author_arr')
    waitForIndex(env, 'idx_category_arr_author_flat')

    searchMultiTextCategory(env)
    searchMultiTextAuthor(env)

    env.execute_command('FT.CREATE', 'idx_book', 'ON', 'JSON', 'SCHEMA',
        '$.category', 'AS', 'category', 'TEXT',
        '$.books[*].authors[*]', 'AS', 'author', 'TEXT',
        '$.books[*].name', 'AS', 'name', 'TEXT')
    waitForIndex(env, 'idx_book')
    res = env.execute_command('FT.SEARCH', 'idx_book',
        '(@name:(design*) -@category:(cloud)) | '
        '(@name:(Kubernetes*) @category:(cloud))',
        'NOCONTENT')
    env.assertListEqual(toSortedFlatList(res), toSortedFlatList([2, 'doc:1', 'doc:3']))


def searchMultiTextCategory(env):
    """ helper function for searching multi-value attributes """

    for idx in ['idx_category_arr', 'idx_category_arr_author_flat']:
        env.debugPrint(idx, force=True)
        env.expect('FT.SEARCH', idx, '@category:(database programming)', 'NOCONTENT', 'SLOP', '98').equal([0])
        env.expect('FT.SEARCH', idx, '@category:(database programming)', 'NOCONTENT', 'SLOP', '99').equal([1, 'doc:1'])
        env.expect('FT.SEARCH', idx, '@category:(database programming)', 'NOCONTENT', 'SLOP', '99', 'INORDER').equal([0])
        env.expect('FT.SEARCH', idx, '@category:(database programming)=>{$slop:99}', 'NOCONTENT', 'SLOP', '1').equal([1, 'doc:1'])
        env.expect('FT.SEARCH', idx, '@category:(database programming)=>{$slop:100; $inorder:true}', 'NOCONTENT').equal([0])
        env.expect('FT.SEARCH', idx, '@category:(database programming)=>{$slop:100; $inorder:false}', 'NOCONTENT').equal([1, 'doc:1'])
        
        res = env.execute_command('FT.SEARCH', idx, '@category:(database)', 'NOCONTENT')
        env.assertListEqual(toSortedFlatList(res), toSortedFlatList([2, 'doc:1', 'doc:2']), message="A " + idx)
        
        res = env.execute_command('FT.SEARCH', idx, '@category:(performance)', 'NOCONTENT')
        env.assertListEqual(toSortedFlatList(res), toSortedFlatList([2, 'doc:2', 'doc:3']), message="B " + idx)

        env.expect('FT.SEARCH', idx, '@category:(high performance)', 'NOCONTENT').equal([1, 'doc:2'])
        env.expect('FT.SEARCH', idx, '@category:(cloud)', 'NOCONTENT').equal([1, 'doc:3'])
    
    # Multi-value attributes which have no definite ordering cannot use slop or inorder
    env.expect('FT.SEARCH', 'idx_category_flat', '@category:(programming science)=>{$slop:200}').error().contains("has undefined ordering")
    env.expect('FT.SEARCH', 'idx_category_flat', '@category:(programming science)=>{$inorder:false}').error().contains("has undefined ordering")    

def searchMultiTextAuthor(env):
    """ helper function for searching multi-value attributes """

    # Not indexing array of arrays
    env.assertEqual(int(index_info(env, 'idx_author_arr')['hash_indexing_failures']), 3)

    for idx in ['idx_author_flat']:
        env.debugPrint(idx, force=True)
        env.expect('FT.SEARCH', idx, '@author:(Richard)', 'NOCONTENT').equal([1, 'doc:1'])
        
        res = env.execute_command('FT.SEARCH', idx, '@author:(Brendan)', 'NOCONTENT')
        env.assertListEqual(toSortedFlatList(res), toSortedFlatList([2, 'doc:2', 'doc:3']))

        res = env.execute_command('FT.SEARCH', idx, '@author:(Redis)', 'NOCONTENT')
        env.assertListEqual(toSortedFlatList(res), toSortedFlatList([3, 'doc:1', 'doc:2', 'doc:3']))

    # None-exact phrase using multi-value attributes which have no definite ordering cannot use slop or inorder
    env.expect('FT.SEARCH', 'idx_author_flat', '@author:(Redis Ltd.)=>{$slop:200}').error().contains("has undefined ordering")
    env.expect('FT.SEARCH', 'idx_author_flat', '@author:(Redis Ltd.)=>{$inorder:true}').error().contains("has undefined ordering")
    
    env.expect('FT.SEARCH', 'idx_author_flat', '@category|author:(Redis Ltd.)=>{$slop:200}').error().contains("has undefined ordering")
    env.expect('FT.SEARCH', 'idx_author_flat', '@category|author:(Redis Ltd.)=>{$inorder:true}').error().contains("has undefined ordering")

    env.expect('FT.SEARCH', 'idx_author_flat', '@category|author:("Redis Ltd.")=>{$inorder:true}').error().contains("has undefined ordering")
    
    env.expect('FT.SEARCH', 'idx_category_arr_author_flat', '@author:(Redis Ltd.)=>{$slop:200}').error().contains("has undefined ordering")
    env.expect('FT.SEARCH', 'idx_category_arr_author_flat', '@author:(Redis Ltd.)=>{$inorder:true}').error().contains("has undefined ordering")

    env.expect('FT.SEARCH', 'idx_category_arr_author_flat', '@category:(programming science)=>{$slop:200; $inorder:false}', 'NOCONTENT').equal([1, 'doc:1'])
    env.expect('FT.SEARCH', 'idx_category_arr_author_flat', '@category:(programming science)=>{$slop:200}', 'NOCONTENT').equal([1, 'doc:1'])
    env.expect('FT.SEARCH', 'idx_category_arr_author_flat', '@category:(programming science)=>{$inorder:false}', 'NOCONTENT').equal([1, 'doc:1'])

def testUndefinedOrderingWithSlopAndInorder(env):
    """ Test that query attributes `slop` and `inorder` cannot be used when order is not well defined """

    # Index both multi flat values and an array
    env.execute_command('FT.CREATE', 'idx_category_arr_author_flat', 'ON', 'JSON', 'SCHEMA',
        '$.books[*].authors', 'AS', 'author', 'TEXT',
        '$.category', 'AS', 'category', 'TEXT')
    waitForIndex(env, 'idx_category_arr_author_flat')
    env.expect('FT.SEARCH', 'idx_category_arr_author_flat', '@category:(does not matter)=>{$slop:200}').equal([0])
    env.expect('FT.SEARCH', 'idx_category_arr_author_flat', '@category:(does not matter)=>{$inorder:false}').equal([0])
    env.expect('FT.SEARCH', 'idx_category_arr_author_flat', '@category:(does not matter)=>{$inorder:true}').equal([0])
    env.expect('FT.SEARCH', 'idx_category_arr_author_flat', '@author:(does not matter)=>{$slop:200}').error().contains("has undefined ordering")
    env.expect('FT.SEARCH', 'idx_category_arr_author_flat', '@author:(does not matter)=>{$inorder:false}').error().contains("has undefined ordering")
    env.expect('FT.SEARCH', 'idx_category_arr_author_flat', '@author:(does not matter)=>{$inorder:true}').error().contains("has undefined ordering")

    env.expect('FT.SEARCH', 'idx_category_arr_author_flat', '@category:(does not matter)', 'SLOP', '200').equal([0])
    env.expect('FT.SEARCH', 'idx_category_arr_author_flat', '@category:(does not matter)', 'INORDER').equal([0])
    env.expect('FT.SEARCH', 'idx_category_arr_author_flat', '@author:(does not matter)', 'SLOP', '200').error().contains("has undefined ordering")
    env.expect('FT.SEARCH', 'idx_category_arr_author_flat', '@author:(does not matter)', 'INORDER').error().contains("has undefined ordering")

    env.expect('FT.SEARCH', 'idx_category_arr_author_flat', '(does not matter)', 'SLOP', '200').error().contains("has undefined ordering")
    env.expect('FT.SEARCH', 'idx_category_arr_author_flat', '(does not matter)', 'INORDER').error().contains("has undefined ordering")

    env.expect('FT.SEARCH', 'idx_category_arr_author_flat', 'does not matter', 'SLOP', '200').error().contains("has undefined ordering")
    env.expect('FT.SEARCH', 'idx_category_arr_author_flat', 'does not matter', 'INORDER').error().contains("has undefined ordering")
    
    env.expect('FT.SEARCH', 'idx_category_arr_author_flat', '(does not matter)=>{$inorder:false}', 'SLOP', '200').error().contains("has undefined ordering")
    env.expect('FT.SEARCH', 'idx_category_arr_author_flat', '(does not matter)=>{$slop:200}', 'INORDER').error().contains("has undefined ordering")

    env.expect('FT.SEARCH', 'idx_category_arr_author_flat', '(does not matter)=>{$inorder:false}').error().contains("has undefined ordering")
    env.expect('FT.SEARCH', 'idx_category_arr_author_flat', '(does not matter)=>{$slop:200}').error().contains("has undefined ordering")

    
def testMultiNonText(env):
    """
    test multiple TEXT values which include some non-text values at root level (null, number, bool, array, object)
    Skip nulls without failing
    Fail on number, bool, object, arr of strings, arr with mixed types
    """
    conn = getConnectionByEnv(env)
    
    non_text_dict = json.loads(doc_non_text_content)
    
    # Create indices and a key per index, e.g.,
    #   FT.CREATE idx1 ON JSON PREFIX 1 doc:1: SCHEMA $ AS root TEXT
    #   JSON.SET doc:1: $ '["first", "second", null, "third", null, "null", null]'
    #
    # First 5 indices are OK (nulls are skipped)
    for (i,v) in enumerate(non_text_dict.values()):
        doc = 'doc:{}:'.format(i+1)
        idx = 'idx{}'.format(i+1)
        env.execute_command('FT.CREATE', idx, 'ON', 'JSON', 'PREFIX', '1', doc, 'SCHEMA', '$', 'AS', 'root', 'TEXT')
        waitForIndex(env, idx)
        env.expect('JSON.SET', doc, '$', json.dumps(v)).ok()
        res_failures = 0 if i+1 <= 5 else 1
        env.assertEqual(int(index_info(env, idx)['hash_indexing_failures']), res_failures, message=str(i))
    
    # Search good indices with content
    env.expect('FT.SEARCH', 'idx1', '@root:(third)', 'NOCONTENT').equal([1, 'doc:1:'])
    env.expect('FT.SEARCH', 'idx2', '@root:(third)', 'NOCONTENT').equal([1, 'doc:2:'])

def testMultiNonTextNested(env):
    """
    test multiple TEXT values which include some non-text values at inner level (null, number, bool, array, object)
    Skip nulls without failing
    Fail on number, bool, object, arr of strings, arr with mixed types
    """

    conn = getConnectionByEnv(env)

    non_text_dict = json.loads(doc_non_text_content)
    
    # Create indices, e.g.,
    #   FT.CREATE idx1 ON JSON SCHEMA $.attr1 AS attr TEXT
    for (i,v) in enumerate(non_text_dict.values()):
        env.execute_command('FT.CREATE', 'idx{}'.format(i+1), 'ON', 'JSON', 'SCHEMA', '$.attr{}'.format(i+1), 'AS', 'attr', 'TEXT')
    env.expect('JSON.SET', 'doc:1', '$', doc_non_text_content).ok()
    
    # First 5 indices are OK (nulls are skipped)
    for (i,v) in enumerate(non_text_dict.values()):
        res_failures = 0 if i+1 <= 5 else 1
        env.assertEqual(int(index_info(env, 'idx{}'.format(i+1))['hash_indexing_failures']), res_failures)
    
    # Search good indices with content
    env.expect('FT.SEARCH', 'idx1', '@attr:(third)', 'NOCONTENT').equal([1, 'doc:1'])
    env.expect('FT.SEARCH', 'idx2', '@attr:(third)', 'NOCONTENT').equal([1, 'doc:1'])

def trim_in_list(val, lst):
    for i, v in enumerate(lst):
        if type(v) == str:
            lst[i] = v.replace(val, '')
    return lst

def testMultiSortRoot(env):
    """
    test sorting by multiple TEXT at root level
    Should sort by first value
    """
    conn = getConnectionByEnv(env)

    (gag_arr, text_cmd_args, tag_cmd_args) = sortMultiPrepare()
    
    env.execute_command('FT.CREATE', 'idx1_multi_text', 'ON', 'JSON', 'STOPWORDS', '0', 'PREFIX', '1', 'multi:', 'SCHEMA', '$', 'AS', 'gag', 'TEXT')
    env.execute_command('FT.CREATE', 'idx2_multi_tag', 'ON', 'JSON', 'STOPWORDS', '0', 'PREFIX', '1', 'multi:', 'SCHEMA', '$.[*]', 'AS', 'gag', 'TAG')
    env.execute_command('FT.CREATE', 'idx3_multi_text_sort', 'ON', 'JSON', 'STOPWORDS', '0', 'PREFIX', '1', 'multi:', 'SCHEMA', '$', 'AS', 'gag', 'TEXT', 'SORTABLE')
    
    env.execute_command('FT.CREATE', 'idx1_single_text', 'ON', 'JSON', 'STOPWORDS', '0', 'PREFIX', '1', 'single:', 'SCHEMA', '$', 'AS', 'gag', 'TEXT')
    env.execute_command('FT.CREATE', 'idx2_single_tag', 'ON', 'JSON', 'STOPWORDS', '0', 'PREFIX', '1', 'single:', 'SCHEMA', '$', 'AS', 'gag', 'TAG')
    env.execute_command('FT.CREATE', 'idx3_single_test_sort', 'ON', 'JSON', 'STOPWORDS', '0', 'PREFIX', '1', 'single:', 'SCHEMA', '$', 'AS', 'gag', 'TEXT', 'SORTABLE')
    
    # docs with array of strings
    for i, gag in enumerate(gag_arr):
        env.expect('JSON.SET', 'multi:doc:{}'.format(i+1), '$', json.dumps(gag)).ok()
    
    # docs with a single string
    for i, gag in enumerate(gag_arr):
        env.expect('JSON.SET', 'single:doc:{}'.format(i+1), '$', json.dumps(gag[0])).ok()

    sortMulti(env, text_cmd_args, tag_cmd_args)

def testMultiSortNested(env):
    """
    Test sorting by multiple TEXT at inner level
    Should sort by first value
    """

    conn = getConnectionByEnv(env)

    (gag_arr, text_cmd_args, tag_cmd_args) = sortMultiPrepare()
    
    env.execute_command('FT.CREATE', 'idx1_multi_text', 'ON', 'JSON', 'STOPWORDS', '0', 'PREFIX', '1', 'multi:', 'SCHEMA', '$.chalkboard', 'AS', 'gag', 'TEXT')
    env.execute_command('FT.CREATE', 'idx2_multi_tag', 'ON', 'JSON', 'STOPWORDS', '0', 'PREFIX', '1', 'multi:', 'SCHEMA', '$.chalkboard[*]', 'AS', 'gag', 'TAG')
    env.execute_command('FT.CREATE', 'idx3_multi_text_sort', 'ON', 'JSON', 'STOPWORDS', '0', 'PREFIX', '1', 'multi:', 'SCHEMA', '$.chalkboard', 'AS', 'gag', 'TEXT', 'SORTABLE')
    
    env.execute_command('FT.CREATE', 'idx1_single_text', 'ON', 'JSON', 'STOPWORDS', '0', 'PREFIX', '1', 'single:', 'SCHEMA', '$.chalkboard', 'AS', 'gag', 'TEXT')
    env.execute_command('FT.CREATE', 'idx2_single_tag', 'ON', 'JSON', 'STOPWORDS', '0', 'PREFIX', '1', 'single:', 'SCHEMA', '$.chalkboard', 'AS', 'gag', 'TAG')
    env.execute_command('FT.CREATE', 'idx3_single_test_sort', 'ON', 'JSON', 'STOPWORDS', '0', 'PREFIX', '1', 'single:', 'SCHEMA', '$.chalkboard', 'AS', 'gag', 'TEXT', 'SORTABLE')
    
    # docs with array of strings
    for i, gag in enumerate(gag_arr):
        env.expect('JSON.SET', 'multi:doc:{}'.format(i+1), '$', json.dumps({ "chalkboard": gag})).ok()
    
    # docs with a single string
    for i, gag in enumerate(gag_arr):
        env.expect('JSON.SET', 'single:doc:{}'.format(i+1), '$', json.dumps({ "chalkboard": gag[0]})).ok()

    sortMulti(env, text_cmd_args, tag_cmd_args)

def sortMultiPrepare():
    """ helper function for sorting multi-value attributes """

    gag_arr = [
        ["mistral"],
        ["fireplace"],
        ["mismatch"],
        ["firefly"],
        ["ahoy"],
        ["class"],
        ["firecracker"],
        ["cluster"],
        ["firewall"],
        ["mischief"],        
        ["classroom"],
        ["mistake"],
        ["classify"]
    ]

    text_cmd_args = [
        ['@gag:(fire*)', 'NOCONTENT', 'SORTBY', 'gag', 'ASC'],
        ['@gag:(class*)', 'NOCONTENT', 'SORTBY', 'gag', 'ASC'],
        ['@gag:(fire*)', 'NOCONTENT', 'SORTBY', 'gag', 'DESC'],
        ['@gag:(class*)', 'NOCONTENT', 'SORTBY', 'gag', 'DESC'],
        ['*', 'NOCONTENT', 'SORTBY', 'gag', 'ASC']
    ]
    tag_cmd_args = [
        ['@gag:{fire*}', 'NOCONTENT', 'SORTBY', 'gag', 'ASC'],
        ['@gag:{class*}', 'NOCONTENT', 'SORTBY', 'gag', 'ASC'],
        ['@gag:{fire*}', 'NOCONTENT', 'SORTBY', 'gag', 'DESC'],
        ['@gag:{class*}', 'NOCONTENT', 'SORTBY', 'gag', 'DESC'],
        ['*', 'NOCONTENT', 'SORTBY', 'gag', 'ASC']
    ]

    return (gag_arr, text_cmd_args, tag_cmd_args)

def sortMulti(env, text_cmd_args, tag_cmd_args):
    """ helper function for sorting multi-value attributes """    
    
    for i, (text_arg,tag_arg) in enumerate(zip(text_cmd_args, tag_cmd_args)):
        env.assertEqual(trim_in_list('multi:', env.execute_command('FT.SEARCH', 'idx1_multi_text', *text_arg)), trim_in_list('single:', env.execute_command('FT.SEARCH', 'idx1_single_text', *text_arg)), message = '{} arg {}'.format(1, i))
        env.assertEqual(trim_in_list('multi:', env.execute_command('FT.SEARCH', 'idx2_multi_tag', *tag_arg)), trim_in_list('single:', env.execute_command('FT.SEARCH', 'idx2_single_tag', *tag_arg)), message = '{} arg {}'.format(2, i))        
        env.assertEqual(env.execute_command('FT.SEARCH', 'idx1_multi_text', *text_arg), env.execute_command('FT.SEARCH', 'idx2_multi_tag', *tag_arg), message = '{} arg {}'.format(3, i))

    for i, text_arg in enumerate(text_cmd_args):
        text_arg.append('WITHSCORES')
        env.assertEqual(trim_in_list('multi:', env.execute_command('FT.SEARCH', 'idx1_multi_text', *text_arg)), trim_in_list('single:', env.execute_command('FT.SEARCH', 'idx1_single_text', *text_arg)), message = '{} arg {}'.format(1, i))



def testMultiEmptyBlankOrNone(env):
    # test empty array or arrays comprised of empty strings or None
    conn = getConnectionByEnv(env)
    
    values = [
        ["", "", ""],
        [""],
        [],
        [None],
        [None, None],
        ["", None, ""]
    ]

    env.execute_command('FT.CREATE', 'idx', 'ON', 'JSON', 'SCHEMA', '$.val', 'AS', 'val', 'TEXT')
    
    for i, val in enumerate(values):
        env.expect('JSON.SET', 'doc:{}'.format(i+1), '$', json.dumps({ "val": val})).ok()
    env.expect('JSON.SET', 'doc', '$', json.dumps({"val": ["haha"]})).ok()
    env.expect('FT.SEARCH', 'idx', '@val:(haha)', 'NOCONTENT', 'SORTBY', 'val', 'ASC').equal([1, 'doc'])

def testconfigMultiTextOffsetDelta(env):
    """ test ft.config `MULTI_TEXT_OFFSET_DELTA` """
    
    conn = getConnectionByEnv(env)
    env.expect('JSON.SET', 'doc:1', '$', doc1_content).ok()
    env.execute_command('FT.CREATE', 'idx_category_arr', 'ON', 'JSON', 'SCHEMA', '$.category', 'AS', 'category', 'TEXT')
    waitForIndex(env, 'idx_category_arr')

    # MULTI_TEXT_OFFSET_DELTA = 100 (Default)
    #
    # Offsets:
    # ["mathematics and computer science", "logic", "programming", "database"]
    #   1                2        3      ,  103   ,  203         ,  303
    
    res = env.execute_command('FT.CONFIG', 'GET', 'MULTI_TEXT_OFFSET_DELTA')
    env.assertEqual(res[0][1], '100')
    env.expect('FT.SEARCH', 'idx_category_arr', '@category:(mathematics database)', 'NOCONTENT').equal([1, 'doc:1'])
    env.expect('FT.SEARCH', 'idx_category_arr', '@category:(mathematics database)', 'NOCONTENT', 'SLOP', '300').equal([0])
    env.expect('FT.SEARCH', 'idx_category_arr', '@category:(mathematics database)', 'NOCONTENT', 'SLOP', '301').equal([1, 'doc:1'])
    
    # MULTI_TEXT_OFFSET_DELTA = 101
    env.expect('FT.CONFIG', 'SET', 'MULTI_TEXT_OFFSET_DELTA', '101').ok()
    # Offsets:
    # ["mathematics and computer science", "logic", "programming", "database"]
    #   1                2        3      ,  104   ,  205         ,  306
    env.execute_command('FT.CREATE', 'idx_category_arr_2', 'ON', 'JSON', 'SCHEMA', '$.category', 'AS', 'category', 'TEXT')
    waitForIndex(env, 'idx_category_arr_2')
    env.expect('FT.SEARCH', 'idx_category_arr_2', '@category:(mathematics database)', 'NOCONTENT', 'SLOP', '303').equal([0])
    env.expect('FT.SEARCH', 'idx_category_arr_2', '@category:(mathematics database)', 'NOCONTENT', 'SLOP', '304').equal([1, 'doc:1'])
    
    env.expect('FT.SEARCH', 'idx_category_arr_2', '@category:(science database)', 'NOCONTENT', 'SLOP', '301').equal([0])
    env.expect('FT.SEARCH', 'idx_category_arr_2', '@category:(science database)', 'NOCONTENT', 'SLOP', '302').equal([1, 'doc:1'])

    # MULTI_TEXT_OFFSET_DELTA = 0
    env.expect('FT.CONFIG', 'SET', 'MULTI_TEXT_OFFSET_DELTA', '0').ok()
    # Offsets:
    # ["mathematics and computer science", "logic", "programming", "database"]
    #   1                2        3      ,  4   ,    5         ,    6
    env.execute_command('FT.CREATE', 'idx_category_arr_3', 'ON', 'JSON', 'SCHEMA', '$.category', 'AS', 'category', 'TEXT')
    waitForIndex(env, 'idx_category_arr_3')
    env.expect('FT.SEARCH', 'idx_category_arr_3', '@category:(mathematics database)', 'NOCONTENT', 'SLOP', '3').equal([0])
    env.expect('FT.SEARCH', 'idx_category_arr_3', '@category:(mathematics database)', 'NOCONTENT', 'SLOP', '4').equal([1, 'doc:1'])

    env.expect('FT.SEARCH', 'idx_category_arr_3', '@category:(science database)', 'NOCONTENT', 'SLOP', '1').equal([0])
    env.expect('FT.SEARCH', 'idx_category_arr_3', '@category:(science database)', 'NOCONTENT', 'SLOP', '2').equal([1, 'doc:1'])

    # MULTI_TEXT_OFFSET_DELTA = -1
    env.expect('FT.CONFIG', 'SET', 'MULTI_TEXT_OFFSET_DELTA', '-1').error()


def testMultiNoHighlight(env):
    """ highlight is not supported with multiple TEXT"""
    pass
