#ifndef MSU_H
#define MSU_H

#include <stddef.h>
#include <stdint.h>

/* MIDI State Update draft 0.3 wire codec.
   Caller-owned buffers, no heap, no transmission.

   msu_parser_feed() is capped at 32 bytes and drops anything longer. It must
   not replace a device parser that also receives editor or firmware SysEx.
   Feed those finished messages to msu_decode(). */

#define MSU_MAX_PACKET 32
#define MSU_BATCH_MAX 10
#define MSU_VERSION 0x01

typedef enum {
  MSU_OK = 0,
  MSU_ERR_NULL = 1,
  MSU_ERR_BUFFER = 2,
  MSU_ERR_ID = 3,
  MSU_ERR_SIGNATURE = 4,
  MSU_ERR_VERSION = 5,
  MSU_ERR_OPCODE = 6,
  MSU_ERR_LENGTH = 7,
  MSU_ERR_CHANNEL = 8,
  MSU_ERR_RANGE = 9,
  MSU_ERR_OPTIONS = 10,
  MSU_ERR_OVERSIZE = 11,
  MSU_ERR_FRAMING = 12
} msu_status_t;

enum {
  MSU_READY = 0x01,
  MSU_GET_STATE = 0x02,
  MSU_STATE_BEGIN = 0x03,
  MSU_STATE_END = 0x04,
  MSU_SET_UPDATE_INTERVAL = 0x05,
  MSU_PC = 0x10,
  MSU_BANKED_PC = 0x11,
  MSU_CC7 = 0x20,
  MSU_CC14 = 0x21,
  MSU_NRPN7 = 0x22,
  MSU_NRPN14 = 0x23,
  MSU_CC7_BATCH = 0x24,
  MSU_TEMPO = 0x30,
  MSU_TRANSPORT = 0x31,
  MSU_EXT_BIT = 0x40
};

enum {
  MSU_OPT_ORIGIN = 0x01,
  MSU_OPT_FLAGS = 0x02,
  MSU_OPT_KNOWN = 0x03
};

enum {
  MSU_ORIGIN_UNKNOWN = 0x00,
  MSU_ORIGIN_LOCAL = 0x01,
  MSU_ORIGIN_MIDI = 0x02,
  MSU_ORIGIN_PRESET = 0x03,
  MSU_ORIGIN_EDITOR = 0x04,
  MSU_ORIGIN_EXPRESSION = 0x05,
  MSU_ORIGIN_AUTOMATION = 0x06,
  MSU_ORIGIN_CLOCK = 0x07
};

enum {
  MSU_FLAG_FINAL = 0x01,
  MSU_FLAG_APPROX = 0x02
};

enum {
  MSU_TRANSPORT_STOPPED = 0x00,
  MSU_TRANSPORT_RUNNING = 0x01,
  MSU_TRANSPORT_PAUSED = 0x02
};

typedef struct {
  uint8_t cc;
  uint8_t value;
} msu_cc7_item_t;

typedef struct {
  uint8_t id[3];
  uint8_t id_len;   /* 1 or 3 */
  uint8_t type;     /* base opcode, without MSU_EXT_BIT */
  uint8_t channel;  /* 0-15 */
  uint8_t extended; /* 1 when the metadata form is present */
  uint8_t options;  /* raw OPTIONS byte when extended */
  uint8_t origin;   /* raw; valid when options & MSU_OPT_ORIGIN */
  uint8_t flags;    /* raw; valid when options & MSU_OPT_FLAGS */
  union {
    uint8_t program;
    struct {
      uint8_t bank_msb;
      uint8_t bank_lsb;
      uint8_t program;
    } banked_pc;
    struct {
      uint8_t cc;
      uint8_t value;
    } cc7;
    struct {
      uint8_t cc_msb;
      uint16_t value;
    } cc14;
    struct {
      uint16_t nrpn;
      uint8_t value;
    } nrpn7;
    struct {
      uint16_t nrpn;
      uint16_t value;
    } nrpn14;
    struct {
      uint8_t count;
      msu_cc7_item_t item[MSU_BATCH_MAX];
    } batch;
    uint16_t tempo_tenths;
    uint8_t transport;
    uint8_t period_10ms;
  } u;
} msu_message_t;

/* Writes one complete SysEx, or writes nothing on failure.
   dest == NULL reports the encoded size after a successful validation. */
msu_status_t msu_encode(const msu_message_t *msg, uint8_t *dest, size_t cap,
  size_t *out_len);

/* Decodes one complete packet including F0 and F7.
   On failure *msg is left unchanged. */
msu_status_t msu_decode(const uint8_t *src, size_t len, msu_message_t *msg);

enum {
  MSU_PARSER_IDLE = 0,
  MSU_PARSER_SYSEX = 1,
  MSU_PARSER_DISCARD = 2
};

typedef struct {
  uint8_t buf[MSU_MAX_PACKET];
  uint8_t len;
  uint8_t state;
} msu_parser_t;

typedef enum {
  MSU_FEED_NONE = 0,
  MSU_FEED_MESSAGE = 1,
  MSU_FEED_REALTIME = 2,
  MSU_FEED_BYTE = 3,
  MSU_FEED_IGNORED_SYSEX = 4
} msu_feed_t;

void msu_parser_init(msu_parser_t *parser);

/* One MIDI byte. A valid MSU message is delivered only after F7.
   F8-FF are reported and are not stored. Any other status aborts an open
   SysEx and is processed as itself. *out is written only for
   MSU_FEED_MESSAGE. *raw is written for realtime and ordinary bytes.
   Either pointer may be NULL. */
msu_feed_t msu_parser_feed(msu_parser_t *parser, uint8_t byte,
  msu_message_t *out, uint8_t *raw);

#endif
