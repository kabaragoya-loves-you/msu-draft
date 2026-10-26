#include "sim.h"

#include <stdio.h>
#include <string.h>

static int g_passed;
static int g_failed;

static void check(int cond, const char *file, int line, const char *expr) {
  if (cond) {
    g_passed++;
    return;
  }
  printf("FAIL %s:%d: %s\n", file, line, expr);
  g_failed++;
}

static void check_st(msu_status_t got, msu_status_t exp, const char *file,
    int line) {
  if (got == exp) {
    g_passed++;
    return;
  }
  printf("FAIL %s:%d: status got %d expected %d\n", file, line, (int)got,
    (int)exp);
  g_failed++;
}

#define CHECK(cond) check((cond) ? 1 : 0, __FILE__, __LINE__, #cond)
#define CHECK_ST(got, exp) check_st((got), (exp), __FILE__, __LINE__)

static void check_bytes(const uint8_t *got, size_t got_len,
    const uint8_t *exp, size_t exp_len, const char *file, int line) {
  size_t i;
  if (got_len == exp_len && memcmp(got, exp, exp_len) == 0) {
    g_passed++;
    return;
  }
  printf("FAIL %s:%d: bytes got", file, line);
  for (i = 0; i < got_len; i++)
    printf(" %02X", (unsigned)got[i]);
  printf(" expected");
  for (i = 0; i < exp_len; i++)
    printf(" %02X", (unsigned)exp[i]);
  printf("\n");
  g_failed++;
}

#define CHECK_BYTES(got, got_len, exp, exp_len) \
  check_bytes((got), (got_len), (exp), (exp_len), __FILE__, __LINE__)

static void id7d(msu_message_t *msg, uint8_t type, uint8_t channel) {
  memset(msg, 0, sizeof *msg);
  msg->id[0] = 0x7d;
  msg->id_len = 1;
  msg->type = type;
  msg->channel = channel;
}

static void expect_roundtrip(const msu_message_t *msg, const uint8_t *exp,
    size_t exp_len) {
  uint8_t buf[MSU_MAX_PACKET];
  uint8_t stained[MSU_MAX_PACKET];
  msu_message_t out;
  size_t n = 0;
  size_t i;
  memset(stained, 0xa5, sizeof stained);
  CHECK_ST(msu_encode(msg, NULL, 0, &n), MSU_OK);
  CHECK(n == exp_len);
  memset(stained, 0xa5, sizeof stained);
  CHECK_ST(msu_encode(msg, stained, 2, &n), MSU_ERR_BUFFER);
  CHECK(n == exp_len);
  for (i = 0; i < sizeof stained; i++) {
    if (stained[i] != 0xa5)
      break;
  }
  CHECK(i == sizeof stained);
  n = 0;
  CHECK_ST(msu_encode(msg, buf, sizeof buf, &n), MSU_OK);
  CHECK_BYTES(buf, n, exp, exp_len);
  memset(&out, 0x5a, sizeof out);
  CHECK_ST(msu_decode(exp, exp_len, &out), MSU_OK);
  n = 0;
  CHECK_ST(msu_encode(&out, buf, sizeof buf, &n), MSU_OK);
  CHECK_BYTES(buf, n, exp, exp_len);
  CHECK(out.id_len == msg->id_len);
  CHECK(out.id[0] == msg->id[0]);
  CHECK(out.type == msg->type);
  CHECK(out.channel == msg->channel);
  CHECK(out.extended == msg->extended);
  CHECK(out.options == msg->options);
}

static void test_section13(void) {
  msu_message_t msg;
  msu_message_t out;
  static const uint8_t ready[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x01, 0x00, 0xF7
  };
  static const uint8_t get_state[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x02, 0x00, 0xF7
  };
  static const uint8_t begin[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x03, 0x00, 0xF7
  };
  static const uint8_t end[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x04, 0x00, 0xF7
  };
  static const uint8_t interval[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x05, 0x00, 0x05, 0xF7
  };
  static const uint8_t pc[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x10, 0x00, 0x0C, 0xF7
  };
  static const uint8_t banked[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x11, 0x00, 0x00, 0x02, 0x0C, 0xF7
  };
  static const uint8_t cc7[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x20, 0x00, 0x05, 0x53, 0xF7
  };
  static const uint8_t cc14[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x21, 0x00, 0x10, 0x00, 0x40, 0xF7
  };
  static const uint8_t nrpn7[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x22, 0x00, 0x52, 0x09, 0x63, 0xF7
  };
  static const uint8_t nrpn14[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x23, 0x00, 0x52, 0x09, 0x00, 0x20,
    0xF7
  };
  static const uint8_t batch[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x24, 0x00, 0x03, 0x05, 0x0F, 0x06,
    0x20, 0x07, 0x44, 0xF7
  };
  static const uint8_t tempo[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x30, 0x00, 0x35, 0x09, 0xF7
  };
  static const uint8_t transport[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x31, 0x00, 0x01, 0xF7
  };
  static const uint8_t ext[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x60, 0x00, 0x05, 0x53, 0x03, 0x01,
    0x01, 0xF7
  };

  id7d(&msg, MSU_READY, 0);
  expect_roundtrip(&msg, ready, sizeof ready);
  id7d(&msg, MSU_GET_STATE, 0);
  expect_roundtrip(&msg, get_state, sizeof get_state);
  id7d(&msg, MSU_STATE_BEGIN, 0);
  expect_roundtrip(&msg, begin, sizeof begin);
  id7d(&msg, MSU_STATE_END, 0);
  expect_roundtrip(&msg, end, sizeof end);
  id7d(&msg, MSU_SET_UPDATE_INTERVAL, 0);
  msg.u.period_10ms = 5;
  expect_roundtrip(&msg, interval, sizeof interval);
  CHECK_ST(msu_decode(interval, sizeof interval, &out), MSU_OK);
  CHECK(out.u.period_10ms == 5);

  id7d(&msg, MSU_PC, 0);
  msg.u.program = 0x0c;
  expect_roundtrip(&msg, pc, sizeof pc);
  CHECK_ST(msu_decode(pc, sizeof pc, &out), MSU_OK);
  CHECK(out.u.program == 0x0c);

  id7d(&msg, MSU_BANKED_PC, 0);
  msg.u.banked_pc.bank_msb = 0;
  msg.u.banked_pc.bank_lsb = 2;
  msg.u.banked_pc.program = 0x0c;
  expect_roundtrip(&msg, banked, sizeof banked);
  CHECK_ST(msu_decode(banked, sizeof banked, &out), MSU_OK);
  CHECK(out.u.banked_pc.bank_msb == 0);
  CHECK(out.u.banked_pc.bank_lsb == 2);
  CHECK(out.u.banked_pc.program == 0x0c);

  id7d(&msg, MSU_CC7, 0);
  msg.u.cc7.cc = 5;
  msg.u.cc7.value = 0x53;
  expect_roundtrip(&msg, cc7, sizeof cc7);
  CHECK_ST(msu_decode(cc7, sizeof cc7, &out), MSU_OK);
  CHECK(out.u.cc7.cc == 5);
  CHECK(out.u.cc7.value == 0x53);

  id7d(&msg, MSU_CC14, 0);
  msg.u.cc14.cc_msb = 0x10;
  msg.u.cc14.value = 8192;
  expect_roundtrip(&msg, cc14, sizeof cc14);
  CHECK_ST(msu_decode(cc14, sizeof cc14, &out), MSU_OK);
  CHECK(out.u.cc14.cc_msb == 0x10);
  CHECK(out.u.cc14.value == 8192);

  id7d(&msg, MSU_NRPN7, 0);
  msg.u.nrpn7.nrpn = 1234;
  msg.u.nrpn7.value = 99;
  expect_roundtrip(&msg, nrpn7, sizeof nrpn7);
  CHECK_ST(msu_decode(nrpn7, sizeof nrpn7, &out), MSU_OK);
  CHECK(out.u.nrpn7.nrpn == 1234);
  CHECK(out.u.nrpn7.value == 99);

  id7d(&msg, MSU_NRPN14, 0);
  msg.u.nrpn14.nrpn = 1234;
  msg.u.nrpn14.value = 4096;
  expect_roundtrip(&msg, nrpn14, sizeof nrpn14);
  CHECK_ST(msu_decode(nrpn14, sizeof nrpn14, &out), MSU_OK);
  CHECK(out.u.nrpn14.nrpn == 1234);
  CHECK(out.u.nrpn14.value == 4096);

  id7d(&msg, MSU_CC7_BATCH, 0);
  msg.u.batch.count = 3;
  msg.u.batch.item[0].cc = 5;
  msg.u.batch.item[0].value = 0x0f;
  msg.u.batch.item[1].cc = 6;
  msg.u.batch.item[1].value = 0x20;
  msg.u.batch.item[2].cc = 7;
  msg.u.batch.item[2].value = 0x44;
  expect_roundtrip(&msg, batch, sizeof batch);
  CHECK_ST(msu_decode(batch, sizeof batch, &out), MSU_OK);
  CHECK(out.u.batch.count == 3);
  CHECK(out.u.batch.item[0].cc == 5 && out.u.batch.item[0].value == 0x0f);
  CHECK(out.u.batch.item[1].cc == 6 && out.u.batch.item[1].value == 0x20);
  CHECK(out.u.batch.item[2].cc == 7 && out.u.batch.item[2].value == 0x44);

  id7d(&msg, MSU_TEMPO, 0);
  msg.u.tempo_tenths = 1205;
  expect_roundtrip(&msg, tempo, sizeof tempo);
  CHECK_ST(msu_decode(tempo, sizeof tempo, &out), MSU_OK);
  CHECK(out.u.tempo_tenths == 1205);

  id7d(&msg, MSU_TRANSPORT, 0);
  msg.u.transport = MSU_TRANSPORT_RUNNING;
  expect_roundtrip(&msg, transport, sizeof transport);
  CHECK_ST(msu_decode(transport, sizeof transport, &out), MSU_OK);
  CHECK(out.u.transport == MSU_TRANSPORT_RUNNING);

  id7d(&msg, MSU_CC7, 0);
  msg.extended = 1;
  msg.options = 0x03;
  msg.origin = MSU_ORIGIN_LOCAL;
  msg.flags = MSU_FLAG_FINAL;
  msg.u.cc7.cc = 5;
  msg.u.cc7.value = 0x53;
  expect_roundtrip(&msg, ext, sizeof ext);
  CHECK_ST(msu_decode(ext, sizeof ext, &out), MSU_OK);
  CHECK(out.extended == 1);
  CHECK(out.options == 0x03);
  CHECK(out.origin == MSU_ORIGIN_LOCAL);
  CHECK(out.flags == MSU_FLAG_FINAL);
  CHECK(out.u.cc7.cc == 5 && out.u.cc7.value == 0x53);
}

