---
lang: en
geometry: margin=0.85in
fontsize: 10pt
colorlinks: true
linkcolor: blue
header-includes:
  - "\\usepackage{fancyhdr}"
  - "\\pagestyle{fancy}"
  - "\\fancyhf{}"
  - "\\lhead{MSU — Experimental Draft 0.3}"
  - "\\rhead{25 October 2026}"
  - "\\cfoot{\\thepage}"
---

# MIDI State Update (MSU)

**Draft:** 0.3  
**Wire protocol version:** 1 (`01`)  
**Status:** Experimental; no implementation or interoperability claims  
**Intended audience:** Firmware authors and developers of MIDI-capable effects pedals, controllers, and routing software

## Abstract

This document specifies a small MIDI 1.0 System Exclusive (SysEx) protocol by which an effects pedal reports changes to its MIDI-addressable state. A receiving controller is assumed to have the pedal's CC, PC, and NRPN mappings from a device definition, manual, or user configuration. MSU carries parameter numbers and their current values; parameter descriptions and device manifests are outside its scope.

Version 1 supports Program Change (PC), banked PC, 7-bit Control Change (CC7), 14-bit CC (CC14), 7-bit and 14-bit Non-Registered Parameter Number (NRPN) updates, compact CC7 batches, tempo, simple transport state, an unsolicited ready announcement, a current-state request, and an optional notification-rate setting. State updates are reported irrespective of whether the change originated locally, over MIDI, from an editor, or from preset recall.

MSU uses existing MIDI 1.0 SysEx framing and defines no new status bytes. Devices without MSU support are not expected to act on these messages. Existing MIDI operation is unaffected unless a participating implementation explicitly enables state notifications or accepts MSU management requests.

## 1. Status and requirements language

This is an experimental, publicly described convention, not a MIDI Association standard. Message numbers and formats in this draft supersede those in MSU draft 0.2. The wire protocol version remains `01` because no devices are known to have implemented either draft. Version and opcode assignments SHALL be frozen before an interoperable public release.

The terms **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT**, and **MAY** are used as defined in RFC 2119 and RFC 8174 when capitalized. An implementer MAY implement only a subset of the optional message families; an unrecognized, otherwise valid MSU message MUST be ignored safely.

## 2. Scope and state model

MSU is intended for pedals whose MIDI surface is already documented. For example, a controller may know from its device profile that CC 5 represents feedback and that CC 24 selects the operating mode. A message carrying `CC 5 = 83` reports the currently applied MIDI-mapped value. The receiver interprets it using its existing profile.

A state notification represents the **effective, public MIDI value**, after any product-specific scaling, quantization, clamping, or mode-dependent mapping. The value may differ from a physical knob position or internal DSP representation. The sender MUST apply the same public value mapping used by its MIDI control interface. Stateless actions (tap, reset, increment, and similar triggers) SHOULD NOT be included in state snapshots unless the device defines a meaningful persistent state for them.

MSU has no device catalog, parameter naming, preset naming, mode schema, remote editing, acknowledgement/retry scheme, session discovery, or per-unit addressing. Two devices sharing both a MIDI input path and a channel cannot be reliably distinguished by MSU. This is a documented limitation; separate ports or channels are recommended for those installations.

## 3. MIDI compatibility and transport

MSU messages have the following shape:

```text
F0 <manufacturer-id> 4D 53 55 01 <opcode> <payload...> F7
```

`4D 53 55` is ASCII `MSU`. `<manufacturer-id>` is either one byte or the three-byte MIDI manufacturer ID whose first byte is `00`. The bytes `7E` and `7F` are Universal SysEx identifiers and MUST NOT be used as MSU manufacturer IDs. `7C` MUST NOT be used as an MSU manufacturer ID: the MIDI Association policy of 15 October 2025 limits `7C` to UMP and MIDI-CI discovery and forbids it for a custom System Exclusive format. A receiver MUST still accept an otherwise valid MSU packet that arrives under `7C`; that acceptance is not permission to transmit under `7C`. `7D` is suitable for non-commercial/private prototyping only; public commercial products MUST use an appropriately assigned identifier. The manufacturer, not each product SKU, receives the manufacturer ID. Model identifiers, when used, are managed by that manufacturer and do not occur in ordinary MSU packets.

