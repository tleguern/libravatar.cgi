## Regress tests

To use these tests the following programs are needed:

* curl ;
* sharness.

An optionnal parameter can be supplied in order to check the avatar associated with a specific email address. It should be a PNG image, otherwise some tests will fail.

Example:

    ./libravatar.org.t
    ./libravatar.t bertrand@example.org
