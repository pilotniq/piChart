mt-geo
======

[![Build Status](https://travis-ci.org/peterhaldbaek/mt-geo.svg?branch=master)](https://travis-ci.org/peterhaldbaek/mt-geo)

Geodesy representation conversion functions.


Installation
------------

    $ npm install mt-geo


Usage
-----

The module is initialized like every other Node module.

    var geo = require('mt-geo');

The module contains functions for converting geodesy representations.


### parseDMS(dms)

Parses string representing degrees/minutes/seconds into numeric degrees.

This is very flexible on formats, allowing signed decimal degrees, or deg-min-sec optionally
suffixed by compass direction (NSEW). A variety of separators are accepted (eg 3º 37' 09"W) 
or fixed-width format without separators (eg. 0033709W). Seconds and minutes may be omitted.
(Note minimal validation is done).

- __dms__ (string|number) Degrees or deg/min/sec in variety of formats

```
var latitude = geo.parseDMS('51° 28′ 40.12″ N');
// => 51.477811
var longitude = geo.parseDMS('000° 00′ 05.31″ W');
// => -0.001475
```


### toDMS(deg, format, dp)

Convert decimal degrees to deg/min/sec format.
Degree, prime, double-prime symbols are added, but sign is discarded, though no compass
direction is added

- __deg__ (number) Degrees
- __format__ (string, optional) Return value as 'd', 'dm', 'dms'
- __dp__ (number, optional) No of decimal places to use - default 0 for dms, 2 for dm, 4 for d

```
var dms = geo.toDMS('47.54');
// => 047°32′24″
```


### toLat(deg, format, dp)

Convert numeric degrees to deg/min/sec latitude (suffixed with N/S).

- __deg__ (number) Degrees
- __format__ (string, optional) Return value as 'd', 'dm', 'dms'
- __dp__ (number, optional) No of decimal places to use - default 0 for dms, 2 for dm, 4 for d

```
var latitude = geo.toLat('47.54');
// => 47°32′24″N
```


### toLon(deg, format, dp)

Convert numeric degrees to deg/min/sec longitude (suffixed with E/W).

- __deg__ (number) Degrees
- __format__ (string, optional) Return value as 'd', 'dm', 'dms'
- __dp__ (number, optional) No of decimal places to use - default 0 for dms, 2 for dm, 4 for d

```
var longitude = geo.toLon('47.54');
// => 047°32′24″E
```


### toBearing(deg, format, dp)

Convert numeric degrees to deg/min/sec as a bearing (0º..360º).

- __deg__ (number) Degrees
- __format__ (string, optional) Return value as 'd', 'dm', 'dms'
- __dp__ (number, optional) No of decimal places to use - default 0 for dms, 2 for dm, 4 for d

```
var bearing = geo.toBearing('47.54');
// => 047°32′24″
```



Copyright and license
---------------------

The original code was written by Chris Veness and can be found at
http://www.movable-type.co.uk/scripts/latlong.html. It is released under the
simple Creative Commons attribution license
(http://creativecommons.org/licenses/by/3.0/).

This project is released under the MIT license.