All bytes between `F0` and `F7` MUST lie in `00`–`7F`. SysEx is not a new MIDI channel message. MSU introduces no new MIDI status code and does not reserve undefined MIDI statuses. Existing devices that do not support the participating manufacturer and MSU format can ignore the SysEx message.

A transmitter MUST NOT insert channel voice or system-common messages inside a SysEx message. MIDI System Real-Time bytes (`F8`–`FF`, subject to the transport's normal handling) may occur between SysEx bytes and MUST be processed independently rather than appended to the MSU payload. USB-MIDI and other transports must preserve the complete SysEx content for MSU to be received; some routing software filters SysEx, and MSU is not guaranteed to pass such routes.

### 3.1 Voluntary cross-manufacturer interpretation

An **MSU-aware receiver** MUST recognize the exact MSU signature immediately after *any syntactically valid manufacturer ID*, irrespective of the receiver's own manufacturer ID. This is a deliberate opt-in to a common, publicly documented payload format. The receiver MUST continue to ignore unrelated manufacturer-specific SysEx. The receiver MUST NOT reinterpret a packet whose marker is absent or misplaced.

Every sender MUST transmit its **own** assigned manufacturer ID (or `7D` in private non-commercial prototypes); it MUST NOT impersonate another manufacturer. A controller sends `GET_STATE` under its own ID, and an MSU pedal deliberately accepts that request under a different ID. The pedal replies under its own ID. No manufacturer-ID directory is needed by either party.

Cross-manufacturer interpretation is an MSU interoperability convention. It should not be represented as a requirement imposed on ordinary MIDI 1.0 receivers or as formal MIDI Association endorsement. An implementation that accepts foreign manufacturer IDs MUST restrict this exception to a fully validated MSU signature, supported version, opcode, and payload length.

## 4. Field encodings

All numerical fields are unsigned seven-bit bytes unless specified. `CH` is zero-based: `00`–`0F` represent MIDI channels 1–16. Channel `7F` is not used in version 1. A recipient ignores channel-specific requests not matching its configured channel; products with multiple MIDI-addressable parts MAY respond separately on each applicable channel. All other `CH` values are invalid.

A fourteen-bit integer is transmitted least-significant seven-bit group first:

```c
lo = number & 0x7f;
hi = (number >> 7) & 0x7f;
number = lo | (hi << 7);
```

For NRPN, the encoded parameter number uses `NRPN_LO` and `NRPN_HI` in this order; the resulting value is `NRPN_LO | (NRPN_HI << 7)`. This order is MSU-specific framing and does not prescribe the order of ordinary MIDI NRPN Select CC messages on the wire.

Receivers MUST validate the exact payload size for each recognized opcode, the manufacturer-ID length, the protocol marker, version, channel, and any constrained value ranges. Invalid or truncated packets MUST be discarded without modifying state or desynchronizing subsequent MIDI messages. Unknown versions and opcodes MUST be ignored.

## 5. Message assignments

The base opcode is the message type listed below. For *state update messages only* (opcodes `10`, `11`, `20`–`24`, `30`, `31`), setting bit `40` indicates the optional metadata form defined in Section 8. Housekeeping messages (`01`–`05`) have no extended form.

| Opcode | Message | Payload bytes after opcode | Direction |
|:--|:--|:--|:--|
| `01` | READY | `CH` | Pedal → controller |
| `02` | GET_STATE | `CH` | Controller → pedal |
| `03` | STATE_BEGIN | `CH` | Pedal → controller |
| `04` | STATE_END | `CH` | Pedal → controller |
| `05` | SET_UPDATE_INTERVAL | `CH PERIOD_10MS` | Controller → pedal |
| `10` | PC | `CH PROGRAM` | Pedal → controller |
| `11` | BANKED_PC | `CH BANK_MSB BANK_LSB PROGRAM` | Pedal → controller |
| `20` | CC7 | `CH CC VALUE` | Pedal → controller |
| `21` | CC14 | `CH CC_MSB VALUE_LO VALUE_HI` | Pedal → controller |
| `22` | NRPN7 | `CH NRPN_LO NRPN_HI VALUE` | Pedal → controller |
| `23` | NRPN14 | `CH NRPN_LO NRPN_HI VALUE_LO VALUE_HI` | Pedal → controller |
| `24` | CC7_BATCH | `CH COUNT (CC VALUE)×COUNT` | Pedal → controller |
| `30` | TEMPO | `CH BPM_TENTHS_LO BPM_TENTHS_HI` | Pedal → controller |
| `31` | TRANSPORT | `CH STATE` | Pedal → controller |

Unassigned opcodes are reserved. Implementers MUST NOT assign private interoperable meanings to them without a protocol revision. The table defines wire types, not mandatory feature support. Minimal producers may implement only `READY`, `GET_STATE`, `STATE_BEGIN`, `STATE_END`, PC and CC7.

## 6. State update messages

### 6.1 PC and banked PC

`PROGRAM` is the raw MIDI Program Change value (`00`–`7F`), regardless of whether the pedal's display uses one-based preset numbering. `BANK_MSB` and `BANK_LSB` are the values associated with Bank Select controllers CC 0 and CC 32. A sender SHOULD use `PC` when bank state is not relevant or is unknown. A sender SHOULD use `BANKED_PC` when the effective program is only unambiguous with bank values; an unused bank select component is encoded as zero where that matches the pedal's public MIDI specification. MSU does not define a sentinel meaning “unknown bank.”

```text
# MIDI channel 1, program 12
F0 7D 4D 53 55 01 10 00 0C F7

# MIDI channel 1, bank MSB 0, bank LSB 2, program 12
F0 7D 4D 53 55 01 11 00 00 02 0C F7
```

### 6.2 CC7

`CC` and `VALUE` are seven-bit values. The sender SHOULD restrict CC update notifications to the documented application controls, normally CC 0–119. It SHOULD NOT report Channel Mode command CCs 120–127 as persistent parameters unless its own published MIDI interface explicitly defines such a value.

```text
# MIDI channel 1, CC 5 now equals 83
F0 7D 4D 53 55 01 20 00 05 53 F7
```

### 6.3 CC14

`CC_MSB` is `00`–`1F` and identifies the standard coarse controller CC 0–31. The fine controller is `CC_MSB + 32`. `VALUE_LO` and `VALUE_HI` encode the same logical 14-bit value. Where a device uses a non-standard CC pair, it MAY emit the two ordinary CC7 values individually. Explicit non-standard pair identifiers are deferred to a possible later revision.

```text
# MIDI channel 1, CC 16/48 now equals 8192
F0 7D 4D 53 55 01 21 00 10 00 40 F7
```

### 6.4 NRPN7 and NRPN14

NRPN messages contain the 14-bit NRPN parameter number and either a 7-bit or 14-bit value. A sender reports the parameter's applied value. It does not emit the ordinary MIDI sequence of NRPN selection and Data Entry controls as a side effect of MSU.

```text
# MIDI channel 1, NRPN 1234 now equals 99 (7-bit)
F0 7D 4D 53 55 01 22 00 52 09 63 F7

# MIDI channel 1, NRPN 1234 now equals 4096 (14-bit)
F0 7D 4D 53 55 01 23 00 52 09 00 20 F7
```

### 6.5 Compact CC7 batch

`CC7_BATCH` combines multiple CC7 changes on a single channel without repeated SysEx headers. `COUNT` is `01`–`0A` and is the number of CC/value pairs that follow. Those pairs contain exactly `2 × COUNT` bytes. Items are applied in their transmitted order. A sender SHOULD include only the latest value per CC within one batch, except where an ordered sequence is required to establish a documented mode or gate change. Batches do not imply MIDI transaction atomicity.

```text
# Channel 1: CC 5 = 15, CC 6 = 32, CC 7 = 68
F0 7D 4D 53 55 01 24 00 03 05 0F 06 20 07 44 F7
```

A three-byte manufacturer ID makes this example 18 bytes; three individual CC7 MSU packets would occupy 39 bytes. On DIN MIDI, shorter packets reduce the interval for which channel voice traffic may be held behind a SysEx transmission. Mixed-type batches and separate batch begin/end transactions are intentionally omitted from version 1.

### 6.6 Tempo

`TEMPO` reports the effective tempo in units of 0.1 beats per minute, encoded as a 14-bit integer. The range is 0.1–1638.3 BPM; zero means unavailable. This message does not transmit beat phase, replace MIDI Clock, or require the receiver to generate clock. Receivers SHOULD use the reported tempo for display/state synchronization only, unless configured otherwise by the user.

```text
# MIDI channel 1, 120.5 BPM = 1205 tenths = 35 09
F0 7D 4D 53 55 01 30 00 35 09 F7
```

A sender slaved to incoming MIDI Clock SHOULD avoid reporting insignificant tempo fluctuations caused by clock jitter. It SHOULD emit meaningful changes to the stable effective tempo, including changes resulting from tap tempo or preset recall.

### 6.7 Transport

`TRANSPORT` is an optional summary of a pedal's current transport state: `00` stopped, `01` running, `02` paused. It describes state and MUST NOT be treated as an MMC command. Devices that cannot express their state faithfully with these values SHOULD omit this message. Looper-specific recording, overdubbing, undo, and other overlapping state flags are outside version 1.

```text
# MIDI channel 1, transport running
F0 7D 4D 53 55 01 31 00 01 F7
```

## 7. Synchronization and connection handling

### 7.1 READY (`01`)

A sender SHOULD issue `READY CH` after its MIDI output is initialized and its effective state has been loaded. It MAY issue `READY` when its MIDI output interface is reinitialized. A sender MUST NOT assume that a connected receiver heard the message. A cable may be disconnected while the pedal remains powered, and DIN MIDI provides no general cable-insertion event.

```text
F0 7D 4D 53 55 01 01 00 F7
```

A receiver that receives `READY` SHOULD mark its cached values for the associated MIDI port/channel as potentially stale and send `GET_STATE`. It SHOULD avoid unnecessarily repeating identical in-flight requests.

### 7.2 GET_STATE (`02`)

`GET_STATE CH` requests a report of the sender's currently applied, persistent MIDI-addressable values on channel `CH`. A sender that implements this request MUST answer using `STATE_BEGIN CH`, zero or more state update packets, and `STATE_END CH`, in that order. A receiver SHOULD also request current state when its MIDI port becomes available or when the user requests reconciliation, even if it has not seen `READY`.

```text
# Controller request (using the controller's own manufacturer ID)
F0 7D 4D 53 55 01 02 00 F7

# Pedal reply (shown with 7D for private examples)
F0 7D 4D 53 55 01 03 00 F7
F0 7D 4D 53 55 01 10 00 0C F7
F0 7D 4D 53 55 01 24 00 02 18 03 05 53 F7
F0 7D 4D 53 55 01 04 00 F7
```

The controller MUST NOT assume that `GET_STATE` is supported merely because an unrelated device responds to MIDI SysEx. A lack of response is not an error requiring repeated rapid retries. Device Identity Inquiry remains a separately available standard MIDI mechanism but is not required for MSU version 1.

The request asks for the public MIDI surface that the sender can currently report. No manifest is required, and a sender need not enumerate parameters that cannot be read back or that have no persistent state. When mode-selecting CC values are known, they SHOULD precede dependent CC values in the snapshot. This permits a receiver with a human-curated, mode-dependent parameter database to interpret following values using the correct mode.

`STATE_END` means that the response has finished. It does not guarantee an atomic snapshot across concurrent knob movements. A sender SHOULD serialize its snapshot against subsequently queued live notifications and SHOULD avoid delivering stale snapshot values after newer live values for the same parameter. Duplicate concurrent requests for one channel MAY be coalesced into one response. If the response cannot complete, the receiver SHOULD retain an unsynchronized state and allow manual retry.

Successfully queuing `GET_STATE` starts a synchronization attempt on that channel. The receiver MUST treat the channel as awaiting a fresh response and MUST NOT treat the cache as synchronized until `STATE_BEGIN` and then `STATE_END` have both been received, in that order, for that attempt. `STATE_END` establishes synchronization only after `STATE_BEGIN` for the current attempt. The receiver then clears the attempt. A later `GET_STATE` that is actually queued starts a new attempt; `STATE_BEGIN` or `STATE_END` belonging to an earlier attempt MUST NOT complete the new one. A request that produces no response leaves the channel unsynchronized. A duplicate `GET_STATE` that is coalesced rather than queued does not start another attempt.

### 7.3 Source association and limitations

The recommended source key at the receiver is the physical/logical MIDI input port plus the `CH` field. The manufacturer ID may be retained as diagnostic metadata but is not an instance identifier. READY and GET_STATE do not add an instance identifier, device serial number, or manufacturer/product registry. If indistinguishable devices occupy the same merged input and channel, state association is undefined.

## 8. Optional metadata extension

The common case has no sequence number, flags, origin, or sender instance ID. For a state-update opcode listed in Section 5, an extended update uses the opcode with bit `40` set. Its payload begins with the normal base fields, followed by `OPTIONS` and only the fields selected by its bits, in ascending bit order:

| OPTIONS bit | Meaning | Additional byte |
|:--|:--|:--|
| `01` | Origin present | `ORIGIN` |
| `02` | Flags present | `FLAGS` |
| `04`–`40` | Reserved | None defined in v1 |

`ORIGIN` is advisory: `00` unknown, `01` local control, `02` incoming MIDI, `03` preset recall, `04` editor, `05` expression/CV, `06` internal automation, `07` clock/tempo source. `FLAGS` bit `01` indicates final/coalesced stable value and bit `02` indicates an approximate value; other `FLAGS` bits are reserved. A receiver MUST ignore unknown origin values and reserved `FLAGS` bits, and MUST NOT reject a packet because those values are present. `OPTIONS` value `00` is valid: the extended payload is the base fields followed by that single byte, and receivers MUST accept it. A receiver MUST reject a packet that sets an unsupported `OPTIONS` bit (`04`–`40`) because its length cannot be interpreted unambiguously.

```text
# Extended CC7: channel 1, CC 5 = 83, local origin, final value
# 60 = 20 | 40, OPTIONS 03, ORIGIN 01, FLAGS 01
F0 7D 4D 53 55 01 60 00 05 53 03 01 01 F7
```

Senders SHOULD omit this extension unless metadata is useful to the receiver. Receivers MUST accept base forms without metadata. Metadata does not determine whether a state notification is authoritative: every valid state update represents the sender's current reported value regardless of its origin.

## 9. Notification rate control

To limit continuous parameter telemetry, a receiver MAY send `SET_UPDATE_INTERVAL CH PERIOD_10MS`. `PERIOD_10MS` is the minimum interval in units of ten milliseconds between successive notifications for the **same parameter**. A value of `05` requests 50 ms (at most 20 updates per second per parameter); `00` requests no additional MSU rate limit. This message is optional to implement and has no acknowledgement in version 1.

```text
# Controller requests at least 50 ms between updates per parameter
F0 7D 4D 53 55 01 05 00 05 F7
```

A sender that implements this request SHOULD apply it to continuously changing CC7, CC14, and NRPN values on the specified channel. It MUST NOT discard the final settled value indefinitely: while changes are coalesced, the latest value MUST be emitted once the interval permits, subject to normal transmission availability. PC, BANKED_PC, READY, state-response framing, discrete mode changes, and transport changes SHOULD NOT be delayed by this rate setting. `GET_STATE` responses MUST NOT be suppressed or delayed by rate limiting. Each value included in a `GET_STATE` response counts as a notification for that parameter and starts that parameter's interval. A value that changes after the response has been queued MUST still be emitted once the interval permits. The sender SHOULD NOT emit an unchanged value again solely because the interval elapses.

The interval is a device/channel-wide policy, not a subscription to a particular remote controller. Multiple requests on the same channel use last-request-wins behavior; the setting SHOULD reset to the device's configured default at reboot. A sender MAY provide its own lower telemetry rate or an off switch. Receivers MUST NOT presume that the requested interval was accepted.

## 10. Priority, buffering, and bandwidth

At 31,250 bits/s, one MIDI DIN byte takes approximately 0.32 ms, including serial framing. A CC7 MSU packet is 11 bytes with a one-byte manufacturer ID or 13 bytes with a three-byte ID, compared with a three-byte conventional CC message. A CC7 batch with ten entries is 30 or 32 bytes respectively.

Version 1 limits an individual MSU SysEx message to **32 bytes total**, including `F0`, all header fields, and `F7`. A sender MUST split larger state reports across packets. An extended state-update form is valid only when the finished SysEx is still within this limit. If `OPTIONS` and the selected metadata fields would exceed 32 bytes, the sender MUST send the base form or split the report. A `CC7_BATCH` with `COUNT` `0A` and a three-byte manufacturer ID is 32 bytes and therefore cannot carry the metadata extension. A receiver MUST reject a packet longer than 32 bytes. At the DIN rate, a 32-byte packet occupies approximately 10.24 ms; MIDI System Real-Time messages can interrupt that packet, but channel voice messages cannot. Rate limiting and batching reduce average load but do not eliminate this single-packet delay.

A sender SHOULD defer starting MSU packets while time-sensitive voice/clock traffic is queued, and SHOULD prioritize System Real-Time bytes in accordance with MIDI transmission rules. It MUST NOT emit MSU directly from an audio ISR, real-time audio callback, or other path that risks blocking audio. Where the MIDI output is a true hardware THRU and cannot originate bytes, MSU transmission may be unavailable without hardware changes.

A receiver MUST treat MSU reports as observations of device state. It MUST NOT automatically forward notifications as conventional CC/PC commands or re-emit them as control messages. Locally manipulated motorized or virtual controls MAY defer visible or physical updates according to their pickup/override policy, while retaining the most recently reported state in the receiver's cache.

## 11. Sender implementation guidance

The natural integration point is a central applied-parameter setter or the event path by which preset recall, MIDI control, editor updates, expression input, and physical controls update the effective state. A sender MUST report the same public MIDI value irrespective of the change origin; changes received over MIDI are included. A sender SHOULD avoid duplicate notifications for an unchanged effective value.

The example below omits hardware-specific queuing and locking:

```c
void applied_parameter_changed(parameter_id id) {
    mapping m;
    if (!msu_enabled || !public_midi_mapping(id, &m)) return;

    // Must use the pedal's documented MIDI mapping, not an
    // arbitrary clamp of the internal DSP value.
    midi_value v = encode_effective_public_midi_value(id, m);
    if (cache_equals_last_reported(m, v)) return;

    cache_latest_pending(m, v);        // keyed by channel + parameter
    schedule_msu_tx_outside_audio_irq();
}

void msu_tx_service(uint32_t now_ms) {
    while (midi_tx_has_budget() && eligible_pending_update(now_ms)) {
        // Prefer a compact CC7 batch when several CCs on one channel
        // are already eligible. Otherwise send one ordinary update.
        encode_next_msu_packet();
        commit_last_reported_on_enqueue();
    }
}

void on_valid_msu_request(msu_message msg) {
    if (msg.channel != configured_channel) return;
    if (msg.type == GET_STATE) {
        queue_state_begin();
        queue_effective_public_values_gate_first();
        queue_state_end();
    } else if (msg.type == SET_UPDATE_INTERVAL && rate_control_supported) {
        rate_interval_ms = msg.period_10ms * 10;
    }
}
```

Implementations SHOULD handle SysEx byte reception without allocating memory proportional to arbitrary incoming message size. A bounded parser may discard overlong inputs while continuing to recognize `F7` or any subsequent status byte. An incoming message MUST be fully validated before a state request or rate-setting side effect is applied.

## 12. Receiver implementation guidance

A receiver may treat MSU as a cache update channel and keep its existing controller database as the semantic source of truth. No manufacturer-specific decoder is needed.

```c
void on_sysex(const uint8_t *bytes, size_t length, midi_port port) {
    msu_message msg;
    if (!msu_decode(bytes, length, &msg)) return; // ignores other SysEx

    source_key source = { port, msg.channel };
    switch (msg.type) {
      case READY:
        invalidate_cached_state(source);
        schedule_get_state(source);  // starts one synchronization attempt
        break;
      case STATE_BEGIN:
        if (sync_attempt_open(source))
          mark_begin_for_attempt(source);
        break;
      case STATE_END:
        if (begin_seen_for_attempt(source)) {
          mark_sync_complete(source);
          clear_attempt(source);
        }
        break;
      case CC7:
      case CC14:
      case NRPN7:
      case NRPN14:
      case PC:
      case BANKED_PC:
      case CC7_BATCH:
      case TEMPO:
      case TRANSPORT:
        apply_state_update_to_cache(source, msg);
        reconcile_ui_without_sending_midi_control(source);
        break;
    }
}
```

On new port availability the receiver SHOULD issue `GET_STATE` using its **own** valid manufacturer ID. It MAY perform a standard MIDI Device Identity Request for other purposes, but MSU operation does not require it. Implementations MUST ignore unrelated SysEx packets, and a product that also has a manufacturer-specific dispatcher MUST try MSU decoding before that manufacturer's ID filter and MUST pass a packet that is not valid MSU to the existing dispatcher. They SHOULD retain known gate/mode values when interpreting later parameter updates and apply batch items in wire order. `schedule_get_state` starts the attempt in Section 7.2. Synchronization is marked complete only when `STATE_END` follows `STATE_BEGIN` for that attempt, and the attempt is cleared when the response completes.

## 13. Reference test vectors

All examples below use `7D` for private prototype purposes and channel `00` (MIDI channel 1). Hex byte sequences include `F0` and `F7`.

| Meaning | Complete packet |
|:--|:--|
| READY | `F0 7D 4D 53 55 01 01 00 F7` |
| GET_STATE | `F0 7D 4D 53 55 01 02 00 F7` |
| STATE_BEGIN | `F0 7D 4D 53 55 01 03 00 F7` |
| STATE_END | `F0 7D 4D 53 55 01 04 00 F7` |
| SET_UPDATE_INTERVAL 50 ms | `F0 7D 4D 53 55 01 05 00 05 F7` |
| PC 12 | `F0 7D 4D 53 55 01 10 00 0C F7` |
| Bank MSB 0, LSB 2, PC 12 | `F0 7D 4D 53 55 01 11 00 00 02 0C F7` |
| CC 5 = 83 | `F0 7D 4D 53 55 01 20 00 05 53 F7` |
| CC14 16/48 = 8192 | `F0 7D 4D 53 55 01 21 00 10 00 40 F7` |
| NRPN7 #1234 = 99 | `F0 7D 4D 53 55 01 22 00 52 09 63 F7` |
| NRPN14 #1234 = 4096 | `F0 7D 4D 53 55 01 23 00 52 09 00 20 F7` |
| CC7_BATCH (5=15, 6=32, 7=68) | `F0 7D 4D 53 55 01 24 00 03 05 0F 06 20 07 44 F7` |
| TEMPO 120.5 BPM | `F0 7D 4D 53 55 01 30 00 35 09 F7` |
| TRANSPORT running | `F0 7D 4D 53 55 01 31 00 01 F7` |
| Extended CC 5 = 83, origin local, final | `F0 7D 4D 53 55 01 60 00 05 53 03 01 01 F7` |

Tests MUST additionally exercise a test-only three-byte manufacturer ID, arbitrary fragmentation, real-time MIDI bytes interleaved inside an MSU SysEx stream, malformed length, illegal channel, unknown opcode/version, absent signature, ordinary unrelated SysEx, batch count mismatch, and packets exceeding 32 bytes. Test-only manufacturer IDs are not authorization to transmit under an ID assigned to someone else on a live MIDI connection.

## 14. Retrofit considerations

A firmware-only implementation is feasible where the device can transmit arbitrary MIDI SysEx, can accept MSU management SysEx requests, and can obtain public MIDI values from its existing parameter/control paths. A manufacturer may implement only basic PC or CC7 emission initially and add other defined families later without changing the envelope.

Hardware and firmware differences matter. Some devices have MIDI IN but no programmable MIDI OUT; some provide a hardware THRU; some process SysEx on USB but not on TRS/DIN. A unidirectional path can send notifications but cannot service `GET_STATE` unless it also has a receive path. A firmware implementation SHOULD allow telemetry to be disabled and SHOULD document which physical MIDI outputs can carry MSU.

Conventional MIDI mirroring of the same CC/PC events MAY be provided as a separate, explicitly enabled compatibility mode. It is not MSU and should not be enabled silently, since legacy receivers may execute the mirrored MIDI commands. Mirroring does not provide MSU's distinction between a command and a state notification.

## 15. Known limitations and open questions

Version 1 intentionally excludes device-specific snapshots/scenes, explicit nonstandard CC14 pairs, atomic state snapshots, per-unit unique IDs, true subscription management, acknowledgements, retransmission, reliable detection of a physically reinserted DIN cable, and looper-specific multidimensional transport state. Devices that need these facilities may continue using proprietary editor protocols or an established higher-level mechanism.

The cross-manufacturer MSU parsing rule is an experimental interoperability choice; it must be implemented only by willing participants. There may be manufacturer-private SysEx formats that happen to begin with `4D 53 55 01`. An MSU receiver avoids acting on ambiguous packets by requiring exact framing, defined opcode, defined length, and configured port/channel. Before publication of version 1 as stable, implementers SHOULD examine collision risks and assess whether an additional protocol-discriminator byte is warranted.

The update-interval request uses last-request-wins behavior on shared MIDI paths. There is no per-controller arbitration. A device that does not support rate configuration may silently ignore the request. No response timeout or retry semantics beyond user-initiated reconciliation are specified.

## 16. References

- MIDI Manufacturers Association, *MIDI 1.0 Detailed Specification* (System Exclusive and System Real-Time sections).
- The MIDI Association, [SysEx ID policy, 15 October 2025](https://midi.org/new-midi-association-sysex-id-policies-as-of-oct-15-2025).
- The MIDI Association, [MIDI specifications and reference tables](https://midi.org/specs).
- Bradner, [RFC 2119](https://www.rfc-editor.org/rfc/rfc2119); Leiba, [RFC 8174](https://www.rfc-editor.org/rfc/rfc8174).

---

*Editorial note: Draft 0.3 is intentionally provisional. No device is known to implement MSU. The protocol name, magic bytes, wire version, and opcode registry should be treated as frozen only after reference implementations and interoperability tests are reviewed.*
