# Gascop

Gascop is a lightweight POCSAG decoder for use with [rtl-sdr](http://sdr.osmocom.org/trac/wiki/rtl-sdr) devices, which is heavily inspired by [dump1090](https://github.com/antirez/dump1090/) and is currently under development mainly as an exercise in processing data from rtl-sdr devices.

Gascop does not use any part of the GNU Radio stack (not that there's anything wrong with it / some of my best friends / etc...), and thus is very easy to build and use.

If you are interested in the theory behind the code, take a look at [the wiki](https://github.com/yuvadm/gascop/wiki/Theory).

## Build

```bash
$ make
```

## Use

```bash
$ ./gascop 123450000  # for listening on 123.450 Mhz
```

## License

Copyright (c) 2013 by Yuval Adam, based on work by Salvatore Sanfilippo, all rights reserved.

Gascop is free for use and distribution under the BSD license.
