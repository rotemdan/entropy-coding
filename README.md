# Entropy coding algorithm implementations

Readable C++ implementations of various entropy coding algorithms.

Currently includes:

* [Binary Arithmetic Coding](https://github.com/rotemdan/entropy-coding/tree/main/include/BinaryArithmeticCoder.h) (uses integer arithmetic)
* [Binary Range Asymmetric Numeral Systems (rANS) coding](https://github.com/rotemdan/entropy-coding/tree/main/include/BinaryRangeANSCoder.h), with support for optional table-based encoding and decoding

## Correctness

Tested via randomly generated inputs, with various probability distributions and lengths.

Please let me know if you encounter any issue.

## Performance

(measured on a single-core of 13th Gen Intel i3, compiled using MSVC 2022)

* Binary Arithmetic Coding: about 70 - 200 Mbit/s for encoder, 100 - 200 Mbit/s for decoder
* Binary rANS: about 300 - 420 Mbit/s for encoder, 250 - 400 Mbit/s for decoder

## License

MIT
