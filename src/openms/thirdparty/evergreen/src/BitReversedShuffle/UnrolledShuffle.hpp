#ifndef _UNROLLEDSHUFFLE_HPP
#define _UNROLLEDSHUFFLE_HPP

#include <algorithm>

// Tools for performing compile-time-optimized bit
// reversal. Substantially faster than other methods, but it requires
// much larger compilation times for large problems (10 bits <-->
// N=2^10 requires roughly 2s to compile).

// Note that the resulting assembly should closely resemble the code
// generated by the following python program:
/*
def rev(n, B):
  return int(('{:0' + str(B) + 'b}').format(n)[::-1], 2)

def generate_reversal_code(B):
  for i in xrange(2**B):
    j = rev(i, B)
    if i < j:
      print '    std::swap( x[' + str(i) + '], x[' + str(j) + '] );'

# Generate a closed form for some specific numbers of bits (e.g., 8):
generate_reversal_code(8)

# The order of those swap operations could also be intelligently
  ordered to minimize cache misses; however, the compiler does fairly
  well with that.
*/

static constexpr unsigned long set_bit_right(unsigned char NUM_BITS, unsigned char REM_BITS, unsigned long value) {
  return value | ( (1ul<<(NUM_BITS>>1)) >> (REM_BITS>>1) );
}
  
static constexpr unsigned long set_bit_left(unsigned char NUM_BITS, unsigned char REM_BITS, unsigned long value) {
  return value | ( (1ul<<(NUM_BITS-1)) >> ((NUM_BITS>>1)-(REM_BITS>>1)) );
}

static constexpr unsigned long set_bits_left_and_right(unsigned char NUM_BITS, unsigned char REM_BITS, unsigned long value) {
  return set_bit_right(NUM_BITS, REM_BITS, set_bit_left(NUM_BITS, REM_BITS, value));
}

template <typename T, unsigned char NUM_BITS, unsigned char REM_BITS, unsigned long VAL, unsigned long REV>
class ShuffleAllValuesHelper {
public:
  //  __attribute__((always_inline))
  static void apply(T * __restrict x) {
    // 0*0
    ShuffleAllValuesHelper<T, NUM_BITS, REM_BITS-2, VAL, REV>::apply(x);

    // 0*1
    ShuffleAllValuesHelper<T, NUM_BITS, REM_BITS-2, set_bit_right(NUM_BITS, REM_BITS, VAL), set_bit_left(NUM_BITS, REM_BITS, REV)>::apply(x);

    // 1*0
    ShuffleAllValuesHelper<T, NUM_BITS, REM_BITS-2, set_bit_left(NUM_BITS, REM_BITS, VAL), set_bit_right(NUM_BITS, REM_BITS, REV)>::apply(x);

    // 1*1
    ShuffleAllValuesHelper<T, NUM_BITS, REM_BITS-2, set_bits_left_and_right(NUM_BITS, REM_BITS, VAL), set_bits_left_and_right(NUM_BITS, REM_BITS, REV)>::apply(x);
  }
};

// When NUM_BITS % 2 == 1:
template <typename T, unsigned char NUM_BITS, unsigned long VAL, unsigned long REV>
class ShuffleAllValuesHelper<T, NUM_BITS, 1, VAL, REV> {
public:
  //  __attribute__((always_inline))
  static void apply(T * __restrict x) {
    constexpr unsigned char MIDDLE_BIT = NUM_BITS>>1;
    // With 0 in middle:
    std::swap(x[VAL], x[REV]);

    // With 1 in middle:
    std::swap(x[VAL | (1ul<<MIDDLE_BIT)], x[REV | (1ul<<MIDDLE_BIT)]);
  }
};

// When NUM_BITS % 2 == 0:
template <typename T, unsigned char NUM_BITS, unsigned long VAL, unsigned long REV>
class ShuffleAllValuesHelper<T, NUM_BITS, 0, VAL, REV> {
public:
  //  __attribute__((always_inline))
  static void apply(T * __restrict x) {
    std::swap(x[VAL], x[REV]);
  }
};

template <typename T, unsigned char NUM_BITS, unsigned char REM_BITS, unsigned long VAL, unsigned long REV>
class UnrolledShuffleHelper {
public:
  //  __attribute__((always_inline))
  static void apply(T * __restrict x) {
    // apply [current_bit digits]0...1[current_bit digits]

    // Applies to all inner values (inequality is already guaranteed):
    ShuffleAllValuesHelper<T, NUM_BITS, REM_BITS-2, set_bit_right(NUM_BITS, REM_BITS, VAL), set_bit_left(NUM_BITS, REM_BITS, REV)>::apply(x);

    // apply [current_bit digits]0...0[current_bit digits]
    UnrolledShuffleHelper<T, NUM_BITS, REM_BITS-2, VAL, REV>::apply(x);

    // apply [current_bit digits]1...1[current_bit digits]
    UnrolledShuffleHelper<T, NUM_BITS, REM_BITS-2, set_bits_left_and_right(NUM_BITS, REM_BITS, VAL), set_bits_left_and_right(NUM_BITS, REM_BITS, REV)>::apply(x);
  }
};

// When NUM_BITS % 2 == 1:
template <typename T, unsigned char NUM_BITS, unsigned long VAL, unsigned long REV>
class UnrolledShuffleHelper<T, NUM_BITS, 1, VAL, REV> {
public:
  //  __attribute__((always_inline))
  static void apply(T * __restrict x) {
    constexpr unsigned char MIDDLE_BIT = NUM_BITS>>1;
    // With 0 in middle:
    std::swap(x[VAL], x[REV]);

    // With 1 in middle:

    // Note: this will swap 1111...1, with 1111...1 (this should be
    // the only case where equality will occur); but there will be no
    // effect:
    std::swap(x[VAL | (1ul<<MIDDLE_BIT)], x[REV | (1ul<<MIDDLE_BIT)]);
  }
};

// When NUM_BITS % 2 == 0:
template <typename T, unsigned char NUM_BITS, unsigned long VAL, unsigned long REV>
class UnrolledShuffleHelper<T, NUM_BITS, 0, VAL, REV> {
public:
  //  __attribute__((always_inline))
  static void apply(T * __restrict x) {
    std::swap(x[VAL], x[REV]);
  }
};

template <typename T, unsigned char NUM_BITS>
class UnrolledShuffle {
public:
  //  __attribute__((always_inline))
  static void apply(T * __restrict x) {
    UnrolledShuffleHelper<T, NUM_BITS, NUM_BITS, 0ul, 0ul>::apply(x);
  }
};

#endif