static void test_ranges(void) {
  msu_message_t msg;
  msu_message_t out;
  uint8_t buf[MSU_MAX_PACKET];
  size_t n = 0;
  uint8_t i;
  static const uint8_t cc14_zero[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x21, 0x00, 0x00, 0x00, 0x00, 0xF7
  };
  static const uint8_t cc14_max[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x21, 0x00, 0x1F, 0x7F, 0x7F, 0xF7
  };
  static const uint8_t tempo_zero[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x30, 0x00, 0x00, 0x00, 0xF7
  };
  static const uint8_t tempo_max[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x30, 0x00, 0x7F, 0x7F, 0xF7
  };
  static const uint8_t nrpn_zero[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x23, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xF7
  };
  static const uint8_t nrpn_max[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x23, 0x00, 0x7F, 0x7F, 0x7F, 0x7F,
    0xF7
  };
  static const uint8_t batch1[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x24, 0x00, 0x01, 0x05, 0x00, 0xF7
  };
  static const uint8_t origin_only[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x50, 0x00, 0x0C, 0x01, 0x01, 0xF7
  };
  static const uint8_t flags_only[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x60, 0x00, 0x05, 0x53, 0x02, 0x05,
    0xF7
  };
  static const uint8_t options_zero[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x60, 0x00, 0x05, 0x53, 0x00, 0xF7
  };
  static const uint8_t unknown_origin[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x60, 0x00, 0x05, 0x53, 0x01, 0x7F,
    0xF7
  };
  static const uint8_t ready3[] = {
    0xF0, 0x00, 0x7D, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x01, 0x00, 0xF7
  };

  id7d(&msg, MSU_CC14, 0);
  msg.u.cc14.cc_msb = 0;
  msg.u.cc14.value = 0;
  expect_roundtrip(&msg, cc14_zero, sizeof cc14_zero);
  msg.u.cc14.cc_msb = 0x1f;
  msg.u.cc14.value = 16383;
  expect_roundtrip(&msg, cc14_max, sizeof cc14_max);
  id7d(&msg, MSU_TEMPO, 0);
  msg.u.tempo_tenths = 0;
  expect_roundtrip(&msg, tempo_zero, sizeof tempo_zero);
  msg.u.tempo_tenths = 16383;
  expect_roundtrip(&msg, tempo_max, sizeof tempo_max);
  id7d(&msg, MSU_NRPN14, 0);
  msg.u.nrpn14.nrpn = 0;
  msg.u.nrpn14.value = 0;
  expect_roundtrip(&msg, nrpn_zero, sizeof nrpn_zero);
  msg.u.nrpn14.nrpn = 16383;
  msg.u.nrpn14.value = 16383;
  expect_roundtrip(&msg, nrpn_max, sizeof nrpn_max);

  id7d(&msg, MSU_CC7_BATCH, 0);
  msg.u.batch.count = 1;
  msg.u.batch.item[0].cc = 5;
  msg.u.batch.item[0].value = 0;
  expect_roundtrip(&msg, batch1, sizeof batch1);

  id7d(&msg, MSU_CC7_BATCH, 0);
  msg.u.batch.count = 10;
  for (i = 0; i < 10; i++) {
    msg.u.batch.item[i].cc = i;
    msg.u.batch.item[i].value = (uint8_t)(i + 1u);
  }
  n = 0;
  CHECK_ST(msu_encode(&msg, buf, sizeof buf, &n), MSU_OK);
  CHECK(n == 30);
  CHECK(buf[0] == 0xf0 && buf[1] == 0x7d && buf[6] == 0x24);
  CHECK(buf[8] == 10);
  CHECK(buf[n - 1] == 0xf7);
  CHECK_ST(msu_decode(buf, n, &out), MSU_OK);
  CHECK(out.u.batch.count == 10);
  CHECK(out.u.batch.item[9].cc == 9 && out.u.batch.item[9].value == 10);

  msg.id_len = 3;
  msg.id[0] = 0x00;
  msg.id[1] = 0x7d;
  msg.id[2] = 0x7d;
  n = 0;
  CHECK_ST(msu_encode(&msg, buf, sizeof buf, &n), MSU_OK);
  CHECK(n == 32);
  CHECK_ST(msu_decode(buf, n, &out), MSU_OK);
  CHECK(out.id_len == 3 && out.id[1] == 0x7d && out.id[2] == 0x7d);
  msg.extended = 1;
  msg.options = MSU_OPT_ORIGIN;
  msg.origin = 1;
  CHECK_ST(msu_encode(&msg, buf, sizeof buf, &n), MSU_ERR_OVERSIZE);

  id7d(&msg, MSU_PC, 0);
  msg.u.program = 0x0c;
  msg.extended = 1;
  msg.options = MSU_OPT_ORIGIN;
  msg.origin = MSU_ORIGIN_LOCAL;
  expect_roundtrip(&msg, origin_only, sizeof origin_only);
  CHECK_ST(msu_decode(origin_only, sizeof origin_only, &out), MSU_OK);
  CHECK(out.extended && out.options == 0x01 && out.origin == 0x01);
  CHECK(out.flags == 0);

  id7d(&msg, MSU_CC7, 0);
  msg.u.cc7.cc = 5;
  msg.u.cc7.value = 0x53;
  msg.extended = 1;
  msg.options = MSU_OPT_FLAGS;
  msg.flags = 0x05;
  expect_roundtrip(&msg, flags_only, sizeof flags_only);
  CHECK_ST(msu_decode(flags_only, sizeof flags_only, &out), MSU_OK);
  CHECK(out.options == 0x02 && out.flags == 0x05 && out.origin == 0);

  id7d(&msg, MSU_CC7, 0);
  msg.u.cc7.cc = 5;
  msg.u.cc7.value = 0x53;
  msg.extended = 1;
  msg.options = 0x00;
  expect_roundtrip(&msg, options_zero, sizeof options_zero);

  id7d(&msg, MSU_CC7, 0);
  msg.u.cc7.cc = 5;
  msg.u.cc7.value = 0x53;
  msg.extended = 1;
  msg.options = MSU_OPT_ORIGIN;
  msg.origin = 0x7f;
  expect_roundtrip(&msg, unknown_origin, sizeof unknown_origin);

  memset(&msg, 0, sizeof msg);
  msg.id_len = 3;
  msg.id[0] = 0x00;
  msg.id[1] = 0x7d;
  msg.id[2] = 0x7d;
  msg.type = MSU_READY;
  expect_roundtrip(&msg, ready3, sizeof ready3);

  id7d(&msg, MSU_CC7, 0);
  msg.u.cc7.cc = 120;
  msg.u.cc7.value = 1;
  CHECK_ST(msu_encode(&msg, buf, sizeof buf, &n), MSU_OK);

  id7d(&msg, MSU_TRANSPORT, 0);
  msg.u.transport = MSU_TRANSPORT_STOPPED;
  CHECK_ST(msu_encode(&msg, buf, sizeof buf, &n), MSU_OK);
  msg.u.transport = MSU_TRANSPORT_PAUSED;
  CHECK_ST(msu_encode(&msg, buf, sizeof buf, &n), MSU_OK);
  msg.u.transport = 3;
  CHECK_ST(msu_encode(&msg, buf, sizeof buf, &n), MSU_ERR_RANGE);

  id7d(&msg, MSU_CC14, 0);
  msg.u.cc14.cc_msb = 32;
  msg.u.cc14.value = 1;
  CHECK_ST(msu_encode(&msg, buf, sizeof buf, &n), MSU_ERR_RANGE);
  msg.u.cc14.cc_msb = 0;
  msg.u.cc14.value = 16384;
  CHECK_ST(msu_encode(&msg, buf, sizeof buf, &n), MSU_ERR_RANGE);

  id7d(&msg, MSU_CC7_BATCH, 0);
  msg.u.batch.count = 0;
  CHECK_ST(msu_encode(&msg, buf, sizeof buf, &n), MSU_ERR_RANGE);
  msg.u.batch.count = 11;
  CHECK_ST(msu_encode(&msg, buf, sizeof buf, &n), MSU_ERR_RANGE);

  id7d(&msg, MSU_READY, 0);
  msg.extended = 1;
  msg.options = 0;
  CHECK_ST(msu_encode(&msg, buf, sizeof buf, &n), MSU_ERR_OPCODE);
  msg.id_len = 1;
  msg.id[0] = 0x00;
  msg.extended = 0;
  msg.type = MSU_READY;
  CHECK_ST(msu_encode(&msg, buf, sizeof buf, &n), MSU_ERR_ID);
  msg.id[0] = 0x7e;
  CHECK_ST(msu_encode(&msg, buf, sizeof buf, &n), MSU_ERR_ID);
  msg.id[0] = 0x7f;
  CHECK_ST(msu_encode(&msg, buf, sizeof buf, &n), MSU_ERR_ID);
  msg.id[0] = 0x7c;
  CHECK_ST(msu_encode(&msg, buf, sizeof buf, &n), MSU_OK);
  msg.id_len = 3;
  msg.id[0] = 0x01;
  msg.id[1] = 0x00;
  msg.id[2] = 0x00;
  CHECK_ST(msu_encode(&msg, buf, sizeof buf, &n), MSU_ERR_ID);
  msg.channel = 16;
  msg.id_len = 1;
  msg.id[0] = 0x7d;
  CHECK_ST(msu_encode(&msg, buf, sizeof buf, &n), MSU_ERR_CHANNEL);
}

