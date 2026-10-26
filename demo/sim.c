#include "sim.h"

#include <string.h>

/* Pedal A uses one-byte fixture 0x71. Pedal B uses three-byte fixture
   00 7D 7D. Neither value is an assignment. The controller uses 0x7D,
   which is the private non-commercial ID. Do not put any of these on
   a live MIDI cable. There is no manufacturer registry. */

static const uint8_t pedal_b_id[3] = {0x00, 0x7d, 0x7d};

static void q_clear(sim_queue_t *q) {
  q->head = 0;
  q->count = 0;
}

void sim_tx_init(sim_tx_t *tx) {
  sim_tx_clear(tx);
}

void sim_tx_clear(sim_tx_t *tx) {
  q_clear(&tx->voice);
  q_clear(&tx->sysex);
  tx->rt_head = 0;
  tx->rt_count = 0;
  tx->active_valid = 0;
  tx->active_kind = 0;
  tx->active_len = 0;
  tx->active_pos = 0;
}

int sim_tx_busy(const sim_tx_t *tx) {
  if (tx->rt_count || tx->active_valid)
    return 1;
  if (tx->voice.count || tx->sysex.count)
    return 1;
  return 0;
}

int sim_tx_sysex_pending(const sim_tx_t *tx) {
  if (tx->sysex.count)
    return 1;
  if (tx->active_valid && tx->active_kind == SIM_FRAME_SYSEX)
    return 1;
  return 0;
}

uint8_t sim_tx_sysex_count(const sim_tx_t *tx) {
  return tx->sysex.count;
}

static int q_push(sim_queue_t *q, const uint8_t *data, uint8_t len) {
  sim_frame_t *frame;
  uint8_t idx;
  uint8_t i;
  if (!len || len > MSU_MAX_PACKET || q->count >= SIM_Q)
    return -1;
  idx = (uint8_t)((q->head + q->count) % SIM_Q);
  frame = &q->frame[idx];
  frame->len = len;
  for (i = 0; i < len; i++)
    frame->data[i] = data[i];
  q->count++;
  return 0;
}

static int q_pop(sim_queue_t *q, sim_frame_t *out) {
  uint8_t i;
  if (!q->count)
    return -1;
  out->len = q->frame[q->head].len;
  for (i = 0; i < out->len; i++)
    out->data[i] = q->frame[q->head].data[i];
  q->head = (uint8_t)((q->head + 1u) % SIM_Q);
  q->count--;
  return 0;
}

int sim_tx_push(sim_tx_t *tx, int kind, const uint8_t *data, uint8_t len) {
  if (kind == SIM_FRAME_VOICE)
    return q_push(&tx->voice, data, len);
  if (kind == SIM_FRAME_SYSEX)
    return q_push(&tx->sysex, data, len);
  return -1;
}

int sim_tx_push_rt(sim_tx_t *tx, uint8_t byte) {
  uint8_t idx;
  if (byte < 0xf8 || tx->rt_count >= SIM_RT_MAX)
    return -1;
  idx = (uint8_t)((tx->rt_head + tx->rt_count) % SIM_RT_MAX);
  tx->rt[idx] = byte;
  tx->rt_count++;
  return 0;
}

static void start_frame(sim_tx_t *tx, const sim_frame_t *frame, uint8_t kind) {
  uint8_t i;
  tx->active_valid = 1;
  tx->active_kind = kind;
  tx->active_len = frame->len;
  tx->active_pos = 0;
  for (i = 0; i < frame->len; i++)
    tx->active[i] = frame->data[i];
}

int sim_tx_pump(sim_tx_t *tx, uint8_t *byte) {
  sim_frame_t frame;
  if (tx->rt_count) {
    *byte = tx->rt[tx->rt_head];
    tx->rt_head = (uint8_t)((tx->rt_head + 1u) % SIM_RT_MAX);
    tx->rt_count--;
    return 1;
  }
  if (!tx->active_valid) {
    if (tx->voice.count) {
      if (q_pop(&tx->voice, &frame) != 0)
        return 0;
      start_frame(tx, &frame, SIM_FRAME_VOICE);
    } else if (tx->sysex.count) {
      if (q_pop(&tx->sysex, &frame) != 0)
        return 0;
      start_frame(tx, &frame, SIM_FRAME_SYSEX);
    } else {
      return 0;
    }
  }
  if (!tx->active_len || tx->active_pos >= tx->active_len) {
    tx->active_valid = 0;
    return 0;
  }
  *byte = tx->active[tx->active_pos++];
  if (tx->active_pos == tx->active_len)
    tx->active_valid = 0;
  return 1;
}

