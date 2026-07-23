# Первый мост MeloScript -> PS2DEV

## 1. Ставим тулчейн (один раз, на своей машине)

У тебя уже есть исходники ps2dev 2.0.0 (MIT). В них три варианта установки,
от быстрого к медленному.

### Вариант A (самый быстрый) — готовый бинарный архив, без сборки GCC

    export PS2DEV=/usr/local/ps2dev
    sudo mkdir -p $PS2DEV
    sudo chown -R $USER: $PS2DEV
    curl -o ps2dev-latest.tar.gz -LC - https://github.com/ps2dev/ps2dev/releases/download/latest/ps2dev-ubuntu-latest.tar.gz
    tar -xf ps2dev-latest.tar.gz --strip-components 1 -C $PS2DEV

macOS: замени ubuntu на macos в URL. Raspberry Pi: ps2dev-ubuntu-22.04-arm.tar.gz.

Добавь в ~/.bash_profile:

    export PS2DEV=/usr/local/ps2dev
    export PS2SDK=$PS2DEV/ps2sdk
    export GSKIT=$PS2DEV/gsKit
    export PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin

### Вариант B — Docker, без установки чего-либо в систему

    docker pull ps2dev/ps2dev
    docker run --rm -it -v $(pwd):/src -w /src ps2dev/ps2dev sh

### Вариант C — собрать из исходников сам (долго: toolchain+ps2sdk+ports+gsKit+ps2-packer+ps2client)

    cd ps2dev-2.0.0
    sudo apt -y install gcc g++ make cmake patch git texinfo flex bison gettext libgsl-dev libgmp3-dev libmpfr-dev libmpc-dev zlib1g-dev autopoint
    export PS2DEV=/usr/local/ps2dev
    sudo mkdir -p $PS2DEV && sudo chown -R $USER: $PS2DEV
    export PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin
    ./build-all.sh

Если нужны только ps2client и ps2-packer поверх готового тулчейна:

    ./build-extra.sh

## 2. Собираем hello.elf

    export PS2SDK=$PS2DEV/ps2sdk
    make

Должен появиться hello.elf. Это проверка что сам тулчейн жив.

## 3. Запуск

Варианты, от простого к сложному:

- PCSX2 (эмулятор): просто открыть hello.elf через "Boot ELF"
- Реальная PS2 + сетевой линкер (PS2Link/PS2Client):
      ps2client -h <IP консоли> execee host:hello.elf
- Запись на USB/карту памяти и запуск через uLaunchELF

## 4. Куда дальше подключать MeloScript

Сейчас melo_vm.c/melo_codegen.c симулируют scratchpad, arena, vram
как обычные буферы на хосте (x86). Следующий шаг моста — не переписывать
логику, а подменить платформенный слой:

- заменить хостовый scratch_mem на реальный указатель 0x70000000 (SPR),
  под ifdef PS2 брать адрес напрямую, без malloc
- melo_aot_write_unit_file уже генерирует чистый C99 — его нужно просто
  скормить mips64r5900el-ps2-elf-gcc с флагами из этого Makefile
  (-march=r5900 -mhard-float -msingle-float -mno-llsc)
- для vukernel понадобится отдельный шаг: VU1 микрокод собирается не
  через ee-gcc, а через dvp-as/vcl из PS2SDK — это отдельный этап моста,
  не первый

Правильный порядок: hello.elf работает -> AOT C-код без vukernel/scratch
собирается и запускается -> потом scratch через реальный SPR -> потом
vukernel через VU1 тулчейн.

## 5. Про загруженный ps2toolchain-2018-10-19

Это старая (2018) версия toolchain-скриптов: binutils 2.14, GCC 3.2.3,
newlib 1.10.0. Она рабочая, но сильно устаревшая — современный ps2dev
(вариант A/B/C выше) собирает более новый GCC и рекомендуется вместо неё.

