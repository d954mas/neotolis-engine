import json
import unittest

from tools.devapi.client import DevApiClient, DevApiResultError


class FakeTransport:
    def __init__(self, responses):
        self.responses = list(responses)
        self.sent = []

    def send(self, line):
        self.sent.append(json.loads(line))

    def recv_line(self):
        if not self.responses:
            raise TimeoutError
        return json.dumps(self.responses.pop(0))

    def close(self):
        pass


def error_response(request_id, method):
    return {
        "request_id": request_id,
        "ok": False,
        "error": {"code": "bad_params", "message": f"{method} rejected"},
    }


class CaptureAndStepTests(unittest.TestCase):
    def test_immediate_capture_reply_is_removed_when_step_fails(self):
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

    def test_late_capture_reply_is_discarded_after_step_fails(self):
        transport = FakeTransport([error_response(2, "time.step")])
        client = DevApiClient(transport)

        with self.assertRaises(DevApiResultError):
            client.capture_frame_and_step()

        transport.responses.extend(
            [
                {"request_id": 1, "ok": True, "result": {"data": "late capture"}},
                {"request_id": 3, "ok": True, "result": {"pong": True}},
            ]
        )
        self.assertEqual({"pong": True}, client.result("ping"))
        self.assertEqual({}, client._pending)

    def test_step_override_rejects_before_sending(self):
        transport = FakeTransport([])
        client = DevApiClient(transport, step_max=2)

        with self.assertRaises(ValueError):
            client.capture_frame_and_step(count=3)

        self.assertEqual([], transport.sent)


if __name__ == "__main__":
    unittest.main()
