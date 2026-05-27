# F1 Tracker - Testing Guide

## Pre-Deployment Testing Checklist

### 1. **Initial Setup & WiFi**
- [ ] **First Boot:**
  - Device creates WiFi AP: `F1Tracker-Setup`
  - Can connect to AP and access captive portal
  - Can configure WiFi credentials
  - Device connects to configured WiFi
  - Display shows "It's lights out and away we go!!!"

- [ ] **WiFi Reconnection:**
  - Disconnect WiFi router (simulate network loss)
  - Device should detect disconnection
  - Device should restart and reconnect automatically
  - Display should continue updating after reconnection

### 2. **Time Synchronization**
- [ ] **NTP Sync:**
  - Device syncs time on boot
  - Serial monitor shows time sync success
  - Display shows correct date/time in header
  - Time persists across reboots (RTC maintained)

- [ ] **Timezone:**
  - Verify timezone is correct (EST/EDT by default)
  - Check daylight saving time handling (if applicable)

### 3. **Display Rendering**
- [ ] **Initial Display:**
  - All sections render correctly:
    - Header with date/time
    - Next race information
    - Driver standings (top 10)
    - Constructor standings (top 5)
    - Podium section
  - No corruption or artifacts
  - Colors render correctly (black, white, red)

- [ ] **Text Rendering:**
  - Driver names display correctly
  - Points display correctly
  - Race names display correctly
  - Countdown timer displays correctly
  - No text overflow or clipping

### 4. **API Calls & Caching**
- [ ] **Initial Data Fetch:**
  - Calendar data fetched successfully
  - Driver standings fetched successfully
  - Constructor standings fetched successfully
  - Serial monitor shows API calls

- [ ] **Caching:**
  - Second update uses cached data (no API calls)
  - Cache expires after TTL (1 hour for standings)
  - Cache invalidates when race finishes
  - Availability cache works (qualifying/results)

### 5. **Update Schedule**
- [ ] **Normal Operation (>48h before race):**
  - Updates every 6 hours (at :00 of 0, 6, 12, 18)
  - WiFi disconnects after update
  - WiFi reconnects for next update
  - Serial monitor shows scheduled updates

- [ ] **24-48h Before Race:**
  - Updates every 2 hours (even hours at :00)
  - Still searching for starting grid
  - WiFi power management works

- [ ] **<24h Before Race:**
  - Updates every 30 minutes (at :00 and :30)
  - Actively searching for starting grid
  - Once grid found, switches to 2-hour updates

- [ ] **Race Window (6h before to 6h after):**
  - Updates every 30 minutes
  - Shows "Race in Progress!" if applicable
  - Shows starting grid when available

### 6. **Content Display Logic**
- [ ] **Next Race Display:**
  - Shows race name
  - Shows countdown timer
  - Shows local date/time
  - Updates countdown correctly

- [ ] **Starting Grid:**
  - Shows grid when within 18h of next race
  - Shows grid during race window
  - Grid displays correctly (positions 1-20)
  - Switches back to standings when appropriate

- [ ] **Podium Display:**
  - Shows last race podium when results available
  - Shows blank podium when race in progress
  - Shows "Awaiting Results" when next race approaching
  - Podium names display correctly

- [ ] **Race State Detection:**
  - Detects "race in progress" correctly
  - Detects "next race approaching" correctly
  - Switches display modes appropriately

### 7. **WiFi Power Management**
- [ ] **WiFi Disconnect:**
  - WiFi disconnects after each update
  - Serial monitor shows "WiFi Disconnected to save power"
  - WiFi mode set to WIFI_OFF

- [ ] **WiFi Reconnect:**
  - WiFi reconnects automatically for API calls
  - Serial monitor shows "WiFi Reconnected!"
  - API calls succeed after reconnection
  - Reconnection is fast (<10 seconds)

- [ ] **Power Consumption:**
  - Measure current draw when WiFi off (should be ~10-20µA)
  - Measure current draw during update (should be ~50-100mA)
  - Verify significant power savings

### 8. **Edge Cases**
- [ ] **No Race Scheduled:**
  - Handles gracefully
  - Shows appropriate message or blank
  - Continues normal operation

