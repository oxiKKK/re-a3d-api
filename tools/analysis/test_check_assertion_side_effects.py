"""Regression tests for the diagnostic argument source check."""
import unittest
from check_assertion_side_effects import invocations, suspicious


class DiagnosticAuditTests(unittest.TestCase):
    def test_required_evaluation(self):
        for expression in ('SUCCEEDED(p->Play(0, 0, 1))', '(LONG)--count >= 0',
                           '++count > 0', '(event = CreateEventA(0,0,0,0)) != 0',
                           'flags |= 1', '(*callback)()', 'callbacks[index]()'):
            with self.subTest(expression=expression):
                self.assertTrue(suspicious('ASSERT', expression))
                self.assertFalse(suspicious('VERIFY', expression))

    def test_diagnostics(self):
        self.assertTrue(suspicious('TRACE', 'value++'))
        self.assertTrue(suspicious('DBGSTR', 'ChangeState()'))
        self.assertFalse(suspicious('TRACE', 'Mp3SscErrorString(status)'))
        self.assertFalse(suspicious('ASSERT', 'SUCCEEDED(hr)'))
        self.assertFalse(suspicious('ASSERT', '(int) (size - delta) >= 0'))
        self.assertFalse(suspicious('ASSERT', 'p->GetDSBuffer() != 0'))

    def test_lexing(self):
        source = '''// ASSERT(Bogus())
/* ASSERT(AlsoBogus()) */
const char *s = R"tag(ASSERT(Bogus()))tag";
#define VERIFY(x) ASSERT(x)
ASSERT /* comment */ (SUCCEEDED(
    p->Play(0, 0, 1))); ASSERT(count >= 0);
TRACE("literal ++ = Call( )", count);
'''
        calls = list(invocations(source))
        self.assertEqual([(line, macro) for line, macro, _ in calls],
                         [(5, 'ASSERT'), (6, 'ASSERT'), (7, 'TRACE')])
        self.assertEqual(suspicious(*calls[0][1:]), ['Play'])
        self.assertFalse(suspicious(*calls[2][1:]))


if __name__ == '__main__':
    unittest.main()
