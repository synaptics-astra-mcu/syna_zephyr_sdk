#!/usr/bin/env python3
"""
Read data back from external XSPI flash through OpenOCD's TCL RPC server.

Example:
    python tools/openocd/scripts/read_xspi_tcl.py \
        --cfg_path tools/openocd/configs/sr100_m55.cfg \
        --flash-offset 0x100000 \
        --size 0xFA000 \
        --output capture.bin
"""

from __future__ import annotations

import argparse
import contextlib
import logging
import os
import shlex
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Optional


DEFAULT_TCL_PORT = 6666
DEFAULT_XIP_BASE = 0x3C000000
CMD_TIMEOUT_S = 120.0
STARTUP_TIMEOUT_S = 10.0
PROGRAM_TIMEOUT_S = 30.0
SRAM_BUF = 0x33F00000
CHUNK_SIZE = 0x40000
WREN_CMD = 0x06
RDSR1_CMD = 0x05
RDSR2_CMD = 0x35
WRSR12_CMD = 0x01


class TclRpcClient:
    def __init__(self, host: str = "127.0.0.1", port: int = DEFAULT_TCL_PORT,
                 timeout_s: float = CMD_TIMEOUT_S) -> None:
        self.host = host
        self.port = port
        self.timeout_s = timeout_s
        self.sock: Optional[socket.socket] = None

    def connect(self, attempts: int = 20, delay_s: float = 0.25) -> None:
        last_err = None
        if self.sock:
            self.close()

        for _ in range(attempts):
            s: Optional[socket.socket] = None
            try:
                s = socket.create_connection((self.host, self.port), timeout=self.timeout_s)
                s.settimeout(self.timeout_s)
                self.sock = s
                out = self.eval("set ::ocd_rpc 1")
                if "1" not in out:
                    raise RuntimeError(f"TCL sanity check failed: {out!r}")
                return
            except (OSError, TimeoutError, RuntimeError) as err:
                last_err = err
                if s:
                    with contextlib.suppress(Exception):
                        s.close()
                self.sock = None
                time.sleep(delay_s)
        raise ConnectionError(
            f"Unable to connect to OpenOCD TCL port at {self.host}:{self.port}: {last_err}"
        )

    def close(self) -> None:
        if self.sock:
            with contextlib.suppress(Exception):
                self.sock.close()
        self.sock = None

    def _send(self, payload: str) -> None:
        if not self.sock:
            raise ConnectionError("TclRpcClient not connected.")
        self.sock.sendall(payload.encode("utf-8", "ignore") + b"\x1a")

    def _recv_until_term(self) -> str:
        if not self.sock:
            raise ConnectionError("TclRpcClient not connected.")

        chunks = []
        deadline = time.monotonic() + self.timeout_s
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(4096)
                if not data:
                    raise ConnectionError("OpenOCD TCL socket closed")
                pos = data.find(b"\x1a")
                if pos >= 0:
                    chunks.append(data[:pos])
                    return b"".join(chunks).decode("utf-8", "ignore")
                chunks.append(data)
            except socket.timeout:
                pass
        raise TimeoutError("Timeout waiting for TCL response terminator 0x1A")

    def eval(self, tcl: str) -> str:
        self._send(tcl)
        return self._recv_until_term()

    def cmd(self, command: str) -> str:
        escaped = command.replace('"', '\\"')
        return self.eval(f'capture "{escaped}"')


class OpenOcdServerConfig:
    def __init__(self, openocd: str, cfg_path: Path, host: str = "127.0.0.1",
                 tcl_port: int = DEFAULT_TCL_PORT, probe: Optional[str] = None) -> None:
        self.openocd = openocd
        self.cfg_path = cfg_path
        self.host = host
        self.tcl_port = tcl_port
        self.probe = probe


