## Synopsis

libravatar.cgi is an open source CGI written in C implementing the [Libravatar](https://www.libravatar.org/) protocol. Its goal is to be minimal, secure and [BCHS](https://learnbchs.org/).

This implementation only serves existing avatars in the JPEG format located in `/var/www/htdocs/avatars` and does not provide any way to upload a file. This should be the responsability of a separated system.

To request an avatar the following steps should be done :

* Create a hash of a lowercased email address using md5 or sha1 ;
* Call the CGI with a HTTP request on the path `/avatar/$hash`.

A few options are accepted as GET parameters :

* size: control the size of the image, must be between 1 and 512 with a default value of 80 ;
* default: `404`, `mm` and `blank` are supported ;
* rating: only kept for compatibility with Gravatar this option does nothing ;
* forcedefault: `y` or `n`.

The current version is not portable and only runs on OpenBSD.

## Installation

Begin by installing kcgi on your system and then configure httpd(1). An example and minimalistic configuration file is provided in the `config/` folder.

```
doas mkdir /var/www/htdocs/avatars/
doas cp config/mm.jpeg /var/www/htdocs/avatars/
make
doas make install
```

Also the code is not portable yet the Makefile is and should work with GNU make.

## Tests

Basic regression tests are provided in the `regress/` folder. They currently checks the correctnes of the returned HTTP status code. The script `regress.sh` emits [TAP](https://testanything.org/) output.

## License

All sources use the ISC license excepts `resample.{c,h}` and `jpegscale.c` which use the MIT license. This files are sourced from the [liboil](https://github.com/ender672/liboil) project.
