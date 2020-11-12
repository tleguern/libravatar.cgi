## Libravatar.cgi

Small Libravatar implementation in C.

## Contents

1. [Synopsis](#synopsis)
2. [Install](#install)
3. [Tests](#tests)
4. [License](#license)

## Synopsis

libravatar.cgi is an open source CGI written in C implementing the [Libravatar](https://www.libravatar.org/) protocol. Its goal is to be minimal, secure and [BCHS](https://learnbchs.org/).

This implementation only serves existing avatars in the PNG format located in `/var/www/htdocs/avatars` and does not provide any way to upload a file. This should be the responsability of a separate system.

To request an avatar the following steps should be done :

* Create a hash of a lowercased email address using md5 or sha256 ;
* Call the CGI with a HTTP request on the path `/avatar/$hash`.

A few options are accepted as GET parameters :

* `size`: control the size of the image, must be between 1 and 512 with a default value of 80 ;
* `default`: `404`, `mm` and `blank` are supported ;
* `rating`: only kept for compatibility with Gravatar this option does nothing ;
* `forcedefault`: `y` or `n`.

The current version is not portable and only runs on OpenBSD.

## Install

### Requires

* C compiler ;
* [kcgi](https://kristaps.bsd.lv/kcgi) ;
* libpng.

#### For testing

* curl ;
* sharness ;
* [pnginfo](https://github.com/Aversiste/pnginfo) (optional).

### Build

Configure your http server of choice to receive this CGI. An example and minimalistic configuration file is provided in the `config/` folder for OpenBSD httpd(8).

```
# mkdir /var/www/htdocs/avatars/
# cp config/default.png /var/www/htdocs/avatars/
$ ./configure
$ make
# make install
```

## Tests

Regression tests are provided in the `regress/` folder. They test this implementation and two others: the old Libravatar from Francois Marier and ivatar from Oliver Falk.

## Contributing

Either send [send GitHub pull requests](https://github.com/Aversiste/libravatar.cgi) or [send patches on SourceHut](https://lists.sr.ht/~tleguern/libravatar-dev).

## License

All sources use the ISC license excepts `oil_libpng.{c,h}`, and `oil_resample.{c,h}` which use the MIT license. These files are sourced from the [liboil](https://github.com/ender672/liboil) project.
