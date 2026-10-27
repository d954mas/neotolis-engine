import unittest

from tools.devapi.client import DevApiClient
from tools.devapi.transport import PlaywrightTransport, Transport


class FakeTransport(Transport):
    def __init__(self, reply):
        self.reply = reply

    def exchange(self, line):
        return self.reply

    def close(self):
        pass


class FakePage:
    def __init__(self):
        self.calls = []

    def evaluate(self, expression, *args):
        self.calls.append(expression)
        return ""


class PlaywrightTransportTests(unittest.TestCase):
    def test_construction_and_close_reset_the_bridge(self):
        page = FakePage()
        transport = PlaywrightTransport(page)
        self.assertEqual(["() => window.__devapi.reset()"], page.calls)  # takes over a killed client
        transport.close()
        transport.close()
        self.assertEqual(["() => window.__devapi.reset()"] * 2, page.calls)
        with self.assertRaises(ConnectionError):
            transport.exchange("{}")


class DevApiClientTests(unittest.TestCase):
    def test_reply_to_another_request_raises(self):
        client = DevApiClient(FakeTransport('{"request_id":7,"ok":true,"result":{}}'))
        with self.assertRaises(ConnectionError):
            client.request("ping")


if __name__ == "__main__":
    unittest.main()
