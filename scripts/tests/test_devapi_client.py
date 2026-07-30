import json
import unittest

from tools.devapi.client import DevApiClient, DevApiResultError
from tools.devapi.transport import PlaywrightTransport


class FakeTransport:
    def __init__(self, responses):
        self.responses = list(responses)
        self.sent = []
        self.closed = False

    def send(self, line):
        self.sent.append(json.loads(line))

    def recv_line(self):
        if not self.responses:
            raise TimeoutError
        return json.dumps(self.responses.pop(0))

    def close(self):
        self.closed = True


def error_response(request_id, method):
    return {
        "request_id": request_id,
        "ok": False,
        "error": {"code": "bad_params", "message": f"{method} rejected"},
    }


class CaptureAndStepTests(unittest.TestCase):
    def test_step_failure_closes_transport_and_clears_pending(self):
        transport = FakeTransport(
            [
                {"request_id": 1, "ok": True, "result": {"enabled": True}},
                error_response(2, "capture.frame"),
                error_response(3, "time.step"),
            ]
        )
        client = DevApiClient(transport)

        with self.assertRaises(DevApiResultError):
            client.capture_frame_and_step()

        self.assertEqual({}, client._pending)
        self.assertTrue(transport.closed)

    def test_step_failure_does_not_add_recovery_state(self):
        transport = FakeTransport(
            [
                {"request_id": 1, "ok": True, "result": {"enabled": True}},
                error_response(3, "time.step"),
            ]
        )
        client = DevApiClient(transport)

        with self.assertRaises(DevApiResultError):
            client.capture_frame_and_step()

        self.assertEqual({}, client._pending)
        self.assertTrue(transport.closed)
        self.assertFalse(hasattr(client, "_abandoned"))

    def test_capture_failure_closes_transport(self):
        transport = FakeTransport(
            [
                {"request_id": 1, "ok": True, "result": {"enabled": True}},
                {"request_id": 3, "ok": True, "result": {"stepped": 1}},
                error_response(2, "capture.frame"),
            ]
        )
        client = DevApiClient(transport)

        with self.assertRaises(DevApiResultError):
            client.capture_frame_and_step()

        self.assertEqual({}, client._pending)
        self.assertTrue(transport.closed)

    def test_invalid_scale_rejects_before_sending(self):
        transport = FakeTransport([])
        client = DevApiClient(transport)

        with self.assertRaises(ValueError):
            client.capture_frame_and_step(scale=3)

        self.assertEqual([], transport.sent)

    def test_render_disabled_rejects_before_capture_and_step(self):
        transport = FakeTransport([{"request_id": 1, "ok": True, "result": {"enabled": False}}])
        client = DevApiClient(transport)

        with self.assertRaises(DevApiResultError):
            client.capture_frame_and_step()

        self.assertEqual(["render.info"], [request["method"] for request in transport.sent])

    def test_pair_always_steps_exactly_one_frame(self):
        transport = FakeTransport(
            [
                {"request_id": 1, "ok": True, "result": {"enabled": True}},
                {"request_id": 2, "ok": True, "result": {"data": "png"}},
                {"request_id": 3, "ok": True, "result": {"stepped": 1}},
            ]
        )
        client = DevApiClient(transport)

        client.capture_frame_and_step(scale=2)

        self.assertEqual({"count": 1}, transport.sent[2]["params"])


class FakePage:
    def __init__(self):
        self.expressions = []

    def evaluate(self, expression, *args):
        self.expressions.append(expression)
        return ""


class PlaywrightTransportTests(unittest.TestCase):
    def test_close_resets_bridge_and_rejects_further_use(self):
        page = FakePage()
        transport = PlaywrightTransport(page)

        transport.close()
        transport.close()

        self.assertEqual(["() => window.__devapi.reset()"], page.expressions)
        with self.assertRaises(ConnectionError):
            transport.send("{}")


if __name__ == "__main__":
    unittest.main()