static void test_reject(void) {
  msu_message_t msg;
  uint8_t big[40];
  uint8_t pkt[MSU_MAX_PACKET];
  size_t i;
  size_t n = 0;
  static const uint8_t bad_sig[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x54, 0x01, 0x01, 0x00, 0xF7
  };
  static const uint8_t bad_ver[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x02, 0x01, 0x00, 0xF7
  };
  static const uint8_t bad_op[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x06, 0x00, 0xF7
  };
  static const uint8_t ext_ready[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x41, 0x00, 0xF7
  };
  static const uint8_t unrelated[] = {
    0xF0, 0x7D, 0x01, 0x02, 0x03, 0xF7
  };
  static const uint8_t foreign[] = {
    0xF0, 0x42, 0x11, 0x22, 0x33, 0xF7
  };
  static const uint8_t uni_nonrt[] = {
    0xF0, 0x7E, 0x4D, 0x53, 0x55, 0x01, 0x01, 0x00, 0xF7
  };
  static const uint8_t uni_rt[] = {
    0xF0, 0x7F, 0x4D, 0x53, 0x55, 0x01, 0x01, 0x00, 0xF7
  };
  static const uint8_t identity[] = {
    0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7
  };
  static const uint8_t ch16[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x01, 0x10, 0xF7
  };
  static const uint8_t ch7f[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x01, 0x7F, 0xF7
  };
  static const uint8_t cc_msb[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x21, 0x00, 0x20, 0x00, 0x00, 0xF7
  };
  static const uint8_t transport3[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x31, 0x00, 0x03, 0xF7
  };
  static const uint8_t pc_long[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x10, 0x00, 0x0C, 0x00, 0xF7
  };
  static const uint8_t cc_short[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x20, 0x00, 0x05, 0xF7
  };
  static const uint8_t batch_short[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x24, 0x00, 0x03, 0x05, 0x0F, 0x06,
    0x20, 0xF7
  };
  static const uint8_t batch_long[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x24, 0x00, 0x03, 0x05, 0x0F, 0x06,
    0x20, 0x07, 0x44, 0x00, 0xF7
  };
  static const uint8_t batch0[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x24, 0x00, 0x00, 0xF7
  };
  static const uint8_t opt_bad[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x60, 0x00, 0x05, 0x53, 0x04, 0xF7
  };
  static const uint8_t opt7[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x60, 0x00, 0x05, 0x53, 0x07, 0xF7
  };
  static const uint8_t high_bit[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x01, 0x80, 0xF7
  };
  static const uint8_t empty[] = {0xF0, 0xF7};
  static const uint8_t any_id[] = {
    0xF0, 0x12, 0x4D, 0x53, 0x55, 0x01, 0x01, 0x00, 0xF7
  };
  static const uint8_t id_7c[] = {
    0xF0, 0x7C, 0x4D, 0x53, 0x55, 0x01, 0x01, 0x00, 0xF7
  };

  memset(&msg, 0x5a, sizeof msg);
  CHECK_ST(msu_decode(bad_sig, sizeof bad_sig, &msg), MSU_ERR_SIGNATURE);
  CHECK(msg.type == 0x5a);
  CHECK_ST(msu_decode(bad_ver, sizeof bad_ver, &msg), MSU_ERR_VERSION);
  CHECK_ST(msu_decode(bad_op, sizeof bad_op, &msg), MSU_ERR_OPCODE);
  CHECK_ST(msu_decode(ext_ready, sizeof ext_ready, &msg), MSU_ERR_OPCODE);
  CHECK_ST(msu_decode(unrelated, sizeof unrelated, &msg), MSU_ERR_SIGNATURE);
  CHECK_ST(msu_decode(foreign, sizeof foreign, &msg), MSU_ERR_SIGNATURE);
  CHECK_ST(msu_decode(uni_nonrt, sizeof uni_nonrt, &msg), MSU_ERR_ID);
  CHECK_ST(msu_decode(uni_rt, sizeof uni_rt, &msg), MSU_ERR_ID);
  CHECK_ST(msu_decode(identity, sizeof identity, &msg), MSU_ERR_ID);
  CHECK_ST(msu_decode(ch16, sizeof ch16, &msg), MSU_ERR_CHANNEL);
  CHECK_ST(msu_decode(ch7f, sizeof ch7f, &msg), MSU_ERR_CHANNEL);
  CHECK_ST(msu_decode(cc_msb, sizeof cc_msb, &msg), MSU_ERR_RANGE);
  CHECK_ST(msu_decode(transport3, sizeof transport3, &msg), MSU_ERR_RANGE);
  CHECK_ST(msu_decode(pc_long, sizeof pc_long, &msg), MSU_ERR_LENGTH);
  CHECK_ST(msu_decode(cc_short, sizeof cc_short, &msg), MSU_ERR_LENGTH);
  CHECK_ST(msu_decode(batch_short, sizeof batch_short, &msg), MSU_ERR_LENGTH);
  CHECK_ST(msu_decode(batch_long, sizeof batch_long, &msg), MSU_ERR_LENGTH);
  CHECK_ST(msu_decode(batch0, sizeof batch0, &msg), MSU_ERR_RANGE);
  CHECK_ST(msu_decode(opt_bad, sizeof opt_bad, &msg), MSU_ERR_OPTIONS);
  CHECK_ST(msu_decode(opt7, sizeof opt7, &msg), MSU_ERR_OPTIONS);
  CHECK_ST(msu_decode(high_bit, sizeof high_bit, &msg), MSU_ERR_FRAMING);
  CHECK_ST(msu_decode(empty, sizeof empty, &msg), MSU_ERR_LENGTH);
  CHECK(msg.type == 0x5a);
  CHECK_ST(msu_decode(any_id, sizeof any_id, &msg), MSU_OK);
  CHECK(msg.id_len == 1 && msg.id[0] == 0x12 && msg.type == MSU_READY);
  CHECK_ST(msu_decode(id_7c, sizeof id_7c, &msg), MSU_OK);
  CHECK(msg.id[0] == 0x7c);

  n = 0;
  pkt[n++] = 0xf0;
  pkt[n++] = 0x7d;
  pkt[n++] = 0x4d;
  pkt[n++] = 0x53;
  pkt[n++] = 0x55;
  pkt[n++] = 0x01;
  pkt[n++] = 0x24;
  pkt[n++] = 0x00;
  pkt[n++] = 11;
  for (i = 0; i < 11; i++) {
    pkt[n++] = (uint8_t)i;
    pkt[n++] = 1;
  }
  pkt[n++] = 0xf7;
  CHECK(n == 32);
  CHECK_ST(msu_decode(pkt, n, &msg), MSU_ERR_RANGE);

  memset(big, 0, sizeof big);
  big[0] = 0xf0;
  big[sizeof big - 1] = 0xf7;
  CHECK_ST(msu_decode(big, sizeof big, &msg), MSU_ERR_OVERSIZE);
  CHECK(msg.type == MSU_READY || msg.id[0] == 0x7c || msg.type == 0x5a ||
    msg.type == MSU_READY);
  CHECK_ST(msu_decode(NULL, 1, &msg), MSU_ERR_NULL);
}

