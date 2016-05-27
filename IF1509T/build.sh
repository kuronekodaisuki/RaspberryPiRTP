#!/bin/sh
case $1 in
  clean)
    arg=clean
    rm -rf ./bin/
    ;;
  *)
    arg=all
    if [ ! -d bin ]; then mkdir bin; fi
    ;;
esac

cd test_cap
echo "*** Enter Directory test_cap/"
make $arg
cd ..

cd test_disp
echo "*** Enter Directory test_disp/"
make $arg
cd ..

if [ -d ./bin/ ]; then
  echo "*** Copy Binary to bin/"
  for i in test_cap/test_cap_udp test_cap/test_cap_rtp test_disp/test_disp; do
    if [ -f $i ]; then cp -f $i ./bin/; fi
  done
fi






