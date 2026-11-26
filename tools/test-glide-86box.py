#!/usr/bin/env python3
"""test-glide-86box.py - runs a 32-bit DOS program of softgl in 86Box,
on a Pentium computer with FreeDOS and a 3D card, and writes the output
of the program. The test passes (exit status 0) when the output has a
line that starts with "PASS".

    test-glide-86box.py [options] DIR EXE [ARGS...]

DIR is the folder of tools/setup-86box.sh. The options:

    --card NAME     the video card of 86Box (default: virge375_pci, the
                    ViRGE/DX); for example virge325_pci (the ViRGE)
    --voodoo        also a Voodoo Graphics card
    --powervr       also a PowerVR PCX2 card (the Matrox m3D)
    --machine NAME  the main board of 86Box (default: p55t2p4, the ASUS
                    P/I-P55T2P4 with PCI slots); p5a (the ASUS P5A) has
                    an AGP slot, for an AGP card such as productiva_g100
    --shot PNG      a screenshot of the screen, SHOT_MS milliseconds of
                    emulated time after FreeDOS starts
    --shot-ms N     (default 5000)
    --timeout N     seconds of emulated time for the program (default 120)

The script copies the FreeDOS image, writes the program and a
FDCONFIG.SYS that starts it into the image with mtools, and controls
86Box through its automation socket: it presses F1 at the BIOS message
of a new CMOS, waits for the program, and reads OUT.TXT back.

Part of clang-retro softgl, a software implementation of OpenGL.
SPDX-License-Identifier: AGPL-3.0-only
"""
import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

# The byte offset of the first partition of the FreeDOS image.
PARTITION_OFFSET = 63 * 512


def write_dos_file(env, text, name, workdir):
    """Writes text with DOS line ends as C:\\name of the image."""
    path = os.path.join(workdir, name)
    with open(path, "w", newline="") as f:
        f.write(text.replace("\n", "\r\n"))
    subprocess.run(["mcopy", "-o", path, "c:/" + name], env=env, check=True)


def make_config(workdir, image, machine, card, voodoo, powervr):
    """Writes 86box.cfg: the main board machine with a Pentium 200,
    32 MB, the video card card (and a Voodoo when voodoo is true, a
    PowerVR PCX2 when powervr is true) and the image as the IDE disk.
    Gives the path of the file."""
    path = os.path.join(workdir, "86box.cfg")
    with open(path, "w") as f:
        f.write(f"""[Machine]
machine = {machine}
cpu_family = pentium_p54c
cpu_speed = 200000000
cpu_multi = 3
mem_size = 32768
cpu_use_dynarec = 1

[Video]
gfxcard = {card}
voodoo = {1 if voodoo else 0}
powervr = {1 if powervr else 0}

[Hard disks]
hdd_01_parameters = 63, 16, 65, 0, ide
hdd_01_fn = {image}
hdd_01_ide_channel = 0:0

[Sound]
sndcard = none
""")
    return path


class Automation:
    """A connection to the automation socket of 86Box."""

    def __init__(self, path):
        """Connects to the socket at path."""
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.connect(path)
        self.file = self.sock.makefile("rw")

    def cmd(self, line):
        """Sends one request and gives the reply as a dictionary."""
        self.file.write(line + "\n")
        self.file.flush()
        reply = self.file.readline()
        if not reply:
            raise RuntimeError("86Box closed the automation socket")
        return json.loads(reply)


def run(opts):
    """Runs the program in 86Box. Gives the output of the program."""
    box = os.path.join(opts.dir, "86Box", "build", "src", "86Box")
    roms = os.path.join(opts.dir, "roms")
    workdir = tempfile.mkdtemp(prefix="sgl86-")
    try:
        image = os.path.join(workdir, "hd.img")
        shutil.copy(os.path.join(opts.dir, "FD13LITE.img"), image)
        env = dict(os.environ, MTOOLSRC=os.path.join(workdir, "mtoolsrc"))
        with open(env["MTOOLSRC"], "w") as f:
            f.write('drive c: file="%s" offset=%d\n' % (image, PARTITION_OFFSET))
        write_dos_file(env, "FILES=40\nSHELL=\\COMMAND.COM /E:1024 /P=\\RUN.BAT\n", "FDCONFIG.SYS", workdir)
        write_dos_file(env, "@ECHO OFF\nRUN.EXE %s > OUT.TXT\nECHO SGL-END\n" % " ".join(opts.args),
                       "RUN.BAT", workdir)
        subprocess.run(["mcopy", "-o", opts.exe, "c:/RUN.EXE"], env=env, check=True)
        config = make_config(workdir, image, opts.machine, opts.card, opts.voodoo, opts.powervr)
        sock = os.path.join(workdir, "auto.sock")
        box_env = dict(os.environ, SDL_AUDIODRIVER="dummy")
        if not box_env.get("DISPLAY"):
            box_env["SDL_VIDEODRIVER"] = "dummy"
        with open(os.path.join(workdir, "86box.out"), "w") as log:
            proc = subprocess.Popen([box, "-R", roms, "-P", workdir, "-N", "--automation", "unix:" + sock,
                                     "--automation-hold", config], env=box_env, stdout=log,
                                    stderr=subprocess.STDOUT)
        try:
            for _ in range(200):
                if os.path.exists(sock):
                    break
                time.sleep(0.05)
            auto = Automation(sock)
            # A new CMOS has a bad checksum: the BIOS waits for F1.
            if auto.cmd("wait text 30000 Press F1").get("matched"):
                auto.cmd("key down 0x3b")
                auto.cmd("key up 0x3b")
            if opts.shot:
                auto.cmd("wait text 30000 FreeDOS")
                auto.cmd("step %d" % opts.shot_ms)
                auto.cmd("screenshot " + os.path.abspath(opts.shot))
            done = auto.cmd("wait text %d SGL-END" % (opts.timeout * 1000)).get("matched")
            auto.cmd("quit")
            proc.wait(timeout=30)
        finally:
            if proc.poll() is None:
                proc.kill()
        out = subprocess.run(["mtype", "c:/OUT.TXT"], env=env, capture_output=True, text=True).stdout
        out = out.replace("\r", "")
        if not done:
            out += "FAIL the program did not end in %d seconds\n" % opts.timeout
        return out
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


def main():
    """Reads the command line, runs the program and gives the exit status."""
    p = argparse.ArgumentParser(description="Runs a DOS program of softgl in 86Box.")
    p.add_argument("--card", default="virge375_pci")
    p.add_argument("--voodoo", action="store_true")
    p.add_argument("--powervr", action="store_true")
    p.add_argument("--machine", default="p55t2p4")
    p.add_argument("--shot")
    p.add_argument("--shot-ms", type=int, default=5000)
    p.add_argument("--timeout", type=int, default=120)
    p.add_argument("dir")
    p.add_argument("exe")
    p.add_argument("args", nargs="*")
    opts = p.parse_args()
    out = run(opts)
    sys.stdout.write(out)
    return 0 if any(line.startswith("PASS") for line in out.splitlines()) else 1


if __name__ == "__main__":
    sys.exit(main())
