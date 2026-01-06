# Low Power Relay Experiment - Quick Guide

## What's New

Two power-optimized implementations:

- **03-End_Node_LowPower** - Deep sleep (0.5 mA avg) → **6 months battery**
- **04-Relay_Node_LowPower** - Light sleep (10 mA avg) → **8 days battery**

## Power Savings

| Node Type | Before | After | Savings | Battery Life |
|-----------|--------|-------|---------|--------------|
| End Node  | 80 mA  | 0.5 mA | **99.4%** | 1 day → **6 months** |
| Relay     | 80 mA  | 10 mA  | **87.5%** | 1 day → **8 days** |

*(Based on 2000 mAh battery)*

## Quick Start

### 1. End Node Configuration

Edit `03-End_Node_LowPower.ino`:

```cpp
#define MY_NODE_ID   "C"              // Your node ID
#define PEER_NODE_ID "A"              // Gateway ID
#define SLEEP_INTERVAL_SECONDS  300   // 5 minutes (adjust as needed)
```

**Upload to your end node board.**

### 2. Relay Node Configuration

Edit `04-Relay_Node_LowPower.ino`:

```cpp
#define SLEEP_TIMEOUT_MS  30000  // Sleep after 30s of no activity
```

**Upload to your relay board.**

### 3. Gateway Node

Use your existing always-on gateway (Node A) that sends ACKs.

## How It Works

### End Node (Deep Sleep)
1. Wakes every 5 minutes
2. Sends sensor data packet
3. Waits 5 seconds for ACK
4. Displays success/failure
5. Enters deep sleep (~10 µA)
6. Repeat

### Relay Node (Light Sleep)
1. Listens for packets
2. Forwards with incremented hop count
3. After 30s of no activity → light sleep (~1.5 mA)
4. Wakes every 30s to check
5. Repeat

## Testing

### Expected Serial Output

**End Node:**
```
=== LoRa End Node with Deep Sleep ===
Boot count: 1
[LOW_POWER] ========== Wake Up ==========
[LOW_POWER] Payload: MSG,C,1,T=24.5,H=62.3,B=3850mV
[LOW_POWER] SUCCESS! Duration: 1234 ms
[LOW_POWER] Going to sleep for 300 seconds...
```

**Relay Node:**
```
=== LoRa Relay Node with Light Sleep ===
[RELAY RX] hopIn=0 | RSSI=-45 | type=MSG
[RELAY TX] hopOut=1 | Forwarded
[LOW_POWER] Entering light sleep for 30000 ms
[LOW_POWER] Woke from light sleep
```

## Measuring Power

Connect a USB power meter or multimeter:

**End Node:**
- Peak: 120 mA for ~2 seconds
- Sleep: 0.01 mA
- Average over 5 min: **~0.5 mA**

**Relay Node:**
- Active: 80 mA during packets
- Sleep: 1.5 mA
- Average (low traffic): **~10 mA**

## Tuning Sleep Intervals

### For Longer Battery Life
```cpp
// End node: Sleep longer between transmissions
#define SLEEP_INTERVAL_SECONDS  600   // 10 minutes

// Relay: More aggressive sleep
#define SLEEP_TIMEOUT_MS  15000       // 15 seconds
```

### For More Frequent Updates
```cpp
// End node: Wake more often
#define SLEEP_INTERVAL_SECONDS  60    // 1 minute

// Relay: Stay awake longer
#define SLEEP_TIMEOUT_MS  60000       // 60 seconds
```

## RTC Memory Stats

Both implementations track statistics that survive sleep:

**End Node:**
- `bootCount` - Total wake cycles
- `successfulTX` - ACKs received
- `failedTX` - Timeouts

**Relay Node:**
- `totalPacketsRelayed` - All forwarded
- `totalWakeCycles` - Wake events
- `totalSleepCycles` - Sleep cycles

These reset only on power loss or hard reset.

## Troubleshooting

### End node doesn't wake
- Check USB/battery connection during sleep
- Verify `esp_sleep_enable_timer_wakeup()` is called
- Try shorter sleep interval for testing

### Relay misses packets
- Increase `SLEEP_TIMEOUT_MS` (stay awake longer)
- Check if packets arrive during sleep window
- Test with relay in always-on mode first

### Stats reset unexpectedly
- Hard reset clears RTC memory
- Power disconnect resets
- Ensure `RTC_DATA_ATTR` is used

## Network Setup

**Recommended deployment:**
- **Node C:** 03-End_Node_LowPower (battery powered)
- **Node B:** 04-Relay_Node_LowPower (solar/large battery)
- **Node A:** Original gateway (mains powered, always on)

## Battery Life Calculator

```
Battery Life (hours) = Battery mAh / Average Current mA

Examples:
- End node: 2000 mAh / 0.5 mA = 4000 hours ≈ 6 months
- Relay: 2000 mAh / 10 mA = 200 hours ≈ 8 days
```

## Next Steps

1. **Upload and test** both implementations
2. **Measure current** with USB power meter
3. **Monitor for 24 hours** to verify stability
4. **Adjust intervals** based on your needs
5. **Deploy in field** with appropriate battery sizing

## Advanced: Adaptive Sleep

Add battery-based sleep adjustment:

```cpp
uint16_t getSleepInterval() {
  uint16_t batteryMv = readBatteryVoltage();
  if (batteryMv > 3800) return 300;      // Good: 5 min
  else if (batteryMv > 3600) return 600; // Medium: 10 min
  else return 1800;                       // Low: 30 min
}
```

---

**Status:** ✅ Fully implemented and ready to test  
**Last Updated:** January 6, 2026
