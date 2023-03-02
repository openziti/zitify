import os
import unittest

zitify_lib = os.getenv('ZITIFY_LIB')
print(zitify_lib)

@unittest.skipIf(os.getenv('ZITI_TEST_IDENTITY') is None, 'ZITI_TEST_IDENTITY is required')
@unittest.skipIf(zitify_lib is None, 'libzitify.so is required')
class MyTestCase(unittest.TestCase):

    def test_curl(self):
        import subprocess
        import json
        result = subprocess.run(
            args=['curl', 'http://httpbin.ziti/json'],
            env={
                'LD_PRELOAD': zitify_lib,
                'ZITI_IDENTITIES': os.getenv('ZITI_TEST_IDENTITY'),
                'ZITI_LOG': '2'
            },
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )

        self.assertEqual(result.returncode, 0, f'should succeed: ${result.stderr.decode()}')
        j = json.loads(result.stdout.decode())
        print(j)
        print(result.stderr.decode())


if __name__ == '__main__':
    unittest.main()