int sim_tx_sysex_at(const sim_tx_t *tx, uint8_t index, uint8_t *dst,
    uint8_t *len) {
  const sim_frame_t *frame;
  uint8_t idx;
  uint8_t i;
  if (index >= tx->sysex.count)
    return -1;
  idx = (uint8_t)((tx->sysex.head + index) % SIM_Q);
  frame = &tx->sysex.frame[idx];
  for (i = 0; i < frame->len; i++)
    dst[i] = frame->data[i];
  *len = frame->len;
  return 0;
}

int sim_tx_drop_last_sysex(sim_tx_t *tx) {
  if (!tx->sysex.count)
    return -1;
  tx->sysex.count--;
  return 0;
}

static int queue_msg(sim_tx_t *tx, const msu_message_t *msg) {
  uint8_t buf[MSU_MAX_PACKET];
  size_t n = 0;
  if (msu_encode(msg, buf, sizeof buf, &n) != MSU_OK)
    return -1;
  return sim_tx_push(tx, SIM_FRAME_SYSEX, buf, (uint8_t)n);
}

static void bind_id(msu_message_t *msg, const uint8_t *id, uint8_t id_len,
    uint8_t type, uint8_t channel) {
  uint8_t i;
  memset(msg, 0, sizeof *msg);
  msg->id_len = id_len;
  for (i = 0; i < id_len; i++)
    msg->id[i] = id[i];
  msg->type = type;
  msg->channel = channel;
}

static void refresh_snapshot(sim_pedal_t *p);

static int pedal_on_surface(const sim_pedal_t *p, uint8_t cc) {
  uint8_t i;
  for (i = 0; i < p->surface_n; i++) {
    if (p->surface[i] == cc)
      return 1;
  }
  return 0;
}

static int emit_cc7(sim_pedal_t *p, uint8_t cc) {
  msu_message_t msg;
  refresh_snapshot(p);
  bind_id(&msg, p->id, p->id_len, MSU_CC7, p->channel);
  msg.u.cc7.cc = cc;
  msg.u.cc7.value = p->cc[cc];
  if (queue_msg(&p->tx, &msg) != 0)
    return -1;
  p->pend[cc].dirty = 0;
  p->pend[cc].ever_sent = 1;
  p->pend[cc].value = p->cc[cc];
  p->pend[cc].last_sent_ms = p->now_ms;
  return 0;
}

static int emit_pc(sim_pedal_t *p, uint8_t program) {
  msu_message_t msg;
  refresh_snapshot(p);
  if (program > 0x7f)
    return -1;
  if (p->have_program && p->program == program)
    return 0;
  p->program = program;
  p->have_program = 1;
  bind_id(&msg, p->id, p->id_len, MSU_PC, p->channel);
  msg.u.program = program;
  return queue_msg(&p->tx, &msg);
}

static int queue_current_cc(sim_pedal_t *p, uint8_t cc) {
  msu_message_t msg;
  bind_id(&msg, p->id, p->id_len, MSU_CC7, p->channel);
  msg.u.cc7.cc = cc;
  msg.u.cc7.value = p->cc[cc];
  return queue_msg(&p->tx, &msg);
}

static int queue_current_pc(sim_pedal_t *p) {
  msu_message_t msg;
  bind_id(&msg, p->id, p->id_len, MSU_PC, p->channel);
  msg.u.program = p->program;
  return queue_msg(&p->tx, &msg);
}

static int queue_simple(sim_pedal_t *p, uint8_t type) {
  msu_message_t msg;
  bind_id(&msg, p->id, p->id_len, type, p->channel);
  return queue_msg(&p->tx, &msg);
}

static void note_reported(sim_pedal_t *p, uint8_t cc) {
  p->pend[cc].dirty = 0;
  p->pend[cc].ever_sent = 1;
  p->pend[cc].value = p->cc[cc];
  p->pend[cc].last_sent_ms = p->now_ms;
}