static int feed_count(const uint8_t *bytes, size_t n, msu_message_t *msg,
    int *realtime, int *ignored, int *plain) {
  msu_parser_t parser;
  size_t i;
  int messages = 0;
  msu_parser_init(&parser);
  *realtime = 0;
  *ignored = 0;
  *plain = 0;
  for (i = 0; i < n; i++) {
    uint8_t raw = 0;
    msu_feed_t ev = msu_parser_feed(&parser, bytes[i], msg, &raw);
    if (ev == MSU_FEED_MESSAGE)
      messages++;
    else if (ev == MSU_FEED_REALTIME) {
      (*realtime)++;
      CHECK(raw == bytes[i]);
    } else if (ev == MSU_FEED_IGNORED_SYSEX)
      (*ignored)++;
    else if (ev == MSU_FEED_BYTE) {
      (*plain)++;
      CHECK(raw == bytes[i]);
    } else {
      CHECK(ev == MSU_FEED_NONE);
    }
  }
  CHECK(parser.state == MSU_PARSER_IDLE);
  return messages;
}

static void test_stream(void) {
  msu_message_t msg;
  msu_parser_t parser;
  uint8_t raw = 0;
  int rt = 0;
  int ignored = 0;
  int plain = 0;
  int messages;
  int early = 0;
  size_t i;
  uint8_t frag[9] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x01, 0x00, 0xF7
  };
  uint8_t clocked[18];
  uint8_t abort_then[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x90, 0x40, 0x40,
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x02, 0x00, 0xF7
  };
  uint8_t restart[] = {
    0xF0, 0x7D, 0x4D, 0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x01, 0x00, 0xF7
  };
  uint8_t with_ff[10] = {
    0xF0, 0x7D, 0xFF, 0x4D, 0x53, 0x55, 0x01, 0x01, 0x00, 0xF7
  };
  uint8_t note[3] = {0x90, 0x40, 0x40};
  uint8_t over[40];
  uint8_t ready[9] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x01, 0x00, 0xF7
  };
  size_t c = 0;

  msu_parser_init(&parser);
  memset(&msg, 0, sizeof msg);
  for (i = 0; i + 1 < sizeof frag; i++) {
    msu_feed_t ev = msu_parser_feed(&parser, frag[i], &msg, &raw);
    CHECK(ev == MSU_FEED_NONE);
    early++;
  }
  CHECK(early == 8);
  CHECK(msu_parser_feed(&parser, frag[8], &msg, &raw) == MSU_FEED_MESSAGE);
  CHECK(msg.type == MSU_READY && msg.channel == 0);

  for (i = 0; i < sizeof frag; i++) {
    clocked[c++] = frag[i];
    if (i + 1 < sizeof frag)
      clocked[c++] = 0xf8;
  }
  messages = feed_count(clocked, c, &msg, &rt, &ignored, &plain);
  CHECK(messages == 1);
  CHECK(rt == 8);
  CHECK(ignored == 0);
  CHECK(msg.type == MSU_READY);

  messages = feed_count(abort_then, sizeof abort_then, &msg, &rt, &ignored,
    &plain);
  CHECK(messages == 1);
  CHECK(plain == 3);
  CHECK(msg.type == MSU_GET_STATE);

  messages = feed_count(restart, sizeof restart, &msg, &rt, &ignored, &plain);
  CHECK(messages == 1);
  CHECK(msg.type == MSU_READY);
  messages = feed_count(with_ff, sizeof with_ff, &msg, &rt, &ignored, &plain);
  CHECK(messages == 1 && rt == 1 && msg.type == MSU_READY);
  messages = feed_count(note, sizeof note, &msg, &rt, &ignored, &plain);
  CHECK(messages == 0 && plain == 3 && rt == 0);

  msu_parser_init(&parser);
  CHECK(msu_parser_feed(&parser, 0xf0, &msg, &raw) == MSU_FEED_NONE);
  for (i = 0; i < 30; i++)
    CHECK(msu_parser_feed(&parser, 0x00, &msg, &raw) == MSU_FEED_NONE);
  CHECK(parser.state == MSU_PARSER_SYSEX);
  CHECK(msu_parser_feed(&parser, 0x00, &msg, &raw) == MSU_FEED_NONE);
  CHECK(parser.state == MSU_PARSER_DISCARD);
  CHECK(msu_parser_feed(&parser, 0x11, &msg, &raw) == MSU_FEED_NONE);
  CHECK(msu_parser_feed(&parser, 0xf8, &msg, &raw) == MSU_FEED_REALTIME);
  CHECK(parser.state == MSU_PARSER_DISCARD);
  CHECK(msu_parser_feed(&parser, 0xf7, &msg, &raw) == MSU_FEED_IGNORED_SYSEX);
  CHECK(parser.state == MSU_PARSER_IDLE);
  messages = 0;
  for (i = 0; i < sizeof ready; i++) {
    if (msu_parser_feed(&parser, ready[i], &msg, &raw) == MSU_FEED_MESSAGE)
      messages++;
  }
  CHECK(messages == 1 && msg.type == MSU_READY);

  memset(over, 0x01, sizeof over);
  over[0] = 0xf0;
  over[33] = 0xf7;
  messages = feed_count(over, 34, &msg, &rt, &ignored, &plain);
  CHECK(messages == 0 && ignored == 1);
  (void)over;
}

static void test_tx_priority(void) {
  sim_tx_t tx;
  msu_message_t msg;
  uint8_t packet[MSU_MAX_PACKET];
  uint8_t voice[3] = {0xb0, 0x05, 0x01};
  uint8_t out[64];
  uint8_t nout = 0;
  uint8_t byte = 0;
  size_t n = 0;
  uint8_t i;
  int in_sysex = 0;
  int saw_voice_inside = 0;
  id7d(&msg, MSU_READY, 0);
  CHECK_ST(msu_encode(&msg, packet, sizeof packet, &n), MSU_OK);
  sim_tx_init(&tx);
  CHECK(sim_tx_push(&tx, SIM_FRAME_SYSEX, packet, (uint8_t)n) == 0);
  CHECK(sim_tx_push(&tx, SIM_FRAME_VOICE, voice, 3) == 0);
  while (sim_tx_pump(&tx, &byte))
    out[nout++] = byte;
  CHECK(nout == n + 3);
  CHECK(out[0] == 0xb0 && out[1] == 0x05 && out[2] == 0x01);
  CHECK_BYTES(out + 3, n, packet, n);

  sim_tx_init(&tx);
  nout = 0;
  CHECK(sim_tx_push(&tx, SIM_FRAME_SYSEX, packet, (uint8_t)n) == 0);
  CHECK(sim_tx_pump(&tx, &byte) == 1);
  CHECK(byte == 0xf0);
  out[nout++] = byte;
  CHECK(sim_tx_push(&tx, SIM_FRAME_VOICE, voice, 3) == 0);
  CHECK(sim_tx_push_rt(&tx, 0xf8) == 0);
  while (sim_tx_pump(&tx, &byte))
    out[nout++] = byte;
  CHECK(out[1] == 0xf8);
  in_sysex = 0;
  for (i = 0; i < nout; i++) {
    if (out[i] == 0xf0)
      in_sysex = 1;
    if (in_sysex && out[i] == 0xb0)
      saw_voice_inside = 1;
    if (out[i] == 0xf7)
      in_sysex = 0;
  }
  CHECK(!saw_voice_inside);
  CHECK(out[nout - 3] == 0xb0 && out[nout - 1] == 0x01);
}

static void expect_log(const sim_world_t *w, const uint8_t *const *pkts,
    const uint8_t *lens, const uint8_t *srcs, int count) {
  uint16_t i = 0;
  int p = 0;
  while (p < count) {
    uint8_t buf[MSU_MAX_PACKET];
    uint8_t n = 0;
    CHECK(i < w->log_len);
    if (g_failed > 40)
      return;
    CHECK(w->log[i].src == srcs[p]);
    while (i < w->log_len && n < MSU_MAX_PACKET) {
      if (n && w->log[i].src != srcs[p])
        break;
      buf[n++] = w->log[i].byte;
      i++;
      if (buf[n - 1] == 0xf7)
        break;
    }
    CHECK_BYTES(buf, n, pkts[p], lens[p]);
    p++;
  }
  CHECK(i == w->log_len);
}

