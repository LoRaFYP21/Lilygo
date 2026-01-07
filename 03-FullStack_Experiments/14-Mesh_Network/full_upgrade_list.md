# A. Addressing, Packet Format, and IDs (Foundation)

### A1) Replace String node names with compact IDs

- **Upgrade:** Use `uint16_t nodeId` (or `uint32_t`) instead of `"Node_1"` strings.
- **Why:** Strings + maps fragment heap and don’t scale.
- **Benefit:** predictable memory, faster parsing, smaller headers.

### A2) Add explicit “previous hop” and “next hop” fields

- **Upgrade:** Header fields: `prevHop`, `nextHop`
- **Why:** LoRa is broadcast; without next-hop enforcement, everyone hears and may forward.
- **Benefit:** controlled forwarding, huge reduction in duplicates/collisions.

### A3) Add packet class + flow/session identifiers

- **Upgrade:** `flowId/sessionId`, `msgType`, `fragId`, `fragCount`, `windowId`.
- **Why:** large transfers and multiple users need independent sessions.
- **Benefit:** parallel transfers, clean reassembly, real ARQ.

### A4) Add versioning + feature flags in header

- **Upgrade:** `protoVersion`, `flags` (supports encryption, ack type, fragmentation, etc.)
- **Benefit:** forward compatibility and staged upgrades.

---

# B. MAC/Channel Access and Congestion Control (LoRa Reality)

### B1) Random jitter on _every_ forwarded/control/ACK transmission

- **Upgrade:** add randomized delay per type (HELLO, RREQ, RREP, ACK, DATA).
- **Why:** prevents synchronization collisions.
- **Benefit:** stability as node count rises.

### B2) Exponential backoff for retries + route discovery

- **Upgrade:** retries use backoff with ceiling; RREQ uses expanding ring search (TTL 2→4→6…).
- **Benefit:** prevents network meltdown under loss.

### B3) Rate limiting (token bucket) per node and per message type

- **Upgrade:** limit RREQ/sec, DATA/sec, ACK/sec.
- **Why:** stops a single node from flooding the channel.
- **Benefit:** fairness and survival under misuse.

### B4) Congestion-aware forwarding (drop policy)

- **Upgrade:** if queue high → drop low priority, delay non-critical, reject new sessions.
- **Benefit:** protects the network from collapse.

### B5) Duty-cycle and airtime budgeting

- **Upgrade:** compute ToA and enforce airtime budgets per minute per node.
- **Benefit:** realistic “deployable” behavior and legal compliance.

---

# C. Routing: From “basic AODV-like” to Scalable Ad-Hoc Routing

### C1) Proper AODV fields and semantics

- **Upgrade:** `originator`, `dest`, `rreqId`, `destSeqNum`, `hopCount`, `metric`.
- **Why:** prevents stale routes and looping.
- **Benefit:** correctness under mobility/changes.

### C2) Maintain reverse-route state for each active RREQ

- **Upgrade:** store `(originator,rreqId) → prevHop, time)`
- **Benefit:** reliable RREP return path.

### C3) Sequence-number based loop freedom (real AODV concept)

- **Upgrade:** maintain destination sequence numbers and route freshness rules.
- **Benefit:** fewer loops, fewer stale routes.

### C4) Multi-metric route selection (not only hop count)

- **Upgrade:** use metric like:

  - ETX-style (loss rate)
  - RSSI/SNR history
  - queue load
  - airtime cost

- **Benefit:** stable routes, better throughput.

### C5) Local repair + route error propagation (RERR)

- **Upgrade:** on forward failure, send RERR upstream, attempt local repair if possible.
- **Benefit:** fast recovery instead of waiting for timeouts.

### C6) Neighbor table with link quality tracking

- **Upgrade:** maintain neighbor entries with RSSI/SNR EMA, lastSeen, LQ score.
- **Benefit:** better next hop decisions, mobility tolerance.

### C7) Cluster/hierarchical mode for very large networks (optional but “full-scale”)

- **Upgrade:** support **cluster heads** / zones:

  - intra-cluster: simple routing
  - inter-cluster: gateway nodes

- **Why:** pure flat routing floods don’t scale beyond a certain size.
- **Benefit:** order-of-magnitude scalability.

---

# D. Reliability: “Real” Delivery Under Multi-Hop and Load

### D1) Split reliability into link-layer vs end-to-end

- **Upgrade:** **Hop-by-hop reliability always enabled for reliable traffic**, plus optional end-to-end confirmation.
- **Why:** LoRa loss amplifies with hops; end-to-end only causes retransmission storms.
- **Benefit:** stable reliability with fewer retransmits.

### D2) ACK types: cumulative ACK + selective ACK

- **Upgrade:** for fragments/windows use:

  - cumulative ACK (highest contiguous received)
  - selective ACK bitmap for missing fragments

- **Benefit:** efficient high-volume transfers.

### D3) Proper ARQ engine per session (GBN/SR) with sliding window

- **Upgrade:** implement SR (best) or GBN (simpler) with:

  - window size based on airtime
  - timers per fragment (SR) / per window (GBN)

- **Benefit:** reliable files over LoRa without choking network.

### D4) Retransmission policies that are congestion-aware

- **Upgrade:** retransmit missing fragments with backoff; pause session if channel busy.
- **Benefit:** avoids total collapse.

### D5) Store-and-forward persistence for critical traffic (optional)

