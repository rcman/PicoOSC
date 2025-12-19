# picoosc

A lightweight Open Sound Control (OSC) library for the Raspberry Pi Pico, built on lwIP.

## Features

- **OSC Client** – Send OSC messages and bundles over UDP
- **OSC Server** – Receive and parse incoming OSC messages with pattern matching
- **Full type support** – int32, float, string, blob, int64, double, timetag, char, MIDI, color, True/False/Nil/Infinitum
- **Bundles** – Group multiple messages with a timetag
- **Zero dependencies** beyond lwIP (included with Pico SDK)

## Installation

Copy `picoosc.hpp` into your project and include it:

```cpp
#include "picoosc.hpp"
```

Ensure lwIP is enabled in your `CMakeLists.txt`:

```cmake
target_link_libraries(your_project pico_stdlib pico_cyw43_arch_lwip_poll)
```

## Quick Start

### Sending Messages

```cpp
#include "picoosc.hpp"

// Create a client targeting IP and port
picoosc::OSCClient client("192.168.1.100", 9000);

// Build and send a message
picoosc::OSCMessage msg;
msg.setAddress("/synth/note");
msg.addInt(60);
msg.addFloat(0.8f);
msg.addString("piano");
msg.send(client);
```

### Receiving Messages

```cpp
#include "picoosc.hpp"

void onMessage(const picoosc::OSCMessageView& msg, void* userData)
{
    if (msg.matchAddress("/synth/note")) {
        int note = msg.getInt(0);
        float vel = msg.getFloat(1);
        // Handle note...
    }
}

int main()
{
    // ... Pico and network init ...

    picoosc::OSCServer server(9000);
    server.start(onMessage, nullptr);

    while (true) {
        cyw43_arch_poll();  // or your lwIP polling method
        sleep_ms(1);
    }
}
```

## API Reference

### OSCClient

UDP client for sending OSC data.

```cpp
OSCClient(const char* address, uint16_t port)
```

| Method | Description |
|--------|-------------|
| `bool send(const char* buffer, uint16_t size)` | Send raw OSC data |
| `bool isValid()` | Check if the client was created successfully |

### OSCMessage

Builder for outgoing OSC messages.

```cpp
OSCMessage msg;
msg.setAddress("/address/pattern");
msg.addInt(42);
msg.send(client);
```

| Method | Description |
|--------|-------------|
| `void clear()` | Reset the message |
| `bool setAddress(const char* address)` | Set the OSC address pattern |
| `bool addInt(int32_t value)` | Add a 32-bit integer (`i`) |
| `bool addFloat(float value)` | Add a 32-bit float (`f`) |
| `bool addString(const char* value)` | Add a string (`s`) |
| `bool addBlob(const void* data, int32_t size)` | Add binary data (`b`) |
| `bool addInt64(int64_t value)` | Add a 64-bit integer (`h`) |
| `bool addDouble(double value)` | Add a 64-bit double (`d`) |
| `bool addTimetag(OSCTimetag tt)` | Add a timetag (`t`) |
| `bool addChar(char c)` | Add a character (`c`) |
| `bool addMidi(uint8_t port, status, data1, data2)` | Add a MIDI message (`m`) |
| `bool addColor(uint8_t r, g, b, a)` | Add an RGBA color (`r`) |
| `bool addTrue()` | Add True (`T`) |
| `bool addFalse()` | Add False (`F`) |
| `bool addNil()` | Add Nil (`N`) |
| `bool addInfinitum()` | Add Infinitum (`I`) |
| `std::size_t build(char* buffer, std::size_t maxSize)` | Build message into buffer |
| `bool send(OSCClient& client)` | Build and send via client |

All `add*` methods return `false` if the message buffer is full.

### OSCBundle

Group multiple messages with a shared timetag.

```cpp
picoosc::OSCBundle bundle;
bundle.setTimetag(picoosc::OSCTimetag::immediate());

picoosc::OSCMessage msg1;
msg1.setAddress("/a");
msg1.addInt(1);
bundle.addMessage(msg1);

picoosc::OSCMessage msg2;
msg2.setAddress("/b");
msg2.addFloat(2.0f);
bundle.addMessage(msg2);

bundle.send(client);
```

| Method | Description |
|--------|-------------|
| `void clear()` | Reset the bundle |
| `void setTimetag(OSCTimetag tt)` | Set execution time |
| `bool addMessage(const OSCMessage& msg)` | Add a message to the bundle |
| `const char* data()` | Get raw bundle data |
| `std::size_t size()` | Get bundle size in bytes |
| `bool send(OSCClient& client)` | Send the bundle |

### OSCTimetag

NTP timestamp for bundles.

```cpp
struct OSCTimetag {
    uint32_t seconds;    // Seconds since Jan 1, 1900
    uint32_t fractions;  // Fractional seconds

    static OSCTimetag immediate();  // Returns "immediately" timetag (1, 0)
};
```

### OSCServer

UDP server that listens for incoming OSC messages.

```cpp
OSCServer(uint16_t port)
```

| Method | Description |
|--------|-------------|
| `bool start(OSCCallback callback, void* userData)` | Start listening |
| `void stop()` | Stop listening |
| `bool isRunning()` | Check if server is active |
| `uint16_t port()` | Get the listening port |

The callback signature is:

```cpp
void callback(const OSCMessageView& msg, void* userData);
```

### OSCMessageView

Read-only view of a received OSC message.

| Method | Description |
|--------|-------------|
| `const char* address()` | Get the address pattern |
| `const char* typeTags()` | Get type tag string (without comma) |
| `std::size_t argCount()` | Number of arguments |
| `const OSCArg* arg(std::size_t index)` | Get raw argument at index |
| `int32_t getInt(std::size_t index, int32_t def = 0)` | Get int argument |
| `float getFloat(std::size_t index, float def = 0)` | Get float argument |
| `const char* getString(std::size_t index, const char* def = "")` | Get string argument |
| `bool getBool(std::size_t index, bool def = false)` | Get True/False argument |
| `bool matchAddress(const char* pattern)` | Match address with wildcards |

### OSCArg

Union structure for parsed arguments:

```cpp
struct OSCArg {
    char type;  // 'i', 'f', 's', 'b', 'h', 'd', 't', 'm', 'c', 'r', 'T', 'F', 'N', 'I'

    // Value (check type first):
    int32_t i;
    float f;
    int64_t h;
    double d;
    OSCTimetag t;
    struct { uint8_t port, status, data1, data2; } midi;
    struct { uint8_t r, g, b, a; } color;
    char c;

    const char* s;            // String pointer
    const uint8_t* blobData;  // Blob data pointer
    int32_t blobSize;         // Blob size
};
```

## Address Pattern Matching

The server supports wildcard matching:

| Pattern | Matches |
|---------|---------|
| `/synth/freq` | Exact match only |
| `/synth/*` | `/synth/freq`, `/synth/amp`, etc. |
| `/synth/osc?` | `/synth/osc1`, `/synth/osc2`, etc. |
| `/*` | Any single-level address |

```cpp
if (msg.matchAddress("/synth/osc*/freq")) {
    // Matches /synth/osc1/freq, /synth/oscA/freq, etc.
}
```

## OSC Type Tags

| Tag | Type | Size | Method |
|-----|------|------|--------|
| `i` | int32 | 4 bytes | `addInt()` |
| `f` | float32 | 4 bytes | `addFloat()` |
| `s` | string | variable | `addString()` |
| `b` | blob | variable | `addBlob()` |
| `h` | int64 | 8 bytes | `addInt64()` |
| `d` | float64 | 8 bytes | `addDouble()` |
| `t` | timetag | 8 bytes | `addTimetag()` |
| `c` | char | 4 bytes | `addChar()` |
| `m` | MIDI | 4 bytes | `addMidi()` |
| `r` | RGBA color | 4 bytes | `addColor()` |
| `T` | True | 0 bytes | `addTrue()` |
| `F` | False | 0 bytes | `addFalse()` |
| `N` | Nil | 0 bytes | `addNil()` |
| `I` | Infinitum | 0 bytes | `addInfinitum()` |

## Examples

### Multiple Arguments

```cpp
picoosc::OSCMessage msg;
msg.setAddress("/mixer/channel/1");
msg.addString("volume");
msg.addFloat(0.75f);
msg.addInt(1);        // muted: false
msg.addTrue();        // solo: true
msg.send(client);
// Type tag string: ",sfIT"
```

### Sending a Blob

```cpp
uint8_t waveform[256];
// ... fill waveform data ...

picoosc::OSCMessage msg;
msg.setAddress("/synth/waveform");
msg.addBlob(waveform, sizeof(waveform));
msg.send(client);
```

### Receiving Blobs

```cpp
void onMessage(const picoosc::OSCMessageView& msg, void* userData)
{
    if (msg.matchAddress("/synth/waveform")) {
        const picoosc::OSCArg* arg = msg.arg(0);
        if (arg && arg->type == 'b') {
            const uint8_t* data = arg->blobData;
            int32_t size = arg->blobSize;
            // Process waveform...
        }
    }
}
```

### MIDI Over OSC

```cpp
// Send note on: channel 1, note 60, velocity 100
picoosc::OSCMessage msg;
msg.setAddress("/midi");
msg.addMidi(0, 0x90, 60, 100);
msg.send(client);
```

### Scheduled Bundle

```cpp
picoosc::OSCBundle bundle;

// Set execution time (NTP timestamp)
picoosc::OSCTimetag when;
when.seconds = 3913517120;  // Some future time
when.fractions = 0;
bundle.setTimetag(when);

picoosc::OSCMessage msg;
msg.setAddress("/event");
msg.addInt(42);
bundle.addMessage(msg);

bundle.send(client);
```

## Configuration

Adjust these constants in `picoosc.hpp` if needed:

```cpp
static constexpr std::size_t MAX_MESSAGE_SIZE = 1024;
static constexpr std::size_t MAX_ADDRESS_SIZE = 256;
static constexpr std::size_t MAX_TYPE_TAG_SIZE = 64;
static constexpr std::size_t MAX_ARG_BUFFER_SIZE = 768;
```

For `OSCBundle`:

```cpp
static constexpr std::size_t MAX_BUNDLE_SIZE = 4096;
```

## License

MIT License. See LICENSE file for details.

## References

- [OSC 1.0 Specification](https://opensoundcontrol.stanford.edu/spec-1_0.html)
- [OSC 1.0 Examples](https://opensoundcontrol.stanford.edu/spec-1_0-examples.html)
- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)