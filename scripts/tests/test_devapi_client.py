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
    def test_close_resets_bridge_and_rejects_further_use(self):
        page = FakePage()
        transport = PlaywrightTransport(page)

        transport.close()
        transport.close()

        self.assertEqual([("() => window.__devapi.reset()", ())], page.calls)
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

    def test_wait_is_bounded_by_read_timeout(self):
        page = FakePage()
        transport = PlaywrightTransport(page, read_timeout=0.05)

        with self.assertRaises(TimeoutError):
            transport.recv_line()

        waits = [args[0] for expression, args in page.calls if "poll()" in expression]
        self.assertTrue(waits)
        self.assertTrue(all(1 <= ms <= 50 for ms in waits))
        # The timeout resets the bridge, so the engine's pending reply cannot leave the session busy.
        self.assertEqual("() => window.__devapi.reset()", page.calls[-1][0])
        with self.assertRaises(ConnectionError):
            transport.send("{}")


if __name__ == "__main__":
    unittest.main()
