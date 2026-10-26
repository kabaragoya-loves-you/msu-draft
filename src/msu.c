#include "msu.h"

/* Draft 0.3 readings, with no extra opcodes or fields:
   One-byte IDs are 01-7D. 7E and 7F are rejected. 7C stays legal on the
   wire because this draft only bans the universal IDs.
   OPTIONS 00 is accepted. Reserved OPTIONS bits are not.
   Unknown ORIGIN values and unknown FLAGS bits are preserved.
   TRANSPORT state must be 00, 01, or 02.
   Packets longer than 32 bytes are rejected.
   F8-FF, including FF, do not end SysEx. */

static void bytes_copy(uint8_t *dst, const uint8_t *src, size_t n) {
  size_t i;
  for (i = 0; i < n; i++)
    dst[i] = src[i];
}

static void msg_zero(msu_message_t *msg) {
  uint8_t *p = (uint8_t *)msg;
  size_t i;
  for (i = 0; i < sizeof *msg; i++)
    p[i] = 0;
}

static void msg_copy(msu_message_t *dst, const msu_message_t *src) {
  bytes_copy((uint8_t *)dst, (const uint8_t *)src, sizeof *dst);
}

static int u7_ok(unsigned v) {
  return v <= 0x7fu;
}

static int type_known(uint8_t type) {
  switch (type) {
    case MSU_READY:
    case MSU_GET_STATE:
    case MSU_STATE_BEGIN:
    case MSU_STATE_END:
    case MSU_SET_UPDATE_INTERVAL:
    case MSU_PC:
    case MSU_BANKED_PC:
    case MSU_CC7:
    case MSU_CC14:
    case MSU_NRPN7:
    case MSU_NRPN14:
    case MSU_CC7_BATCH:
    case MSU_TEMPO:
    case MSU_TRANSPORT:
      return 1;
    default:
      return 0;
  }
}

static int type_state(uint8_t type) {
  switch (type) {
    case MSU_PC:
    case MSU_BANKED_PC:
    case MSU_CC7:
    case MSU_CC14:
    case MSU_NRPN7:
    case MSU_NRPN14:
    case MSU_CC7_BATCH:
    case MSU_TEMPO:
    case MSU_TRANSPORT:
      return 1;
    default:
      return 0;
  }
}

static msu_status_t id_check(const uint8_t *id, uint8_t id_len) {
  if (id_len == 1) {
    if (id[0] < 0x01 || id[0] > 0x7d)
      return MSU_ERR_ID;
    return MSU_OK;
  }
  if (id_len != 3)
    return MSU_ERR_ID;
  if (id[0] != 0x00 || !u7_ok(id[1]) || !u7_ok(id[2]))
    return MSU_ERR_ID;
  return MSU_OK;
}

static void put14(uint8_t *p, uint16_t value) {
  p[0] = (uint8_t)(value & 0x7fu);
  p[1] = (uint8_t)((value >> 7) & 0x7fu);
}

static uint16_t get14(uint8_t lo, uint8_t hi) {
  return (uint16_t)(lo | ((uint16_t)hi << 7));
}

