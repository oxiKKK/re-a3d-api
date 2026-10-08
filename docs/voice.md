# Voice

A **voice** is one playback instance managed by an audio backend. It holds the
state needed to play a sound, such as its current sample position, playback
rate and gain. An audible voice contributes audio to the output; a virtual
voice can track playback without producing audio.

The term describes an engine resource. A voice can play an explosion, music,
a footstep or spoken dialogue. It does not specifically mean a human voice.

## Source, audio data, voice and channel

| Term | Meaning in this project |
| --- | --- |
| Source | The application-facing sound object, with playback and spatial settings |
| Audio data | The samples being played; playback instances can share sample storage |
| Voice | A backend playback instance with its own playback state |
| Output channel | One component of the final audio stream, such as left or right |

For example, two explosions can use the same recorded sound while playing at
different times and positions. Their playback needs independent voice state.
Both can contribute to the same left and right output channels. Two output
channels therefore do not imply a limit of two voices.

A source can remain alive while its backend assignment changes. Creating a
source or loading its audio does not guarantee an audible voice. The
[resource manager](internals/resource-management.md) assigns backend resources
according to capacity, modes and priorities. See
[sources and playback](programming/sources-and-playback.md) for the client view.

## What implements a voice?

The implementation depends on the selected
[audio backend](internals/audio-backends.md).

| Backend | Voice implementation |
| --- | --- |
| [A2D](internals/software-renderer.md) | CPU mixer state for one playback instance; many voices mix into one shared DirectSound output buffer |
| [D2D](internals/backend-d2d.md) | A DirectSound secondary buffer with volume, pan and playback-rate controls |
| [D3D](internals/backend-d3d.md) | A DirectSound3D buffer with spatial controls and, when supported, A3D driver properties |
| [EMU](internals/backend-emu.md) | Playback timing and state without audio output |

There is no single class named `Voice` representing all these cases. Source
records include [A2DBuffer](../src/a3dapi/a2dbuffer.h),
[D2DBuffer](../src/a3dapi/d2dbuffer.h),
[D3DBuffer](../src/a3dapi/d3dbuffer.h) and
[EMUBuffer](../src/a3dapi/emubuffer.h). In implementation discussions, a
"backend buffer" often refers to the object implementing a voice, rather than
only to a block of sample memory.

## Audible, virtual, hardware and software voices

An **audible voice** has an assignment capable of rendering audio. It can still
be silent because of its samples, gain or other controls. A **virtual voice**
keeps eligible playback progressing without rendering it. Consequently,
"playing" status alone does not prove that samples reach the output.

A **software voice** is processed by a software renderer, such as A2D.
A **hardware voice** describes a resource advertised through the hardware
backend/capability path. That label alone does not identify where processing
physically runs: a compatibility provider can implement an advertised device
in software. See [modern Windows output](user/modern-systems.md).

With this project's capability emulation, hardware and software queries can
report the same A2D capacity. Those counts must not be added together as if
they described independent pools.

## Reflection voices

A reflected path can require playback resources in addition to the source's
direct sound. The D3D reflection implementation can duplicate source buffers
and control their gain, direction and timing. These additional playback
instances are called **reflection voices**.

The optional A2D reflection processor instead keeps per-voice tap state and
delay rings and adds reflected samples to the software mix. A reflection tap
is not another application-created source, and its capacity should not be
confused with the number of direct voices or output channels. See
[reflections and reverb](programming/reflections-and-reverb.md) for effect
selection and limits.
