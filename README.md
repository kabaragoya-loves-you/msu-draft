# MSU draft 0.3 reference codec

Portable C99 encoder and decoder for MIDI State Update draft 0.3. The wire
format is defined by `midi-pedal-state-update.md`. This tree does not transmit
MIDI and does not change that specification.

Copy `include/msu.h` and `src/msu.c` into firmware. `demo/` and `tests/` are
host-only. The codec allocates nothing, does not block, and does not send.

## Do not replace an existing SysEx parser

`msu_parser_feed()` keeps a 32-byte buffer. A packet longer than that is
discarded until `F7` or the next non-real-time status. That is correct for an
endpoint whose only SysEx interest is MSU. It is the wrong parser for a pedal
that also receives editor dumps or firmware updates. Those messages are longer
than 32 bytes and this parser will throw them away.

Keep the device's existing bounded SysEx assembler. When a message completes,
call `msu_decode()` on that buffer. `msu_decode()` returns an error for
non-MSU and for oversize MSU, and it does not consume the following MIDI
stream. Real-time bytes (`F8`–`FF`) are not part of either API's stored
payload; a stream parser must pull them out before `msu_decode()`, which
`msu_parser_feed()` already does.

## Recognize MSU before the manufacturer filter

Existing SysEx dispatchers often drop a message whose manufacturer ID is not
their own. That filter rejects a controller's `GET_STATE`, which is sent
under the controller's ID, and it rejects another pedal's notifications on a
merged port.

Run MSU recognition before that filter. `msu_decode()` accepts any
syntactically valid manufacturer ID when the MSU signature, version, opcode,
and length all match. Handle that message as MSU. When `msu_decode()` fails,
pass the original SysEx buffer to the existing manufacturer-specific
dispatcher. Unrelated proprietary SysEx, including foreign IDs that are not
MSU, stays on that path. MSU recognition does not replace it and does not
swallow it.

## Test manufacturer IDs

The demo uses fixtures: controller `7D`, Pedal A `71`, Pedal B `00 7D 7D`.
Do not transmit them on a live MIDI connection. A shipped product needs a
SysEx ID assigned to its manufacturer. `7D` is only for private
non-commercial experiments. This codec does not contain a manufacturer
registry. `7C` is syntactically accepted because draft 0.3 only bans `7E`
and `7F`, but the MIDI Association policy of 15 October 2025 forbids `7C`
for a custom SysEx format. Do not send MSU under `7C`.

## API

```c
msu_status_t msu_encode(const msu_message_t *msg, uint8_t *dest, size_t cap,
  size_t *out_len);
msu_status_t msu_decode(const uint8_t *src, size_t len, msu_message_t *msg);
void msu_parser_init(msu_parser_t *parser);
msu_feed_t msu_parser_feed(msu_parser_t *parser, uint8_t byte,
  msu_message_t *out, uint8_t *raw);
```

`msu_encode` writes one complete SysEx, including `F0` and `F7`, or it writes
nothing. Pass `dest == NULL` to read the required length after validation.
`msu_decode` accepts one complete packet. On failure the caller's
`msu_message_t` is unchanged. `msu_parser_feed` returns a decoded message
only after `F7` and a successful decode. Other MIDI bytes come back as
`MSU_FEED_BYTE` or `MSU_FEED_REALTIME`.

The message struct is tagged by `type` (the base opcode). Fourteen-bit values
are `uint16_t` in the struct. The codec splits them least-significant seven
bits first. Batch items live in a fixed array of 10. Metadata is `extended`,
`options`, `origin`, and `flags`. There are no bitfields and no pointers into
codec buffers.

Every version-1 message carries a channel. The codec rejects channels outside
`00`–`0F`. Matching that channel to a device's configured channel is the
caller's job. Message direction is not enforced.

## Build

From the repository root, with a host GCC. If `CC` in the environment
points at a cross compiler, pass the host compiler on the command line.

```text
mingw32-make -C reference/msu test
mingw32-make -C reference/msu demo
```

On this machine `CC` is the ESP-IDF RISC-V compiler, so the host build is:

```text
mingw32-make -C reference/msu test CC=C:/mingw64/bin/gcc NM=C:/mingw64/bin/nm
mingw32-make -C reference/msu demo CC=C:/mingw64/bin/gcc NM=C:/mingw64/bin/nm
```

`test` compiles the codec with `-std=c99 -Wall -Wextra -Wpedantic -Werror`
and `-ffreestanding -fno-builtin -fno-stack-protector
-fno-tree-loop-distribute-patterns`. It links `build/msu.o` with
`-nostdlib -nostartfiles` so unresolved libc symbols fail the link, writes
`build/msu.undef` from `nm -u`, and runs `build/msu_tests`. An empty
`msu.undef` is required: `findstr` on Windows, `test ! -s` on other systems.
`demo` runs `build/msu_demo`. The host programs are normal hosted C; only
`msu.c` is compiled freestanding.

## Behavior notes

These follow draft 0.3. They are not extra protocol fields.

- `OPTIONS == 00` is accepted. Reserved `OPTIONS` bits are rejected.
- Unknown origin values and unknown flag bits are preserved.
- `TRANSPORT` state must be `00`, `01`, or `02`.
- A 10-entry `CC7_BATCH` is 30 bytes with a one-byte manufacturer ID and 32
  bytes with a three-byte ID. Metadata that would exceed 32 bytes is refused.
- `F8`–`FF` do not terminate SysEx. Any other status does.
- The simulator answers `GET_STATE` immediately. Each value in that response
  starts the per-parameter interval. A later change is sent once the interval
  allows, and an unchanged value is not sent again when the interval elapses.
- Queuing `GET_STATE` opens one synchronization attempt and clears any
  previous `STATE_BEGIN`. The channel is synchronized only by `STATE_END`
  after `STATE_BEGIN` for that attempt. A coalesced duplicate request does
  not open another attempt. A lost response leaves the channel unsynchronized.