static void refresh_snapshot(sim_pedal_t *p) {
  if (p->snapshot_active && !sim_tx_sysex_pending(&p->tx))
    p->snapshot_active = 0;
}

static void rollback_sysex(sim_pedal_t *p, uint8_t queued) {
  p->tx.sysex.count = queued;
}

static int queue_snapshot(sim_pedal_t *p) {
  uint8_t i;
  uint8_t queued;
  refresh_snapshot(p);
  if (p->snapshot_active)
    return 0;
  queued = p->tx.sysex.count;
  if (queue_simple(p, MSU_STATE_BEGIN) != 0)
    return -1;
  for (i = 0; i < p->snap_n; i++) {
    int rc;
    if (p->snap[i].kind == SIM_SNAP_PC)
      rc = queue_current_pc(p);
    else
      rc = queue_current_cc(p, p->snap[i].cc);
    if (rc != 0) {
      rollback_sysex(p, queued);
      return -1;
    }
  }
  if (queue_simple(p, MSU_STATE_END) != 0) {
    rollback_sysex(p, queued);
    return -1;
  }
  for (i = 0; i < p->snap_n; i++) {
    if (p->snap[i].kind == SIM_SNAP_CC)
      note_reported(p, p->snap[i].cc);
  }
  p->snapshot_active = 1;
  return 0;
}

static int eligible(const sim_pedal_t *p, uint8_t cc) {
  const sim_pend_t *slot = &p->pend[cc];
  uint32_t elapsed;
  if (!slot->dirty)
    return 0;
  if (!p->continuous[cc] || p->interval_ms == 0 || !slot->ever_sent)
    return 1;
  elapsed = p->now_ms - slot->last_sent_ms;
  return elapsed >= p->interval_ms;
}

static int flush_continuous(sim_pedal_t *p) {
  uint8_t list[MSU_BATCH_MAX];
  uint8_t n;
  uint8_t cc;
  uint8_t i;
  int guard = 0;
  while (guard++ < 20) {
    msu_message_t msg;
    n = 0;
    for (cc = 0; cc < 128; cc++) {
      if (!eligible(p, cc))
        continue;
      list[n++] = cc;
      if (n == MSU_BATCH_MAX)
        break;
    }
    if (!n)
      return 0;
    if (n == 1) {
      if (emit_cc7(p, list[0]) != 0)
        return -1;
      continue;
    }
    bind_id(&msg, p->id, p->id_len, MSU_CC7_BATCH, p->channel);
    msg.u.batch.count = n;
    for (i = 0; i < n; i++) {
      msg.u.batch.item[i].cc = list[i];
      msg.u.batch.item[i].value = p->cc[list[i]];
    }
    if (queue_msg(&p->tx, &msg) != 0)
      return -1;
    for (i = 0; i < n; i++)
      note_reported(p, list[i]);
  }
  return -1;
}

static int apply_cc(sim_pedal_t *p, uint8_t cc, uint8_t value) {
  if (cc > 0x7f || value > 0x7f || !pedal_on_surface(p, cc))
    return -1;
  if (p->have_cc[cc] && p->cc[cc] == value)
    return 0;
  p->cc[cc] = value;
  p->have_cc[cc] = 1;
  if (p->continuous[cc] && p->interval_ms != 0) {
    p->pend[cc].dirty = 1;
    p->pend[cc].value = value;
    return 0;
  }
  return emit_cc7(p, cc);
}

static uint8_t voice_data_len(uint8_t status) {
  switch (status & 0xf0) {
    case 0x80:
    case 0x90:
    case 0xa0:
    case 0xb0:
    case 0xe0:
      return 2;
    case 0xc0:
    case 0xd0:
      return 1;
    default:
      return 0;
  }
}

static void voice_dispatch(sim_pedal_t *p) {
  uint8_t status = p->voice_rx.status;
  uint8_t channel = (uint8_t)(status & 0x0f);
  if (channel != p->channel)
    return;
  if ((status & 0xf0) == 0xb0)
    apply_cc(p, p->voice_rx.data[0], p->voice_rx.data[1]);
  else if ((status & 0xf0) == 0xc0)
    emit_pc(p, p->voice_rx.data[0]);
}

static void voice_cancel_running(sim_pedal_t *p) {
  p->voice_rx.status = 0;
  p->voice_rx.expected = 0;
  p->voice_rx.got = 0;
}

