"""Package-only sweep contract tests; no desktop app or hardware required."""

import asyncio
import importlib
import io
import json
import unittest
from contextlib import redirect_stdout
from unittest.mock import Mock, patch

from sniffy_mcp.__main__ import _cli_call
from sniffy_mcp.client import SniffyClient


MODULE = "Arb. generator"


class SweepClientTests(unittest.TestCase):
    def setUp(self):
        self.client = SniffyClient()
        self.rpc = Mock(return_value={"ok": True})
        patcher = patch.object(self.client, "_call", self.rpc)
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_legacy_positional_call_defaults_to_ch1(self):
        result = self.client.arbgen_set_sweep(MODULE, True, 100, 10000, 5)
        self.assertEqual(result, {"ok": True})
        self.rpc.assert_called_once_with("arbgen_set_sweep", {
            "module": MODULE, "enable": True, "channel": 0,
            "min_freq": 100, "max_freq": 10000, "sweep_time": 5,
        })

    def test_omitted_options_keep_bridge_settings(self):
        self.client.arbgen_set_sweep(MODULE, True)
        self.rpc.assert_called_once_with("arbgen_set_sweep", {
            "module": MODULE, "enable": True, "channel": 0,
        })

    def test_new_options_append_to_positional_signature(self):
        self.client.arbgen_set_sweep(
            MODULE, True, 0.01, 1e6, 60, 1, "exponential", "symmetric",
        )
        self.rpc.assert_called_once_with("arbgen_set_sweep", {
            "module": MODULE, "enable": True, "channel": 1,
            "min_freq": 0.01, "max_freq": 1e6, "sweep_time": 60,
            "scale": "exponential", "mode": "symmetric",
        })

    def test_channels_scales_and_modes_forward_independently(self):
        for channel in (0, 1):
            for scale in ("linear", "exponential"):
                for mode in ("saw", "symmetric"):
                    with self.subTest(channel=channel, scale=scale, mode=mode):
                        self.client.arbgen_set_sweep(
                            MODULE, True, 10, 100, 0.1,
                            channel=channel, scale=scale, mode=mode,
                        )
                        self.rpc.assert_called_with("arbgen_set_sweep", {
                            "module": MODULE, "enable": True, "channel": channel,
                            "min_freq": 10, "max_freq": 100, "sweep_time": 0.1,
                            "scale": scale, "mode": mode,
                        })
        self.assertEqual(self.rpc.call_count, 8)

    def test_disabled_channel_and_legacy_numeric_omission(self):
        self.client.arbgen_set_sweep(MODULE, False, 10, 100, 5, channel=1)
        self.rpc.assert_called_once_with("arbgen_set_sweep", {
            "module": MODULE, "enable": False, "channel": 1,
        })

    def test_scale_and_mode_can_be_supplied_without_numeric_options(self):
        self.client.arbgen_set_sweep(
            MODULE, False, channel=1, scale="linear", mode="saw",
        )
        self.rpc.assert_called_once_with("arbgen_set_sweep", {
            "module": MODULE, "enable": False, "channel": 1,
            "scale": "linear", "mode": "saw",
        })

    def test_sync_uses_existing_method_without_implicit_sweep_edits(self):
        for enabled in (False, True):
            self.client.arbgen_set_freq_sync(MODULE, 1, enabled)
            self.rpc.assert_called_with("arbgen_set_freq_sync", {
                "module": MODULE, "channel": 1, "enabled": enabled,
            })
        self.assertEqual(self.rpc.call_count, 2)

    def test_state_readback_preserves_legacy_aliases_and_channel_sweeps(self):
        ch1 = {
            "enabled": True, "active": True, "min_freq": 10,
            "max_freq": 100, "sweep_time": 5, "scale": "linear", "mode": "saw",
        }
        ch2 = {
            "enabled": True, "active": False, "min_freq": 20,
            "max_freq": 200, "sweep_time": 10,
            "scale": "exponential", "mode": "symmetric",
        }
        state = {
            "sweep_enabled": True, "sweep_min_freq": 10,
            "sweep_max_freq": 100, "sweep_time": 5,
            "channels": [{"sweep": ch1}, {"sweep": ch2, "freq_sync_ch1": True}],
        }
        self.rpc.return_value = state
        self.assertIs(self.client.get_module_state(MODULE), state)
        self.rpc.assert_called_once_with("get_module_state", {"module": MODULE})


class SweepBridgeErrorTests(unittest.TestCase):
    def test_bridge_validation_errors_propagate(self):
        client = SniffyClient()
        cases = [
            ({"channel": 2}, "invalid channel"),
            ({"scale": "log"}, "invalid scale"),
            ({"mode": "triangle"}, "invalid mode"),
            ({"min_freq": 0.001}, "frequency out of range"),
            ({"max_freq": 1e6 + 1}, "frequency out of range"),
            ({"min_freq": 100, "max_freq": 10}, "min_freq exceeds max_freq"),
            ({"sweep_time": 0.01}, "time out of range"),
            ({"sweep_time": 61}, "time out of range"),
            ({"channel": 1}, "disable frequency sync first"),
        ]
        for options, message in cases:
            with self.subTest(options=options):
                reply = json.dumps({
                    "error": {"code": -32602, "message": message},
                }).encode()
                with patch.object(client, "_send") as send, \
                        patch.object(client, "_recv", return_value=reply):
                    with self.assertRaisesRegex(RuntimeError, message):
                        client.arbgen_set_sweep(MODULE, True, **options)
                request = json.loads(send.call_args.args[0])
                self.assertEqual(request["method"], "arbgen_set_sweep")
                for key, value in options.items():
                    self.assertEqual(request["params"][key], value)


