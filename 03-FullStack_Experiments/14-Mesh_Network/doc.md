# LoRa Intelligent Mesh Network (Unified Node Firmware)

## Technical Documentation for Panel Presentation

### 1. Purpose and Scope

This implementation provides a **multi-hop LoRa mesh network** for ESP32 + SX127x devices using a **single unified firmware**. Every device runs the same codebase and can dynamically behave as:

- **Endpoint node**: originates and consumes application data
- **Relay node**: forwards packets for others
- **Hybrid node**: endpoint + relay depending on traffic

The goal is to enable **reliable multi-hop delivery** for mixed payload types (text / image / voice / seismic/SHM metadata) under LoRa constraints (low bitrate, high airtime cost, collisions).

---

## 2. System Overview

### 2.1 Network Model

- **Distributed ad-hoc mesh**: no base station or fixed router role.
- **On-demand routing**: routes are discovered only when needed (AODV-inspired).
- **Hop-limited forwarding**: packets carry TTL to prevent loops.
- **Traffic-adaptive behavior**:

  - if `dst == me` → consume and ACK
  - if `dst != me` → forward (if possible)

### 2.2 Hardware and PHY Layer

- Device: **ESP32 T-Display + SX127x LoRa**
- Radio profile (example):

  - Band: **AS923 @ 923 MHz**
  - SF: 7, BW: 125 kHz, CR: 4/5
  - CRC enabled

- Payload limitation: **~255 bytes per LoRa packet** (hence fragmentation support for large data)

---

# 3. Protocol Architecture

This implementation behaves like a miniature network stack:

### 3.1 Planes of Operation

1. **Control plane**

   - Neighbor discovery (HELLO)
   - Route discovery (RREQ/RREP)
   - Route expiry / invalidation

2. **Data plane**

   - DATA delivery (single-packet)
   - Forwarding via relay queue
   - Fragmentation framework (declared; bulk transfer supported via fragment types)

3. **Reliability plane**

   - End-to-end ACK (ACK)
   - Hop-by-hop reinforcement (RACK for high reliability traffic)
   - Retry policy (based on reliability class)

---

# 4. Packet Format and Addressing

### 4.1 Addressing

Nodes are identified by **string node names** (e.g., `Node_1`, `Node_2`).
Packets carry:

- `src` = original sender
- `dst` = final destination
- `dst = BROADCAST` used for discovery messages

### 4.2 Header Fields

All packets embed a lightweight header containing:

- **Type**: packet function (DATA/ACK/RREQ/…)
- **Source / Destination**: end addresses
- **Sequence number (seq)**: unique per source for de-duplication + tracking
- **Hop count**: number of forwards already done
- **TTL**: decreases each hop, prevents infinite forwarding loops
- **Reliability class**: informs retry + ACK rules

### 4.3 Message Types

**Control**

- `HELLO`: 1-hop neighbor discovery (TTL=1)
- `RREQ`: route request (broadcast)
- `RREP`: route reply (unicast back to origin)
- `RERR`: route error (declared for future/extension)

**Data**

- `DATA`: application payload (single-packet)
- `FRAG`: fragment of large message (declared)
- `FACK`: fragment acknowledgment (declared)

**Reliability**

- `ACK`: end-to-end delivery acknowledgment
- `RACK`: hop-by-hop relay acknowledgment (extra reliability)

---

# 5. Reliability Design

### 5.1 Reliability Classes

The sender selects reliability per transmission:

- `REL_NONE`: no ACK, no retries
- `REL_LOW`: 1 retry, short timeout (lightweight messages)
- `REL_MEDIUM`: 2 retries, medium timeout (images/medium importance)
- `REL_HIGH`: 3 retries, longer timeout
- `REL_CRITICAL`: 5 retries, very long timeout (seismic/SHM critical traffic)

### 5.2 What Reliability Controls

Reliability class configures:

- **max retries**
- **ACK timeout**
- whether relays send **RACK** (for HIGH and above)

### 5.3 Reliability Guarantees (Practical)

- `REL_NONE`: best-effort delivery only
- `REL_LOW/MED`: sender confirms receiver got it (ACK)
- `REL_HIGH/CRIT`: receiver ACK + relay RACK reduce multi-hop loss amplification

---

# 6. Routing Design

### 6.1 Routing Table

Each node maintains:

**RoutingTable[dest] → RouteEntry**

- `nextHop`: who to transmit to in order to reach dest
- `hopCount`: route length
- `timestamp`: last update time
- `rssi / snr`: last observed link quality
- `isValid`

### 6.2 Route Lifetime

Routes expire after a fixed timeout (example: **5 minutes**).
Expired routes are removed during lookup.

### 6.3 Route Update Policy

Routes update when:

- new route appears
- route has fewer hops
- same hops but newer information (freshness tie-break)

---

# 7. Route Discovery (AODV-inspired)

### 7.1 When Route Discovery Starts

Before sending DATA:

- If no routing entry exists for destination → initiate route discovery using `RREQ`.

### 7.2 RREQ Propagation

RREQ is broadcast and forwarded by intermediate nodes under:

- **deduplication check**
- **TTL constraint**
- **high-priority queueing** (to prefer routing traffic over normal data)

Intermediate nodes:

1. store “reverse reachability” to origin (so replies can return)
2. forward RREQ if not destination and TTL permits

### 7.3 RREP Return Path

If a node is the target destination:

- it generates an RREP addressed back to the originator.

Intermediate nodes forward RREP toward origin using their learned reverse routes.

### 7.4 What Route Discovery Achieves

After RREP is received:

- source now has a usable route and can send data over multi-hop.

---