static void test_exchange(void) {
  static sim_world_t w;
  static const uint8_t get0[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x02, 0x00, 0xF7
  };
  static const uint8_t begin_a[] = {
    0xF0, 0x71, 0x4D, 0x53, 0x55, 0x01, 0x03, 0x00, 0xF7
  };
  static const uint8_t pc_a[] = {
    0xF0, 0x71, 0x4D, 0x53, 0x55, 0x01, 0x10, 0x00, 0x0C, 0xF7
  };
  static const uint8_t cc24[] = {
    0xF0, 0x71, 0x4D, 0x53, 0x55, 0x01, 0x20, 0x00, 0x18, 0x03, 0xF7
  };
  static const uint8_t cc102[] = {
    0xF0, 0x71, 0x4D, 0x53, 0x55, 0x01, 0x20, 0x00, 0x66, 0x40, 0xF7
  };
  static const uint8_t cc5[] = {
    0xF0, 0x71, 0x4D, 0x53, 0x55, 0x01, 0x20, 0x00, 0x05, 0x53, 0xF7
  };
  static const uint8_t end_a[] = {
    0xF0, 0x71, 0x4D, 0x53, 0x55, 0x01, 0x04, 0x00, 0xF7
  };
  static const uint8_t get1[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x02, 0x01, 0xF7
  };
  static const uint8_t begin_b[] = {
    0xF0, 0x00, 0x7D, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x03, 0x01, 0xF7
  };
  static const uint8_t cc10[] = {
    0xF0, 0x00, 0x7D, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x20, 0x01, 0x0A, 0x0A,
    0xF7
  };
  static const uint8_t cc11[] = {
    0xF0, 0x00, 0x7D, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x20, 0x01, 0x0B, 0x14,
    0xF7
  };
  static const uint8_t cc12[] = {
    0xF0, 0x00, 0x7D, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x20, 0x01, 0x0C, 0x1E,
    0xF7
  };
  static const uint8_t end_b[] = {
    0xF0, 0x00, 0x7D, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x04, 0x01, 0xF7
  };
  const uint8_t *pkts[] = {
    get0, begin_a, pc_a, cc24, cc102, cc5, end_a,
    get1, begin_b, cc10, cc11, cc12, end_b
  };
  uint8_t lens[13];
  uint8_t srcs[13];
  int i;
  lens[0] = sizeof get0;
  lens[1] = sizeof begin_a;
  lens[2] = sizeof pc_a;
  lens[3] = sizeof cc24;
  lens[4] = sizeof cc102;
  lens[5] = sizeof cc5;
  lens[6] = sizeof end_a;
  lens[7] = sizeof get1;
  lens[8] = sizeof begin_b;
  lens[9] = sizeof cc10;
  lens[10] = sizeof cc11;
  lens[11] = sizeof cc12;
  lens[12] = sizeof end_b;
  for (i = 0; i < 13; i++)
    srcs[i] = (i == 0 || i == 7) ? SIM_SRC_CONTROLLER :
      (i < 7 ? SIM_SRC_PEDAL_A : SIM_SRC_PEDAL_B);
  sim_world_init(&w);
  CHECK(sim_controller_get_state(&w.ctl, SIM_CH_A) == 0);
  CHECK(sim_world_pump(&w) == 0);
  CHECK(sim_controller_is_synced(&w.ctl, SIM_CH_A));
  CHECK(!sim_controller_is_synced(&w.ctl, SIM_CH_B));
  CHECK(sim_controller_get_state(&w.ctl, SIM_CH_B) == 0);
  CHECK(sim_world_pump(&w) == 0);
  CHECK(sim_controller_is_synced(&w.ctl, SIM_CH_B));
  CHECK(w.ctl.cache[0].program == 12);
  CHECK(w.ctl.cache[0].cc[24] == 3);
  CHECK(w.ctl.cache[0].cc[102] == 64);
  CHECK(w.ctl.cache[0].cc[5] == 83);
  CHECK(w.ctl.cache[1].cc[10] == 10);
  CHECK(w.ctl.cache[1].cc[11] == 20);
  CHECK(w.ctl.cache[1].cc[12] == 30);
  CHECK(w.ctl.tx.voice.count == 0);
  expect_log(&w, pkts, lens, srcs, 13);
}

static void feed_pedal(sim_pedal_t *p, const uint8_t *b, size_t n) {
  size_t i;
  for (i = 0; i < n; i++)
    sim_pedal_rx(p, b[i]);
}

static void test_independence(void) {
  static sim_world_t w;
  msu_message_t msg;
  uint8_t buf[MSU_MAX_PACKET];
  uint8_t len = 0;
  size_t n = 0;
  uint8_t program;
  uint8_t cc5;
  uint8_t cc24;
  uint8_t cc102;
  uint16_t interval;
  uint8_t sysex;
  static const uint8_t unrelated[] = {
    0xF0, 0x62, 0x11, 0x22, 0x33, 0xF7
  };
  static const uint8_t universal[] = {
    0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7
  };
  static const uint8_t get_foreign_clock[] = {
    0xF0, 0x62, 0xF8, 0x4D, 0x53, 0x55, 0x01, 0x02, 0x00, 0xF7
  };
  sim_world_init(&w);
  program = w.a.program;
  cc5 = w.a.cc[5];
  cc24 = w.a.cc[24];
  cc102 = w.a.cc[102];
  interval = w.a.interval_ms;
  sysex = sim_tx_sysex_count(&w.a.tx);

  memset(&msg, 0, sizeof msg);
  msg.id[0] = 0x62;
  msg.id_len = 1;
  msg.type = MSU_GET_STATE;
  msg.channel = SIM_CH_B;
  CHECK_ST(msu_encode(&msg, buf, sizeof buf, &n), MSU_OK);
  feed_pedal(&w.a, buf, n);
  CHECK(w.a.program == program && w.a.cc[5] == cc5);
  CHECK(w.a.cc[24] == cc24 && w.a.cc[102] == cc102);
  CHECK(w.a.interval_ms == interval);
  CHECK(sim_tx_sysex_count(&w.a.tx) == sysex);
  CHECK(sim_tx_sysex_count(&w.b.tx) == 0);

  feed_pedal(&w.a, unrelated, sizeof unrelated);
  feed_pedal(&w.a, universal, sizeof universal);
  CHECK(w.a.program == program && w.a.cc[5] == cc5);
  CHECK(w.a.interval_ms == interval);
  CHECK(sim_tx_sysex_count(&w.a.tx) == sysex);
  CHECK(w.a.tx.voice.count == 0);

  msg.channel = SIM_CH_A;
  msg.type = MSU_SET_UPDATE_INTERVAL;
  msg.u.period_10ms = 9;
  msg.channel = SIM_CH_B;
  CHECK_ST(msu_encode(&msg, buf, sizeof buf, &n), MSU_OK);
  feed_pedal(&w.a, buf, n);
  CHECK(w.a.interval_ms == interval);
  CHECK(sim_tx_sysex_count(&w.a.tx) == sysex);

  id7d(&msg, MSU_CC7, SIM_CH_A);
  msg.u.cc7.cc = 5;
  msg.u.cc7.value = 1;
  CHECK_ST(msu_encode(&msg, buf, sizeof buf, &n), MSU_OK);
  feed_pedal(&w.a, buf, n);
  CHECK(w.a.cc[5] == cc5);
  CHECK(sim_tx_sysex_count(&w.a.tx) == sysex);

  feed_pedal(&w.a, get_foreign_clock, sizeof get_foreign_clock);
  CHECK(w.a.program == program && w.a.cc[5] == cc5);
  CHECK(w.a.cc[24] == cc24 && w.a.cc[102] == cc102);
  CHECK(sim_tx_sysex_count(&w.a.tx) > sysex);
  CHECK(sim_tx_sysex_at(&w.a.tx, 0, buf, &len) == 0);
  CHECK_ST(msu_decode(buf, len, &msg), MSU_OK);
  CHECK(msg.type == MSU_STATE_BEGIN);
  CHECK(msg.id_len == 1 && msg.id[0] == SIM_ID_PEDAL_A);
  CHECK(msg.id[0] != 0x62);
}

