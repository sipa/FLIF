#pragma once

#include <vector>
#include <stdint.h>
#include <assert.h>
#include "../config.h"

#define STACK_CODER_MAX_SYMBOLS (1 << 19)

template<typename IO>
class StackDecoder
{
    IO& io;
    uint32_t accumulator;
    uint32_t symbols_left;
    uint32_t bytes_left;

    inline int read_catch_eof() {
        int c = io.getc();
        if (c == io.EOS) return 0;
        return c;
    }

    inline void fetch() {
        if (symbols_left == 0) {
            uint32_t b = read_catch_eof();
            /* Read blob length descriptor:
             * - 0xxxxxxx = x [0-127]
             * - 10xxxxxx yyyyyyyy = (x << 8) + y + 0x80 [128-16511]
             * - 11xxxxxx yyyyyyyy zzzzzzzz = (x << 16) + (y << 8) + z + 0x4080 [16512-4210815]
             */
            if (b < 0x80) {
                bytes_left = b;
            } else if (b < 0xC0) {
                bytes_left = ((b & 0x3F) << 8) + read_catch_eof() + 0x80;
            } else {
                bytes_left = ((b & 0x3F) << 16) + (((uint32_t)read_catch_eof()) << 8) + read_catch_eof() + 0x4080;
            }
            accumulator = 0;
            symbols_left = STACK_CODER_MAX_SYMBOLS;
        }
        symbols_left--;
        while (accumulator < 0x1000000UL && bytes_left) {
            bytes_left--;
            accumulator = (accumulator << 8) | read_catch_eof();
        }
    }

public:
    StackDecoder(IO& io_) : io(io_), symbols_left(0) {}

    bool read_12bit_chance(int b12) {
        fetch();
        int low = accumulator & 0xFFF;
        if (low >= b12) {
            accumulator = (accumulator >> 12) * (0x1000 - b12) + low - b12;
            return false;
        } else {
            accumulator = (accumulator >> 12) * b12 + low;
            return true;
        }
    }

    bool read_bit() {
        fetch();
        bool ret = accumulator & 1;
        accumulator >>= 1;
        return ret;
    }
};

#ifdef HAS_ENCODER
class StackEncoderBlob
{
    std::vector<unsigned char> output;
    uint32_t accumulator;

public:
    StackEncoderBlob() : accumulator(0) {}

    void write_12bit_chance(uint16_t b12, bool bit) {
        uint16_t add = bit ? 0 : b12;
        uint16_t chance = bit ? b12 : 0x1000 - b12;
        uint32_t reduced = accumulator / chance;
        if (reduced >= 0x100000) {
            if (reduced >= 0x10000000) {
                output.push_back(accumulator & 0xFF);
                accumulator >>= 8;
                reduced >>= 8;
            }
            output.push_back(accumulator & 0xFF);
            accumulator >>= 8;
            reduced >>= 8;
        }
        uint16_t overflow = accumulator - (reduced * chance);
        accumulator = (reduced << 12) | (add + overflow);
    }

    void write_bit(bool bit) {
        if (accumulator >= 0x80000000UL) {
            output.push_back(accumulator & 0xFF);
            accumulator >>= 8;
        }
        accumulator = (accumulator << 1) | bit;
    }

    template<typename IO>
    void dump(IO& io) {
        while (accumulator > 0) {
            output.push_back(accumulator & 0xFF);
            accumulator >>= 8;
        }
        uint32_t size = output.size();
        if (size < 0x80) {
            io.fputc(size);
        } else {
            size -= 0x80;
            if (size < 0x4000) {
                io.fputc((size >> 8) | 0x80);
                io.fputc(size & 0xFF);
            } else {
                size -= 0x4000;
                io.fputc((size >> 16) | 0xC0);
                io.fputc((size >> 8) & 0xFF);
                io.fputc(size & 0xFF);
            }
        }
        for (std::vector<unsigned char>::const_reverse_iterator it = output.rbegin(); it != output.rend(); it++) {
            io.fputc(*it);
        }
        output.clear();
        accumulator = 0;
    }
};

template<typename IO>
class StackEncoder
{
    std::vector<uint16_t> samples;
    IO& io;

    void produce_blob() {
        StackEncoderBlob blob;

        for (std::vector<uint16_t>::const_reverse_iterator it = samples.rbegin(); it != samples.rend(); it++) {
            uint16_t val = *it;
            bool bit = val >> 15;
            switch ((val >> 12) & 1) {
            case 0:
                blob.write_12bit_chance(val & 0xFFF, bit);
                break;
            case 1:
                blob.write_bit(bit);
                break;
            }
        }
        blob.dump(io);
        samples.clear();
    }

public:
    StackEncoder(IO& io_) : io(io_) {}

    void write_12bit_chance(int b12, bool bit) {
        samples.push_back((bit << 15) | b12);
        if (samples.size() == STACK_CODER_MAX_SYMBOLS) produce_blob();
    }

    void write_bit(bool bit) {
        samples.push_back((bit << 15) | 0x1000);
        if (samples.size() == STACK_CODER_MAX_SYMBOLS) produce_blob();
    }

    void flush() {
        produce_blob();
        io.flush();
    }
};
#endif

class RacDummy
{
public:
    void inline write_12bit_chance(uint16_t b12, bool) { }
    void inline write_bit(bool) { }
    void inline flush() { }
};
