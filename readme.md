# MyOS

Free and active developing os for free use on 32-bit C kernel

Открытая и активно разрабатывающаяся система для всех на 32 битном ядре, написанном на С

# Интересное

Своя ФС (cd, mkdir, rm, type, dir,write, touch)
Многосекторные файлы

Edit - полноценный текстовый редактор

Уникальный экран смерти - RSOD(Red Screen of Death)(тест командой crash)

autoexec.bat и config.sys

# примеры использования 

**autoexec.bat**
```
@echo off
echo Hello, user!
echo Lets go!
```

**config.sys**

```
; MyOS configuration file
color=7,0
prompt=$P$G
echo=on
clock=on
```

# Как использовать

# qemu

В линуксе или в виндовс через msys2

```bash
qemu-system-i386 disk.img
```

# VirtualBox
1. создаем виртуалку. Other / Other 32-bit
2. **Система** > **Материнская плата** > отключаем UEFI
3. **Носители** IDE контроллер > подключаем disk.vdi
4. Пользуемся

# VMvare
1. Нажимаем на Open a virtual machine
2. Выбираем other.vmx
3. запускаем и пользуемся

# Сборка при использовании git репозитория
1.  Необходимые пакеты - `nasm`, `clang`, `lld`, `llvm-objcopy`. 
2.  Собираем (Если clang пишет warring - просто игнорируем)
```bash
cd src
chmod +x ./build.sh
./build.sh
```
3. Пользуемся