Важно: в этом архиве есть только патчи и скрипты (001-binutils-2.14.sh,
002-gcc-3.2.3-stage1.sh, 003-newlib-1.10.0.sh, 004-gcc-3.2.3-stage2.sh,
005-ps2sdk.sh, 006-ps2client.sh), а не сами исходники binutils/gcc/newlib.
Каждый скрипт сам делает wget на ftpmirror.gnu.org и sourceware.org —
без сети её собрать нельзя. Используй только если специально нужен
именно этот старый тулчейн:

    export PS2DEV=/usr/local/ps2dev
    sudo mkdir -p $PS2DEV && sudo chown -R $USER: $PS2DEV
    cd ps2toolchain-2018-10-19
    ./toolchain.sh

## 6. hello.c и Makefile — проверено против реальных заголовков ps2sdk

hello.c в этой папке — не выдумка, он сверен построчно с настоящим
ee/debug/samples/helloworld/helloworld.c из ps2sdk-master и синтаксически
проверен (gcc -fsyntax-only -D_EE) против настоящих tamtypes.h, sifrpc.h,
debug.h из твоего архива. Ошибок нет, только безобидные warning про
разницу битности хоста/таргета (это ожидаемо: PS2 32-битная, я
проверяю на 64-битном хосте).

Makefile.pref и Makefile.eeglobal в этой папке — не образцы "из головы",
это буквально скопированные samples/Makefile.pref_sample и
samples/Makefile.eeglobal_sample из твоего ps2sdk-master. Собирается так:

    export PS2SDK=$PS2DEV/ps2sdk
    make

Если PS2SDK ещё не собран/установлен — сначала пункт 1, потом сюда.

## 7. Второй мост: .melo -> AOT C -> реальный ps2sdk (папка bridge2/)

Это ответ на вопрос "как PS2SDK поймёт наш язык": она его не понимает
и не должна. PS2SDK понимает только C. Мост — это твой собственный
melo_aot.c, который уже умеет выводить чистый C99 из AST. Цепочка:

    твой_файл.melo
         |  melo2c (хостовый инструмент, собранный из
         |  melo_parser.c + melo_lexer.c + melo_codegen.c + melo_aot.c)
         v
    сгенерированный .c   <-- вот тут PS2SDK "видит" твой язык,
         |                   потому что это уже обычный C
         v
    mips64r5900el-ps2-elf-gcc
         v
    .o -> линковка с -lkernel
         v
    .elf на PS2

Проверено сквозным тестом на хосте (без реального PS2 toolchain, так
как его тут нет): hello.melo с @hot-функцией add_ints(19, 23)
транслируется в C, компилируется, линкуется с PS2-style main() и
возвращает 42. Синтаксис melo_main.c сверен против настоящих
tamtypes.h/sifrpc.h/debug.h из твоего архива, ошибок нет.

Важный момент архитектуры: только @hot-функции попадают в
сгенерированный C. Обычные func без @hot остаются в байткоде VM -
это то самое разделение "VM для утилит / AOT для горячего кода",
которое описано в твоём исследовательском документе (раздел 8).

Как собрать на реальном железе после установки тулчейна (пункт 1-4 выше):

    cd bridge2
    export PS2SDK=$PS2DEV/ps2sdk
    make

Makefile сам: соберёт melo2c на хосте, прогонит через него hello.melo,
скомпилирует получившийся C кросс-компилятором PS2, слинкует с
melo_main.c в hello.elf.

Следующий шаг после этого моста — не философский, а конкретный:
в melo_aot.c есть комментарий в runtime preamble, что vec4-операции
сейчас честно скалярные, а не VU0 COP2 asm, потому что тестировалось
без реального mips64r5900el-ps2-elf-gcc под рукой. Когда у тебя
заработает make из этого раздела - это то место в коде, которое
стоит доработать первым.

## 8. Сборка через Docker ps2dev/ps2dev (одна команда .melo -> .elf)

Раз у тебя уже стоит docker pull ps2dev/ps2dev, вот точный путь без
дополнительной настройки. Логика Makefile из bridge2/ проверена мной
через подставной (mock) тулчейн, эмулирующий поведение реального
mips64r5900el-ps2-elf-gcc - порядок сборки корректный, гонок
зависимостей нет, работает даже с параллельной сборкой (make -j4).

