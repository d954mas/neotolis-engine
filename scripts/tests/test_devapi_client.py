import json
import unittest

from tools.devapi.transport import PlaywrightTransport


class FakePage:
    def __init__(self, polls=()):
        self.polls = list(polls)
        self.calls = []

    def evaluate(self, expression, *args):
        self.calls.append((expression, args))
        if "poll()" in expression:
            return self.polls.pop(0) if self.polls else ""
        return ""


class PlaywrightTransportTests(unittest.TestCase):
    def test_construction_and_close_reset_bridge(self):
        page = FakePage()
        transport = PlaywrightTransport(page)
        self.assertEqual([("() => window.__devapi.reset()", ())], page.calls)  # takes over a killed client

        transport.close()
        transport.close()

        self.assertEqual([("() => window.__devapi.reset()", ())] * 2, page.calls)
        with self.assertRaises(ConnectionError):
            transport.send("{}")

    def test_manual_mode_submits_nothing_of_its_own(self):
        line = '{"request_id":2,"ok":true,"result":{}}'
        page = FakePage(polls=["", line])
        transport = PlaywrightTransport(page)

        transport.send(json.dumps({"method": "time.set_mode", "request_id": 1, "params": {"mode": "manual"}}))
        transport.send(json.dumps({"method": "capture.frame", "request_id": 2}))

        self.assertEqual(line, transport.recv_line())
        submitted = [args[0] for expression, args in page.calls if "submit" in expression]
        self.assertEqual(["time.set_mode", "capture.frame"], [json.loads(s)["method"] for s in submitted])


if __name__ == "__main__":
    unittest.main()
