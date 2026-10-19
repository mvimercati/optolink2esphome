#pragma once
// Viessmann Optolink, P300 (VS2) protocol: pure functions, no I/O, testable on the host.
//
// Link setup: EOT puts the boiler back in KW mode, where it sends ENQ about every 2 s;
// on ENQ we send SYNC and the boiler ACKs: P300 is active. Left idle (~10 minutes) the
// boiler falls back to KW and starts sending ENQ again.
//
// Request  41 len 00 fn addrH addrL n [data] crc     fn 01 = read, 02 = write
// Reply    06 41 len 01 fn addrH addrL n [data] crc  type 03 instead of 01: error
// len counts the bytes from the type to the data, crc is the sum of len and those bytes.
// Values are little endian; addresses big endian.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esphome {
namespace optolink {

static constexpr uint8_t EOT = 0x04, ENQ = 0x05, ACK = 0x06, NAK = 0x15, START = 0x41;
static constexpr uint8_t SYNC[] = {0x16, 0x00, 0x00};
static constexpr uint8_t READ = 0x01, WRITE = 0x02;
static constexpr size_t MAX_DATA = 32;            // per transaction (the boiler handles ~55)
static constexpr size_t MAX_FRAME = 9 + MAX_DATA;  // ACK + reply header + data + crc

// Value types of a datapoint. DATE: 7 bytes, year (LE16) month day hour minute second.
enum Type : uint8_t { U8, S8, U16, S16, U32, S32, DATE };

inline uint8_t type_size(Type t) {
  static const uint8_t sizes[] = {1, 1, 2, 2, 4, 4, 7};
  return sizes[t];
}

inline uint8_t checksum(const uint8_t *frame) {  // frame[0] is START, frame[1] the length
  uint8_t sum = 0;
  for (size_t i = 1; i < size_t(frame[1]) + 2; i++)
    sum += frame[i];
  return sum;
}

// Builds a read (data == nullptr) or write request; returns its length.
inline size_t request(uint8_t *out, uint16_t address, uint8_t n, const uint8_t *data = nullptr) {
  size_t len = 5 + (data ? n : 0);
  out[0] = START;
  out[1] = len;
  out[2] = 0x00;
  out[3] = data ? WRITE : READ;
  out[4] = address >> 8;
  out[5] = address & 0xff;
  out[6] = n;
  if (data)
    memcpy(out + 7, data, n);
  out[len + 2] = checksum(out);
  return len + 3;
}

enum Reply : uint8_t { INCOMPLETE, OK, ERROR, BAD };

// Checks the bytes received so far for the request req. On OK the read data starts at
// rx + 8. ERROR: a valid frame where the boiler refuses the request (e.g. unknown address).
inline Reply reply(const uint8_t *req, const uint8_t *rx, size_t n) {
  if (n == 0)
    return INCOMPLETE;
  if (rx[0] != ACK)
    return BAD;  // NAK: the boiler got a bad frame; ENQ: it's back in KW mode
  if (n >= 2 && rx[1] != START)
    return BAD;
  if (n < 3)
    return INCOMPLETE;
  const uint8_t *f = rx + 1;
  size_t len = f[1];
  if (len < 5 || len + 4 > MAX_FRAME)
    return BAD;
  if (n < len + 4)
    return INCOMPLETE;
  if (checksum(f) != f[len + 2] || f[3] != req[3] || f[4] != req[4] || f[5] != req[5] || f[6] != req[6])
    return BAD;
  if (f[2] == 0x03)
    return ERROR;
  size_t expected = 5 + (req[3] == READ ? req[6] : 0);
  return f[2] == 0x01 && len == expected ? OK : BAD;
}

inline int64_t decode(Type t, const uint8_t *p) {
  uint32_t u = 0;
  for (size_t i = type_size(t); i-- > 0;)
    u = u << 8 | p[i];
  switch (t) {
    case S8: return int8_t(u);
    case S16: return int16_t(u);
    case S32: return int32_t(u);
    default: return u;
  }
}

inline void encode(Type t, int32_t v, uint8_t *p) {
  for (size_t i = 0; i < type_size(t); i++)
    p[i] = uint32_t(v) >> (8 * i);
}

}  // namespace optolink
}  // namespace esphome
