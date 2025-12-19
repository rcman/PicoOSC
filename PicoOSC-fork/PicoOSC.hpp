#pragma once

#include <climits>
#include <cstdint>
#include <cstring>

#include "lwip/pbuf.h"
#include "lwip/udp.h"

namespace picoosc
{

// Configuration constants
static constexpr std::size_t MAX_MESSAGE_SIZE = 1024;
static constexpr std::size_t MAX_ADDRESS_SIZE = 256;
static constexpr std::size_t MAX_TYPE_TAG_SIZE = 64;
static constexpr std::size_t MAX_ARG_BUFFER_SIZE = 768;

// OSC Timetag representing NTP timestamp
struct OSCTimetag
{
    uint32_t seconds;      // Seconds since Jan 1, 1900
    uint32_t fractions;    // Fractional seconds

    static OSCTimetag immediate() { return {1, 0}; }
};

// Swap endianness for network byte order (big-endian)
// Pico is little-endian, OSC uses big-endian
template<typename T>
constexpr T swap_endian(T value)
{
    static_assert(CHAR_BIT == 8, "CHAR_BIT must be 8");

    if constexpr (sizeof(T) == 1) {
        return value;
    } else if constexpr (sizeof(T) == 2) {
        return static_cast<T>(
            ((value & 0x00FF) << 8) |
            ((value & 0xFF00) >> 8)
        );
    } else if constexpr (sizeof(T) == 4) {
        return static_cast<T>(
            ((value & 0x000000FF) << 24) |
            ((value & 0x0000FF00) << 8)  |
            ((value & 0x00FF0000) >> 8)  |
            ((value & 0xFF000000) >> 24)
        );
    } else if constexpr (sizeof(T) == 8) {
        return static_cast<T>(
            ((value & 0x00000000000000FFULL) << 56) |
            ((value & 0x000000000000FF00ULL) << 40) |
            ((value & 0x0000000000FF0000ULL) << 24) |
            ((value & 0x00000000FF000000ULL) << 8)  |
            ((value & 0x000000FF00000000ULL) >> 8)  |
            ((value & 0x0000FF0000000000ULL) >> 24) |
            ((value & 0x00FF000000000000ULL) >> 40) |
            ((value & 0xFF00000000000000ULL) >> 56)
        );
    }
}

// Swap endianness for float (reinterpret as uint32_t)
inline float swap_endian_float(float value)
{
    uint32_t temp;
    std::memcpy(&temp, &value, sizeof(temp));
    temp = swap_endian(temp);
    std::memcpy(&value, &temp, sizeof(value));
    return value;
}

// Swap endianness for double (reinterpret as uint64_t)
inline double swap_endian_double(double value)
{
    uint64_t temp;
    std::memcpy(&temp, &value, sizeof(temp));
    temp = swap_endian(temp);
    std::memcpy(&value, &temp, sizeof(value));
    return value;
}

/**
 * UDP client for sending OSC messages
 */
class OSCClient
{
public:
    OSCClient(const char* address, uint16_t port)
        : mPort(port)
    {
        ipaddr_aton(address, &mAddr);
        mPcb = udp_new();
    }

    ~OSCClient()
    {
        if (mPcb) {
            udp_remove(mPcb);
        }
    }

    // Non-copyable
    OSCClient(const OSCClient&) = delete;
    OSCClient& operator=(const OSCClient&) = delete;

    // Movable
    OSCClient(OSCClient&& other) noexcept
        : mPcb(other.mPcb)
        , mAddr(other.mAddr)
        , mPort(other.mPort)
    {
        other.mPcb = nullptr;
    }

    OSCClient& operator=(OSCClient&& other) noexcept
    {
        if (this != &other) {
            if (mPcb) udp_remove(mPcb);
            mPcb = other.mPcb;
            mAddr = other.mAddr;
            mPort = other.mPort;
            other.mPcb = nullptr;
        }
        return *this;
    }

    /**
     * Send raw data over UDP
     * @return true on success, false on failure
     */
    bool send(const char* buffer, uint16_t size)
    {
        if (!mPcb) return false;

        struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, size, PBUF_RAM);
        if (!p) {
            return false;
        }

