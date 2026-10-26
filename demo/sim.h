#ifndef MSU_SIM_H
#define MSU_SIM_H

#include "msu.h"

/* Host-side controller and pedal simulation. Not part of the wire codec.
   Manufacturer IDs here are fixtures and must not be transmitted. */

enum {
  SIM_SRC_CONTROLLER = 0,
  SIM_SRC_PEDAL_A = 1,
  SIM_SRC_PEDAL_B = 2,
  SIM_FRAME_VOICE = 0,
  SIM_FRAME_SYSEX = 1,
  SIM_Q = 16,
  SIM_RT_MAX = 8,
  SIM_LOG_MAX = 2048,
  SIM_SYNC_NONE = 0,
  SIM_SYNC_AWAITING = 1,
  SIM_SYNC_IN_PROGRESS = 2,
  SIM_SYNC_COMPLETE = 3,
  SIM_SNAP_PC = 0,
  SIM_SNAP_CC = 1,
  SIM_SURFACE = 8,
  SIM_ID_CONTROLLER = 0x7d,
  SIM_ID_PEDAL_A = 0x71,
  SIM_CH_A = 0,
  SIM_CH_B = 1
};

typedef struct {
  uint8_t data[MSU_MAX_PACKET];
  uint8_t len;
} sim_frame_t;

typedef struct {
  sim_frame_t frame[SIM_Q];
  uint8_t head;
  uint8_t count;
} sim_queue_t;

typedef struct {
  sim_queue_t voice;
  sim_queue_t sysex;
  uint8_t rt[SIM_RT_MAX];
  uint8_t rt_head;
  uint8_t rt_count;
  uint8_t active_valid;
  uint8_t active_kind;
  uint8_t active[MSU_MAX_PACKET];
  uint8_t active_len;
  uint8_t active_pos;
} sim_tx_t;

typedef struct {
  uint8_t status;
  uint8_t expected;
  uint8_t got;
  uint8_t data[2];
} sim_voice_rx_t;

typedef struct {
  uint8_t dirty;
  uint8_t ever_sent;
  uint8_t value;
  uint32_t last_sent_ms;
} sim_pend_t;

typedef struct {
  uint8_t kind;
  uint8_t cc;
} sim_snap_t;

typedef struct {
  uint8_t id[3];
  uint8_t id_len;
  uint8_t channel;
  uint8_t program;
  uint8_t have_program;
  uint8_t cc[128];
  uint8_t have_cc[128];
  uint8_t surface[SIM_SURFACE];
  uint8_t surface_n;
  uint8_t continuous[128];
  sim_snap_t snap[SIM_SURFACE];
  uint8_t snap_n;
  uint16_t interval_ms;
  uint16_t default_interval_ms;
  sim_pend_t pend[128];
  uint8_t snapshot_active;
  uint32_t now_ms;
  msu_parser_t parser;
  sim_voice_rx_t voice_rx;
  sim_tx_t tx;
} sim_pedal_t;

typedef struct {
  uint8_t program;
  uint8_t have_program;
  uint8_t bank_msb;
  uint8_t bank_lsb;
  uint8_t have_bank;
  uint8_t cc[128];
  uint8_t have_cc[128];
  uint8_t sync;
  uint8_t saw_begin;
  uint16_t tempo_tenths;
  uint8_t have_tempo;
  uint8_t transport;
  uint8_t have_transport;
  uint8_t cc14_msb;
  uint16_t cc14_value;
  uint8_t have_cc14;
  uint16_t nrpn;
  uint16_t nrpn_value;
  uint8_t have_nrpn;
} sim_cache_t;

typedef struct {
  uint8_t id[3];
  uint8_t id_len;
  sim_cache_t cache[16];
  uint8_t get_pending[16];
  msu_parser_t parser;
  sim_tx_t tx;
  uint32_t msu_updates;
} sim_controller_t;

typedef struct {
  uint8_t src;
  uint8_t byte;
} sim_log_ent_t;

typedef struct {
  sim_controller_t ctl;
  sim_pedal_t a;
  sim_pedal_t b;
  sim_log_ent_t log[SIM_LOG_MAX];
  uint16_t log_len;
  int overflow;
  uint32_t now_ms;
} sim_world_t;

void sim_tx_init(sim_tx_t *tx);
int sim_tx_push(sim_tx_t *tx, int kind, const uint8_t *data, uint8_t len);
int sim_tx_push_rt(sim_tx_t *tx, uint8_t byte);
int sim_tx_pump(sim_tx_t *tx, uint8_t *byte);
int sim_tx_busy(const sim_tx_t *tx);
int sim_tx_sysex_pending(const sim_tx_t *tx);
uint8_t sim_tx_sysex_count(const sim_tx_t *tx);
int sim_tx_sysex_at(const sim_tx_t *tx, uint8_t index, uint8_t *dst,
  uint8_t *len);
int sim_tx_drop_last_sysex(sim_tx_t *tx);
void sim_tx_clear(sim_tx_t *tx);

void sim_world_init(sim_world_t *w);
void sim_world_set_now(sim_world_t *w, uint32_t now_ms);
void sim_log_reset(sim_world_t *w);
int sim_world_pump(sim_world_t *w);
int sim_world_pump_source(sim_world_t *w, int src);

int sim_controller_get_state(sim_controller_t *c, uint8_t channel);
int sim_controller_reconcile(sim_controller_t *c, uint8_t channel);
int sim_controller_set_interval(sim_controller_t *c, uint8_t channel,
  uint8_t period_10ms);
void sim_controller_rx(sim_controller_t *c, uint8_t byte);
int sim_controller_is_synced(const sim_controller_t *c, uint8_t channel);

void sim_pedal_rx(sim_pedal_t *p, uint8_t byte);
int sim_pedal_knob(sim_pedal_t *p, uint8_t cc, uint8_t value);
int sim_pedal_recall(sim_pedal_t *p, uint8_t program, const uint8_t *ccs,
  const uint8_t *values, uint8_t n);
int sim_pedal_morph(sim_pedal_t *p, const uint8_t *ccs, const uint8_t *values,
  uint8_t n);
int sim_pedal_service(sim_pedal_t *p, uint32_t now_ms);
int sim_pedal_restart(sim_pedal_t *p);

#endif