static int log_extended(const sim_world_t *w, uint16_t from) {
  uint16_t i = from;
  int bad = 0;
  while (i < w->log_len) {
    uint8_t buf[MSU_MAX_PACKET];
    uint8_t n = 0;
    msu_message_t msg;
    uint8_t src = w->log[i].src;
    while (i < w->log_len && w->log[i].src == src && n < MSU_MAX_PACKET) {
      buf[n++] = w->log[i].byte;
      i++;
      if (buf[n - 1] == 0xf7)
        break;
    }
    if (src == SIM_SRC_CONTROLLER)
      continue;
    if (msu_decode(buf, n, &msg) != MSU_OK) {
      bad++;
      continue;
    }
    if (msg.extended)
      bad++;
    if ((buf[6] & 0x40) != 0)
      bad++;
  }
  return bad;
}

static int controller_sent_voice(const sim_world_t *w) {
  uint16_t i;
  int in_sysex = 0;
  for (i = 0; i < w->log_len; i++) {
    uint8_t b;
    if (w->log[i].src != SIM_SRC_CONTROLLER)
      continue;
    b = w->log[i].byte;
    if (b == 0xf0)
      in_sysex = 1;
    if (!in_sysex && b >= 0x80 && b < 0xf0)
      return 1;
    if (b == 0xf7)
      in_sysex = 0;
  }
  return 0;
}

static void test_origins(void) {
  static sim_world_t w;
  uint8_t ccs[3] = {24, 102, 5};
  uint8_t values[3] = {1, 2, 3};
  uint16_t mark;
  sim_world_init(&w);
  mark = w.log_len;
  CHECK(sim_pedal_knob(&w.a, 5, 10) == 0);
  CHECK(sim_world_pump(&w) == 0);
  CHECK(log_extended(&w, mark) == 0);
  CHECK(w.ctl.cache[0].cc[5] == 10);
  CHECK(!controller_sent_voice(&w));
  CHECK(w.ctl.tx.voice.count == 0);

  sim_pedal_rx(&w.a, 0xb0);
  sim_pedal_rx(&w.a, 102);
  sim_pedal_rx(&w.a, 9);
  mark = w.log_len;
  CHECK(sim_world_pump(&w) == 0);
  CHECK(log_extended(&w, mark) == 0);
  CHECK(w.ctl.cache[0].cc[102] == 9);
  CHECK(!controller_sent_voice(&w));

  CHECK(sim_pedal_recall(&w.a, 4, ccs, values, 3) == 0);
  mark = w.log_len;
  CHECK(sim_world_pump(&w) == 0);
  CHECK(log_extended(&w, mark) == 0);
  CHECK(w.ctl.cache[0].program == 4);
  CHECK(w.ctl.cache[0].cc[24] == 1);
  CHECK(w.ctl.cache[0].cc[102] == 2);
  CHECK(w.ctl.cache[0].cc[5] == 3);
  CHECK(!controller_sent_voice(&w));
  CHECK(w.ctl.msu_updates > 0);
}

static void test_ready(void) {
  static sim_world_t w;
  msu_message_t msg;
  uint8_t buf[MSU_MAX_PACKET];
  uint8_t n = 0;
  sim_world_init(&w);
  CHECK(!sim_controller_is_synced(&w.ctl, SIM_CH_A));
  CHECK(sim_pedal_restart(&w.a) == 0);
  CHECK(sim_world_pump(&w) == 0);
  CHECK(sim_controller_is_synced(&w.ctl, SIM_CH_A));
  CHECK(w.ctl.cache[0].cc[5] == 83);
  CHECK(sim_tx_sysex_at(&w.a.tx, 0, buf, &n) != 0);

  sim_world_init(&w);
  CHECK(sim_controller_get_state(&w.ctl, SIM_CH_A) == 0);
  CHECK(sim_world_pump(&w) == 0);
  CHECK(sim_controller_is_synced(&w.ctl, SIM_CH_A));
  CHECK(w.ctl.cache[0].cc[5] == 83);
  CHECK(sim_pedal_knob(&w.a, 5, 10) == 0);
  sim_tx_clear(&w.a.tx);
  CHECK(sim_pedal_restart(&w.a) == 0);
  CHECK(w.a.interval_ms == 0);
  sim_tx_clear(&w.a.tx);
  CHECK(sim_tx_sysex_count(&w.ctl.tx) == 0);
  CHECK(sim_controller_is_synced(&w.ctl, SIM_CH_A));
  CHECK(w.ctl.cache[0].cc[5] == 83);
  CHECK(w.a.cc[5] == 10);
  sim_log_reset(&w);
  CHECK(sim_controller_reconcile(&w.ctl, SIM_CH_A) == 0);
  CHECK(sim_world_pump(&w) == 0);
  CHECK(sim_controller_is_synced(&w.ctl, SIM_CH_A));
  CHECK(w.ctl.cache[0].cc[5] == 10);
  CHECK(sim_tx_sysex_count(&w.ctl.tx) == 0);
  CHECK(w.log_len > 0 && w.log[0].src == SIM_SRC_CONTROLLER);
  n = 0;
  while (n < w.log_len && w.log[n].src == SIM_SRC_CONTROLLER &&
      n < MSU_MAX_PACKET) {
    buf[n] = w.log[n].byte;
    n++;
    if (buf[n - 1] == 0xf7)
      break;
  }
  CHECK_ST(msu_decode(buf, n, &msg), MSU_OK);
  CHECK(msg.type == MSU_GET_STATE && msg.id[0] == 0x7d);
}

static void test_incomplete(void) {
  static sim_world_t w;
  msu_message_t msg;
  uint8_t buf[MSU_MAX_PACKET];
  uint8_t len = 0;
  size_t n = 0;
  uint8_t i;
  sim_world_init(&w);
  CHECK(sim_controller_get_state(&w.ctl, SIM_CH_A) == 0);
  CHECK(sim_world_pump_source(&w, SIM_SRC_CONTROLLER) == 0);
  CHECK(sim_tx_sysex_count(&w.b.tx) == 0);
  CHECK(sim_tx_sysex_count(&w.a.tx) == 6);
  CHECK(sim_tx_sysex_at(&w.a.tx, 5, buf, &len) == 0);
  CHECK_ST(msu_decode(buf, len, &msg), MSU_OK);
  CHECK(msg.type == MSU_STATE_END);
  CHECK(sim_tx_drop_last_sysex(&w.a.tx) == 0);
  CHECK(sim_world_pump_source(&w, SIM_SRC_PEDAL_A) == 0);
  CHECK(w.ctl.cache[0].sync == SIM_SYNC_IN_PROGRESS);
  CHECK(!sim_controller_is_synced(&w.ctl, SIM_CH_A));
  CHECK(w.ctl.cache[0].have_program && w.ctl.cache[0].program == 12);
  CHECK(w.ctl.cache[0].cc[24] == 3);
  CHECK(w.ctl.cache[0].cc[102] == 64);
  CHECK(w.ctl.cache[0].cc[5] == 83);
  CHECK(w.ctl.tx.voice.count == 0);
  id7d(&msg, MSU_STATE_END, SIM_CH_A);
  msg.id[0] = SIM_ID_PEDAL_A;
  CHECK_ST(msu_encode(&msg, buf, sizeof buf, &n), MSU_OK);
  for (i = 0; i < n; i++)
    sim_controller_rx(&w.ctl, buf[i]);
  CHECK(sim_controller_is_synced(&w.ctl, SIM_CH_A));
}

static int batch_is(const uint8_t *pkt, uint8_t len, uint8_t a, uint8_t b,
    uint8_t c) {
  msu_message_t msg;
  if (msu_decode(pkt, len, &msg) != MSU_OK)
    return 0;
  if (msg.type != MSU_CC7_BATCH || msg.u.batch.count != 3)
    return 0;
  if (msg.id_len != 3 || msg.id[0] != 0x00 || msg.id[1] != 0x7d)
    return 0;
  if (msg.channel != SIM_CH_B)
    return 0;
  if (msg.u.batch.item[0].cc != 10 || msg.u.batch.item[0].value != a)
    return 0;
  if (msg.u.batch.item[1].cc != 11 || msg.u.batch.item[1].value != b)
    return 0;
  if (msg.u.batch.item[2].cc != 12 || msg.u.batch.item[2].value != c)
    return 0;
  return 1;
}

