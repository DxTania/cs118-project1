CS 118 Project 1 - HTTP Proxy
=============================

Partners:

Tania DePasquale

Julian Brown

## To start proxy server:

```
./waf

build/http-proxy
```

## To test:

Use telnet!

First run the proxy server, then run the following telnet command

```
telnet 127.0.0.1 14886
```

## Notes to TA

* http-tester.py passes all tests
* http-tester-conditionalGET-LAtime.py passes all tests
* Must restart server in bewteen the tests because the cached value from first test interferes
* I emailed you about someone forking our repository. If it seems as though someone else has similar code, I can make the following github repo: https://github.com/DxTania/cs118-project1 public again so I may prove this code is ours.
* There are timeouts for the following:
 - Server read timeout - 10 seconds
 - Client read timeout - 10 seconds
 - Server read short timeout of 5 seconds (using select) in relayResponse