        std::memcpy(p->payload, buffer, size);
        const err_t error = udp_sendto(mPcb, p, &mAddr, mPort);
        pbuf_free(p);

        return error == ERR_OK;
    }

    bool isValid() const { return mPcb != nullptr; }

private:
    udp_pcb* mPcb = nullptr;
    ip_addr_t mAddr{};
    uint16_t mPort = 0;
};

/**
 * OSC Message builder
 * 
 * Usage:
 *   OSCMessage msg;
 *   msg.setAddress("/synth/freq");
 *   msg.addFloat(440.0f);
 *   msg.addInt(1);
 *   msg.send(client);
 * 
 * See: https://opensoundcontrol.stanford.edu/spec-1_0-examples.html
 */
class OSCMessage
{
public:
    OSCMessage() { clear(); }

    /**
     * Clear the message, resetting all buffers
     */
    void clear()
    {
        mAddressSize = 0;
        mTypeTagCount = 0;
        mArgBufferSize = 0;
        std::memset(mAddress, 0, MAX_ADDRESS_SIZE);
        std::memset(mTypeTags, 0, MAX_TYPE_TAG_SIZE);
        std::memset(mArgBuffer, 0, MAX_ARG_BUFFER_SIZE);
    }

    /**
     * Set the OSC address pattern (e.g., "/synth/freq")
     * @return true on success, false if address too long
     */
    bool setAddress(const char* address)
    {
        const std::size_t len = std::strlen(address);
        if (len >= MAX_ADDRESS_SIZE) {
            return false;
        }

        std::memcpy(mAddress, address, len + 1);  // Include null terminator
        mAddressSize = len + 1;

        // Pad to 4-byte boundary
        while (mAddressSize % 4 != 0) {
            mAddress[mAddressSize++] = '\0';
        }

        return true;
    }

    /**
     * Add a 32-bit integer argument
     */
    bool addInt(int32_t value)
    {
        if (!canAddArg(4)) return false;

        mTypeTags[mTypeTagCount++] = 'i';
        value = swap_endian(value);
        std::memcpy(mArgBuffer + mArgBufferSize, &value, 4);
        mArgBufferSize += 4;
        return true;
    }

    /**
     * Add a 32-bit float argument
     */
    bool addFloat(float value)
    {
        if (!canAddArg(4)) return false;

        mTypeTags[mTypeTagCount++] = 'f';
        value = swap_endian_float(value);
        std::memcpy(mArgBuffer + mArgBufferSize, &value, 4);
        mArgBufferSize += 4;
        return true;
    }

    /**
     * Add a string argument
     */
    bool addString(const char* value)
    {
        const std::size_t len = std::strlen(value);
        const std::size_t paddedLen = ((len + 1) + 3) & ~3;  // Round up to 4-byte boundary

        if (!canAddArg(paddedLen)) return false;

        mTypeTags[mTypeTagCount++] = 's';

        // Copy string with null terminator
        std::memcpy(mArgBuffer + mArgBufferSize, value, len + 1);
        mArgBufferSize += len + 1;

        // Pad to 4-byte boundary
        while (mArgBufferSize % 4 != 0) {
            mArgBuffer[mArgBufferSize++] = '\0';
        }

        return true;
    }

    /**
     * Add a blob (binary data) argument
     */
    bool addBlob(const void* data, int32_t size)
    {
        const std::size_t paddedDataLen = (static_cast<std::size_t>(size) + 3) & ~3;

        if (!canAddArg(4 + paddedDataLen)) return false;

        mTypeTags[mTypeTagCount++] = 'b';

        // Write size (big-endian)
        int32_t beSize = swap_endian(size);
        std::memcpy(mArgBuffer + mArgBufferSize, &beSize, 4);
        mArgBufferSize += 4;

        // Write data
        std::memcpy(mArgBuffer + mArgBufferSize, data, static_cast<std::size_t>(size));
        mArgBufferSize += size;

        // Pad to 4-byte boundary
        while (mArgBufferSize % 4 != 0) {
            mArgBuffer[mArgBufferSize++] = '\0';
        }

        return true;
    }

