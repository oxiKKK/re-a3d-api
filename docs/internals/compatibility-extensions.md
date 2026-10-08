# Compatibility extensions

These options intentionally change or supplement reference behavior. Keep their
validation separate from comparisons against unmodified 3.3.677.

## A3D_FIXES

This compile-time boundary includes the optional implementations and runtime
configuration. It defaults to ON; OFF excludes both. Each runtime branch reads
immutable settings from [A3dConfig.cpp](../../src/a3dapi/A3dConfig.cpp).
No reconstructed object gains configuration fields or public interface slots.

`FixPropertyDeadlocks` controls two changes in
[resman.cpp](../../src/a3dapi/resman.cpp): skip the manager wait when flushing
has been disabled, and signal a buffer's completion event when no DAL is
attached. These address specific original deadlocks, not every possible hang.

## EmulateHardware

When hardware candidates are unavailable, this option exposes the A2D software
device through hardware-capability queries used by legacy clients. Flags,
rates, and capacities describe the same software device as the software view.
Hardware-source limit access follows that capacity. Hardware routes retain
priority only when both software effects are disabled.

The Quake III regression exercises the IA3d4 capability gate and positional
audio on the software route. The effect settings are independent of this key.

## Software effect emulation

`SoftwareReverb` enables the reverb adapter and engine.
`SoftwareReflections` enables [reflection voices](../voice.md#reflection-voices) and delay rings. Both default to enabled,
are independent, and require the A2D software route:

| Addition | Design and limit |
| --- | --- |
| `CA3dReverbEmuPropertySet` | Accepts supported property operations and configures the software engine |
| `CA3dReverbEmuEngine` | Shared stereo effect; each channel has four damped comb filters followed by two allpass stages |
| `CA3dReflectionEmuVoice` | Per-voice reflection state outside `A2DBuffer`'s reconstructed layout |
| Reflection delay ring | Maximum 0.5 seconds, independent of `SetMaxReflectionDelayTime` |

Reverb configuration and processing use a critical section. Reflection entries
persist after voice destruction in the current implementation. This is a
resource/lifetime limitation to consider in long-running source-churn tests.

The processing is written for this project. It does not establish hardware
I3DL2/EAX/Aureal DSP equivalence merely because it accepts related parameters.
See [A3dReverbEmu.cpp](../../src/a3dapi/A3dReverbEmu.cpp) and
[A3dReflectionEmu.cpp](../../src/a3dapi/A3dReflectionEmu.cpp).

## Decoders and credits

`EnableMP3Decoder` and `EnableAC3Decoder` replace selected proprietary decoder
interfaces; decoded sample equivalence is unverified. Their dependencies are
described in [decoding](decoding-and-streaming.md).

`UseNewCredits` selects the embedded 678 scrolling credits. It defaults to
false, retaining the 677 program. `RelaxCreditsActivation` also defaults to
false; enabling it removes the My Computer requirement but retains Calculator.
Both payloads are present in additions-enabled builds. Neither setting affects
audio rendering.

## Reproducible comparisons

Record A3D_FIXES, runtime settings, DLL hashes, and client inputs. Use separate build
directories for the [reference](../development/building.md#reference-comparison-build)
and [game](../user/getting-started.md) configurations. Passing an extension test
does not establish parity; an expected parity difference in an extension build
does not by itself indicate an implementation regression.
