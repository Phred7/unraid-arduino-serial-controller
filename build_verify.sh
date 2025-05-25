echo "Verfiying build (manual inspection required)"
echo
ls -la dist/arduino-serial-controller
echo
file dist/arduino-serial-controller
echo
echo "^^^ see verification logs ^^^"
echo "Check the following:"
echo " 1. Size: 10+ MB"
echo " 2. Format: ELF 64-bit Linux exe"
echo " 3. Perms: -rwxrwxrwx"
echo " 4. Arch: x86-64"
echo " 5. Date: Matches build time"