    /**
     * Add a 64-bit integer argument
     */
    bool addInt64(int64_t value)
    {
        if (!canAddArg(8)) return false;

        mTypeTags[mTypeTagCount++] = 'h';
        value = swap_endian(value);
        std::memcpy(mArgBuffer + mArgBufferSize, &value, 8);
        mArgBufferSize += 8;
        return true;
    }

    /**
     * Add a 64-bit double argument
     */
    bool addDouble(double value)
    {
        if (!canAddArg(8)) return false;

        mTypeTags[mTypeTagCount++] = 'd';
        value = swap_endian_double(value);
        std::memcpy(mArgBuffer + mArgBufferSize, &value, 8);
        mArgBufferSize += 8;
        return true;
    }

    /**
     * Add a timetag argument
     */
    bool addTimetag(OSCTimetag tt)
    {
        if (!canAddArg(8)) return false;

        mTypeTags[mTypeTagCount++] = 't';
        tt.seconds = swap_endian(tt.seconds);
        tt.fractions = swap_endian(tt.fractions);
        std::memcpy(mArgBuffer + mArgBufferSize, &tt.seconds, 4);
        std::memcpy(mArgBuffer + mArgBufferSize + 4, &tt.fractions, 4);
        mArgBufferSize += 8;
        return true;
    }

    /**
     * Add a True value (no data, just type tag)
     */
    bool addTrue()
    {
        if (mTypeTagCount >= MAX_TYPE_TAG_SIZE - 1) return false;
        mTypeTags[mTypeTagCount++] = 'T';
        return true;
    }

    /**
     * Add a False value (no data, just type tag)
     */
    bool addFalse()
    {
        if (mTypeTagCount >= MAX_TYPE_TAG_SIZE - 1) return false;
        mTypeTags[mTypeTagCount++] = 'F';
        return true;
    }

    /**
     * Add a Nil value (no data, just type tag)
     */
    bool addNil()
    {
        if (mTypeTagCount >= MAX_TYPE_TAG_SIZE - 1) return false;
        mTypeTags[mTypeTagCount++] = 'N';
        return true;
    }

    /**
     * Add an Infinitum value (no data, just type tag)
     */
    bool addInfinitum()
    {
        if (mTypeTagCount >= MAX_TYPE_TAG_SIZE - 1) return false;
        mTypeTags[mTypeTagCount++] = 'I';
        return true;
    }

    /**
     * Add a MIDI message (4 bytes: port, status, data1, data2)
     */
    bool addMidi(uint8_t port, uint8_t status, uint8_t data1, uint8_t data2)
    {
        if (!canAddArg(4)) return false;

        mTypeTags[mTypeTagCount++] = 'm';
        mArgBuffer[mArgBufferSize++] = static_cast<char>(port);
        mArgBuffer[mArgBufferSize++] = static_cast<char>(status);
        mArgBuffer[mArgBufferSize++] = static_cast<char>(data1);
        mArgBuffer[mArgBufferSize++] = static_cast<char>(data2);
        return true;
    }

    /**
     * Add a character argument
     */
    bool addChar(char c)
    {
        if (!canAddArg(4)) return false;

        mTypeTags[mTypeTagCount++] = 'c';
        // Character is stored as a 32-bit value (big-endian, char in last byte)
        mArgBuffer[mArgBufferSize++] = '\0';
        mArgBuffer[mArgBufferSize++] = '\0';
        mArgBuffer[mArgBufferSize++] = '\0';
        mArgBuffer[mArgBufferSize++] = c;
        return true;
    }

    /**
     * Add an RGBA color argument
     */
    bool addColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        if (!canAddArg(4)) return false;

