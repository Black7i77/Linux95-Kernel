#!/usr/bin/env python3

import ast
from pathlib import Path
import unittest


HARNESS = Path(__file__).with_name("qemu_smoke.py")


class QemuSmokePolicyTest(unittest.TestCase):
    def test_late_completion_cannot_finish_grace_before_deadline(self):
        from qemu_smoke_policy import grace_complete

        self.assertFalse(grace_complete(11.7, 12.0, 12.0))
        self.assertFalse(grace_complete(11.7, 12.7, 12.0))
        self.assertTrue(grace_complete(10.7, 11.7, 12.0))

    def test_harness_rejects_incomplete_observation_before_markers(self):
        tree = ast.parse(HARNESS.read_text())
        required_at = next(index for index, node in enumerate(tree.body)
                           if isinstance(node, ast.Assign) and any(
                               isinstance(target, ast.Name) and target.id == "required"
                               for target in node.targets))
        guards = [node for node in tree.body[:required_at]
                  if isinstance(node, ast.If) and
                  isinstance(node.test, ast.UnaryOp) and
                  isinstance(node.test.op, ast.Not) and
                  isinstance(node.test.operand, ast.Name) and
                  node.test.operand.id == "saw_completion"]
        self.assertTrue(guards, "incomplete grace must fail before marker checks")
        self.assertTrue(any(
            isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)
            and isinstance(node.func.value, ast.Name)
            and node.func.value.id == "sys" and node.func.attr == "exit"
            for node in ast.walk(guards[0])), "incomplete grace guard must exit")

    def test_qemu_shutdown_is_visible(self):
        tree = ast.parse(HARNESS.read_text())
        command = next(node for node in tree.body
                       if isinstance(node, ast.Assign) and any(
                           isinstance(target, ast.Name) and target.id == "cmd"
                           for target in node.targets))
        flags = [element.value for element in command.value.elts
                 if isinstance(element, ast.Constant) and
                 isinstance(element.value, str)]
        self.assertIn("-no-reboot", flags)
        self.assertNotIn("-no-shutdown", flags)


if __name__ == "__main__":
    unittest.main()