static msu_status_t fields_ok(const msu_message_t *msg, uint8_t *body) {
  uint8_t i;
  if (msg->channel > 0x0f)
    return MSU_ERR_CHANNEL;
  switch (msg->type) {
    case MSU_READY:
    case MSU_GET_STATE:
    case MSU_STATE_BEGIN:
    case MSU_STATE_END:
      *body = 1;
      return MSU_OK;
    case MSU_SET_UPDATE_INTERVAL:
      if (!u7_ok(msg->u.period_10ms))
        return MSU_ERR_RANGE;
      *body = 2;
      return MSU_OK;
    case MSU_PC:
      if (!u7_ok(msg->u.program))
        return MSU_ERR_RANGE;
      *body = 2;
      return MSU_OK;
    case MSU_TRANSPORT:
      if (msg->u.transport > MSU_TRANSPORT_PAUSED)
        return MSU_ERR_RANGE;
      *body = 2;
      return MSU_OK;
    case MSU_CC7:
      if (!u7_ok(msg->u.cc7.cc) || !u7_ok(msg->u.cc7.value))
        return MSU_ERR_RANGE;
      *body = 3;
      return MSU_OK;
    case MSU_TEMPO:
      if (msg->u.tempo_tenths > 16383u)
        return MSU_ERR_RANGE;
      *body = 3;
      return MSU_OK;
    case MSU_BANKED_PC:
      if (!u7_ok(msg->u.banked_pc.bank_msb) ||
        !u7_ok(msg->u.banked_pc.bank_lsb) ||
        !u7_ok(msg->u.banked_pc.program))
        return MSU_ERR_RANGE;
      *body = 4;
      return MSU_OK;
    case MSU_CC14:
      if (msg->u.cc14.cc_msb > 0x1f || msg->u.cc14.value > 16383u)
        return MSU_ERR_RANGE;
      *body = 4;
      return MSU_OK;
    case MSU_NRPN7:
      if (msg->u.nrpn7.nrpn > 16383u || !u7_ok(msg->u.nrpn7.value))
        return MSU_ERR_RANGE;
      *body = 4;
      return MSU_OK;
    case MSU_NRPN14:
      if (msg->u.nrpn14.nrpn > 16383u || msg->u.nrpn14.value > 16383u)
        return MSU_ERR_RANGE;
      *body = 5;
      return MSU_OK;
    case MSU_CC7_BATCH:
      if (msg->u.batch.count < 1 || msg->u.batch.count > MSU_BATCH_MAX)
        return MSU_ERR_RANGE;
      for (i = 0; i < msg->u.batch.count; i++) {
        if (!u7_ok(msg->u.batch.item[i].cc) ||
          !u7_ok(msg->u.batch.item[i].value))
          return MSU_ERR_RANGE;
      }
      *body = (uint8_t)(2u + 2u * msg->u.batch.count);
      return MSU_OK;
    default:
      return MSU_ERR_OPCODE;
  }
}

static msu_status_t meta_ok(const msu_message_t *msg, uint8_t *meta) {
  uint8_t n = 0;
  if (!msg->extended) {
    *meta = 0;
    return MSU_OK;
  }
  if (!type_state(msg->type))
    return MSU_ERR_OPCODE;
  if (!u7_ok(msg->options) || (msg->options & (uint8_t)~MSU_OPT_KNOWN) != 0)
    return MSU_ERR_OPTIONS;
  n = 1;
  if (msg->options & MSU_OPT_ORIGIN) {
    if (!u7_ok(msg->origin))
      return MSU_ERR_RANGE;
    n++;
  }
  if (msg->options & MSU_OPT_FLAGS) {
    if (!u7_ok(msg->flags))
      return MSU_ERR_RANGE;
    n++;
  }
  *meta = n;
  return MSU_OK;
}

static msu_status_t prepare(const msu_message_t *msg, uint8_t *body,
    uint8_t *meta, uint16_t *total) {
  msu_status_t st;
  if (!type_known(msg->type))
    return MSU_ERR_OPCODE;
  st = id_check(msg->id, msg->id_len);
  if (st != MSU_OK)
    return st;
  st = fields_ok(msg, body);
  if (st != MSU_OK)
    return st;
  st = meta_ok(msg, meta);
  if (st != MSU_OK)
    return st;
  *total = (uint16_t)(7u + msg->id_len + *body + *meta);
  if (*total > MSU_MAX_PACKET)
    return MSU_ERR_OVERSIZE;
  return MSU_OK;
}