        mTypeTags[mTypeTagCount++] = 'r';
        mArgBuffer[mArgBufferSize++] = static_cast<char>(r);
        mArgBuffer[mArgBufferSize++] = static_cast<char>(g);
        mArgBuffer[mArgBufferSize++] = static_cast<char>(b);
        mArgBuffer[mArgBufferSize++] = static_cast<char>(a);
        return true;
    }

    /**
     * Build the complete OSC message into the provided buffer
     * @param outBuffer Buffer to write to
     * @param maxSize Maximum size of the buffer
     * @return Number of bytes written, or 0 on error
     */
    std::size_t build(char* outBuffer, std::size_t maxSize) const
    {
        // Calculate total size needed
        const std::size_t typeTagSize = 1 + mTypeTagCount + 1;  // comma + tags + null
        const std::size_t typeTagPadded = (typeTagSize + 3) & ~3;
        const std::size_t totalSize = mAddressSize + typeTagPadded + mArgBufferSize;

        if (totalSize > maxSize || mAddressSize == 0) {
            return 0;
        }

        std::size_t pos = 0;

        // Write address
        std::memcpy(outBuffer + pos, mAddress, mAddressSize);
        pos += mAddressSize;

        // Write type tag string: comma + types + null + padding
        outBuffer[pos++] = ',';
        std::memcpy(outBuffer + pos, mTypeTags, mTypeTagCount);
        pos += mTypeTagCount;
        outBuffer[pos++] = '\0';
        while (pos % 4 != 0) {
            outBuffer[pos++] = '\0';
        }

        // Write arguments
        std::memcpy(outBuffer + pos, mArgBuffer, mArgBufferSize);
        pos += mArgBufferSize;

        return pos;
    }

    /**
     * Send the message using an OSCClient
     * @return true on success, false on failure
     */
    bool send(OSCClient& client) const
    {
        char buffer[MAX_MESSAGE_SIZE];
        const std::size_t size = build(buffer, MAX_MESSAGE_SIZE);
        if (size == 0) {
            return false;
        }
        return client.send(buffer, static_cast<uint16_t>(size));
    }

    // Accessors for debugging
    std::size_t addressSize() const { return mAddressSize; }
    std::size_t typeTagCount() const { return mTypeTagCount; }
    std::size_t argBufferSize() const { return mArgBufferSize; }

private:
    bool canAddArg(std::size_t bytes) const
    {
        return (mTypeTagCount < MAX_TYPE_TAG_SIZE - 1) &&
               (mArgBufferSize + bytes <= MAX_ARG_BUFFER_SIZE);
    }

    char mAddress[MAX_ADDRESS_SIZE];
    std::size_t mAddressSize = 0;

    char mTypeTags[MAX_TYPE_TAG_SIZE];
    std::size_t mTypeTagCount = 0;

    char mArgBuffer[MAX_ARG_BUFFER_SIZE];
    std::size_t mArgBufferSize = 0;
};

/**
 * OSC Bundle builder
 * 
 * A bundle contains a timetag and multiple messages.
 * Bundles can be nested (bundles containing bundles).
 */
class OSCBundle
{
public:
    static constexpr std::size_t MAX_BUNDLE_SIZE = 4096;

    OSCBundle() { clear(); }

    void clear()
    {
        mBufferSize = 0;
        std::memset(mBuffer, 0, MAX_BUNDLE_SIZE);

        // Write bundle header "#bundle\0"
        static const char header[] = "#bundle";
        std::memcpy(mBuffer, header, 8);
        mBufferSize = 8;

        // Reserve space for timetag (will be set later)
        mBufferSize += 8;
    }

    /**
     * Set the bundle timetag
     */
    void setTimetag(OSCTimetag tt)
    {
        uint32_t seconds = swap_endian(tt.seconds);
        uint32_t fractions = swap_endian(tt.fractions);
        std::memcpy(mBuffer + 8, &seconds, 4);
        std::memcpy(mBuffer + 12, &fractions, 4);
    }

    /**
     * Add a message to the bundle
     * @return true on success, false if bundle is full
     */
    bool addMessage(const OSCMessage& msg)
    {
        char msgBuffer[OSCMessage::MAX_MESSAGE_SIZE];
        const std::size_t msgSize = msg.build(msgBuffer, sizeof(msgBuffer));
        if (msgSize == 0) {
            return false;
        }

        // Check if we have space (4 bytes for size + message)
        if (mBufferSize + 4 + msgSize > MAX_BUNDLE_SIZE) {
            return false;
        }

        // Write message size (big-endian)
        int32_t beSize = swap_endian(static_cast<int32_t>(msgSize));
        std::memcpy(mBuffer + mBufferSize, &beSize, 4);
        mBufferSize += 4;

        // Write message content
        std::memcpy(mBuffer + mBufferSize, msgBuffer, msgSize);
        mBufferSize += msgSize;

        return true;
    }

