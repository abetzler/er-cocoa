# er-cocoa

This repository contains the files necessary to implement the Simple Congestion Control / Advanced (CoCoA) for Erbium CoAP, which is part of the Contiki OS toolset.

# Installation

To incorporate CoCoA, please copy the following files into the "er-coap" directory within "/Contiki/apps"

- er-coap-transactions.c
- er-coap-transactions.h
- Makefie.er-coap
- er-cocoa.c

This set of files contains the main cocoa file (er-cocoa.c) and some modified Er-CoAP files.

We recommend that you use the latest Contiki release, since the current version of Er-CoAP implements the full CoAP RFC. If Erbium CoAP or Contiki gets updated, we cannot guarantee that it will work with CoCoA (though only small changes should be necessary to make it work in case there are compatibility issues).
