# Wired Server 2.0

Wired Server is BBS-oriented server for UNIX-based operating system. It uses libwired and provide an implementation of the Wired 2.0 protocol. This project is a fork of the original Wired Server developed by Axel Andersson at [Zanka Software](http://zankasoftware.com/). 

### Requirements

This program is mainly tested on Debian/Ubuntu Linux distributions, FreeBSD and Mac OS X. The source code is under BSD license and is totally free for use with respect of its attributed license.

To install the Wired server, you need the following prerequisites:

	* OpenSSL: http://www.openssl.org/source/
	* libxml2: http://xmlsoft.org/
	* zlib: http://zlib.net/
	* git: http://git-scm.com/

These are usually distributed with operating systems.

### Getting started

##### 1. Clone Wired Server repository:

`git clone https://bitbucket.org/nark/wired-server.git wired/`

Then move to the cloned package:

`cd wired/`

##### 2. Download submodules:

`git submodule update --init --recursive`

Check that the "libwired" directory was not empty.

##### 3. Run the configuration script:

`./configure`

This will install the Wired server into /usr/local by default. To change this, instead run:

`./configure --prefix=/path`

To change the default user the installation will write files as, run:

`./configure --with-user=USER`

Use `./configure --help` to show more options.

##### 4. Compile source code:

`make` or `gmake` on BSD-like systems

##### 5. If make is successful, install the software:

`make install` or `gmake install` on BSD-like systems

This will require write permissions to `/usr/local/wired`, or whatever directory you set as the prefix above.


##### 6. Running

To start an installed Wired server, run:

`/usr/local/wired/wiredctl start`

By default a user with the login "admin" and no password is created.

### Getting More

If you are interested in the Wired project, check the Wired Wiki at [http://www.read-write.fr/wired/wiki/](http://www.read-write.fr/wired/wiki/)
