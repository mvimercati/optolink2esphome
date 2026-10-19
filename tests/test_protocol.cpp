// Host test of components/optolink/protocol.h. From the project root:
//   sudo docker run --rm -v $PWD:/src:ro -w /src python:3.13-alpine sh -c "apk add -q g++ &&
//     g++ -std=c++17 -Wall -Icomponents/optolink tests/test_protocol.cpp -o /tmp/t && /tmp/t"
// Frames from the openv wiki (P300): outdoor temperature 0x5525, 26.3 °C.

#include <cstdio>
#include <vector>

#include "protocol.h"

using namespace esphome::optolink;

static int failures = 0;
#define CHECK(cond) \
  if (!(cond)) { \
    printf("FAIL line %d: %s\n", __LINE__, #cond); \
    failures++; \
  }

static Reply feed(const uint8_t *req, std::vector<uint8_t> rx) {  // byte by byte, as from the UART
  Reply r = INCOMPLETE;
  for (size_t n = 1; n <= rx.size() && r == INCOMPLETE; n++)
    r = reply(req, rx.data(), n);
  return r;
}

int main() {
  uint8_t req[MAX_FRAME];
  CHECK(request(req, 0x5525, 2) == 8);
  CHECK(std::vector<uint8_t>(req, req + 8) == std::vector<uint8_t>({0x41, 0x05, 0x00, 0x01, 0x55, 0x25, 0x02, 0x82}));

  std::vector<uint8_t> ok = {0x06, 0x41, 0x07, 0x01, 0x01, 0x55, 0x25, 0x02, 0x07, 0x01, 0x8D};
  CHECK(feed(req, ok) == OK);
  CHECK(reply(req, ok.data(), ok.size() - 1) == INCOMPLETE);
  CHECK(decode(S16, ok.data() + 8) == 263);

  auto bad = ok;
  bad[10] ^= 1;  // crc
  CHECK(feed(req, bad) == BAD);
  bad = ok;
  bad[6] = 0x26;  // another address (crc fixed)
  bad[10] += 1;
  CHECK(feed(req, bad) == BAD);
  CHECK(feed(req, {NAK}) == BAD);
  CHECK(feed(req, {ENQ}) == BAD);
  CHECK(feed(req, {ACK, ENQ}) == BAD);
  CHECK(feed(req, {ACK, 0x41, 0x05, 0x03, 0x01, 0x55, 0x25, 0x02, 0x85}) == ERROR);

  uint8_t data[] = {42};
  CHECK(request(req, 0x6300, 1, data) == 9);
  CHECK(std::vector<uint8_t>(req, req + 9) == std::vector<uint8_t>({0x41, 0x06, 0x00, 0x02, 0x63, 0x00, 0x01, 0x2A, 0x96}));
  CHECK(feed(req, {ACK, 0x41, 0x05, 0x01, 0x02, 0x63, 0x00, 0x01, 0x6C}) == OK);

  uint8_t neg[] = {0xF6, 0xFF}, big[] = {234, 67, 37, 0}, out[4];
  CHECK(decode(S16, neg) == -10);
  CHECK(decode(U16, neg) == 65526);
  CHECK(decode(U32, big) == 2442218);  // 0x08A7 in MemoryMap.csv: 678 h
  encode(S8, -3, out);
  CHECK(out[0] == 0xFD);

  printf(failures ? "%d FAILED\n" : "all ok\n", failures);
  return failures != 0;
}
