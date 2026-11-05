# eth_vlg — Third-Party MAC Layer / Сторонний MAC-уровень

This folder is a placeholder for the third-party **`eth_vlg`** Ethernet MAC
implementation (Verilog/SystemVerilog, AXI-Stream based).

Эта папка — заглушка для сторонней реализации Ethernet MAC **`eth_vlg`**
(Verilog/SystemVerilog, на основе AXI-Stream).

---

## 🇬🇧 English

### Source / Источник

Original repository:

> https://github.com/hypernyan/eth_vlg

### How to obtain the sources (manual, archive download)

The code is **not** included in this repository. You must download it yourself.

1. Open https://github.com/hypernyan/eth_vlg in a browser.
2. Click the green **"Code"** button → **"Download ZIP"**
   (or open the direct link: https://github.com/hypernyan/eth_vlg/archive/refs/heads/main.zip —
   adjust the branch name if the default branch is different).
3. Unpack the downloaded archive anywhere on your machine.
4. Copy the required RTL files (typically from the `rtl/` directory inside
   the unpacked archive) into this folder `third_party/eth_vlg/`.
5. Keep this `README.md` alongside the copied files.

After copying, the folder should look approximately like this:

```
third_party/eth_vlg/
├── README.md            ← this file
├── eth_vlg.v            ← (or .sv) top-level MAC module
├── eth_vlg_rx.v
├── eth_vlg_tx.v
└── ...
```

The exact file list depends on the version you download — check the `rtl/`
directory of the upstream repository.

### ⚠️ License

The code in this folder is **not** distributed under the MIT license of this
project. `eth_vlg` is distributed under its own license — see the `LICENSE`
file in the upstream repository.

You are required to:

1. Download the sources yourself from the original location;
2. Comply with the upstream author's license terms;
3. Preserve attribution when redistributing derived works.

### Version pinning (recommended)

To keep the build reproducible, record the exact version you copied:

```
eth_vlg version used:  <tag or commit hash>
Date copied:           <YYYY-MM-DD>
```

Example:

```
eth_vlg version used:  main @ 3f9a1c2
Date copied:           2026-11-05
```

---

## 🇷🇺 Русский

### Источник

Оригинальный репозиторий:

> https://github.com/hypernyan/eth_vlg

### Как получить исходники (вручную, скачиванием архива)

Код **не** входит в состав этого репозитория. Его нужно скачать самостоятельно.

1. Откройте https://github.com/hypernyan/eth_vlg в браузере.
2. Нажмите зелёную кнопку **"Code"** → **"Download ZIP"**
   (или сразу перейдите по ссылке: https://github.com/hypernyan/eth_vlg/archive/refs/heads/main.zip —
   если ветка по умолчанию называется иначе, замените `main` на её имя).
3. Распакуйте скачанный архив в любое удобное место.
4. Скопируйте нужные RTL-файлы (как правило, из каталога `rtl/`
   внутри распакованного архива) в эту папку `third_party/eth_vlg/`.
5. Сохраните этот `README.md` рядом со скопированными файлами.

После копирования структура папки должна выглядеть примерно так:

```
third_party/eth_vlg/
├── README.md            ← этот файл
├── eth_vlg.v            ← (или .sv) верхний модуль MAC
├── eth_vlg_rx.v
├── eth_vlg_tx.v
└── ...
```

Точный список файлов зависит от скачанной версии — сверяйтесь с каталогом
`rtl/` в оригинальном репозитории.

### ⚠️ Лицензия

Код в этой папке **не** распространяется под лицензией MIT данного проекта.
`eth_vlg` распространяется под собственной лицензией — см. файл `LICENSE`
в оригинальном репозитории.

Вы обязаны:

1. Самостоятельно скачать исходники из оригинального источника;
2. Соблюдать условия лицензии автора `eth_vlg`;
3. Указывать авторство при распространении производных работ.

### Фиксация версии (рекомендуется)

Чтобы сборка была воспроизводимой, запишите, какая именно версия скопирована:

```
Используемая версия eth_vlg:  <тег или хеш коммита>
Дата копирования:             <ГГГГ-ММ-ДД>
```

Пример:

```
Используемая версия eth_vlg:  main @ 3f9a1c2
Дата копирования:             2026-11-05
```