static void voice_rx(sim_pedal_t *p, uint8_t byte) {
  sim_voice_rx_t *voice = &p->voice_rx;
  if (byte >= 0x80) {
    /* System Common cancels running status. F8-FF never reach this
       function; real-time bytes must leave a channel status in place. */
    if (byte >= 0xf0) {
      voice_cancel_running(p);
      return;
    }
    voice->status = byte;
    voice->expected = voice_data_len(byte);
    voice->got = 0;
    return;
  }
  if (!voice->status || !voice->expected)
    return;
  voice->data[voice->got++] = byte;
  if (voice->got >= voice->expected) {
    voice_dispatch(p);
    voice->got = 0;
  }
}

static void pedal_on_msu(sim_pedal_t *p, const msu_message_t *msg) {
  if (msg->type == MSU_GET_STATE) {
    if (msg->channel == p->channel)
      queue_snapshot(p);
    return;
  }
  if (msg->type == MSU_SET_UPDATE_INTERVAL) {
    if (msg->channel == p->channel)
      p->interval_ms = (uint16_t)(msg->u.period_10ms * 10u);
    return;
  }
}

void sim_pedal_rx(sim_pedal_t *p, uint8_t byte) {
  msu_message_t msg;
  uint8_t raw = 0;
  msu_feed_t ev;
  refresh_snapshot(p);
  /* F0 is consumed by the SysEx parser and is not delivered as a voice
     byte. Running status ends at that status, including when the SysEx
     is not MSU. F8-FF stay out of this branch. */
  if (byte == 0xf0)
    voice_cancel_running(p);
  ev = msu_parser_feed(&p->parser, byte, &msg, &raw);
  if (ev == MSU_FEED_MESSAGE)
    pedal_on_msu(p, &msg);
  else if (ev == MSU_FEED_BYTE)
    voice_rx(p, raw);
}

int sim_pedal_knob(sim_pedal_t *p, uint8_t cc, uint8_t value) {
  return apply_cc(p, cc, value);
}

int sim_pedal_recall(sim_pedal_t *p, uint8_t program, const uint8_t *ccs,
    const uint8_t *values, uint8_t n) {
  uint8_t i;
  if (emit_pc(p, program) != 0)
    return -1;
  for (i = 0; i < n; i++) {
    if (apply_cc(p, ccs[i], values[i]) != 0)
      return -1;
  }
  return 0;
}

int sim_pedal_morph(sim_pedal_t *p, const uint8_t *ccs, const uint8_t *values,
    uint8_t n) {
  uint8_t i;
  for (i = 0; i < n; i++) {
    if (apply_cc(p, ccs[i], values[i]) != 0)
      return -1;
  }
  return flush_continuous(p);
}

int sim_pedal_service(sim_pedal_t *p, uint32_t now_ms) {
  p->now_ms = now_ms;
  return flush_continuous(p);
}

int sim_pedal_restart(sim_pedal_t *p) {
  uint8_t cc;
  p->interval_ms = p->default_interval_ms;
  p->snapshot_active = 0;
  for (cc = 0; cc < 128; cc++) {
    p->pend[cc].dirty = 0;
    p->pend[cc].ever_sent = 0;
  }
  return queue_simple(p, MSU_READY);
}

static void release_gets(sim_controller_t *c) {
  uint8_t ch;
  if (sim_tx_sysex_pending(&c->tx))
    return;
  for (ch = 0; ch < 16; ch++)
    c->get_pending[ch] = 0;
}

int sim_controller_get_state(sim_controller_t *c, uint8_t channel) {
  msu_message_t msg;
  if (channel > 0x0f)
    return -1;
  release_gets(c);
  if (c->get_pending[channel])
    return 0;
  bind_id(&msg, c->id, c->id_len, MSU_GET_STATE, channel);
  if (queue_msg(&c->tx, &msg) != 0)
    return -1;
  c->get_pending[channel] = 1;
  c->cache[channel].saw_begin = 0;
  c->cache[channel].sync = SIM_SYNC_AWAITING;
  return 0;
}

int sim_controller_reconcile(sim_controller_t *c, uint8_t channel) {
  return sim_controller_get_state(c, channel);
}

