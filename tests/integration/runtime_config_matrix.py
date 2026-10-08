"""Run runtime settings in isolated module directories and fresh processes."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


KEYS = (
    "EmulateHardware", "SoftwareReverb", "SoftwareReflections",
    "FixPropertyDeadlocks", "EnableMP3Decoder", "EnableAC3Decoder",
    "UseNewCredits", "RelaxCreditsActivation",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--dll", type=Path)
    parser.add_argument("--capture", type=Path)
    parser.add_argument("--wave", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    work = args.output.resolve() / "working-directory"
    work.mkdir(exist_ok=True)
    (work / "a3dapi.conf").write_text(
        "[A3D]\n" + "\n".join(f"{key}=false" for key in KEYS))
    cases = [("missing", None, "11111100")]
    for mask in range(8):
        bits = f"{mask:03b}" + "11100"
        cases.append((f"effects-{mask}", "[A3D]\n" + "\n".join(
            f"{key}={'true' if bit == '1' else 'false'}"
            for key, bit in zip(KEYS, bits)), bits))
    cases += [
        ("off", "[A3D]\n" + "\n".join(f"{key}=0" for key in KEYS),
         "00000000"),
        ("on", "[a3d]\n" + "\n".join(f"{key.lower()}=TrUe" for key in KEYS),
         "11111111"),
        ("invalid", "[A3D]\nEmulateHardware=invalid\nSoftwareReverb=2\n"
         "SoftwareReflections=" + "false" * 40 + "\nUnknown=true\n",
         "11111100"),
        ("partial", "[A3D]\nSoftwareReverb= FaLsE \nUseNewCredits=1\n",
         "10111110"),
    ]
    for name, content, bits in cases:
        # Non-ASCII module paths exercise GetModuleFileNameW/profile reads.
        folder = args.output.resolve() / (name + "-\u017elu\u0165")
        folder.mkdir(exist_ok=True)
        exe = folder / args.exe.name
        shutil.copy2(args.exe, exe)
        conf = folder / "a3dapi.conf"
        conf.unlink(missing_ok=True)
        if content is not None:
            conf.write_text(content)
        env = {key.upper(): value for key, value in os.environ.items()}
        env["A3D_TEST_CONFIG_BITS"] = bits
        command = [str(exe), "--gtest_filter=RuntimeConfig.*"]
        for restart in (False, True):
            if restart:
                env["A3D_TEST_CONFIG_BITS"] = str(1 - int(bits[0])) + bits[1:]
            run = subprocess.run(command, cwd=work, env=env, timeout=15,
                                 capture_output=True, text=True)
            (folder / ("restart.log" if restart else "test.log")).write_text(
                run.stdout + run.stderr)
            if run.returncode:
                raise RuntimeError(f"{name} restart={restart}: {run.stdout}{run.stderr}")
        print(f"PASS {name}: {bits}, including restart")
    print(f"Passed {len(cases)} configurations and their restart checks.")
    if args.dll:
        if not args.capture or not args.wave:
            parser.error("--dll requires --capture and --wave")
        # The host executable, working directory and DLL directory differ.
        # Capture still takes ANSI paths; wide paths are tested above.
        for mask in range(8):
            bits = f"{mask:03b}"
            folder = Path(tempfile.mkdtemp(prefix=f"dll-{bits}-",
                                          dir=args.output.resolve()))
            dll = folder / "a3dapi.dll"
            shutil.copy2(args.dll, dll)
            (folder / "a3dapi.conf").write_text("[A3D]\n" + "\n".join(
                f"{key}={bit}" for key, bit in zip(KEYS[:3], bits)))
            # Q3 without reverb has a retained startup wait. Its bounded
            # measurements are separate from the ordinary regression.
            clients = ["default", "q3"] if bits[1] == "1" else ["default"]
            for client in clients:
                output = folder / client
                run = subprocess.run([
                    str(args.capture.resolve()), "--dll", str(dll),
                    "--wave", str(args.wave.resolve()), "--scene", "right",
                    "--client", client, "--output", str(output),
                    "--timeout-ms", "8000",
                ], cwd=work, timeout=12, capture_output=True, text=True)
                (folder / f"{client}.log").write_text(run.stdout + run.stderr)
                expected = 1 if client == "q3" and bits[0] == "0" else 0
                if run.returncode != expected:
                    raise RuntimeError(f"DLL {bits} {client}: {run.stdout}{run.stderr}")
                if expected:
                    log = run.stdout
                    if "Q3 hardware flags=0x0 voices=0" not in log:
                        raise RuntimeError(f"DLL {bits}: wrong Q3 failure: {log}")
                print(f"PASS DLL {bits} {client}: exit {run.returncode}")


if __name__ == "__main__":
    main()
