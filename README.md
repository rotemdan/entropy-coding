# Entropy coding algorithm implementations

Readable C++ implementations of various entropy coding algorithms.

Currently includes:

* Binary Arithmetic Coding (uses integer arithmetic)
* Binary Range Asymmetric Numeral Systems (rANS) coding

## Correctness

Tested via randomly generated inputs, with various probability distributions and lengths.

Please let me know if you encounter any issue.

## Performance

(measured on a single-core of 13th Gen Intel i3, compiled using MSVC 2022)

* Binary Arithmetic Coding: about 70 - 200 Mbit/s for both encoder and decoder
* Binary rANS: about 180 - 200 Mbit/s for encoder, 200 - 300 Mbit/s for decoder

## License

MIT
