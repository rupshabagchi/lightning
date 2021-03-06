#ifndef LIGHTNING_DAEMON_PEER_H
#define LIGHTNING_DAEMON_PEER_H
#include "config.h"
#include "bitcoin/locktime.h"
#include "bitcoin/privkey.h"
#include "bitcoin/pubkey.h"
#include "bitcoin/script.h"
#include "bitcoin/shadouble.h"
#include "funding.h"
#include "lightning.pb-c.h"
#include "netaddr.h"
#include "state.h"
#include <ccan/crypto/sha256/sha256.h>
#include <ccan/crypto/shachain/shachain.h>
#include <ccan/list/list.h>
#include <ccan/time/time.h>

enum htlc_stage_type {
	HTLC_ADD,
	HTLC_FULFILL,
	HTLC_FAIL
};

struct htlc_add {
	enum htlc_stage_type add;
	struct channel_htlc htlc;
};

struct htlc_fulfill {
	enum htlc_stage_type fulfill;
	u64 id;
	struct sha256 r;
};

struct htlc_fail {
	enum htlc_stage_type fail;
	u64 id;
};

union htlc_staging {
	enum htlc_stage_type type;
	struct htlc_add add;
	struct htlc_fulfill fulfill;
	struct htlc_fail fail;
};

struct anchor_input {
	struct sha256_double txid;
	unsigned int index;
	/* Amount of input (satoshis) */
	u64 amount;
	/* Wallet entry to use to spend. */
	struct wallet *w;
};

struct commit_info {
	/* Previous one, if any. */
	struct commit_info *prev;
	/* Commit number (0 == from open) */
	u64 commit_num;
	/* Revocation hash. */
	struct sha256 revocation_hash;
	/* Commit tx. */
	struct bitcoin_tx *tx;
	/* Channel state for this tx. */
	struct channel_state *cstate;
	/* Other side's signature for last commit tx (if known) */
	struct bitcoin_signature *sig;
	/* Map for permutation: see commit_tx.c */
	int *map;
	/* Revocation preimage (if known). */
	struct sha256 *revocation_preimage;
	/* unacked changes (already applied to staging_cstate) */
	union htlc_staging *unacked_changes;
};

struct peer_visible_state {
	/* CMD_OPEN_WITH_ANCHOR or CMD_OPEN_WITHOUT_ANCHOR */
	enum state_input offer_anchor;
	/* Key for commitment tx inputs, then key for commitment tx outputs */
	struct pubkey commitkey, finalkey;
	/* How long to they want the other's outputs locked (seconds) */
	struct rel_locktime locktime;
	/* Minimum depth of anchor before channel usable. */
	unsigned int mindepth;
	/* Commitment fee they're offering (satoshi). */
	u64 commit_fee_rate;
	/* Revocation hash for next commit tx. */
	struct sha256 next_revocation_hash;
	/* Commit txs: last one is current. */
	struct commit_info *commit;

	/* cstate to generate next commitment tx. */
	struct channel_state *staging_cstate;
};

struct htlc_progress {
	/* The HTLC we're working on. */
	union htlc_staging stage;
};

struct out_pkt {
	Pkt *pkt;
	void (*ack_cb)(struct peer *peer, void *arg);
	void *ack_arg;
};

/* Off peer->outgoing_txs */
struct outgoing_tx {
	struct list_node list;
	const struct bitcoin_tx *tx;
	struct sha256_double txid;
};

struct peer {
	/* dstate->peers list */
	struct list_node list;

	/* State in state machine. */
	enum state state;

	/* Network connection. */
	struct io_conn *conn;

	/* If we're doing a commit, this is the command which triggered it */
	struct command *commit_jsoncmd;

	/* Global state. */
	struct lightningd_state *dstate;

	/* The other end's address. */
	struct netaddr addr;

	/* Their ID. */
	struct pubkey id;

	/* Current received packet. */
	Pkt *inpkt;

	/* Queue of output packets. */
	Pkt **outpkt;

	/* Anchor tx output */
	struct {
		struct sha256_double txid;
		unsigned int index;
		u64 satoshis;
		u8 *witnessscript;

		/* If we're creating anchor, this tells us where to source it */
		struct anchor_input *input;
	
		/* If we created it, we keep entire tx. */
		const struct bitcoin_tx *tx;
		struct anchor_watch *watches;
	} anchor;

	struct {
		/* Their signature for our current commit sig. */
		struct bitcoin_signature theirsig;
		/* The watch we have on a live commit tx. */
		struct txwatch *watch;
	} cur_commit;

	/* Counter to make unique HTLC ids. */
	u64 htlc_id_counter;

	/* Mutual close info. */
	struct {
		/* Our last suggested closing fee. */
		u64 our_fee;
		/* If they've offered a signature, these are set: */
		struct bitcoin_signature *their_sig;
		/* If their_sig is non-NULL, this is the fee. */
		u64 their_fee;
		/* scriptPubKey we/they want for closing. */
		u8 *our_script, *their_script;
	} closing;

	/* If we're closing on-chain */
	struct {
		/* Everything (watches, resolved[], etc) tal'ed off this */
		const struct bitcoin_tx *tx;
		const struct commit_info *ci;
		const struct bitcoin_tx **resolved;
	} closing_onchain;
	
	/* If not INPUT_NONE, send this when we have no more HTLCs. */
	enum state_input cleared;

	/* Current ongoing packetflow */
	struct io_data *io_data;
	
	/* What happened. */
	struct log *log;

	/* Things we're watching for (see watches.c) */
	struct list_head watches;

	/* Bitcoin transctions we're broadcasting (see chaintopology.c) */
	struct list_head outgoing_txs;
	
	/* Timeout for close_watch. */
	struct oneshot *close_watch_timeout;

	/* Timeout for collecting changes before sending commit. */
	struct oneshot *commit_timer;
	
	/* Private keys for dealing with this peer. */
	struct peer_secrets *secrets;

	/* For testing. */
	bool fake_close;
	bool output_enabled;

	/* Stuff we have in common. */
	struct peer_visible_state local, remote;

	/* this is where we will store their revocation preimages*/
	struct shachain their_preimages;
};

void setup_listeners(struct lightningd_state *dstate, unsigned int portnum);

/* Populates very first peer->{local,remote}.commit->{tx,cstate} */
bool setup_first_commit(struct peer *peer);

/* Set up timer: we have something we can commit. */
void remote_changes_pending(struct peer *peer);

/* Add this unacked change */
void add_unacked(struct peer_visible_state *which,
		 const union htlc_staging *stage);

/* Peer has recieved revocation, or problem (if non-NULL). */
void peer_update_complete(struct peer *peer, const char *problem);

/* Peer has completed open, or problem (if non-NULL). */
void peer_open_complete(struct peer *peer, const char *problem);

void peer_add_htlc_expiry(struct peer *peer,
			  const struct abs_locktime *expiry);

struct bitcoin_tx *peer_create_close_tx(struct peer *peer, u64 fee);

uint64_t commit_tx_fee(const struct bitcoin_tx *commit,
		       uint64_t anchor_satoshis);

bool resolve_one_htlc(struct peer *peer, u64 id, const struct sha256 *preimage);
#endif /* LIGHTNING_DAEMON_PEER_H */
