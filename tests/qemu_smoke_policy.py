"""Completion timing policy shared by the QEMU smoke harness and its tests."""


def grace_complete(first_seen_at, observed_at, deadline, grace_seconds=1.0):
    return (first_seen_at is not None and observed_at < deadline and
            observed_at - first_seen_at >= grace_seconds)
