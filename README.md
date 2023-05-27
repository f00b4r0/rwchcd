# rwchcd

A weather compensated central heating controller daemon

Home page: http://hacks.slashdirt.org/sw/rwchcd/

Initially designed to operate the [rWCHC hardware](http://hacks.slashdirt.org/hw/rwchc/),
this software is now completely standalone and hardware-independent.

## License

GPLv2-only - http://www.gnu.org/licenses/gpl-2.0.html

Copyright: (C) 2016-2023 Thibaut VARENE

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 version 2, as published by the Free Software Foundation.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See LICENSE.md for details

## Dependencies

 - Mandatory: **make gcc**
 - Recommended: **flex bison** (for config file support), **libdb-dev** (for permanent storage support)
 - Optional: **pkg-config libglib2.0-dev wiringpi librrd-dev libmosquitto-dev**
 - To build documentation: **doxygen graphviz**

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
are provided in `filecfg/examples`. `rwchcd -h` provides information about the
(very few) available command line parameters.

A systemd service file is provided that will be installed when running
`make install` if DBus is available. The daemon will be started at next reboot
unless manually started with `systemctl start rwchcd.service`