- [ ] **API Failures:**
  - Handles HTTP errors gracefully
  - Retries failed requests (3 attempts)
  - Falls back to cached data if available
  - Display doesn't corrupt on API failure

- [ ] **Network Issues:**
  - Handles timeouts correctly
  - Handles DNS failures
  - Recovers when network restored

- [ ] **Memory:**
  - No heap fragmentation issues
  - No memory leaks over time
  - Serial monitor shows free heap (should be stable)

### 9. **Long-Term Testing**
- [ ] **24-Hour Test:**
  - Run for 24 hours
  - Verify all scheduled updates occur
  - Verify WiFi power management works
  - Check for memory leaks
  - Verify display doesn't corrupt

- [ ] **Race Weekend Test:**
  - Test during actual race weekend
  - Verify grid appears when posted
  - Verify results appear after race
  - Verify schedule transitions work correctly

## Testing Procedure

### Quick Test (5 minutes)
1. Upload code to device
2. Connect to Serial Monitor (115200 baud)
3. Verify WiFi setup works
4. Verify initial display renders
5. Wait for first scheduled update
6. Verify WiFi disconnects after update

### Full Test (1 hour)
1. Complete Quick Test
2. Manually trigger updates (adjust time or wait)
3. Test all display modes:
   - Normal standings
   - Starting grid
   - Race in progress
   - Podium display
4. Test WiFi reconnection
5. Verify caching works
6. Check Serial Monitor for errors

### Extended Test (24 hours)
1. Let device run for 24 hours
2. Monitor Serial output periodically
3. Verify all scheduled updates occur
4. Check for memory issues
5. Verify power consumption
6. Test during different times of day

## Serial Monitor Checklist

Watch for these messages in Serial Monitor:

**Good Signs:**
- `[WiFi] Connected!`
- `[Time] Synced!`
- `[Scheduler] Wall-clock aligned update at XX:XX`
- `[WiFi] Disconnected to save power`
- `[WiFi] Reconnected!`
- `[DATA] Last: R...`
- `[DATA] Next: R...`

**Warning Signs:**
- `[ERR] HTTP GET failed`
- `[ERR] JSON parse failed`
- `[WiFi] Connection lost`
- `Failed to connect to WiFi`
- Heap fragmentation warnings

## Common Issues & Solutions

### Issue: Display Not Updating
**Check:**
- WiFi connection status
- Time sync status
- API endpoint accessibility
- Serial monitor for errors

### Issue: WiFi Not Disconnecting
**Check:**
- Serial monitor for "WiFi Disconnected" message
- WiFi status in code
- Power consumption (should drop after update)

### Issue: Wrong Update Schedule
**Check:**
- Current time (NTP sync)
- Next race epoch calculation
- `shouldUpdateNow()` logic
- Serial monitor for scheduler messages

### Issue: Memory Issues
**Check:**
- Free heap in Serial Monitor
- String usage (should use reserve())
- Cache sizes
- JSON document sizes

## Performance Benchmarks

### Expected Values:
- **WiFi Reconnection:** <10 seconds
- **API Call:** 2-5 seconds per endpoint
- **Display Refresh:** 15-30 seconds (3-color)
- **Update Cycle:** 30-60 seconds total
- **Idle Power:** 10-20µA (WiFi off)
- **Active Power:** 50-100mA (during update)

### Memory Usage:
- **Free Heap:** Should remain >200KB
- **JSON Buffer:** 12KB
- **Cache Size:** ~50-100KB total
- **String Usage:** Should use reserve() to prevent fragmentation

## Success Criteria

✅ **All tests pass:**
- Display updates correctly
- WiFi power management works
- Update schedule follows timing rules
- All content displays correctly
- No memory leaks
- No crashes or resets
- Power consumption is low

## Next Steps After Testing

1. **If all tests pass:** Deploy to production
2. **If issues found:** Fix and retest
3. **Monitor in production:** Check periodically for issues
4. **Optimize if needed:** Based on real-world usage

---

**Happy Testing! 🏎️🏁**