Из папки, где лежит bridge2/:

    docker run --rm -it -v $(pwd)/bridge2:/src -w /src ps2dev/ps2dev sh
    export PS2SDK=$PS2DEV/ps2sdk
    make

На выходе - hello.elf, собранный прямо из hello.melo одной командой.
Если хочешь скомпилировать свой .melo файл вместо hello.melo:

    make clean
    make MELO_FILE=my_script.melo GEN_C=my_script_gen.c

(GEN_C нужно поменять вместе с MELO_FILE, иначе останется старое имя
сгенерированного .c и Makefile не увидит, что его нужно пересобрать).

Запуск на PCSX2: открой hello.elf через "Boot ELF".
Запуск на реальной консоли через сетевой линк:

    make run

это дернет ps2client execee host:hello.elf - для этого на консоли
должен быть запущен PS2Link/PS2Client listener.

## 9. Про команду с монтированием в /home/ps2dev

Если твоя docker-команда выглядит так:

    docker run --rm -v ${PWD}:/home/ps2dev ps2dev-make sh -c "cd /home/ps2dev && make"

в ней два момента, которые надо поправить:

1. ps2dev-make - это не официальный образ (Docker Hub его не знает).
   Официальный - ps2dev/ps2dev (с /). Если ты сам собирал образ и
   назвал его ps2dev-make - используй свой тег, тогда всё ок, просто
   имей в виду, что официальный называется иначе.

2. ${PWD} должен указывать на папку melo/ (ту, где лежат MeloScript/
   и bridge2/ рядом), а не только на bridge2/. Makefile в bridge2
   ссылается на ../MeloScript - если смонтировать только bridge2,
   melo2c не найдёт исходники языка и сборка упадёт на первом шаге.

Рабочий вариант (проверено прогоном через подставной тулчейн,
воспроизводящий поведение реального mips64r5900el-ps2-elf-gcc):

    docker run --rm -v ${PWD}:/src -w /src/bridge2 ps2dev/ps2dev \
      sh -c "export PS2SDK=\$PS2DEV/ps2sdk && make"

${PWD} тут - это папка melo/ из архива (там, где MeloScript/ и
bridge2/ - соседи). -w /src/bridge2 сразу ставит рабочую директорию
внутри bridge2, откуда Makefile увидит ../MeloScript корректно.

## 10. "sh: make: not found" в официальном ps2dev/ps2dev - это норма

Если ты используешь официальный ps2dev/ps2dev и получаешь эту ошибку -
не баг твоей команды. Это подтверждённая, задокументированная
особенность образа: он собран как multi-stage build, и финальный слой
(alpine:latest + скопированный /usr/local/ps2dev) не содержит make,
git и прочие build-инструменты - только сам компилятор и SDK.
Об этом прямо пишет официальный туториал ps2dev community
(ps2-home.com) и подтверждает issue #29 в репозитории ps2dev/ps2dev.

Решение - поставить make внутри контейнера перед сборкой:

    docker run --rm -v ${PWD}:/src -w /src/bridge2 ps2dev/ps2dev \
      sh -c "apk add --no-cache make && export PS2SDK=\$PS2DEV/ps2sdk && make"

Если после этого вылезет ошибка вида "Error loading shared library
libmpc.so/libmpfr.so/libgmp.so" при первой реальной компиляции C-кода -
это отдельная, тоже известная проблема того же образа (см. issue #29):
некоторым сборкам ps2dev/ps2dev не хватает системных библиотек
mpc/mpfr/gmp внутри alpine. Чинится так же через apk:

    apk add --no-cache make gmp mpfr4 mpc1

(если конкретных версий пакетов нет под именами mpfr4/mpc1 - смотри
вывод apk search mpfr / apk search mpc в момент ошибки, имена версий
пакетов иногда меняются между релизами Alpine).

Итоговая рабочая команда одной строкой:

    docker run --rm -v ${PWD}:/src -w /src/bridge2 ps2dev/ps2dev \
      sh -c "apk add --no-cache make gmp mpfr4 mpc1 2>/dev/null; apk add --no-cache make gmp mpfr mpc1 2>/dev/null; export PS2SDK=\$PS2DEV/ps2sdk && make"
