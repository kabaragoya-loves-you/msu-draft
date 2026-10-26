#include "sim.h"

#include <stdio.h>

static const char *type_name(uint8_t type) {
  switch (type) {
    case MSU_READY: return "READY";
    case MSU_GET_STATE: return "GET_STATE";
    case MSU_STATE_BEGIN: return "STATE_BEGIN";
    case MSU_STATE_END: return "STATE_END";
    case MSU_SET_UPDATE_INTERVAL: return "SET_UPDATE_INTERVAL";
    case MSU_PC: return "PC";
    case MSU_BANKED_PC: return "BANKED_PC";
    case MSU_CC7: return "CC7";
    case MSU_CC14: return "CC14";
    case MSU_NRPN7: return "NRPN7";
    case MSU_NRPN14: return "NRPN14";
    case MSU_CC7_BATCH: return "CC7_BATCH";
    case MSU_TEMPO: return "TEMPO";
    case MSU_TRANSPORT: return "TRANSPORT";
    default: return "OTHER";
  }
}

static const char *src_name(uint8_t src) {
  if (src == SIM_SRC_CONTROLLER)
    return "Controller";
  if (src == SIM_SRC_PEDAL_A)
    return "Pedal A";
  return "Pedal B";
}

static void print_bytes(const uint8_t *b, uint8_t n) {
  uint8_t i;
  for (i = 0; i < n; i++)
    printf(" %02X", (unsigned)b[i]);
}

static void print_log(const sim_world_t *w, uint16_t from) {
  uint16_t i = from;
  while (i < w->log_len) {
    uint8_t src = w->log[i].src;
    uint8_t buf[MSU_MAX_PACKET];
    uint8_t n = 0;
    msu_message_t msg;
    if (w->log[i].byte >= 0xf8) {
      printf("%s -> %s:%02X realtime\n", src_name(src),
        src == SIM_SRC_CONTROLLER ? "pedals" : "controller",
        (unsigned)w->log[i].byte);
      i++;
      continue;
    }
    while (i < w->log_len && w->log[i].src == src && n < MSU_MAX_PACKET) {
      buf[n++] = w->log[i].byte;
      i++;
      if (buf[n - 1] == 0xf7)
        break;
    }
    printf("%s -> %s:", src_name(src),
      src == SIM_SRC_CONTROLLER ? "pedals" : "controller");
    print_bytes(buf, n);
    if (msu_decode(buf, n, &msg) == MSU_OK) {
      printf("  %s ch %u", type_name(msg.type), (unsigned)(msg.channel + 1u));
      if (msg.type == MSU_CC7)
        printf(" cc %u = %u", (unsigned)msg.u.cc7.cc,
          (unsigned)msg.u.cc7.value);
      else if (msg.type == MSU_PC)
        printf(" program %u", (unsigned)msg.u.program);
      else if (msg.type == MSU_CC7_BATCH)
        printf(" count %u", (unsigned)msg.u.batch.count);
      else if (msg.type == MSU_SET_UPDATE_INTERVAL)
        printf(" period %u", (unsigned)msg.u.period_10ms);
    }
    printf("\n");
  }
}

static int run_exchange(sim_world_t *w) {
  sim_log_reset(w);
  if (sim_controller_get_state(&w->ctl, SIM_CH_A) != 0)
    return -1;
  if (sim_world_pump(w) != 0)
    return -1;
  if (sim_controller_get_state(&w->ctl, SIM_CH_B) != 0)
    return -1;
  if (sim_world_pump(w) != 0)
    return -1;
  return 0;
}

static void print_warning(void) {
  printf("WARNING: manufacturer IDs in this demo are test fixtures.\n");
  printf("Do not transmit them on a live MIDI connection.\n");
  printf("Production devices must use a legitimately assigned SysEx ID.\n");
  printf("0x7D is only for private non-commercial experiments.\n");
  printf("0x7C is not used. The October 2025 MIDI Association policy\n");
  printf("forbids 0x7C for custom SysEx formats.\n\n");
}

static int demo_origins(sim_world_t *w) {
  uint8_t ccs[3];
  uint8_t values[3];
  uint16_t mark;
  printf("Pedal A local knob, incoming CC, preset recall\n");
  sim_log_reset(w);
  mark = w->log_len;
  if (sim_pedal_knob(&w->a, 5, 10) != 0)
    return -1;
  if (sim_world_pump(w) != 0)
    return -1;
  print_log(w, mark);
  sim_pedal_rx(&w->a, 0xb0);
  sim_pedal_rx(&w->a, 102);
  sim_pedal_rx(&w->a, 9);
  mark = w->log_len;
  if (sim_world_pump(w) != 0)
    return -1;
  print_log(w, mark);
  ccs[0] = 24;
  ccs[1] = 102;
  ccs[2] = 5;
  values[0] = 1;
  values[1] = 2;
  values[2] = 3;
  if (sim_pedal_recall(&w->a, 4, ccs, values, 3) != 0)
    return -1;
  mark = w->log_len;
  if (sim_world_pump(w) != 0)
    return -1;
  print_log(w, mark);
  return 0;
}