static void write_payload(uint8_t *p, const msu_message_t *msg) {
  uint8_t i;
  switch (msg->type) {
    case MSU_SET_UPDATE_INTERVAL:
      p[0] = msg->u.period_10ms;
      break;
    case MSU_PC:
      p[0] = msg->u.program;
      break;
    case MSU_TRANSPORT:
      p[0] = msg->u.transport;
      break;
    case MSU_CC7:
      p[0] = msg->u.cc7.cc;
      p[1] = msg->u.cc7.value;
      break;
    case MSU_TEMPO:
      put14(p, msg->u.tempo_tenths);
      break;
    case MSU_BANKED_PC:
      p[0] = msg->u.banked_pc.bank_msb;
      p[1] = msg->u.banked_pc.bank_lsb;
      p[2] = msg->u.banked_pc.program;
      break;
    case MSU_CC14:
      p[0] = msg->u.cc14.cc_msb;
      put14(p + 1, msg->u.cc14.value);
      break;
    case MSU_NRPN7:
      put14(p, msg->u.nrpn7.nrpn);
      p[2] = msg->u.nrpn7.value;
      break;
    case MSU_NRPN14:
      put14(p, msg->u.nrpn14.nrpn);
      put14(p + 2, msg->u.nrpn14.value);
      break;
    case MSU_CC7_BATCH:
      p[0] = msg->u.batch.count;
      for (i = 0; i < msg->u.batch.count; i++) {
        p[1u + 2u * i] = msg->u.batch.item[i].cc;
        p[2u + 2u * i] = msg->u.batch.item[i].value;
      }
      break;
    default:
      break;
  }
}

msu_status_t msu_encode(const msu_message_t *msg, uint8_t *dest, size_t cap,
    size_t *out_len) {
  uint8_t body = 0;
  uint8_t meta = 0;
  uint16_t total = 0;
  uint8_t pkt[MSU_MAX_PACKET];
  uint8_t n = 0;
  uint8_t i;
  uint8_t payload_at;
  msu_status_t st;
  if (!msg || !out_len)
    return MSU_ERR_NULL;
  st = prepare(msg, &body, &meta, &total);
  if (st != MSU_OK)
    return st;
  if (!dest) {
    *out_len = total;
    return MSU_OK;
  }
  if (cap < total) {
    *out_len = total;
    return MSU_ERR_BUFFER;
  }
  pkt[n++] = 0xf0;
  for (i = 0; i < msg->id_len; i++)
    pkt[n++] = msg->id[i];
  pkt[n++] = 0x4d;
  pkt[n++] = 0x53;
  pkt[n++] = 0x55;
  pkt[n++] = MSU_VERSION;
  pkt[n++] = (uint8_t)(msg->type | (msg->extended ? MSU_EXT_BIT : 0));
  pkt[n++] = msg->channel;
  payload_at = n;
  write_payload(pkt + n, msg);
  n = (uint8_t)(payload_at + body - 1u);
  if (msg->extended) {
    pkt[n++] = msg->options;
    if (msg->options & MSU_OPT_ORIGIN)
      pkt[n++] = msg->origin;
    if (msg->options & MSU_OPT_FLAGS)
      pkt[n++] = msg->flags;
  }
  pkt[n++] = 0xf7;
  if (n != total)
    return MSU_ERR_LENGTH;
  bytes_copy(dest, pkt, n);
  *out_len = n;
  return MSU_OK;
}

static msu_status_t take_body(uint8_t base, const uint8_t *pay, size_t pay_len,
    uint8_t *body) {
  uint8_t count;
  switch (base) {
    case MSU_READY:
    case MSU_GET_STATE:
    case MSU_STATE_BEGIN:
    case MSU_STATE_END:
      *body = 1;
      return MSU_OK;
    case MSU_SET_UPDATE_INTERVAL:
    case MSU_PC:
    case MSU_TRANSPORT:
      *body = 2;
      return MSU_OK;
    case MSU_CC7:
    case MSU_TEMPO:
      *body = 3;
      return MSU_OK;
    case MSU_BANKED_PC:
    case MSU_CC14:
    case MSU_NRPN7:
      *body = 4;
      return MSU_OK;
    case MSU_NRPN14:
      *body = 5;
      return MSU_OK;
    case MSU_CC7_BATCH:
      if (pay_len < 2)
        return MSU_ERR_LENGTH;
      count = pay[1];
      if (count < 1 || count > MSU_BATCH_MAX)
        return MSU_ERR_RANGE;
      *body = (uint8_t)(2u + 2u * count);
      return MSU_OK;
    default:
      return MSU_ERR_OPCODE;
  }
}

