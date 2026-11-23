"""The agent bench without a model or a window (python3 -m unittest test_agent_bench, in tools/): what a run's stream
says - calls, refusals, turns, tokens, cost, the agent's feedback - and the table of runs."""
import json
import unittest

import agent_bench


def line(**d):
    return json.dumps(d, ensure_ascii=False)


class BenchTests(unittest.TestCase):
    def test_metrics(self):
        stream = [
            line(type="assistant", message={"content": [{"type": "tool_use", "id": "a", "name": "mcp__ke-editor__assembly_add", "input": {}},
                                                        {"type": "tool_use", "id": "b", "name": "mcp__ke-editor__sim_measure", "input": {}}]}),
            line(type="user", message={"content": [{"type": "tool_result", "tool_use_id": "a", "content": [{"type": "text", "text": '{"ok": true}'}]},
                                                   {"type": "tool_result", "tool_use_id": "b", "is_error": True, "content": "no"}]}),
            line(type="user", message={"content": [{"type": "tool_result", "tool_use_id": "c", "content": json.dumps({"ok": False})}]}),
            "not json",
            line(type="result", num_turns=3, total_cost_usd=0.0055, result="Стоит.\nОТЗЫВ: ничего",
                 usage={"input_tokens": 10, "cache_read_input_tokens": 1000, "cache_creation_input_tokens": 90, "output_tokens": 400}),
        ]
        m = agent_bench.metrics(stream)
        self.assertEqual((m["calls"], m["errors"], m["turns"], m["input_tokens"], m["output_tokens"]), (2, 2, 3, 1100, 400))
        self.assertEqual(m["ops"], {"assembly_add": 1, "sim_measure": 1})
        self.assertEqual(m["feedback"], "ничего")

    def test_table(self):
        row = {"task": "ready_car", "model": "haiku", "profile": "agent", "wall_s": 23.7, "calls": 10, "errors": 0, "turns": 11,
               "input_tokens": 150000, "output_tokens": 4000, "cost_usd": 0.0055, "feedback": "a | b",
               "checks": {"drives": {"ok": True, "why": ""}, "ground": {"ok": False, "why": ""}}}
        text = agent_bench.table([row])
        self.assertIn("| ready_car | haiku | agent | ✓drives ✗ground | 23.7 | 10 | 0 | 11 | 150k/4k | 0.0055 | a / b |", text)


if __name__ == "__main__":
    unittest.main()
