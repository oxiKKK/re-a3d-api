# Sources, audio data, and playback

Use one logical source for each independently controlled sound. Select its
source type and audio format before allocating or loading data.

## Creation and data

`NewSource` returns an `IA3dSource2` wrapper. Creation flags distinguish native,
unmanaged, streamed, and AC-3 hardware requests. `LoadFile` selects WAV, MP3,
or AC-3 through its format flags; `LoadWaveFile` is the older WAV entry point.
`LoadWaveData` accepts an in-memory WAV image, including format/container data.

For application-supplied PCM, set the `WAVEFORMATEX` through `SetAudioFormat`,
allocate storage through `AllocateAudioData`, and fill it through `Lock` and
`Unlock`. Buffer lengths and positions are in bytes unless a method explicitly
uses time. Align operations to complete sample frames (`nBlockAlign`).

`Lock` can return two regions when an interval wraps. Write both returned
regions, then return both pointers and lengths to `Unlock`. `A3D_ENTIREBUFFER`
requests the whole buffer. Do not retain lock pointers after unlocking or
freeing the source's audio data.

The [decoder chapter](../internals/decoding-and-streaming.md) explains encoded
format limitations. `GetAudioFormat` describes decoded playback data;
`GetCaps` can describe the encoded input. Those need not have the same channel
count or sample rate.

## Playback controls

Choose single or looping playback according to the sound's role. Treat
stopping, seeking, and restarting as separate application decisions; stream
and decoder paths can impose different restrictions. Playback events let the
game react to progress without tying audio timing to the game frame rate.

For the scene-driven route, submit `Flush` after playback requests and state
updates. `A3DSTATUS_WAITING_FOR_FLUSH` represents pending playback. Playback
continues between scene submissions; withholding `Flush` leaves spatial state
stale.

## Gain, pitch, and native playback

Gain changes amplitude; pitch changes playback rate. Native sources use channel
pan values and bypass the ordinary positional path. Positional controls can
be refused in native mode, sometimes after storing a value. Accepted ranges
and failure behavior are documented beside the source methods.

For positional effects, start with a mono PCM fixture and neutral gain/pitch.
Add streaming, channel conversion, or effects only after the basic route is
understood. This makes format errors and spatial errors easier to separate.

## Streaming, duplication, and resource limits

`SetStreamingProperties` controls buffering and thread priority for source
streaming. Its buffer length is measured in milliseconds. Source streaming uses
refill callbacks; the device output ring is a different buffer.

`DuplicateSource` can share sample data while preserving independent controls.
Active streams and external decoder graphs have duplication restrictions. Check the method result before assuming a duplicate exists.

A playing logical source can be virtual or lose an audible
[voice](../voice.md) under resource pressure. Interpret `GetStatus`,
`GetAudibility`, capability limits, and PCM output together. Release sources
when their game entities disappear, and keep event handles valid until their
registrations are removed.

Use [A3dSource.h](../../src/a3dapi/A3dSource.h) for named parameters and
[A3dSource.cpp](../../src/a3dapi/A3dSource.cpp) for method contracts. The public
wrapper is declared in [a3dsourcecom.h](../../src/a3dapi/a3dsourcecom.h).