class OpenOcdServer:
    def __init__(self, config: OpenOcdServerConfig) -> None:
        self.config = config
        self.proc: Optional[subprocess.Popen] = None

    def start(self) -> None:
        if self.proc and self.proc.poll() is None:
            raise RuntimeError("OpenOCD already running; stop() it first.")

        exe = shutil.which(self.config.openocd)
        if exe is None:
            raise FileNotFoundError(f"openocd binary not found: {self.config.openocd}")
        if not self.config.cfg_path.exists():
            raise FileNotFoundError(f"OpenOCD cfg not found: {self.config.cfg_path}")

        args = [exe, "-c", f"tcl_port {self.config.tcl_port}"]
        if self.config.probe:
            args += ["-c", f"set PROBE {self.config.probe}"]
        args += ["-c", "debug_level 1", "-c", 'log_output "openocd.log"', "-f", str(self.config.cfg_path)]

        logging.debug("Starting OpenOCD: %s", " ".join(shlex.quote(a) for a in args))
        self.proc = subprocess.Popen(
            args,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.STDOUT,
            env=os.environ.copy(),
            text=True,
            bufsize=1,
        )

        try:
            deadline = time.monotonic() + STARTUP_TIMEOUT_S
            while time.monotonic() < deadline:
                code = self.proc.poll()
                if code is not None:
                    raise RuntimeError(f"OpenOCD exited early with code {code}")
                try:
                    with socket.create_connection((self.config.host, self.config.tcl_port), timeout=0.2):
                        return
                except OSError:
                    time.sleep(0.1)
            raise TimeoutError(f"Timed out waiting for OpenOCD TCL port {self.config.tcl_port}.")
        except Exception:
            self.stop()
            raise

    def stop(self) -> None:
        if not self.proc:
            return
        if self.proc.poll() is None:
            with contextlib.suppress(Exception):
                self.proc.terminate()
            try:
                self.proc.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                with contextlib.suppress(Exception):
                    self.proc.kill()
        self.proc = None


class Ocd:
    def __init__(self, client: TclRpcClient) -> None:
        self.c = client

    def version(self) -> str:
        return self.c.cmd("version")

    def halt(self, ms: int = 500) -> None:
        self.c.cmd(f"halt {ms}")

    def mdw(self, addr: int) -> int:
        out = self.c.cmd(f"mdw {hex(addr)} 1")
        for line in out.splitlines():
            if ":" in line:
                _, value = line.split(":", 1)
                value = value.strip().split()[0]
                return int(value, 16)
        raise RuntimeError(f"Unexpected mdw output: {out.strip()}")

    def mww(self, addr: int, value: int) -> None:
        out = self.c.cmd(f"mww {hex(addr)} {hex(value & 0xFFFFFFFF)}")
        lower = out.lower()
        if any(token in lower for token in ("error", "fail", "unknown", "usage")):
            raise RuntimeError(f"mww failed: {out.strip()}")

    def dump_image(self, out_file: Path, addr: int, size: int) -> None:
        out = self.c.cmd(f'dump_image "{out_file.as_posix()}" {hex(addr)} {hex(size)}')
        lower = out.lower()
        if any(token in lower for token in ("error", "fail", "unknown", "usage")):
            raise RuntimeError(f"dump_image failed: {out.strip()}")