static void test_rate(void) {
  static sim_world_t w;
  static const uint8_t ccs[3] = {10, 11, 12};
  static const uint8_t v1[3] = {1, 2, 3};
  static const uint8_t v2[3] = {4, 5, 6};
  static const uint8_t v3[3] = {7, 8, 9};
  uint8_t buf[MSU_MAX_PACKET];
  uint8_t len = 0;
  msu_message_t msg;
  uint8_t count;
  sim_world_init(&w);
  sim_world_set_now(&w, 0);
  CHECK(sim_controller_set_interval(&w.ctl, SIM_CH_B, 5) == 0);
  CHECK(sim_world_pump(&w) == 0);
  CHECK(w.b.interval_ms == 50);
  CHECK(w.a.interval_ms == 0);
  CHECK(sim_tx_sysex_count(&w.b.tx) == 0);

  CHECK(sim_pedal_morph(&w.b, ccs, v1, 3) == 0);
  CHECK(sim_tx_sysex_count(&w.b.tx) == 1);
  CHECK(sim_tx_sysex_at(&w.b.tx, 0, buf, &len) == 0);
  CHECK(batch_is(buf, len, 1, 2, 3));

  sim_world_set_now(&w, 10);
  CHECK(sim_pedal_morph(&w.b, ccs, v2, 3) == 0);
  CHECK(sim_tx_sysex_count(&w.b.tx) == 1);
  sim_world_set_now(&w, 20);
  CHECK(sim_pedal_morph(&w.b, ccs, v3, 3) == 0);
  CHECK(sim_tx_sysex_count(&w.b.tx) == 1);
  sim_world_set_now(&w, 50);
  CHECK(sim_pedal_service(&w.b, 50) == 0);
  CHECK(sim_tx_sysex_count(&w.b.tx) == 2);
  CHECK(sim_tx_sysex_at(&w.b.tx, 1, buf, &len) == 0);
  CHECK(batch_is(buf, len, 7, 8, 9));

  sim_world_init(&w);
  sim_world_set_now(&w, 0);
  CHECK(sim_controller_set_interval(&w.ctl, SIM_CH_B, 5) == 0);
  CHECK(sim_world_pump(&w) == 0);
  CHECK(sim_pedal_morph(&w.b, ccs, v1, 3) == 0);
  sim_world_set_now(&w, 10);
  CHECK(sim_pedal_morph(&w.b, ccs, v2, 3) == 0);
  CHECK(sim_tx_sysex_count(&w.b.tx) == 1);
  sim_world_set_now(&w, 15);
  count = sim_tx_sysex_count(&w.b.tx);
  CHECK(sim_controller_get_state(&w.ctl, SIM_CH_B) == 0);
  CHECK(sim_world_pump_source(&w, SIM_SRC_CONTROLLER) == 0);
  CHECK(sim_tx_sysex_count(&w.b.tx) == (uint8_t)(count + 5));
  CHECK(sim_tx_sysex_at(&w.b.tx, 2, buf, &len) == 0);
  CHECK_ST(msu_decode(buf, len, &msg), MSU_OK);
  CHECK(msg.type == MSU_CC7 && msg.u.cc7.cc == 10 && msg.u.cc7.value == 4);
  CHECK(sim_tx_sysex_at(&w.b.tx, 3, buf, &len) == 0);
  CHECK_ST(msu_decode(buf, len, &msg), MSU_OK);
  CHECK(msg.u.cc7.cc == 11 && msg.u.cc7.value == 5);
  CHECK(sim_tx_sysex_at(&w.b.tx, 4, buf, &len) == 0);
  CHECK_ST(msu_decode(buf, len, &msg), MSU_OK);
  CHECK(msg.u.cc7.cc == 12 && msg.u.cc7.value == 6);
  sim_world_set_now(&w, 50);
  CHECK(sim_pedal_service(&w.b, 50) == 0);
  CHECK(sim_tx_sysex_count(&w.b.tx) == (uint8_t)(count + 5));
  sim_world_set_now(&w, 60);
  CHECK(sim_pedal_morph(&w.b, ccs, v3, 3) == 0);
  CHECK(sim_tx_sysex_count(&w.b.tx) == (uint8_t)(count + 5));
  CHECK(sim_pedal_service(&w.b, 65) == 0);
  CHECK(sim_tx_sysex_count(&w.b.tx) == (uint8_t)(count + 6));
  CHECK(sim_tx_sysex_at(&w.b.tx, (uint8_t)(count + 5), buf, &len) == 0);
  CHECK(batch_is(buf, len, 7, 8, 9));
  CHECK(sim_pedal_restart(&w.b) == 0);
  CHECK(w.b.interval_ms == 0);
}

static void test_running_status(void) {
  static sim_world_t w;
  static const uint8_t cc_10[] = {0xB0, 0x05, 0x0A};
  static const uint8_t cc_data[] = {0x05, 0x33};
  static const uint8_t clock[] = {0xF8};
  static const uint8_t cc_with_clock[] = {0xB0, 0x05, 0xF8, 0x14};
  static const uint8_t partial[] = {0xB0, 0x05};
  static const uint8_t orphan[] = {0x33};
  static const uint8_t fresh[] = {0xB0, 0x05, 0x01};
  static const uint8_t msu_clock[] = {
    0xF0, 0x7D, 0xF8, 0x4D, 0x53, 0x55, 0x01, 0x01, 0x00, 0xF7
  };
  static const uint8_t other_clock[] = {
    0xF0, 0x71, 0x11, 0xF8, 0x22, 0x33, 0xF7
  };
  sim_world_init(&w);
  feed_pedal(&w.a, cc_10, sizeof cc_10);
  CHECK(w.a.cc[5] == 0x0a);
  feed_pedal(&w.a, msu_clock, sizeof msu_clock);
  feed_pedal(&w.a, cc_data, sizeof cc_data);
  CHECK(w.a.cc[5] == 0x0a);

  feed_pedal(&w.a, cc_10, sizeof cc_10);
  feed_pedal(&w.a, other_clock, sizeof other_clock);
  feed_pedal(&w.a, cc_data, sizeof cc_data);
  CHECK(w.a.cc[5] == 0x0a);

  feed_pedal(&w.a, cc_10, sizeof cc_10);
  feed_pedal(&w.a, clock, sizeof clock);
  feed_pedal(&w.a, cc_data, sizeof cc_data);
  CHECK(w.a.cc[5] == 0x33);

  feed_pedal(&w.a, cc_with_clock, sizeof cc_with_clock);
  CHECK(w.a.cc[5] == 0x14);

  feed_pedal(&w.a, partial, sizeof partial);
  feed_pedal(&w.a, msu_clock, sizeof msu_clock);
  feed_pedal(&w.a, orphan, sizeof orphan);
  CHECK(w.a.cc[5] == 0x14);
  feed_pedal(&w.a, fresh, sizeof fresh);
  CHECK(w.a.cc[5] == 0x01);
}

static int fill_sysex(sim_tx_t *tx, uint8_t count) {
  static const uint8_t dummy[] = {0xF0, 0x01, 0x02, 0xF7};
  while (sim_tx_sysex_count(tx) < count) {
    if (sim_tx_push(tx, SIM_FRAME_SYSEX, dummy, (uint8_t)sizeof dummy) != 0)
      return -1;
  }
  return 0;
}

static void test_snapshot_queue_full(void) {
  static sim_world_t w;
  msu_message_t msg;
  uint8_t buf[MSU_MAX_PACKET];
  uint8_t len = 0;
  uint8_t before;
  static const uint8_t get0[] = {
    0xF0, 0x7D, 0x4D, 0x53, 0x55, 0x01, 0x02, 0x00, 0xF7
  };
  sim_world_init(&w);
  CHECK(fill_sysex(&w.a.tx, SIM_Q) == 0);
  before = sim_tx_sysex_count(&w.a.tx);
  feed_pedal(&w.a, get0, sizeof get0);
  CHECK(sim_tx_sysex_count(&w.a.tx) == before);
  CHECK(sim_tx_sysex_at(&w.a.tx, (uint8_t)(before - 1u), buf, &len) == 0);
  CHECK(msu_decode(buf, len, &msg) != MSU_OK);
  CHECK(w.a.cc[5] == 83);

  sim_tx_clear(&w.a.tx);
  CHECK(fill_sysex(&w.a.tx, (uint8_t)(SIM_Q - 2u)) == 0);
  before = sim_tx_sysex_count(&w.a.tx);
  feed_pedal(&w.a, get0, sizeof get0);
  CHECK(sim_tx_sysex_count(&w.a.tx) == before);
  CHECK(w.a.snapshot_active == 0);

  sim_tx_clear(&w.a.tx);
  feed_pedal(&w.a, get0, sizeof get0);
  CHECK(sim_tx_sysex_count(&w.a.tx) == 6);
  CHECK(sim_tx_sysex_at(&w.a.tx, 0, buf, &len) == 0);
  CHECK_ST(msu_decode(buf, len, &msg), MSU_OK);
  CHECK(msg.type == MSU_STATE_BEGIN);
  CHECK(sim_tx_sysex_at(&w.a.tx, 5, buf, &len) == 0);
  CHECK_ST(msu_decode(buf, len, &msg), MSU_OK);
  CHECK(msg.type == MSU_STATE_END);
}

