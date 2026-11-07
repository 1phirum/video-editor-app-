"""Bounded, thread-safe request metadata for Pool Pro's live routing display."""
from collections import deque
import threading
import time
import uuid


class RouterActivity:
    def __init__(self, publish=None):
        self._active = {}
        self._recent = deque(maxlen=20)
        self._lock = threading.Lock()
        self._publish = publish
        self._revision = 0
        self._session = uuid.uuid4().hex

    def _snapshot(self):
        return {"activitySession": self._session, "activityRevision": self._revision,
                "activeRequests": [dict(item) for item in self._active.values()],
                "recentRequests": [dict(item) for item in self._recent]}

    def snapshot(self):
        with self._lock:
            return self._snapshot()

    def _notify(self, state):
        if self._publish:
            try:
                self._publish(state)
            except Exception:
                # The app can exit while its router still serves HTTP clients.
                # A closed UI pipe must never abort a request or strand it in
                # 'queued'. Keep lifecycle state valid for the /health fallback.
                pass

    def begin(self, model):
        request_id = uuid.uuid4().hex
        with self._lock:
            self._active[request_id] = {"id": request_id, "model": str(model)[:100],
                                       "stage": "queued", "accountId": "",
                                       "startedAtMs": int(time.time() * 1000)}
            self._revision += 1
            state = self._snapshot()
        self._notify(state)
        return request_id

    def update(self, request_id, stage, account_id=""):
        with self._lock:
            item = self._active.get(request_id)
            if item is None or (item["stage"] == stage and item["accountId"] == account_id):
                return
            item.update(stage=stage, accountId=account_id)
            self._revision += 1
            state = self._snapshot()
        self._notify(state)

    def finish(self, request_id, status):
        with self._lock:
            item = self._active.pop(request_id, None)
            if item is None:
                return
            now = int(time.time() * 1000)
            item.update(stage="completed" if 200 <= status < 300 else "failed",
                        status=status, finishedAtMs=now,
                        durationMs=max(0, now - item["startedAtMs"]))
            self._recent.appendleft(item)
            self._revision += 1
            state = self._snapshot()
        self._notify(state)
