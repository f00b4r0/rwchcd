# rwchcd

A weather compensated central heating controller daemon

Home page: http://hacks.slashdirt.org/sw/rwchcd/

## License

GPLv2-only - http://www.gnu.org/licenses/gpl-2.0.html

Copyright: (C) 2016-2021 Thibaut VARENE

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 version 2, as published by the Free Software Foundation.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See LICENSE.md for details

## Dependencies

 - Mandatory: make gcc flex bison
 - Recommended: libdb-dev (for permanent storage only)
 - Optional: pkg-config libglib2.0-dev wiringpi librrd-dev libmosquitto-dev
 - To build documentation: doxygen graphviz

## Building

 - To build, run `make`
 - To generate documentation, run `make doc`

Code documentation is available at http://hacks.slashdirt.org/sw/rwchcd/doc/html/

## Installing

To install, as root run `make install`
To uninstall, as root run `make uninstall`

## Usage

The daemon expects to find its configuration in `/etc/rwchcd.conf`,
unless specified differently on the command line. Example configurations
are provided in `filecfg/examples`.

A systemd service file is provided that will be installed when running
`make install`. The daemon will be started at next reboot unless manually
started with systemctl start rwchcd.service
