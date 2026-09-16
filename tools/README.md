# Offset dump generator

`dump_offsets.py` converts the current `constexpr uint64_t` dump into a C++
header containing `namespace OFFSET`. JSON is also accepted for future dumps:
either an object mapping names to values, or a list of `{ "name", "value" }`
objects.

From the `PubgExt` directory:

```text
python ../tools/dump_offsets.py --input offsetdump.txt --output Config/Offsets.h
```

From the repository root, use the equivalent paths:

```text
python tools/dump_offsets.py --input PubgExt/offsetdump.txt --output PubgExt/Config/Offsets.h
```

Use `--check` in CI to verify that an existing header is current without
rewriting it:

```text
python tools/dump_offsets.py --input PubgExt/offsetdump.txt --output PubgExt/Config/Offsets.h --check
```
