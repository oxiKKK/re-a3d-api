# Sound card hardware

**Short version:** this project has never been tested on real Aureal
hardware. Everything is developed and tested on ordinary modern PCs, where
A3D runs entirely in software. If you have an Aureal Vortex card, the code
paths for it exist, but nobody has confirmed that they work.

## What "hardware A3D" means

In the late 1990s, A3D could run in two ways:

- **On an Aureal sound card**, built on the Vortex 1 or Vortex 2 chip (for
  example the Diamond Monster Sound MX300). The card's chip did the 3D audio
  processing, and the game talked to it through Aureal's driver.
- **In software**, on the CPU, for PCs without an Aureal card.

Some other cards of that era also accelerated standard DirectSound3D, and
A3D could hand sounds to them.

## What happens on your PC

On Windows Vista and later, Windows no longer gives programs direct access
to sound card 3D hardware. All DirectSound audio is mixed in software by
Windows itself. So on a modern PC this project always uses its software
renderer: it computes the 3D sound on the CPU and plays the result through
your normal sound card or headphones.

This is the path that is developed and tested. Our automated tests compare
its output, sample by sample, with the original Aureal software renderer.

Some old games refuse to enable A3D unless they detect a hardware card. The
`EmulateHardware` setting makes the software renderer answer those checks,
so the game turns A3D on. The sound is still produced in software; see
[choosing a configuration](configuration.md).

## If you have an Aureal card

The reconstructed DLL contains the same hardware routes as the original:

- the DirectSound3D route, which sends sounds to a card with hardware 3D
  buffers, including the A3D extensions that Aureal's driver understands;
- the route through Aureal's own driver components, which come from the
  driver package and are not part of this project.

None of these routes has been run against a physical card. Expect problems,
and treat any result as new information rather than a known-good setup.
Known points:

- The shipped [a3dapi.conf](../../a3dapi.conf) turns on the software reverb
  and reflections, and either one makes the DLL skip the hardware routes. To
  try the hardware, set both `SoftwareReverb=false` and
  `SoftwareReflections=false`.
- You still need the original Aureal driver for your card, which only exists
  for old Windows versions (Windows 95 through XP).
- The original DLL has a known crash when cleaning up hardware 3D buffers.
  The reconstruction copies the original's behavior in every build, so it
  may crash in the same place.
- The Vortex chip's own processing (its DSP program) is not reconstructed.
  The project only rebuilds the DLL that talks to it.

## Help test it

If you try this on real hardware, please report:

- the card model and the driver version;
- the Windows version;
- whether you used the reconstructed `a3dapi.dll`, the original, or both;
- your `a3dapi.conf`;
- what happened: does the game start, is there sound, is it positioned
  correctly, does it crash on exit.

Results on real cards, even failures, are the only way to fill this gap.
See [troubleshooting](troubleshooting.md) for how to collect the details.