# 8. Duplicate Suppression (Loop + Storm Control)

### 8.1 Motivation

Because LoRa broadcast forwarding can create:

- repeated re-forwarding
- broadcast storms
- loops

### 8.2 Mechanism

Node stores a cache keyed by:

- `msgId = src:seq`

If a packet with the same `(src, seq)` is seen again within the cache window:

- it is treated as a duplicate and dropped (except when destination is self).

### 8.3 Timeout

Duplicate cache entries expire after a short window (example: **60 seconds**).

### 8.4 Benefit

- large reduction in redundant airtime
- prevents infinite rebroadcast
- lowers relay load

---

# 9. Relay Queue and Forwarding Strategy

### 9.1 Motivation (LoRa + MCU constraints)

Direct immediate forwarding is risky because:

- LoRa channel may be busy
- collisions happen easily
- relay nodes must avoid RAM overflow

### 9.2 Queue Model

Relays enqueue packets with:

- `packet`
- `queueTime`
- `priority`

Queue limits:

- max queue size: **20**
- max age: **10 seconds** (stale packets dropped)

### 9.3 Priority Scheduling

Lower number = higher priority:

- 0: routing messages (RREQ/RREP)
- 1: ACKs
- 2: normal DATA forward

### 9.4 Benefit

- protects device memory
- prevents control messages being delayed by data bursts
- gives stable multi-hop behavior under congestion

---

# 10. Data Delivery and ACK Handling

## 10.1 Data Receive Behavior (Destination Node)

When `dst == myNodeName`:

- payload delivered to application
- if reliability != NONE → send end-to-end ACK back to source
- optional: show on OLED, log RSSI/SNR

## 10.2 Data Forwarding Behavior (Relay Node)

When `dst != myNodeName`:

- look up next hop toward destination
- if route exists + TTL allows:

  - enqueue forwarded packet
  - increment hopCount, decrement TTL

For `REL_HIGH` and above:

- send **RACK** to previous hop (local confirmation)

## 10.3 ACK Forwarding

ACK packets are also forwarded multi-hop using routing table entries, so the original sender can receive confirmation even across multiple relays.

---

# 11. Neighbor Discovery (HELLO)

### 11.1 Purpose

Maintains knowledge of **direct neighbors** and enables fast route formation.

### 11.2 Behavior

- every node broadcasts HELLO periodically (e.g., every 30 seconds)
- TTL=1 (not forwarded)
- receivers add sender as a **1-hop route**

---

# 12. Link Quality Tracking (RSSI/SNR)

### 12.1 Collected Metrics

Nodes record:

- RSSI and SNR per received packet
- optional EMA smoothing of RSSI

### 12.2 Uses

- reporting / debugging
- possible future routing improvements (choose next hop based on link quality)

---

# 13. Node Interfaces and Control

### 13.1 Serial Commands (Operator Interface)

- `SEND:<dest>:<rel>:<data>`
  Send payload to destination using reliability class.
- `DISCOVER:<dest>`
  Force route discovery.
- `ROUTES`
  Print current routing table.
- `STATS`
  Print counters: TX/RX, relayed, duplicates dropped, queue size, routes count.

### 13.2 OLED Display (Local UI)

Used for:

- network state (“route discovery”, “route found”)
- TX/RX confirmation
- signal info snapshots

---

# 14. Performance and Safety Limits

### 14.1 Designed Constraints

- LoRa packet size limit ⇒ fragmentation required for large transfers (framework present)
- queue max length avoids RAM crash
- dedup cache avoids rebroadcast storms
- TTL avoids infinite loops
- route timeout prevents stale paths persisting forever

### 14.2 Expected Failure Modes

- High collision environments may cause:

  - route discovery delays
  - ACK timeouts / retransmits
  - queue drops when overloaded

- If no route exists and discovery fails within the wait window:

  - send attempt fails gracefully

---

# 15. Demonstration Scenarios (Panel-Friendly)

## Scenario A: Neighbor formation

1. nodes boot
2. periodic HELLO broadcasts
3. routing table gains 1-hop neighbor entries

**What to show:** `ROUTES` output + RSSI values

## Scenario B: Multi-hop route discovery

1. Node_1 sends to Node_4 with no route
2. Node_1 broadcasts RREQ
3. intermediate nodes forward RREQ
4. Node_4 responds with RREP
5. Node_1 learns route, then sends DATA

**What to show:** serial logs for `[RREQ]`, `[RREP]`, route table entry

## Scenario C: Reliable multi-hop data delivery

1. Node_1 sends `REL_HIGH` message
2. relays forward DATA using queue
3. relays optionally send RACKs
4. destination sends ACK
5. ACK forwarded back to origin

**What to show:** logs for `[FWD]`, `[RACK]`, `[ACK]`

---

# 16. Summary (What this implementation achieves)

This firmware delivers a **practical LoRa mesh node** with:

- Unified firmware deployment (only node name changes)
- AODV-style on-demand routing (RREQ/RREP)
- Stable forwarding under LoRa constraints (queue + TTL)
- Duplicate suppression (prevents rebroadcast storms)
- Adaptive reliability classes (retries + timeouts)
- Multi-hop acknowledgments (ACK, plus relay-level RACK for critical data)
- Operator tools for evaluation (routes, stats, logs, OLED feedback)

---

## 17. Suggested Panel Diagram (what to draw on 1 slide)

Draw 4–6 nodes in a line / grid and label:

- HELLO is 1-hop broadcast
- RREQ broadcast floods outward (TTL-limited)
- RREP unicast returns to source
- DATA follows route (queued at relays)
- ACK returns back across same mesh