static msu_status_t store_payload(msu_message_t *msg, const uint8_t *pay) {
  uint8_t i;
  switch (msg->type) {
    case MSU_SET_UPDATE_INTERVAL:
      msg->u.period_10ms = pay[1];
      return MSU_OK;
    case MSU_PC:
      msg->u.program = pay[1];
      return MSU_OK;
    case MSU_TRANSPORT:
      if (pay[1] > MSU_TRANSPORT_PAUSED)
        return MSU_ERR_RANGE;
      msg->u.transport = pay[1];
      return MSU_OK;
    case MSU_CC7:
      msg->u.cc7.cc = pay[1];
      msg->u.cc7.value = pay[2];
      return MSU_OK;
    case MSU_TEMPO:
      msg->u.tempo_tenths = get14(pay[1], pay[2]);
      return MSU_OK;
    case MSU_BANKED_PC:
      msg->u.banked_pc.bank_msb = pay[1];
      msg->u.banked_pc.bank_lsb = pay[2];
      msg->u.banked_pc.program = pay[3];
      return MSU_OK;
    case MSU_CC14:
      if (pay[1] > 0x1f)
        return MSU_ERR_RANGE;
      msg->u.cc14.cc_msb = pay[1];
      msg->u.cc14.value = get14(pay[2], pay[3]);
      return MSU_OK;
    case MSU_NRPN7:
      msg->u.nrpn7.nrpn = get14(pay[1], pay[2]);
      msg->u.nrpn7.value = pay[3];
      return MSU_OK;
    case MSU_NRPN14:
      msg->u.nrpn14.nrpn = get14(pay[1], pay[2]);
      msg->u.nrpn14.value = get14(pay[3], pay[4]);
      return MSU_OK;
    case MSU_CC7_BATCH:
      msg->u.batch.count = pay[1];
      for (i = 0; i < pay[1]; i++) {
        msg->u.batch.item[i].cc = pay[2u + 2u * i];
        msg->u.batch.item[i].value = pay[3u + 2u * i];
      }
      return MSU_OK;
    default:
      return MSU_OK;
  }
}

msu_status_t msu_decode(const uint8_t *src, size_t len, msu_message_t *msg) {
  msu_message_t tmp;
  size_t i;
  size_t sig;
  size_t pay_at;
  size_t pay_len;
  size_t at;
  uint8_t id_len;
  uint8_t opcode;
  uint8_t base;
  uint8_t extended;
  uint8_t body = 0;
  uint8_t meta;
  uint8_t options = 0;
  const uint8_t *pay;
  msu_status_t st;
  if (!src || !msg)
    return MSU_ERR_NULL;
  if (len > MSU_MAX_PACKET)
    return MSU_ERR_OVERSIZE;
  if (len < 2 || src[0] != 0xf0 || src[len - 1] != 0xf7)
    return MSU_ERR_FRAMING;
  for (i = 1; i + 1 < len; i++) {
    if (src[i] > 0x7f)
      return MSU_ERR_FRAMING;
  }
  if (len < 3)
    return MSU_ERR_LENGTH;
  if (src[1] == 0x7e || src[1] == 0x7f)
    return MSU_ERR_ID;
  if (src[1] == 0x00) {
    if (len < 5)
      return MSU_ERR_LENGTH;
    id_len = 3;
  } else {
    id_len = 1;
  }
  sig = (size_t)1u + id_len;
  if (sig + 2 >= len - 1)
    return MSU_ERR_LENGTH;
  if (src[sig] != 0x4d || src[sig + 1] != 0x53 || src[sig + 2] != 0x55)
    return MSU_ERR_SIGNATURE;
  if (sig + 3 >= len - 1)
    return MSU_ERR_LENGTH;
  if (src[sig + 3] != MSU_VERSION)
    return MSU_ERR_VERSION;
  if (sig + 4 >= len - 1)
    return MSU_ERR_LENGTH;
  opcode = src[sig + 4];
  extended = (uint8_t)((opcode & MSU_EXT_BIT) != 0);
  base = (uint8_t)(opcode & (uint8_t)~MSU_EXT_BIT);
  if (!type_known(base) || (extended && !type_state(base)))
    return MSU_ERR_OPCODE;
  pay_at = sig + 5u;
  pay = src + pay_at;
  pay_len = len - pay_at - 1u;
  st = take_body(base, pay, pay_len, &body);
  if (st != MSU_OK)
    return st;
  if (extended) {
    if (pay_len < (size_t)body + 1u)
      return MSU_ERR_LENGTH;
    options = pay[body];
    if ((options & (uint8_t)~MSU_OPT_KNOWN) != 0)
      return MSU_ERR_OPTIONS;
    meta = 1;
    if (options & MSU_OPT_ORIGIN)
      meta++;
    if (options & MSU_OPT_FLAGS)
      meta++;
  } else {
    meta = 0;
  }
  if (pay_len != (size_t)body + meta)
    return MSU_ERR_LENGTH;
  if (pay_len < 1 || pay[0] > 0x0f)
    return MSU_ERR_CHANNEL;
  msg_zero(&tmp);
  tmp.id_len = id_len;
  for (i = 0; i < id_len; i++)
    tmp.id[i] = src[1u + i];
  tmp.type = base;
  tmp.channel = pay[0];
  tmp.extended = extended;
  tmp.options = options;
  st = store_payload(&tmp, pay);
  if (st != MSU_OK)
    return st;
  if (extended) {
    at = (size_t)body + 1u;
    if (options & MSU_OPT_ORIGIN)
      tmp.origin = pay[at++];
    if (options & MSU_OPT_FLAGS)
      tmp.flags = pay[at];
  }
  msg_copy(msg, &tmp);
  return MSU_OK;
}

