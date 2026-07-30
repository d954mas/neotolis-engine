import json
import unittest

from tools.devapi.client import DevApiClient, DevApiResultError


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
                error_response(1, "capture.frame"),
                error_response(2, "time.step"),
            ]
        )
        client = DevApiClient(transport)

        with self.assertRaises(DevApiResultError):
            client.capture_frame_and_step(scale=3)

        self.assertEqual({}, client._pending)
        self.assertTrue(transport.closed)

    def test_step_failure_does_not_add_recovery_state(self):
        transport = FakeTransport([error_response(2, "time.step")])
        client = DevApiClient(transport)

        with self.assertRaises(DevApiResultError):
            client.capture_frame_and_step()

        self.assertEqual({}, client._pending)
        self.assertTrue(transport.closed)
        self.assertFalse(hasattr(client, "_abandoned"))

    def test_capture_failure_closes_transport(self):
        transport = FakeTransport(
            [
                {"request_id": 2, "ok": True, "result": {"stepped": 1}},
                error_response(1, "capture.frame"),
            ]
        )
        client = DevApiClient(transport)

        with self.assertRaises(DevApiResultError):
            client.capture_frame_and_step(scale=3)

        self.assertEqual({}, client._pending)
        self.assertTrue(transport.closed)

    def test_step_override_rejects_before_sending(self):
        transport = FakeTransport([])
        client = DevApiClient(transport, step_max=2)

        with self.assertRaises(ValueError):
            client.capture_frame_and_step(count=3)

        self.assertEqual([], transport.sent)


if __name__ == "__main__":
    unittest.main()