static void test_change_while_snapshot_queued(void) {
  static sim_world_t w;
  msu_message_t msg;
  uint8_t buf[MSU_MAX_PACKET];
  uint8_t len = 0;
  sim_world_init(&w);
  CHECK(sim_controller_get_state(&w.ctl, SIM_CH_A) == 0);
  CHECK(sim_world_pump_source(&w, SIM_SRC_CONTROLLER) == 0);
  CHECK(sim_tx_sysex_count(&w.a.tx) == 6);
  CHECK(sim_pedal_knob(&w.a, 5, 10) == 0);
  CHECK(sim_tx_sysex_count(&w.a.tx) == 7);
  CHECK(sim_tx_sysex_at(&w.a.tx, 4, buf, &len) == 0);
  CHECK_ST(msu_decode(buf, len, &msg), MSU_OK);
  CHECK(msg.type == MSU_CC7 && msg.u.cc7.cc == 5 && msg.u.cc7.value == 83);
  CHECK(sim_tx_sysex_at(&w.a.tx, 6, buf, &len) == 0);
  CHECK_ST(msu_decode(buf, len, &msg), MSU_OK);
  CHECK(msg.type == MSU_CC7 && msg.u.cc7.cc == 5 && msg.u.cc7.value == 10);
  CHECK(sim_world_pump(&w) == 0);
  CHECK(w.ctl.cache[0].cc[5] == 10);
  CHECK(sim_controller_is_synced(&w.ctl, SIM_CH_A));
}

static void test_settled_value(void) {
  static sim_world_t w;
  msu_message_t msg;
  uint8_t buf[MSU_MAX_PACKET];
  uint8_t len = 0;
  sim_world_init(&w);
  sim_world_set_now(&w, 0);
  CHECK(sim_controller_set_interval(&w.ctl, SIM_CH_B, 5) == 0);
  CHECK(sim_world_pump(&w) == 0);
  CHECK(sim_pedal_knob(&w.b, 10, 1) == 0);
  CHECK(sim_pedal_service(&w.b, 0) == 0);
  CHECK(sim_tx_sysex_count(&w.b.tx) == 1);
  sim_world_set_now(&w, 5);
  CHECK(sim_pedal_knob(&w.b, 10, 2) == 0);
  CHECK(sim_pedal_service(&w.b, 5) == 0);
  CHECK(sim_tx_sysex_count(&w.b.tx) == 1);
  sim_world_set_now(&w, 15);
  CHECK(sim_pedal_knob(&w.b, 10, 8) == 0);
  CHECK(sim_pedal_service(&w.b, 15) == 0);
  CHECK(sim_tx_sysex_count(&w.b.tx) == 1);
  CHECK(sim_pedal_service(&w.b, 50) == 0);
  CHECK(sim_tx_sysex_count(&w.b.tx) == 2);
  CHECK(sim_tx_sysex_at(&w.b.tx, 1, buf, &len) == 0);
  CHECK_ST(msu_decode(buf, len, &msg), MSU_OK);
  CHECK(msg.type == MSU_CC7 && msg.u.cc7.cc == 10 && msg.u.cc7.value == 8);
  CHECK(sim_pedal_service(&w.b, 90) == 0);
  CHECK(sim_tx_sysex_count(&w.b.tx) == 2);
}

static void feed_controller_msg(sim_controller_t *c, uint8_t type,
    uint8_t channel) {
  msu_message_t msg;
  uint8_t buf[MSU_MAX_PACKET];
  size_t n = 0;
  size_t i;
  memset(&msg, 0, sizeof msg);
  msg.id[0] = SIM_ID_PEDAL_A;
  msg.id_len = 1;
  msg.type = type;
  msg.channel = channel;
  CHECK_ST(msu_encode(&msg, buf, sizeof buf, &n), MSU_OK);
  for (i = 0; i < n; i++)
    sim_controller_rx(c, buf[i]);
}

static void test_sync_transactions(void) {
  static sim_world_t w;
  uint8_t queued;
  sim_world_init(&w);
  CHECK(sim_controller_get_state(&w.ctl, SIM_CH_A) == 0);
  queued = sim_tx_sysex_count(&w.ctl.tx);
  CHECK(sim_controller_get_state(&w.ctl, SIM_CH_A) == 0);
  CHECK(sim_tx_sysex_count(&w.ctl.tx) == queued);
  CHECK(w.ctl.cache[0].sync == SIM_SYNC_AWAITING);
  CHECK(w.ctl.cache[0].saw_begin == 0);
  feed_controller_msg(&w.ctl, MSU_STATE_END, SIM_CH_A);
  CHECK(!sim_controller_is_synced(&w.ctl, SIM_CH_A));
  CHECK(sim_world_pump(&w) == 0);
  CHECK(sim_controller_is_synced(&w.ctl, SIM_CH_A));
  CHECK(w.ctl.cache[0].saw_begin == 0);
  feed_controller_msg(&w.ctl, MSU_STATE_END, SIM_CH_A);
  CHECK(sim_controller_is_synced(&w.ctl, SIM_CH_A));

  CHECK(sim_controller_get_state(&w.ctl, SIM_CH_A) == 0);
  CHECK(w.ctl.cache[0].sync == SIM_SYNC_AWAITING);
  CHECK(!sim_controller_is_synced(&w.ctl, SIM_CH_A));
  CHECK(sim_world_pump_source(&w, SIM_SRC_CONTROLLER) == 0);
  sim_tx_clear(&w.a.tx);
  CHECK(w.ctl.cache[0].sync == SIM_SYNC_AWAITING);
  feed_controller_msg(&w.ctl, MSU_STATE_END, SIM_CH_A);
  CHECK(!sim_controller_is_synced(&w.ctl, SIM_CH_A));
  CHECK(w.ctl.cache[0].saw_begin == 0);
  CHECK(sim_controller_get_state(&w.ctl, SIM_CH_A) == 0);
  CHECK(sim_world_pump(&w) == 0);
  CHECK(sim_controller_is_synced(&w.ctl, SIM_CH_A));
  CHECK(w.ctl.cache[0].cc[5] == 83);

  CHECK(sim_controller_get_state(&w.ctl, SIM_CH_A) == 0);
  CHECK(sim_world_pump_source(&w, SIM_SRC_CONTROLLER) == 0);
  CHECK(sim_tx_drop_last_sysex(&w.a.tx) == 0);
  CHECK(sim_world_pump_source(&w, SIM_SRC_PEDAL_A) == 0);
  CHECK(w.ctl.cache[0].sync == SIM_SYNC_IN_PROGRESS);
  CHECK(!sim_controller_is_synced(&w.ctl, SIM_CH_A));
  CHECK(sim_controller_get_state(&w.ctl, SIM_CH_A) == 0);
  CHECK(w.ctl.cache[0].sync == SIM_SYNC_AWAITING);
  CHECK(w.ctl.cache[0].saw_begin == 0);
  feed_controller_msg(&w.ctl, MSU_STATE_END, SIM_CH_A);
  CHECK(!sim_controller_is_synced(&w.ctl, SIM_CH_A));
  CHECK(sim_world_pump(&w) == 0);
  CHECK(sim_controller_is_synced(&w.ctl, SIM_CH_A));
  CHECK(w.ctl.cache[0].saw_begin == 0);

  sim_world_init(&w);
  feed_controller_msg(&w.ctl, MSU_STATE_BEGIN, SIM_CH_A);
  feed_controller_msg(&w.ctl, MSU_STATE_END, SIM_CH_A);
  CHECK(!sim_controller_is_synced(&w.ctl, SIM_CH_A));
  CHECK(w.ctl.cache[0].sync == SIM_SYNC_NONE);
}

static void test_resync_after_missing_end(void) {
  static sim_world_t w;
  sim_world_init(&w);
  CHECK(sim_controller_get_state(&w.ctl, SIM_CH_A) == 0);
  CHECK(sim_world_pump_source(&w, SIM_SRC_CONTROLLER) == 0);
  CHECK(sim_tx_drop_last_sysex(&w.a.tx) == 0);
  CHECK(sim_world_pump_source(&w, SIM_SRC_PEDAL_A) == 0);
  CHECK(!sim_controller_is_synced(&w.ctl, SIM_CH_A));
  CHECK(w.ctl.cache[0].cc[5] == 83);
  CHECK(sim_pedal_knob(&w.a, 5, 11) == 0);
  CHECK(sim_controller_get_state(&w.ctl, SIM_CH_A) == 0);
  CHECK(sim_world_pump(&w) == 0);
  CHECK(sim_controller_is_synced(&w.ctl, SIM_CH_A));
  CHECK(w.ctl.cache[0].cc[5] == 11);
  CHECK(w.ctl.cache[0].program == 12);
}

int main(void) {
  test_section13();
  test_ranges();
  test_reject();
  test_stream();
  test_tx_priority();
  test_exchange();
  test_independence();
  test_origins();
  test_ready();
  test_incomplete();
  test_rate();
  test_running_status();
  test_snapshot_queue_full();
  test_change_while_snapshot_queued();
  test_settled_value();
  test_resync_after_missing_end();
  test_sync_transactions();
  printf("msu tests: %d passed, %d failed\n", g_passed, g_failed);
  return g_failed ? 1 : 0;
}