class SweepCliTests(unittest.TestCase):
    def test_cli_preserves_legacy_and_appended_options(self):
        cases = [
            ([MODULE, "true", "100", "10000", "5"], {
                "module": MODULE, "enable": True, "channel": 0,
                "min_freq": 100, "max_freq": 10000, "sweep_time": 5,
            }),
            ([MODULE, "true", "100", "10000", "5",
              "channel=1", "scale=exponential", "mode=symmetric"], {
                "module": MODULE, "enable": True, "channel": 1,
                "min_freq": 100, "max_freq": 10000, "sweep_time": 5,
                "scale": "exponential", "mode": "symmetric",
            }),
            ([MODULE, "true", "100", "10000", "5",
              "1", "linear", "saw"], {
                "module": MODULE, "enable": True, "channel": 1,
                "min_freq": 100, "max_freq": 10000, "sweep_time": 5,
                "scale": "linear", "mode": "saw",
            }),
            ([MODULE, "false", "channel=1"], {
                "module": MODULE, "enable": False, "channel": 1,
            }),
        ]
        for args, expected in cases:
            with self.subTest(args=args):
                output = io.StringIO()
                with patch.object(SniffyClient, "connect"), \
                        patch.object(SniffyClient, "assert_bridge_compatibility"), \
                        patch.object(SniffyClient, "close") as close, \
                        patch.object(SniffyClient, "_call", return_value={"ok": True}) as rpc, \
                        redirect_stdout(output):
                    _cli_call(["arbgen_set_sweep", *args])
                rpc.assert_called_once_with("arbgen_set_sweep", expected)
                close.assert_called_once()
                self.assertEqual(json.loads(output.getvalue()), {"ok": True})


class SweepMcpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            stdio = importlib.import_module("mcp.server.stdio")
        except ModuleNotFoundError as exc:
            raise unittest.SkipTest("Optional MCP dependency is not installed") from exc
        # Some MCP versions lack the existing run_server entry point. These
        # tests exercise registration/dispatch, never the stdio entry point.
        with patch.object(stdio, "run_server", create=True):
            cls.server = importlib.import_module("sniffy_mcp.mcp_server")

    def setUp(self):
        self.client = SniffyClient()
        self.rpc = Mock(return_value={"ok": True})
        for patcher in (
            patch.object(self.client, "_call", self.rpc),
            patch.object(self.server, "_get_client", return_value=self.client),
        ):
            patcher.start()
            self.addCleanup(patcher.stop)

    def test_sweep_tool_schema(self):
        tools = asyncio.run(self.server.list_tools())
        tool = next(t for t in tools if t.name == "sniffy_arbgen_set_sweep")
        schema = tool.inputSchema
        self.assertEqual(schema["required"], ["module", "enable"])
        properties = schema["properties"]
        self.assertEqual(properties["channel"]["type"], "integer")
        self.assertEqual(properties["channel"]["enum"], [0, 1])
        self.assertEqual(properties["channel"]["default"], 0)
        self.assertEqual(properties["scale"]["type"], "string")
        self.assertEqual(properties["scale"]["enum"], ["linear", "exponential"])
        self.assertEqual(properties["mode"]["type"], "string")
        self.assertEqual(properties["mode"]["enum"], ["saw", "symmetric"])
        for key, bounds in (
            ("min_freq", (0.01, 1e6)), ("max_freq", (0.01, 1e6)),
            ("sweep_time", (0.1, 60)),
        ):
            self.assertEqual(properties[key]["type"], "number")
            self.assertEqual(
                (properties[key]["minimum"], properties[key]["maximum"]), bounds,
            )

    def test_sweep_dispatch_defaults_and_all_options(self):
        cases = [
            {"module": MODULE, "enable": True},
            {"module": MODULE, "enable": True, "min_freq": 10,
             "max_freq": 100, "sweep_time": 5},
            {"module": MODULE, "enable": True, "channel": 1,
             "min_freq": 10, "max_freq": 100, "sweep_time": 5,
             "scale": "exponential", "mode": "symmetric"},
            {"module": MODULE, "enable": False, "channel": 1},
        ]
        for arguments in cases:
            with self.subTest(arguments=arguments):
                result = asyncio.run(self.server.call_tool(
                    "sniffy_arbgen_set_sweep", arguments,
                ))
                expected = {"channel": 0, **arguments}
                self.rpc.assert_called_with("arbgen_set_sweep", expected)
                self.assertEqual(json.loads(result[0].text), {"ok": True})
        self.assertEqual(self.rpc.call_count, len(cases))

    def test_sync_dispatch_is_unchanged(self):
        arguments = {"module": MODULE, "channel": 1, "enabled": True}
        asyncio.run(self.server.call_tool("sniffy_arbgen_set_freq_sync", arguments))
        self.rpc.assert_called_once_with("arbgen_set_freq_sync", arguments)

    def test_synced_ch2_error_is_reported_without_disabling_sync(self):
        self.rpc.side_effect = RuntimeError("disable frequency sync first")
        result = asyncio.run(self.server.call_tool(
            "sniffy_arbgen_set_sweep",
            {"module": MODULE, "enable": False, "channel": 1},
        ))
        self.assertEqual(result[0].text, "Error: disable frequency sync first")
        self.rpc.assert_called_once_with("arbgen_set_sweep", {
            "module": MODULE, "enable": False, "channel": 1,
        })


if __name__ == "__main__":
    unittest.main()