int sim_controller_set_interval(sim_controller_t *c, uint8_t channel,
    uint8_t period_10ms) {
  msu_message_t msg;
  if (channel > 0x0f || period_10ms > 0x7f)
    return -1;
  bind_id(&msg, c->id, c->id_len, MSU_SET_UPDATE_INTERVAL, channel);
  msg.u.period_10ms = period_10ms;
  return queue_msg(&c->tx, &msg);
}

static void apply_update(sim_controller_t *c, const msu_message_t *msg) {
  sim_cache_t *cache;
  uint8_t i;
  if (msg->channel > 0x0f)
    return;
  cache = &c->cache[msg->channel];
  switch (msg->type) {
    case MSU_READY:
      if (sim_controller_get_state(c, msg->channel) != 0) {
        cache->saw_begin = 0;
        cache->sync = SIM_SYNC_NONE;
      }
      break;
    case MSU_STATE_BEGIN:
      if (cache->sync == SIM_SYNC_AWAITING ||
          cache->sync == SIM_SYNC_IN_PROGRESS) {
        cache->saw_begin = 1;
        cache->sync = SIM_SYNC_IN_PROGRESS;
      }
      break;
    case MSU_STATE_END:
      if (cache->saw_begin && cache->sync == SIM_SYNC_IN_PROGRESS) {
        cache->sync = SIM_SYNC_COMPLETE;
        cache->saw_begin = 0;
        c->get_pending[msg->channel] = 0;
      }
      break;
    case MSU_PC:
      cache->program = msg->u.program;
      cache->have_program = 1;
      c->msu_updates++;
      break;
    case MSU_BANKED_PC:
      cache->bank_msb = msg->u.banked_pc.bank_msb;
      cache->bank_lsb = msg->u.banked_pc.bank_lsb;
      cache->have_bank = 1;
      cache->program = msg->u.banked_pc.program;
      cache->have_program = 1;
      c->msu_updates++;
      break;
    case MSU_CC7:
      cache->cc[msg->u.cc7.cc] = msg->u.cc7.value;
      cache->have_cc[msg->u.cc7.cc] = 1;
      c->msu_updates++;
      break;
    case MSU_CC7_BATCH:
      for (i = 0; i < msg->u.batch.count; i++) {
        uint8_t cc = msg->u.batch.item[i].cc;
        cache->cc[cc] = msg->u.batch.item[i].value;
        cache->have_cc[cc] = 1;
      }
      c->msu_updates++;
      break;
    case MSU_CC14:
      cache->cc14_msb = msg->u.cc14.cc_msb;
      cache->cc14_value = msg->u.cc14.value;
      cache->have_cc14 = 1;
      c->msu_updates++;
      break;
    case MSU_NRPN7:
      cache->nrpn = msg->u.nrpn7.nrpn;
      cache->nrpn_value = msg->u.nrpn7.value;
      cache->have_nrpn = 1;
      c->msu_updates++;
      break;
    case MSU_NRPN14:
      cache->nrpn = msg->u.nrpn14.nrpn;
      cache->nrpn_value = msg->u.nrpn14.value;
      cache->have_nrpn = 1;
      c->msu_updates++;
      break;
    case MSU_TEMPO:
      cache->tempo_tenths = msg->u.tempo_tenths;
      cache->have_tempo = 1;
      c->msu_updates++;
      break;
    case MSU_TRANSPORT:
      cache->transport = msg->u.transport;
      cache->have_transport = 1;
      c->msu_updates++;
      break;
    default:
      break;
  }
}

void sim_controller_rx(sim_controller_t *c, uint8_t byte) {
  msu_message_t msg;
  uint8_t raw = 0;
  msu_feed_t ev = msu_parser_feed(&c->parser, byte, &msg, &raw);
  if (ev == MSU_FEED_MESSAGE)
    apply_update(c, &msg);
  (void)raw;
}

int sim_controller_is_synced(const sim_controller_t *c, uint8_t channel) {
  if (channel > 0x0f)
    return 0;
  return c->cache[channel].sync == SIM_SYNC_COMPLETE;
}

static void add_cc(sim_pedal_t *p, uint8_t cc, uint8_t value, int continuous) {
  p->surface[p->surface_n++] = cc;
  p->cc[cc] = value;
  p->have_cc[cc] = 1;
  p->continuous[cc] = (uint8_t)(continuous ? 1 : 0);
}

