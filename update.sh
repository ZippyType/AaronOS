#!/bin/bash
find . -maxdepth 1 ! -name 'update.sh' ! -name '.' -exec rm -rf {} +
git clone https://github.com/ZippyType/AaronOS
cp -r AaronOS/* .
rm -rf AaronOS
echo "Run build script? [y/N]"
read ans < /dev/tty

if [[ "$ans" == [Yy]* ]]; then

    bash setup.sh
fi