class DmaController:
    def __init__(self, ocd: Ocd, xip_base: int = DEFAULT_XIP_BASE) -> None:
        self.ocd = ocd
        self.xip_base = xip_base

        self.reg_ch_cmd = 0x50323000
        self.reg_ch_intren = 0x50323008
        self.reg_ch_ctrl = 0x5032300C
        self.reg_ch_srcaddr = 0x50323010
        self.reg_ch_desaddr = 0x50323018
        self.reg_ch_xsize = 0x50323020
        self.reg_ch_xsizehi = 0x50323024
        self.reg_ch_srctranscfg = 0x50323028
        self.reg_ch_destranscfg = 0x5032302C
        self.reg_ch_xaddrinc = 0x50323030

        self.reg_clk_enable1 = 0x50330700
        self.reg_top_sticky_rst = 0x50330658
        self.reg_top_rst = 0x50330640
        self.reg_dma_triginsec0 = 0x50322008
        self.reg_dma_trig_en = 0x50302054

    def init(self) -> None:
        val = self.ocd.mdw(self.reg_clk_enable1)
        self.ocd.mww(self.reg_clk_enable1, val | (1 << 4))

        val = self.ocd.mdw(self.reg_top_sticky_rst)
        self.ocd.mww(self.reg_top_sticky_rst, val | (1 << 2))

        val = self.ocd.mdw(self.reg_top_rst)
        self.ocd.mww(self.reg_top_rst, val | (1 << 0))
        val = self.ocd.mdw(self.reg_top_rst)
        self.ocd.mww(self.reg_top_rst, val & ~(1 << 0))

        self.ocd.mww(self.reg_dma_triginsec0, 0x0)
        self.ocd.mww(self.reg_dma_trig_en, 0xFFFFFFFF)

    def channel_config_read(self, flash_off: int, size: int, dst_addr: int = SRAM_BUF) -> None:
        flash_addr = self.xip_base + flash_off
        transfer_units = (size // 4) & 0xFFFFFFFF

        srcxsize_low = transfer_units & 0xFFFF
        srcxsize_high = (transfer_units >> 16) & 0xFFFF
        desxsize_low = srcxsize_low
        desxsize_high = srcxsize_high

        xsize = (desxsize_low << 16) | srcxsize_low
        xsizehi = (desxsize_high << 16) | srcxsize_high

        self.ocd.mww(self.reg_ch_srctranscfg, 0x000F4877)
        self.ocd.mww(self.reg_ch_srcaddr, flash_addr)
        self.ocd.mww(self.reg_ch_destranscfg, 0x000F4822)
        self.ocd.mww(self.reg_ch_desaddr, dst_addr)
        self.ocd.mww(self.reg_ch_xsize, xsize)
        self.ocd.mww(self.reg_ch_xsizehi, xsizehi)
        self.ocd.mww(self.reg_ch_ctrl, 0x00200202)
        self.ocd.mww(self.reg_ch_xaddrinc, 0x00010001)

    def channel_start(self) -> None:
        self.ocd.mww(self.reg_ch_intren, 0x00000003)
        self.ocd.mww(self.reg_ch_cmd, 0x00000001)
        for _ in range(10):
            if self.ocd.mdw(self.reg_ch_cmd) == 0x00000001:
                break
            time.sleep(0.001)

    def wait_done(self, timeout_s: float = 5.0) -> bool:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            try:
                if self.ocd.mdw(self.reg_ch_cmd) == 0:
                    return True
            except Exception:
                time.sleep(0.005)
                continue
            time.sleep(0.002)
        return False


class SR110:
    def __init__(self, ocd: Ocd) -> None:
        self.ocd = ocd

    def init_rwtc_cfg_0p9(self) -> None:
        self.ocd.halt(500)
        self.ocd.mww(0xB48A0094, 0x01)

        val = self.ocd.mdw(0xB48A0090)
        self.ocd.mww(0xB48A0090, val & ~0x3)

        self.ocd.mww(0xB5007018, 0xA26CA20B)
        self.ocd.mww(0xB500701C, 0x6C2C2)
        self.ocd.mww(0xB48A0080, 0xA26CA20B)
        self.ocd.mww(0xB48A0084, 0x6C2C2)
        self.ocd.mww(0x50330100, 0xA26CA20B)
        self.ocd.mww(0x50330104, 0x6C2C2)

        val = self.ocd.mdw(0xB48A0090)
        val &= ~(0x01 << 0x01)
        self.ocd.mww(0xB48A0090, val | 0x2)

        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            if self.ocd.mdw(0xB48A0094) != 0:
                break
            time.sleep(0.001)
        else:
            raise TimeoutError("RWTC status did not update within 5s")

        val = self.ocd.mdw(0x50350090)
        val &= ~(0x07 << 0x10)
        self.ocd.mww(0x50350090, val | ((0x05 & 0x07) << 0x10))
        time.sleep(1.0)

        val = self.ocd.mdw(0xB48A0004)
        self.ocd.mww(0xB48A0004, val & ~(0x01 << 0x03))

        val = self.ocd.mdw(0x50330720)
        self.ocd.mww(0x50330720, val & ~(0x01 << 0x02))


class XspiController:
    def __init__(self, ocd: Ocd, xip_base: int) -> None:
        self.ocd = ocd
        self.xip_base = xip_base
        self.base = 0x5031B000
        self.reg_cmd0 = self.base + 0x000
        self.reg_cmd1 = self.base + 0x004
        self.reg_cmd2 = self.base + 0x008
        self.reg_cmd3 = self.base + 0x00C
        self.reg_cmd4 = self.base + 0x010
        self.reg_cmd_status = self.base + 0x044
        self.reg_ctrl_status = self.base + 0x100
        self.reg_intr_status = self.base + 0x110
        self.reg_ctrl_config = self.base + 0x230
        self.reg_global_seq_cfg = self.base + 0x390
        self.reg_global_seq_cfg_1 = self.base + 0x394
        self.reg_direct_access_cfg = self.base + 0x398
        self.reg_direct_access_rmp = self.base + 0x39C
        self.reg_direct_access_rmp_1 = self.base + 0x3A0
        self.reg_prog_seq_cfg_0 = self.base + 0x420
        self.reg_prog_seq_cfg_1 = self.base + 0x424
        self.reg_prog_seq_cfg_2 = self.base + 0x428
        self.reg_read_seq_cfg_0 = self.base + 0x430
        self.reg_read_seq_cfg_1 = self.base + 0x434
        self.reg_read_seq_cfg_2 = self.base + 0x438
        self.reg_stat_seq_cfg_0 = self.base + 0x450
        self.reg_stat_seq_cfg_1 = self.base + 0x454
        self.reg_stat_seq_cfg_2 = self.base + 0x458
        self.reg_stat_seq_cfg_3 = self.base + 0x45C
        self.reg_stat_seq_cfg_5 = self.base + 0x464
        self.reg_stat_seq_cfg_7 = self.base + 0x46C
        self.reg_stat_seq_cfg_8 = self.base + 0x470
        self.reg_dll_phy_ctrl = self.base + 0x1034
        self.reg_wp_settings = self.base + 0x1000
        phy_base = self.base + 0x2000
        self.reg_phy_dq_timing = phy_base + 0x000
        self.reg_phy_dqs_timing = phy_base + 0x004
        self.reg_phy_gate_lpbk_ctrl = phy_base + 0x008
        self.reg_phy_dll_master_ctrl = phy_base + 0x00C
        self.reg_phy_dll_slave_ctrl = phy_base + 0x010
        self.reg_phy_dll_misc = phy_base + 0x080

    def _wait_cmd_idle(self, timeout_s: float = 0.1) -> bool:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            if (self.ocd.mdw(self.reg_ctrl_status) & 0xFFFF) == 0:
                return True
            time.sleep(0.001)
        return False

    def _intr_clear_all(self) -> None:
        self.ocd.mww(self.reg_intr_status, 0xFFFFFFFF)

    def _enter_stig(self) -> None:
        self.ocd.mww(self.reg_ctrl_config, 0x20)

    def enter_direct(self) -> None:
        self.ocd.mww(self.reg_ctrl_config, 0x00)

    def _stig_read_status_byte(self, opcode: int) -> int:
        self._enter_stig()
        self._intr_clear_all()

        self.ocd.mww(self.reg_cmd4, 0x00000000)
        self.ocd.mww(self.reg_cmd3, (opcode & 0xFF) << 16)
        self.ocd.mww(self.reg_cmd2, 0x00000000)
        self.ocd.mww(self.reg_cmd1, 0x00000001)
        self.ocd.mww(self.reg_cmd0, 0x00000000)

        self.ocd.mww(self.reg_cmd4, 0x00000000)
        self.ocd.mww(self.reg_cmd3, 0x00000000)
        self.ocd.mww(self.reg_cmd2, 0x00010000)
        self.ocd.mww(self.reg_cmd1, 0x0100007F)
        self.ocd.mww(self.reg_cmd0, 0x00000000)

        if not self._wait_cmd_idle():
            raise TimeoutError("Timeout waiting for STIG read status command.")
        return (self.ocd.mdw(self.reg_cmd_status) >> 16) & 0xFF

    def read_sr1(self) -> int:
        return self._stig_read_status_byte(RDSR1_CMD)

    def read_sr2(self) -> int:
        return self._stig_read_status_byte(RDSR2_CMD)

    def write_enable(self) -> bool:
        self._enter_stig()
        self._intr_clear_all()
        self.ocd.mww(self.reg_cmd4, 0x00000000)
        self.ocd.mww(self.reg_cmd3, (WREN_CMD & 0xFF) << 16)
        self.ocd.mww(self.reg_cmd2, 0x00000000)
        self.ocd.mww(self.reg_cmd1, 0x00000000)
        self.ocd.mww(self.reg_cmd0, 0x00000000)
        if not self._wait_cmd_idle():
            raise TimeoutError("Timeout waiting for STIG write enable command.")
        return (self.read_sr1() & 0x2) != 0

    def _wrsr12(self, sr1: int, sr2: int) -> None:
        self._enter_stig()
        self._intr_clear_all()
        self.ocd.mww(self.reg_cmd4, ((sr1 & 0xFF) << 24) | ((sr2 & 0xFF) << 16))
        self.ocd.mww(self.reg_cmd3, (WRSR12_CMD & 0xFF) << 16)
        self.ocd.mww(self.reg_cmd2, 0x00000000)
        self.ocd.mww(self.reg_cmd1, 0x00020000)
        self.ocd.mww(self.reg_cmd0, 0x00000000)
        if not self._wait_cmd_idle():
            raise TimeoutError("Timeout waiting for STIG WRSR command.")

    def enable_qe_bit(self) -> bool:
        sr1 = self.read_sr1()
        sr2 = self.read_sr2()
        if sr2 & (1 << 1):
            return True
        if not self.write_enable():
            raise RuntimeError("WREN not set before QE WRSR.")
        self._wrsr12(sr1, sr2 | (1 << 1))

        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            if (self.read_sr1() & 0x01) == 0:
                break
            time.sleep(0.01)
        else:
            raise TimeoutError("Timeout waiting for QE WRSR to complete.")
        return (self.read_sr2() & (1 << 1)) != 0

    def xspic_clk_enable(self) -> None:
        val = self.ocd.mdw(0x50330704)
        self.ocd.mww(0x50330704, val | 0x01)
        val = self.ocd.mdw(0x50330730)
        self.ocd.mww(0x50330730, val | 0x01)
        val = self.ocd.mdw(0x50330700)
        self.ocd.mww(0x50330700, val | 0x80)

    def xspic_clk_disable(self) -> None:
        val = self.ocd.mdw(0x50330704)
        self.ocd.mww(0x50330704, val & ~0x01)
        val = self.ocd.mdw(0x50330730)
        self.ocd.mww(0x50330730, val & ~0x01)
        val = self.ocd.mdw(0x50330700)
        self.ocd.mww(0x50330700, val & ~0x80)

    def xspic_reset(self) -> None:
        self.xspic_clk_disable()
        self.ocd.mww(0x50330680, 0x20)
        time.sleep(0.5)
        self.xspic_clk_enable()
        self.ocd.mww(0x50330680, 0x0)
        time.sleep(0.5)

    def direct_read_cfg(self) -> None:
        self.ocd.mww(self.reg_read_seq_cfg_0, 0x062230EB)
        self.ocd.mww(self.reg_read_seq_cfg_1, 0x0)
        self.ocd.mww(self.reg_read_seq_cfg_2, 0x0)

    def direct_prog_cfg(self) -> None:
        self.ocd.mww(self.reg_prog_seq_cfg_0, 0x3002)
        self.ocd.mww(self.reg_prog_seq_cfg_1, 0x0)
        self.ocd.mww(self.reg_prog_seq_cfg_2, 0x0)
        self.ocd.mww(self.reg_stat_seq_cfg_0, 0x0)
        self.ocd.mww(self.reg_stat_seq_cfg_1, 0x0)
        self.ocd.mww(self.reg_stat_seq_cfg_2, 0x05000005)
        self.ocd.mww(self.reg_stat_seq_cfg_3, 0x0)
        self.ocd.mww(self.reg_stat_seq_cfg_5, 0x40)
        self.ocd.mww(self.reg_stat_seq_cfg_7, 0x0)
        self.ocd.mww(self.reg_stat_seq_cfg_8, 0x0)

    def direct_remap_addr(self) -> None:
        self.ocd.mww(self.reg_global_seq_cfg, 0x8F)
        self.ocd.mww(self.reg_global_seq_cfg_1, 0x0)
        self.ocd.mww(self.reg_direct_access_cfg, 0x1000)
        self.ocd.mww(self.reg_direct_access_rmp, self.xip_base)
        self.ocd.mww(self.reg_direct_access_rmp_1, 0x0)


class XspiReader:
    def __init__(self, ocd: Ocd, xip_base: int) -> None:
        self.ocd = ocd
        self.xspi = XspiController(ocd, xip_base)
        self.dma = DmaController(ocd, xip_base)

    def bringup(self) -> None:
        self.xspi.xspic_clk_enable()
        self.ocd.mww(0x50330730, 0x35)

        for addr in (
            0x5033885C, 0x50338860, 0x50338864, 0x50338868, 0x5033886C,
            0x50338870, 0x50338874, 0x50338878, 0x50338880, 0x50338884,
        ):
            self.ocd.mww(addr, 0x1B)
        self.ocd.mww(0x50338888, 0x30)

        global_cfg_base = 0x50330000
        self.ocd.mww(global_cfg_base + 0x0A6C, 0xA)
        self.ocd.mww(global_cfg_base + 0x0A5C, 0x2)
        self.ocd.mww(global_cfg_base + 0x0A68, 0x498EB)
        self.ocd.mww(global_cfg_base + 0x0A64, 0x30E)
        self.ocd.mww(global_cfg_base + 0x0A70, 0x101)
        self.ocd.mww(global_cfg_base + 0x0A74, 0x700404)
        self.ocd.mww(global_cfg_base + 0x0A78, 0x200030)
        self.ocd.mww(global_cfg_base + 0x0A7C, 0x800000)
        self.ocd.mww(global_cfg_base + 0x0A80, 0x00007801)

        self.xspi.xspic_reset()
        time.sleep(0.5)

        self.ocd.mww(global_cfg_base + 0x0A60, 0x2)
        self.ocd.mww(self.xspi.reg_dll_phy_ctrl, 0x0030707)
        self.ocd.mww(self.xspi.reg_phy_dq_timing, 0x101)
        self.ocd.mww(self.xspi.reg_phy_dqs_timing, 0x700404)
        self.ocd.mww(self.xspi.reg_phy_gate_lpbk_ctrl, 0x200030)
        self.ocd.mww(self.xspi.reg_phy_dll_master_ctrl, 0x800000)
        self.ocd.mww(self.xspi.reg_phy_dll_slave_ctrl, 0x3300)
        self.ocd.mww(self.xspi.reg_phy_dll_misc, 0x4000)
        self.ocd.mww(self.xspi.reg_dll_phy_ctrl, 0x00000707)
        self.ocd.mww(self.xspi.reg_dll_phy_ctrl, 0x001030707)

        self.ocd.mww(self.xspi.reg_ctrl_config, 0x20)
        self.ocd.mww(self.xspi.reg_wp_settings, 0x01)

        if not self.xspi.write_enable():
            raise RuntimeError("WREN not set during XSPI configuration.")
        if not self.xspi.enable_qe_bit():
            raise RuntimeError("QE bit not set during XSPI configuration.")

        self.xspi.direct_remap_addr()
        self.xspi.direct_read_cfg()
        self.xspi.direct_prog_cfg()
        self.xspi.enter_direct()
        self.dma.init()
        time.sleep(0.2)

    def dump(self, flash_offset: int, size: int, out_file: Path) -> int:
        dump_addr = self.xspi.xip_base + flash_offset
        logging.info(
            "Dumping XSPI region: flash_offset=%s addr=%s size=%s -> %s",
            hex(flash_offset), hex(dump_addr), hex(size), out_file,
        )
        if flash_offset & 0x3:
            raise ValueError(f"DMA read requires 4-byte aligned flash offset; got {hex(flash_offset)}")

        remaining = size
        cur_flash_off = flash_offset
        with open(out_file, "wb") as final_out:
            while remaining > 0:
                chunk = min(CHUNK_SIZE, remaining)
                chunk -= (chunk & 0x3)
                if chunk == 0:
                    raise ValueError("Dump size must be 4-byte aligned for DMA reads.")

                self.xspi.enter_direct()
                self.dma.channel_config_read(cur_flash_off, chunk, SRAM_BUF)
                self.dma.channel_start()
                if not self.dma.wait_done(timeout_s=PROGRAM_TIMEOUT_S):
                    raise TimeoutError("Timeout waiting for DMA completion during flash readback.")

                with tempfile.NamedTemporaryFile(delete=False) as tmp:
                    tmp_path = Path(tmp.name)
                try:
                    self.ocd.dump_image(tmp_path, SRAM_BUF, chunk)
                    with open(tmp_path, "rb") as chunk_in:
                        final_out.write(chunk_in.read())
                finally:
                    with contextlib.suppress(Exception):
                        os.remove(tmp_path)

                cur_flash_off += chunk
                remaining -= chunk
        return dump_addr


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description="Dump a region from external XSPI flash via OpenOCD TCL RPC."
    )
    parser.add_argument("--openocd", default="openocd", help="Path to openocd executable.")
    parser.add_argument("--cfg_path", required=True, type=Path, help="OpenOCD config (.cfg).")
    parser.add_argument("--output", required=True, type=Path, help="Host file to create.")
    parser.add_argument("--flash-offset", required=True, help="Flash offset to read, e.g. 0x100000.")
    parser.add_argument("--size", required=True, help="Number of bytes to read, e.g. 0xFA000.")
    parser.add_argument("--xip-base", default=hex(DEFAULT_XIP_BASE),
                        help=f"Memory-mapped XSPI base address (default {hex(DEFAULT_XIP_BASE)}).")
    parser.add_argument("--tcl_port", type=int, default=DEFAULT_TCL_PORT,
                        help="OpenOCD TCL port (default 6666).")
    parser.add_argument("--tcl_host", default="127.0.0.1",
                        help="OpenOCD TCL host (default 127.0.0.1).")
    parser.add_argument("--probe", default="cmsis-dap",
                        help="Adapter driver, e.g. cmsis-dap or jlink.")
    parser.add_argument("--log-level", default="INFO",
                        help="Logging level (DEBUG/INFO/WARN/ERROR).")
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    logging.basicConfig(
        level=getattr(logging, args.log_level.upper(), logging.INFO),
        format="%(levelname)s: %(message)s",
    )

    try:
        flash_offset = int(args.flash_offset, 0)
        size = int(args.size, 0)
        xip_base = int(args.xip_base, 0)
    except ValueError:
        logging.error("Invalid numeric argument. Use integers like 1024 or 0x400.")
        return 2

    if size <= 0:
        logging.error("--size must be greater than zero.")
        return 2

    output = args.output.expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    server = OpenOcdServer(OpenOcdServerConfig(
        openocd=args.openocd,
        cfg_path=args.cfg_path,
        host=args.tcl_host,
        tcl_port=args.tcl_port,
        probe=args.probe,
    ))
    client = TclRpcClient(host=args.tcl_host, port=args.tcl_port, timeout_s=CMD_TIMEOUT_S)
    ocd = Ocd(client)

    try:
        server.start()
        client.connect()
        logging.info("Connected to OpenOCD: %s", ocd.version().splitlines()[0])

        SR110(ocd).init_rwtc_cfg_0p9()
        reader = XspiReader(ocd, xip_base)
        reader.bringup()
        dump_addr = reader.dump(flash_offset, size, output)

        actual_size = output.stat().st_size if output.exists() else 0
        if actual_size != size:
            raise RuntimeError(
                f"Dump completed but file size mismatch: expected {size} bytes, got {actual_size} bytes"
            )

        logging.info("Saved %d bytes from %s to %s", actual_size, hex(dump_addr), output)
        return 0
    except KeyboardInterrupt:
        logging.error("Interrupted by user.")
        return 130
    except Exception as err:
        logging.error("FAIL: %s", err)
        return 1
    finally:
        client.close()
        server.stop()


if __name__ == "__main__":
    sys.exit(main())
