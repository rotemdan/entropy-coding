# Entropy coding algorithm implementations

Readable C++ implementations of various entropy coding algorithms.

Currently includes:

* [Binary Arithmetic Coding](https://github.com/rotemdan/entropy-coding/tree/main/include/BinaryArithmeticCoder.h) (uses fixed-point integer arithmetic)
* [Binary Range Asymmetric Numeral Systems (rANS) coding](https://github.com/rotemdan/entropy-coding/tree/main/include/BinaryRangeANSCoder.h), with support for optional table-based encoding and decoding

## Correctness

Tested via randomly generated inputs, with various probability distributions and lengths.

Please let me know if you encounter any issue.

## Performance

(measured on a single-core of 13th Gen Intel i3, compiled using MSVC 2022)

* Binary Arithmetic Coding: about 100 - 500 Mbit/s for encoder, 130 - 500 Mbit/s for decoder
* Binary rANS: about 300 - 420 Mbit/s for encoder, 250 - 400 Mbit/s for decoder

Encoding and decoding times can vary significantly based on compression ratio, and other parameters.

## License

MIT