    /**
     * Get the bundle data
     */
    const char* data() const { return mBuffer; }

    /**
     * Get the bundle size
     */
    std::size_t size() const { return mBufferSize; }

    /**
     * Send the bundle using an OSCClient
     */
    bool send(OSCClient& client) const
    {
        return client.send(mBuffer, static_cast<uint16_t>(mBufferSize));
    }

private:
    char mBuffer[MAX_BUNDLE_SIZE];
    std::size_t mBufferSize = 0;

    static constexpr std::size_t MAX_MESSAGE_SIZE = 1024;
};

/**
 * Parsed OSC argument
 */
struct OSCArg
{
    char type;
    union {
        int32_t i;
        float f;
        int64_t h;
        double d;
        OSCTimetag t;
        struct { uint8_t port, status, data1, data2; } midi;
        struct { uint8_t r, g, b, a; } color;
        char c;
    };
    const char* s;           // For strings (points into original buffer)
    const uint8_t* blobData; // For blobs (points into original buffer)
    int32_t blobSize;        // For blobs
};

/**
 * Parsed OSC message (read-only view into received data)
 */
class OSCMessageView
{
public:
    OSCMessageView() { clear(); }

    void clear()
    {
        mAddress = nullptr;
        mTypeTags = nullptr;
        mArgCount = 0;
    }

    /**
     * Parse an OSC message from a buffer
     * @return true if valid OSC message, false otherwise
     */
    bool parse(const char* buffer, std::size_t size)
    {
        clear();
        if (size < 4 || buffer[0] != '/') {
            return false;  // Must start with '/'
        }

        std::size_t pos = 0;

        // Parse address
        mAddress = buffer;
        while (pos < size && buffer[pos] != '\0') pos++;
        if (pos >= size) return false;
        pos++;  // Skip null
        pos = (pos + 3) & ~3;  // Align to 4 bytes

        // Parse type tag string
        if (pos >= size || buffer[pos] != ',') {
            // No type tags = no arguments (valid)
            mTypeTags = "";
            return true;
        }

        mTypeTags = buffer + pos + 1;  // Skip comma
        while (pos < size && buffer[pos] != '\0') pos++;
        if (pos >= size) return false;
        pos++;
        pos = (pos + 3) & ~3;

        // Parse arguments based on type tags
        const char* types = mTypeTags;
        while (*types && mArgCount < MAX_ARGS) {
            OSCArg& arg = mArgs[mArgCount];
            arg.type = *types;
            arg.s = nullptr;
            arg.blobData = nullptr;

            switch (*types) {
                case 'i':  // int32
                    if (pos + 4 > size) return false;
                    std::memcpy(&arg.i, buffer + pos, 4);
                    arg.i = swap_endian(arg.i);
                    pos += 4;
                    break;

                case 'f':  // float32
                    if (pos + 4 > size) return false;
                    std::memcpy(&arg.f, buffer + pos, 4);
                    arg.f = swap_endian_float(arg.f);
                    pos += 4;
                    break;

                case 's':  // string
                case 'S':  // symbol (treated same as string)
                    arg.s = buffer + pos;
                    while (pos < size && buffer[pos] != '\0') pos++;
                    if (pos >= size) return false;
                    pos++;
                    pos = (pos + 3) & ~3;
                    break;

                case 'b':  // blob
                    if (pos + 4 > size) return false;
                    std::memcpy(&arg.blobSize, buffer + pos, 4);
                    arg.blobSize = swap_endian(arg.blobSize);
                    pos += 4;
                    if (pos + static_cast<std::size_t>(arg.blobSize) > size) return false;
                    arg.blobData = reinterpret_cast<const uint8_t*>(buffer + pos);
                    pos += static_cast<std::size_t>(arg.blobSize);
                    pos = (pos + 3) & ~3;
                    break;

                case 'h':  // int64
                    if (pos + 8 > size) return false;
                    std::memcpy(&arg.h, buffer + pos, 8);
                    arg.h = swap_endian(arg.h);
                    pos += 8;
                    break;

                case 'd':  // double
                    if (pos + 8 > size) return false;
                    std::memcpy(&arg.d, buffer + pos, 8);
                    arg.d = swap_endian_double(arg.d);
                    pos += 8;
                    break;

                case 't':  // timetag
                    if (pos + 8 > size) return false;
                    std::memcpy(&arg.t.seconds, buffer + pos, 4);
                    std::memcpy(&arg.t.fractions, buffer + pos + 4, 4);
                    arg.t.seconds = swap_endian(arg.t.seconds);
                    arg.t.fractions = swap_endian(arg.t.fractions);
                    pos += 8;
                    break;

                case 'm':  // MIDI
                    if (pos + 4 > size) return false;
                    arg.midi.port = static_cast<uint8_t>(buffer[pos]);
                    arg.midi.status = static_cast<uint8_t>(buffer[pos + 1]);
                    arg.midi.data1 = static_cast<uint8_t>(buffer[pos + 2]);
                    arg.midi.data2 = static_cast<uint8_t>(buffer[pos + 3]);
                    pos += 4;
                    break;

                case 'c':  // char (stored as int32)
                    if (pos + 4 > size) return false;
                    arg.c = buffer[pos + 3];  // Last byte
                    pos += 4;
                    break;

                case 'r':  // RGBA color
                    if (pos + 4 > size) return false;
                    arg.color.r = static_cast<uint8_t>(buffer[pos]);
                    arg.color.g = static_cast<uint8_t>(buffer[pos + 1]);
                    arg.color.b = static_cast<uint8_t>(buffer[pos + 2]);
                    arg.color.a = static_cast<uint8_t>(buffer[pos + 3]);
                    pos += 4;
                    break;

                case 'T':  // True
                case 'F':  // False
                case 'N':  // Nil
                case 'I':  // Infinitum
                    // No data bytes
                    break;

                default:
                    // Unknown type, skip
                    break;
            }

            mArgCount++;
            types++;
        }

        return true;
    }