static int demo_ready(sim_world_t *w) {
  uint16_t mark;
  printf("READY delivered, then READY dropped and explicit reconcile\n");
  sim_world_init(w);
  if (sim_pedal_restart(&w->a) != 0)
    return -1;
  sim_log_reset(w);
  mark = 0;
  if (sim_world_pump(w) != 0)
    return -1;
  print_log(w, mark);
  printf("controller synced ch 1: %d\n",
    sim_controller_is_synced(&w->ctl, SIM_CH_A));
  sim_world_init(w);
  if (sim_controller_get_state(&w->ctl, SIM_CH_A) != 0)
    return -1;
  if (sim_world_pump(w) != 0)
    return -1;
  if (sim_pedal_knob(&w->a, 5, 10) != 0)
    return -1;
  sim_tx_clear(&w->a.tx);
  if (sim_pedal_restart(&w->a) != 0)
    return -1;
  sim_tx_clear(&w->a.tx);
  printf("READY dropped. controller synced=%d cache CC5=%u pedal CC5=%u\n",
    sim_controller_is_synced(&w->ctl, SIM_CH_A),
    (unsigned)w->ctl.cache[SIM_CH_A].cc[5],
    (unsigned)w->a.cc[5]);
  sim_log_reset(w);
  if (sim_controller_reconcile(&w->ctl, SIM_CH_A) != 0)
    return -1;
  if (sim_world_pump(w) != 0)
    return -1;
  print_log(w, 0);
  printf("after reconcile synced=%d cache CC5=%u\n\n",
    sim_controller_is_synced(&w->ctl, SIM_CH_A),
    (unsigned)w->ctl.cache[SIM_CH_A].cc[5]);
  return 0;
}

static int demo_rate(sim_world_t *w) {
  static const uint8_t ccs[3] = {10, 11, 12};
  static const uint8_t v1[3] = {1, 2, 3};
  static const uint8_t v2[3] = {4, 5, 6};
  static const uint8_t v3[3] = {7, 8, 9};
  uint8_t before;
  printf("Pedal B morph, 50 ms per parameter\n");
  sim_world_init(w);
  sim_world_set_now(w, 0);
  if (sim_controller_set_interval(&w->ctl, SIM_CH_B, 5) != 0)
    return -1;
  if (sim_world_pump(w) != 0)
    return -1;
  before = sim_tx_sysex_count(&w->b.tx);
  if (sim_pedal_morph(&w->b, ccs, v1, 3) != 0)
    return -1;
  printf("t=0 queued %u\n",
    (unsigned)(sim_tx_sysex_count(&w->b.tx) - before));
  sim_world_set_now(w, 10);
  before = sim_tx_sysex_count(&w->b.tx);
  if (sim_pedal_morph(&w->b, ccs, v2, 3) != 0)
    return -1;
  printf("t=10 queued %u\n",
    (unsigned)(sim_tx_sysex_count(&w->b.tx) - before));
  sim_world_set_now(w, 20);
  before = sim_tx_sysex_count(&w->b.tx);
  if (sim_pedal_morph(&w->b, ccs, v3, 3) != 0)
    return -1;
  printf("t=20 queued %u\n",
    (unsigned)(sim_tx_sysex_count(&w->b.tx) - before));
  sim_world_set_now(w, 50);
  before = sim_tx_sysex_count(&w->b.tx);
  if (sim_pedal_service(&w->b, 50) != 0)
    return -1;
  printf("t=50 queued %u (final values)\n",
    (unsigned)(sim_tx_sysex_count(&w->b.tx) - before));
  sim_world_init(w);
  sim_world_set_now(w, 0);
  if (sim_controller_set_interval(&w->ctl, SIM_CH_B, 5) != 0)
    return -1;
  if (sim_world_pump(w) != 0)
    return -1;
  if (sim_pedal_morph(&w->b, ccs, v1, 3) != 0)
    return -1;
  sim_world_set_now(w, 10);
  if (sim_pedal_morph(&w->b, ccs, v2, 3) != 0)
    return -1;
  before = sim_tx_sysex_count(&w->b.tx);
  sim_world_set_now(w, 15);
  if (sim_controller_get_state(&w->ctl, SIM_CH_B) != 0)
    return -1;
  if (sim_world_pump_source(w, SIM_SRC_CONTROLLER) != 0)
    return -1;
  printf("t=15 GET_STATE during the quiet period queued %u\n",
    (unsigned)(sim_tx_sysex_count(&w->b.tx) - before));
  return 0;
}

int main(void) {
  static sim_world_t world;
  print_warning();
  sim_world_init(&world);
  printf("GET_STATE across manufacturer IDs\n");
  if (run_exchange(&world) != 0)
    return 1;
  print_log(&world, 0);
  printf("\n");
  sim_world_init(&world);
  if (demo_origins(&world) != 0)
    return 1;
  printf("\n");
  if (demo_ready(&world) != 0)
    return 1;
  if (demo_rate(&world) != 0)
    return 1;
  return 0;
}