static void add_snap_cc(sim_pedal_t *p, uint8_t cc) {
  p->snap[p->snap_n].kind = SIM_SNAP_CC;
  p->snap[p->snap_n].cc = cc;
  p->snap_n++;
}

static void init_pedal_a(sim_pedal_t *p) {
  memset(p, 0, sizeof *p);
  p->id[0] = SIM_ID_PEDAL_A;
  p->id_len = 1;
  p->channel = SIM_CH_A;
  p->program = 12;
  p->have_program = 1;
  add_cc(p, 24, 3, 0);
  add_cc(p, 102, 64, 0);
  add_cc(p, 5, 83, 0);
  p->snap[0].kind = SIM_SNAP_PC;
  p->snap_n = 1;
  add_snap_cc(p, 24);
  add_snap_cc(p, 102);
  add_snap_cc(p, 5);
  msu_parser_init(&p->parser);
  sim_tx_init(&p->tx);
}

static void init_pedal_b(sim_pedal_t *p) {
  memset(p, 0, sizeof *p);
  p->id[0] = pedal_b_id[0];
  p->id[1] = pedal_b_id[1];
  p->id[2] = pedal_b_id[2];
  p->id_len = 3;
  p->channel = SIM_CH_B;
  add_cc(p, 10, 10, 1);
  add_cc(p, 11, 20, 1);
  add_cc(p, 12, 30, 1);
  add_snap_cc(p, 10);
  add_snap_cc(p, 11);
  add_snap_cc(p, 12);
  msu_parser_init(&p->parser);
  sim_tx_init(&p->tx);
}

void sim_world_init(sim_world_t *w) {
  memset(w, 0, sizeof *w);
  w->ctl.id[0] = SIM_ID_CONTROLLER;
  w->ctl.id_len = 1;
  msu_parser_init(&w->ctl.parser);
  sim_tx_init(&w->ctl.tx);
  init_pedal_a(&w->a);
  init_pedal_b(&w->b);
}

void sim_world_set_now(sim_world_t *w, uint32_t now_ms) {
  w->now_ms = now_ms;
  w->a.now_ms = now_ms;
  w->b.now_ms = now_ms;
}

void sim_log_reset(sim_world_t *w) {
  w->log_len = 0;
  w->overflow = 0;
}

static int pump_from(sim_world_t *w, int src, sim_tx_t *tx) {
  uint8_t byte = 0;
  if (!sim_tx_pump(tx, &byte))
    return 0;
  if (w->log_len >= SIM_LOG_MAX) {
    w->overflow = 1;
    return -1;
  }
  w->log[w->log_len].src = (uint8_t)src;
  w->log[w->log_len].byte = byte;
  w->log_len++;
  if (src == SIM_SRC_CONTROLLER) {
    sim_pedal_rx(&w->a, byte);
    sim_pedal_rx(&w->b, byte);
  } else {
    sim_controller_rx(&w->ctl, byte);
  }
  return 1;
}

int sim_world_pump(sim_world_t *w) {
  int guard = 0;
  while (sim_tx_busy(&w->ctl.tx) || sim_tx_busy(&w->a.tx) ||
      sim_tx_busy(&w->b.tx)) {
    int rc;
    if (sim_tx_busy(&w->ctl.tx))
      rc = pump_from(w, SIM_SRC_CONTROLLER, &w->ctl.tx);
    else if (sim_tx_busy(&w->a.tx))
      rc = pump_from(w, SIM_SRC_PEDAL_A, &w->a.tx);
    else
      rc = pump_from(w, SIM_SRC_PEDAL_B, &w->b.tx);
    if (rc <= 0)
      return -1;
    if (++guard > 10000)
      return -1;
  }
  return w->overflow ? -1 : 0;
}

int sim_world_pump_source(sim_world_t *w, int src) {
  sim_tx_t *tx;
  int guard = 0;
  if (src == SIM_SRC_CONTROLLER)
    tx = &w->ctl.tx;
  else if (src == SIM_SRC_PEDAL_A)
    tx = &w->a.tx;
  else if (src == SIM_SRC_PEDAL_B)
    tx = &w->b.tx;
  else
    return -1;
  while (sim_tx_busy(tx)) {
    int rc = pump_from(w, src, tx);
    if (rc <= 0)
      return -1;
    if (++guard > 10000)
      return -1;
  }
  return w->overflow ? -1 : 0;
}