    const char* address() const { return mAddress; }
    const char* typeTags() const { return mTypeTags; }
    std::size_t argCount() const { return mArgCount; }

    const OSCArg* arg(std::size_t index) const
    {
        return (index < mArgCount) ? &mArgs[index] : nullptr;
    }

    // Convenience accessors
    int32_t getInt(std::size_t index, int32_t defaultVal = 0) const
    {
        const OSCArg* a = arg(index);
        return (a && a->type == 'i') ? a->i : defaultVal;
    }

    float getFloat(std::size_t index, float defaultVal = 0.0f) const
    {
        const OSCArg* a = arg(index);
        return (a && a->type == 'f') ? a->f : defaultVal;
    }

    const char* getString(std::size_t index, const char* defaultVal = "") const
    {
        const OSCArg* a = arg(index);
        return (a && (a->type == 's' || a->type == 'S') && a->s) ? a->s : defaultVal;
    }

    bool getBool(std::size_t index, bool defaultVal = false) const
    {
        const OSCArg* a = arg(index);
        if (!a) return defaultVal;
        if (a->type == 'T') return true;
        if (a->type == 'F') return false;
        return defaultVal;
    }

    /**
     * Check if address matches a pattern
     * Supports basic wildcard: * matches any sequence, ? matches single char
     */
    bool matchAddress(const char* pattern) const
    {
        if (!mAddress || !pattern) return false;
        return matchPattern(pattern, mAddress);
    }

private:
    static constexpr std::size_t MAX_ARGS = 64;

    static bool matchPattern(const char* pattern, const char* str)
    {
        while (*pattern && *str) {
            if (*pattern == '*') {
                pattern++;
                if (!*pattern) return true;
                while (*str) {
                    if (matchPattern(pattern, str)) return true;
                    str++;
                }
                return false;
            } else if (*pattern == '?' || *pattern == *str) {
                pattern++;
                str++;
            } else {
                return false;
            }
        }
        while (*pattern == '*') pattern++;
        return !*pattern && !*str;
    }

    const char* mAddress = nullptr;
    const char* mTypeTags = nullptr;
    OSCArg mArgs[MAX_ARGS];
    std::size_t mArgCount = 0;
};

