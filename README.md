# optolink2esphome

ESPHome firmware that talks to a Viessmann boiler over the **Optolink** (P300 protocol) and publishes it
to Home Assistant as native entities: no server, no vcontrold/vitalk, no MQTT.

```
Home Assistant ──API── ESP32-S3 (USB host) ──USB── Optolink adapter (FTDI) ──IR── boiler
```

Running on a **Vitodens 242-F** (Vitotronic 200 HO1, device id 0x20CB). Other P300 controllers should
work with their own register table.

## Hardware

- **ESP32-S3** devkit (here an N16R8). The native USB port is the **USB host** for the adapter and must
  supply 5 V on VBUS (on many devkits a jumper or solder pad); the other port (UART0) is for flashing and
  logs.
- **Optolink adapter** with an FTDI FT232R (`0403:6001`), 4800 baud 8E2. DTR/RTS are not needed.

## Files

| File | Content |
|------|---------|
| `optolink-bridge.yaml` | board, network, USB host, `optolink` hub, status LED, diagnostics |
| `viessmann.yaml` | register table → entities, hot water switches |
| `components/optolink/` | external component: P300 protocol (`protocol.h`), hub, entity platforms |
| `tests/test_protocol.cpp` | host test of `protocol.h` |
| `secrets.example.yaml` | template for `secrets.yaml` |

## Datapoints

Every register is a normal ESPHome entity of platform `optolink`:

```yaml
sensor:
  - platform: optolink
    name: Temperatura esterna
    address: 0x5525      # big endian, as in the openv tables
    type: s16            # u8 (default) s8 u16 s16 u32 s32; text_sensor also: date
    divisor: 10          # sensor, number
    offset: 0            # value at this byte of a longer read
    update_interval: 60s # required
```

`text_sensor` and `select` take `options: {raw: text}`, `switch` takes `on_value` / `off_value`,
`number` takes `min_value` / `max_value` / `step`. One read serves every datapoint whose bytes it covers;
writes go first and are read back. Writing the value last read is skipped. While the link is down the
entities are *unknown*; the hub publishes a `connected` binary sensor and an `errors` counter.

The link is set up as in P300: EOT, wait for the boiler's ENQ, send `16 00 00`, expect ACK. One
transaction at a time, `loop()` never blocks; on ENQ or three failures in a row the link is set up again.

## Build

ESPHome ≥ 2026.10 (`usb_host` + `usb_uart`, esp-idf framework), here in Docker:

```sh
cp secrets.example.yaml secrets.yaml   # fill it in
IMG=ghcr.io/esphome/esphome:2026.10.0
docker run --rm -v "$PWD":/config $IMG compile optolink-bridge.yaml
docker run --rm --network host -v "$PWD":/config $IMG run optolink-bridge.yaml --device <ip or serial port>
```

Protocol test on the host:

```sh
g++ -std=c++17 -Wall -Icomponents/optolink tests/test_protocol.cpp -o /tmp/t && /tmp/t
```

## Status LED

Blinking red: no boiler link. Blinking purple: no Home Assistant. Burner on: blue while loading hot
water, orange while heating, brighter with more power. Dim green: idle.

## Credits

Protocol from [vitalk](https://github.com/klauweg/vitalk) and the [openv](https://github.com/openv/openv/wiki)
wiki.
