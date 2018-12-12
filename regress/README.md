## Regress tests

To use these tests the following programs are needed:

* curl ;
* sharness (from git master branch).

Some tests require an aditionnal utility named [pnginfo](https://github.com/Aversiste/pnginfo) but as it is currently undocumented as well as untested on Linux the dependency is not mandatory.

An optionnal parameter can be supplied in order to check the avatar associated with a specific email address. It should be a PNG image, otherwise pnginfo based tests will fail.

Example:

    ./libravatar.org.t
    ./libravatar.org.t bertrand@example.org