/**
 * Callback type for received OSC messages
 */
using OSCCallback = void (*)(const OSCMessageView& msg, void* userData);

/**
 * OSC Server - listens for incoming OSC messages
 */
class OSCServer
{
public:
    explicit OSCServer(uint16_t port)
        : mPort(port)
    {
    }

    ~OSCServer()
    {
        stop();
    }

    // Non-copyable
    OSCServer(const OSCServer&) = delete;
    OSCServer& operator=(const OSCServer&) = delete;

    /**
     * Start listening for OSC messages
     * @param callback Function to call when a message is received
     * @param userData User data passed to callback
     * @return true on success
     */
    bool start(OSCCallback callback, void* userData = nullptr)
    {
        if (mPcb) return false;  // Already running

        mCallback = callback;
        mUserData = userData;

        mPcb = udp_new();
        if (!mPcb) return false;

        // Bind to port
        err_t err = udp_bind(mPcb, IP_ADDR_ANY, mPort);
        if (err != ERR_OK) {
            udp_remove(mPcb);
            mPcb = nullptr;
            return false;
        }

        // Set receive callback
        udp_recv(mPcb, &OSCServer::udpRecvCallback, this);

        return true;
    }

    /**
     * Stop listening
     */
    void stop()
    {
        if (mPcb) {
            udp_remove(mPcb);
            mPcb = nullptr;
        }
        mCallback = nullptr;
        mUserData = nullptr;
    }

    bool isRunning() const { return mPcb != nullptr; }
    uint16_t port() const { return mPort; }

private:
    static void udpRecvCallback(void* arg, udp_pcb* pcb, pbuf* p,
                                 const ip_addr_t* addr, uint16_t port)
    {
        (void)pcb;
        (void)addr;
        (void)port;

        OSCServer* server = static_cast<OSCServer*>(arg);
        if (!server || !p || !server->mCallback) {
            if (p) pbuf_free(p);
            return;
        }

        // Copy data from potentially chained pbufs
        char buffer[MAX_MESSAGE_SIZE];
        std::size_t totalLen = 0;

        for (pbuf* q = p; q != nullptr && totalLen < MAX_MESSAGE_SIZE; q = q->next) {
            std::size_t copyLen = q->len;
            if (totalLen + copyLen > MAX_MESSAGE_SIZE) {
                copyLen = MAX_MESSAGE_SIZE - totalLen;
            }
            std::memcpy(buffer + totalLen, q->payload, copyLen);
            totalLen += copyLen;
        }

        pbuf_free(p);

        // Check if this is a bundle
        if (totalLen >= 8 && std::memcmp(buffer, "#bundle", 7) == 0) {
            server->parseBundle(buffer, totalLen);
        } else {
            // Single message
            OSCMessageView msg;
            if (msg.parse(buffer, totalLen)) {
                server->mCallback(msg, server->mUserData);
            }
        }
    }

    void parseBundle(const char* buffer, std::size_t size)
    {
        // Skip "#bundle\0" (8 bytes) and timetag (8 bytes)
        std::size_t pos = 16;

        while (pos + 4 <= size) {
            // Read element size
            int32_t elemSize;
            std::memcpy(&elemSize, buffer + pos, 4);
            elemSize = swap_endian(elemSize);
            pos += 4;

            if (elemSize <= 0 || pos + static_cast<std::size_t>(elemSize) > size) {
                break;
            }

            const char* elemData = buffer + pos;

            // Check if nested bundle
            if (static_cast<std::size_t>(elemSize) >= 8 &&
                std::memcmp(elemData, "#bundle", 7) == 0) {
                parseBundle(elemData, static_cast<std::size_t>(elemSize));
            } else {
                // Parse as message
                OSCMessageView msg;
                if (msg.parse(elemData, static_cast<std::size_t>(elemSize))) {
                    mCallback(msg, mUserData);
                }
            }

            pos += static_cast<std::size_t>(elemSize);
        }
    }

    udp_pcb* mPcb = nullptr;
    uint16_t mPort;
    OSCCallback mCallback = nullptr;
    void* mUserData = nullptr;
};

}  // namespace picoosc