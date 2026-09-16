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

The generator preserves the project's historical names through aliases and
applies the reflected-field overrides verified against the local SDK build
`2609.1.1.93`. Fields used by legacy code without a matching SDK property are
kept for compatibility and emitted with a `TODO` comment; their values must be
verified at runtime before relying on them.

## Dumps locais

Os dumps do SDK, incluindo `2609.1.1.93/`, são gerados localmente e estão
ignorados pelo Git. Mantenha esse diretório apenas na cópia de trabalho e use
seus arquivos como entrada do gerador; não versione os dumps no repositório.

## kdmapper.exe

`kdmapper.exe` é um utilitário genérico de manual mapping e pode ser usado como
alternativa ao `ReadWriteDriverMapper.sys` quando não se quer compilar o
mapper. Ele não faz parte do fluxo oficial `PubgExt` → `ReadWriteDriver` e é
mantido como uma opção externa.

Exemplo de uso:

```text
tools\kdmapper.exe ReadWriteDriver\ReadWriteDriver\x64\Release\ReadWriteDriver.sys
```

Requer o modo de testes de assinatura desabilitado e execução como
administrador. O binário tem aproximadamente 154 KB e permanece ignorado pelo
Git.

Para preparar uma VM isolada, configurar test signing e executar o carregamento
com segurança, consulte o [guia de Teste Seguro do Driver](../docs/testing.md).