- **Upgrade:** buffer critical messages in flash (LittleFS) until delivered.
- **Benefit:** survives reboots, power loss, intermittent links.

---

# E. Fragmentation & Large Data Transfer (Must-have for “real use”)

### E1) Session-based bulk transfer protocol

- **Upgrade:** stages:

  1. DISCOVER/ROUTE
  2. HANDSHAKE (capabilities, MTU, window size)
  3. TRANSFER (fragments)
  4. VERIFY (hash)
  5. TEARDOWN

- **Benefit:** robust and debuggable.

### E2) Fragment headers + reassembly buffers with bounded RAM

- **Upgrade:** fixed-size reassembly slots, eviction policy, chunk buffering to flash.
- **Benefit:** can handle many senders without crashing.

### E3) Integrity verification

- **Upgrade:** per-chunk CRC + end-to-end hash (CRC32 / SHA-256).
- **Benefit:** “really usable” file transfer.

### E4) Resume support

- **Upgrade:** receiver can request missing ranges after reconnect.
- **Benefit:** practical in disaster field conditions.

---

# F. Duplicate Suppression and Flood Control (At Scale)

### F1) Replace map-based seen cache with fixed-size hash/LRU ring

- **Upgrade:** store compact `(srcId, seq)` hashes in a ring buffer / bloom filter + LRU.
- **Benefit:** predictable memory and speed.

### F2) Per-type dedup rules

- **Upgrade:** RREQ dedup key = (originator, rreqId)
  DATA dedup key = (srcId, seq or msgId)
  FRAG dedup key = (sessionId, fragId)
- **Benefit:** correctness.

### F3) Controlled flooding

- **Upgrade:** forward RREQ only if:

  - better metric than previous seen, or
  - comes from preferred neighbor, or
  - probabilistic forwarding

- **Benefit:** scale route discovery.

---

# G. Queues, Scheduling, and Fairness (Multi-user support)

### G1) Multiple queues (control, ack, realtime, bulk)

- **Upgrade:** separate queues with strict priority + weighted fairness.
- **Benefit:** bulk file transfer won’t kill text/emergency messages.

### G2) Per-flow fairness

- **Upgrade:** do not allow one sender to occupy relay capacity.
- **Benefit:** many users can coexist.

### G3) Deadline/priority support

- **Upgrade:** critical packets get deadlines; drop expired packets.
- **Benefit:** reliable emergency semantics.

---

# H. Security (Required for “real” ad-hoc use)

### H1) Authentication for control packets (RREQ/RREP/RERR/HELLO)

- **Upgrade:** HMAC (shared group key) or lightweight signature.
- **Benefit:** prevents route poisoning/spoofing.

### H2) Encryption for payload (optional but recommended)

- **Upgrade:** AES-GCM/ChaCha20-Poly1305 with nonce and replay protection.
- **Benefit:** privacy + integrity.

### H3) Replay protection

- **Upgrade:** per-node counters / time-based window.
- **Benefit:** prevents recorded packet re-injection.

_(If we need “no infrastructure”, pre-shared group keys or identity-based crypto are typical choices.)_

---

# I. Operations: Network Management and Observability

### I1) Network-wide telemetry frames

- **Upgrade:** periodic compact stats packets: queue depth, drop rate, ETX, neighbors.
- **Benefit:** we can actually debug and tune the deployment.

### I2) Event logging + pcap-style capture (optional)

- **Upgrade:** log key events (route changes, retries, drops) with timestamps.
- **Benefit:** panel/demo proof + field reliability.

### I3) Time sync strategy

- **Upgrade:** lightweight sync (gateway beacon or coarse sync) for scheduling/backoff fairness.
- **Benefit:** reduces collisions, improves coordination.

---

# J. Implementation engineering for ESP32 stability (scale-critical)

### J1) Remove heavy dynamic allocations

- **Upgrade:** avoid `std::map<String,…>`; use fixed arrays + small structs.
- **Benefit:** no heap fragmentation, long uptime.

### J2) Binary packet encoding (not string parsing)

- **Upgrade:** pack header into bytes (struct) + CRC.
- **Benefit:** smaller packets, faster, fewer bugs.

### J3) Watchdog-safe, non-blocking state machines

- **Upgrade:** eliminate busy waiting during send; event-driven loop.
- **Benefit:** handles many sessions concurrently.

---

# K. “General Network” Features people expect in real systems

### K1) Multi-destination (broadcast groups) with controlled fanout

- **Upgrade:** group IDs and group ACK policies.
- **Benefit:** alerts to many users without ACK implosion.

### K2) Mobility handling

- **Upgrade:** faster neighbor aging, route repair, metric-based stability.
- **Benefit:** nodes can move and network survives.

### K3) Gateway bridging (still ad-hoc)

- **Upgrade:** optional gateway node that can bridge to phone/Wi-Fi when available.
- **Benefit:** disaster usefulness without requiring infrastructure.

---

## What “scales” best in practice (important reality)

For **very large** LoRa meshes, a **pure flat flooding-based discovery** becomes expensive. Real deployments usually adopt one of:

- **Hierarchical clustering**, or
- **Gateway-assisted discovery**, or
- **Controlled flooding + strict next-hop forwarding + airtime budgets**

So our “full upgrade” should include a **scalable mode** (cluster or gateway-assisted) even if we keep the “fully ad-hoc flat mode” for small networks.

---