void msu_parser_init(msu_parser_t *parser) {
  if (!parser)
    return;
  parser->len = 0;
  parser->state = MSU_PARSER_IDLE;
}

msu_feed_t msu_parser_feed(msu_parser_t *parser, uint8_t byte,
    msu_message_t *out, uint8_t *raw) {
  if (!parser)
    return MSU_FEED_NONE;
  for (;;) {
    if (byte >= 0xf8) {
      if (raw)
        *raw = byte;
      return MSU_FEED_REALTIME;
    }
    if (parser->state == MSU_PARSER_DISCARD) {
      if (byte == 0xf7) {
        parser->state = MSU_PARSER_IDLE;
        parser->len = 0;
        return MSU_FEED_IGNORED_SYSEX;
      }
      if (byte >= 0x80) {
        parser->state = MSU_PARSER_IDLE;
        parser->len = 0;
        continue;
      }
      return MSU_FEED_NONE;
    }
    if (parser->state == MSU_PARSER_IDLE) {
      if (byte == 0xf0) {
        parser->buf[0] = 0xf0;
        parser->len = 1;
        parser->state = MSU_PARSER_SYSEX;
        return MSU_FEED_NONE;
      }
      if (raw)
        *raw = byte;
      return MSU_FEED_BYTE;
    }
    if (byte == 0xf7) {
      msu_message_t tmp;
      msu_status_t st;
      if (parser->len >= MSU_MAX_PACKET) {
        parser->state = MSU_PARSER_IDLE;
        parser->len = 0;
        return MSU_FEED_IGNORED_SYSEX;
      }
      parser->buf[parser->len++] = 0xf7;
      st = msu_decode(parser->buf, parser->len, &tmp);
      parser->state = MSU_PARSER_IDLE;
      parser->len = 0;
      if (st != MSU_OK)
        return MSU_FEED_IGNORED_SYSEX;
      if (out)
        msg_copy(out, &tmp);
      return MSU_FEED_MESSAGE;
    }
    if (byte >= 0x80) {
      parser->state = MSU_PARSER_IDLE;
      parser->len = 0;
      continue;
    }
    if (parser->len >= MSU_MAX_PACKET - 1) {
      parser->state = MSU_PARSER_DISCARD;
      parser->len = 0;
      return MSU_FEED_NONE;
    }
    parser->buf[parser->len++] = byte;
    return MSU_FEED_NONE;
  }
}
